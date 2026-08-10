# Speaker Output Enhancer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Waveshare ESP32-S3-Touch-LCD-1.46/1.46C speaker output enhancer that improves perceived loudness for prompts and TTS without touching microphone, wake word, AEC, VAD, protocol, or uplink Opus encoding.

**Architecture:** Add a PCM-only software limiter utility that is host-testable, then add `SpeakerOutputEnhancer` as the ESP-IDF playback-side wrapper for `esp_audio_effects` EQ/DRC plus the software post-limiter. `AudioService::AudioOutputTask()` calls the enhancer immediately before `AudioCodec::OutputData()`, so OGG prompts and downlink Opus/TTS share the same processing path.

**Tech Stack:** ESP-IDF C++17-style project, FreeRTOS tasks, `esp_audio_effects` v1.2.1 (`esp_ae_eq`, `esp_ae_drc`), existing `NoAudioCodecSimplex` I2S output, host C++ tests compiled with `g++`.

## Global Constraints

- Target board: Waveshare ESP32-S3-Touch-LCD-1.46/1.46C, repo config `BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_46`.
- Add `CONFIG_SPEAKER_OUTPUT_ENHANCER` near the audio config area in `main/Kconfig.projbuild`; default `y`; `depends on BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_46`.
- Insert processing between `AudioOutputTask` dequeue and `AudioCodec::OutputData()`.
- Do not modify microphone input, wake word, ASR/STT, AEC, VAD, server protocol, or uplink Opus encoding.
- Do not introduce SpeexDSP, WebRTC APM, npm/Web Audio, or offline OGG regeneration in v1.
- Use EQ + DRC from `esp_audio_effects`; implement the final limiter as software post-limiter.
- `SpeakerOutputEnhancer::Initialize(int sample_rate)` returns `bool` and logs internal `esp_ae_err_t` failures; callers must not depend on `esp_ae_err_t`.
- First version processes all playback content through one chain; do not add source-based branching for short OGG prompts in v1.
- Default output sample rate for the target board is 24000 Hz; sample-rate reinitialization is defensive.
- `NoAudioCodec::Write()` applies `pow(output_volume / 100.0, 2) * 65536 * output_boost` after enhancer output; keep enhancer makeup gain conservative and limiter ceiling at -3 dBFS.

---

## File Structure

- Create `main/audio/speaker_output_limiter.h`: small host-testable declarations for converting dBFS ceiling to int16 peak and clamping PCM in place.
- Create `main/audio/speaker_output_limiter.cc`: pure C++ limiter implementation with no ESP-IDF includes.
- Create `main/audio/speaker_output_enhancer.h`: playback enhancer interface consumed by `AudioService`.
- Create `main/audio/speaker_output_enhancer.cc`: ESP-IDF implementation that owns EQ/DRC handles, applies EQ, DRC, then `speaker_output_limiter_apply`.
- Modify `main/audio/audio_service.h`: include enhancer header and add `SpeakerOutputEnhancer speaker_output_enhancer_;`.
- Modify `main/audio/audio_service.cc`: initialize enhancer in `Initialize()` and process `task->pcm` in `AudioOutputTask()` before `codec_->OutputData(task->pcm)`.
- Modify `main/CMakeLists.txt`: add `audio/speaker_output_limiter.cc` and `audio/speaker_output_enhancer.cc` to `SOURCES`.
- Modify `main/Kconfig.projbuild`: add `CONFIG_SPEAKER_OUTPUT_ENHANCER` near `USE_AUDIO_PROCESSOR` and `USE_AUDIO_DEBUGGER`.
- Create `tests/speaker_output_limiter_test.cc`: host tests for PCM-only limiter behavior.

---

### Task 1: Host-Tested Software Post-Limiter

**Files:**
- Create: `main/audio/speaker_output_limiter.h`
- Create: `main/audio/speaker_output_limiter.cc`
- Test: `tests/speaker_output_limiter_test.cc`

**Interfaces:**
- Consumes: no earlier task output.
- Produces:
  - `int16_t speaker_output_limiter_peak_from_db(float ceiling_db);`
  - `void speaker_output_limiter_apply(std::vector<int16_t>& pcm, int16_t peak);`

- [ ] **Step 1: Write the failing limiter test**

Create `tests/speaker_output_limiter_test.cc`:

```cpp
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

#include "../main/audio/speaker_output_limiter.h"

static void peak_from_minus_3_dbfs_leaves_headroom() {
    int16_t peak = speaker_output_limiter_peak_from_db(-3.0f);
    assert(peak >= 23190);
    assert(peak <= 23210);
}

static void limiter_clamps_positive_and_negative_samples() {
    std::vector<int16_t> pcm = {-32768, -20000, 0, 20000, 32767};
    speaker_output_limiter_apply(pcm, 20000);
    assert(pcm[0] == -20000);
    assert(pcm[1] == -20000);
    assert(pcm[2] == 0);
    assert(pcm[3] == 20000);
    assert(pcm[4] == 20000);
}

static void limiter_keeps_length_for_empty_and_normal_frames() {
    std::vector<int16_t> empty;
    speaker_output_limiter_apply(empty, 20000);
    assert(empty.empty());

    std::vector<int16_t> pcm = {-1000, 1000};
    speaker_output_limiter_apply(pcm, 20000);
    assert(pcm.size() == 2);
    assert(pcm[0] == -1000);
    assert(pcm[1] == 1000);
}

static void invalid_peak_falls_back_to_full_scale() {
    std::vector<int16_t> pcm = {-32768, 32767};
    speaker_output_limiter_apply(pcm, 0);
    assert(pcm[0] == -32767);
    assert(pcm[1] == 32767);
}

int main(void) {
    peak_from_minus_3_dbfs_leaves_headroom();
    limiter_clamps_positive_and_negative_samples();
    limiter_keeps_length_for_empty_and_normal_frames();
    invalid_peak_falls_back_to_full_scale();
    puts("speaker_output_limiter tests passed");
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
g++ -std=c++17 -Wall -Wextra -Werror tests\speaker_output_limiter_test.cc main\audio\speaker_output_limiter.cc -I main\audio -o build\speaker_output_limiter_test.exe
```

Expected: FAIL because `main\audio\speaker_output_limiter.h` and `.cc` do not exist.

- [ ] **Step 3: Create the limiter header**

Create `main/audio/speaker_output_limiter.h`:

```cpp
#ifndef SPEAKER_OUTPUT_LIMITER_H
#define SPEAKER_OUTPUT_LIMITER_H

#include <stdint.h>
#include <vector>

int16_t speaker_output_limiter_peak_from_db(float ceiling_db);
void speaker_output_limiter_apply(std::vector<int16_t>& pcm, int16_t peak);

#endif  // SPEAKER_OUTPUT_LIMITER_H
```

- [ ] **Step 4: Create the limiter implementation**

Create `main/audio/speaker_output_limiter.cc`:

```cpp
#include "speaker_output_limiter.h"

#include <algorithm>
#include <cmath>

int16_t speaker_output_limiter_peak_from_db(float ceiling_db) {
    if (ceiling_db >= 0.0f) {
        return INT16_MAX;
    }
    float linear = std::pow(10.0f, ceiling_db / 20.0f);
    int peak = static_cast<int>(std::lround(static_cast<float>(INT16_MAX) * linear));
    peak = std::clamp(peak, 1, static_cast<int>(INT16_MAX));
    return static_cast<int16_t>(peak);
}

void speaker_output_limiter_apply(std::vector<int16_t>& pcm, int16_t peak) {
    int16_t safe_peak = peak > 0 ? peak : INT16_MAX;
    for (auto& sample : pcm) {
        if (sample > safe_peak) {
            sample = safe_peak;
        } else if (sample < -safe_peak) {
            sample = static_cast<int16_t>(-safe_peak);
        }
    }
}
```

- [ ] **Step 5: Run the limiter test to verify it passes**

Run:

```powershell
g++ -std=c++17 -Wall -Wextra -Werror tests\speaker_output_limiter_test.cc main\audio\speaker_output_limiter.cc -I main\audio -o build\speaker_output_limiter_test.exe; .\build\speaker_output_limiter_test.exe
```

Expected: PASS and prints `speaker_output_limiter tests passed`.

- [ ] **Step 6: Commit Task 1**

Run:

```powershell
git add main\audio\speaker_output_limiter.h main\audio\speaker_output_limiter.cc tests\speaker_output_limiter_test.cc
git commit -m "test: add speaker output limiter"
```

Expected: commit succeeds.

---

### Task 2: ESP EQ/DRC SpeakerOutputEnhancer Module

**Files:**
- Create: `main/audio/speaker_output_enhancer.h`
- Create: `main/audio/speaker_output_enhancer.cc`
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Consumes:
  - `speaker_output_limiter_peak_from_db(float ceiling_db)`
  - `speaker_output_limiter_apply(std::vector<int16_t>& pcm, int16_t peak)`
- Produces:
  - `class SpeakerOutputEnhancer`
  - `SpeakerOutputEnhancer::Config`
  - `bool SpeakerOutputEnhancer::Initialize(int sample_rate)`
  - `void SpeakerOutputEnhancer::Process(std::vector<int16_t>& pcm)`
  - `void SpeakerOutputEnhancer::SetConfig(const Config& config)`
  - `bool SpeakerOutputEnhancer::IsActive() const`
  - `int SpeakerOutputEnhancer::sample_rate() const`

- [ ] **Step 1: Add enhancer source files to CMake before creating them**

Modify the opening `set(SOURCES ...)` block in `main/CMakeLists.txt` to include:

```cmake
set(SOURCES "audio/audio_codec.cc"
            "audio/audio_service.cc"
            "audio/speaker_output_limiter.cc"
            "audio/speaker_output_enhancer.cc"
            "audio/demuxer/ogg_demuxer.cc"
            "audio/codecs/no_audio_codec.cc"
```

- [ ] **Step 2: Create the enhancer header**

Create `main/audio/speaker_output_enhancer.h`:

```cpp
#ifndef SPEAKER_OUTPUT_ENHANCER_H
#define SPEAKER_OUTPUT_ENHANCER_H

#include <stdint.h>
#include <vector>

class SpeakerOutputEnhancer {
public:
    struct Config {
        bool enabled = true;
        bool enable_eq = true;
        bool enable_drc = true;
        bool enable_limiter = true;
        float limiter_ceiling_db = -3.0f;
        float makeup_gain_db = 1.5f;
    };

    explicit SpeakerOutputEnhancer(Config config = {});
    ~SpeakerOutputEnhancer();

    bool Initialize(int sample_rate);
    void Process(std::vector<int16_t>& pcm);
    void SetConfig(const Config& config);
    bool IsActive() const;
    int sample_rate() const;

private:
    Config config_;
    int sample_rate_ = 0;
    bool active_ = false;
    bool warned_process_failure_ = false;
    void* eq_handle_ = nullptr;
    void* drc_handle_ = nullptr;

    void Close();
};

#endif  // SPEAKER_OUTPUT_ENHANCER_H
```

- [ ] **Step 3: Create the enhancer implementation**

Create `main/audio/speaker_output_enhancer.cc`:

```cpp
#include "speaker_output_enhancer.h"

#include <esp_log.h>

#include "esp_ae_drc.h"
#include "esp_ae_eq.h"
#include "speaker_output_limiter.h"

#define TAG "SpeakerOutputEnhancer"

namespace {

constexpr int kChannelCount = 1;
constexpr int kBitsPerSample = 16;

esp_ae_eq_filter_para_t EqFilter(esp_ae_eq_filter_type_t type, uint32_t fc, float q, float gain) {
    return esp_ae_eq_filter_para_t{
        .filter_type = type,
        .fc = fc,
        .q = q,
        .gain = gain,
    };
}

}  // namespace

SpeakerOutputEnhancer::SpeakerOutputEnhancer(Config config) : config_(config) {
}

SpeakerOutputEnhancer::~SpeakerOutputEnhancer() {
    Close();
}

void SpeakerOutputEnhancer::Close() {
    if (eq_handle_ != nullptr) {
        esp_ae_eq_close(static_cast<esp_ae_eq_handle_t>(eq_handle_));
        eq_handle_ = nullptr;
    }
    if (drc_handle_ != nullptr) {
        esp_ae_drc_close(static_cast<esp_ae_drc_handle_t>(drc_handle_));
        drc_handle_ = nullptr;
    }
    active_ = false;
    sample_rate_ = 0;
    warned_process_failure_ = false;
}

bool SpeakerOutputEnhancer::Initialize(int sample_rate) {
    if (!config_.enabled) {
        Close();
        return false;
    }
    if (sample_rate <= 0) {
        ESP_LOGW(TAG, "Invalid sample rate: %d", sample_rate);
        Close();
        return false;
    }
    if (active_ && sample_rate_ == sample_rate) {
        return true;
    }

    Close();
    sample_rate_ = sample_rate;

    esp_ae_err_t ret = ESP_AE_ERR_OK;
    if (config_.enable_eq) {
        esp_ae_eq_filter_para_t filters[] = {
            EqFilter(ESP_AE_EQ_FILTER_HIGH_PASS, 150, 0.7f, 0.0f),
            EqFilter(ESP_AE_EQ_FILTER_LOW_SHELF, 220, 0.7f, -2.5f),
            EqFilter(ESP_AE_EQ_FILTER_PEAK, 2100, 1.0f, 2.5f),
            EqFilter(ESP_AE_EQ_FILTER_PEAK, 3800, 1.0f, 1.5f),
        };
        esp_ae_eq_cfg_t eq_cfg = {
            .sample_rate = static_cast<uint32_t>(sample_rate),
            .channel = kChannelCount,
            .bits_per_sample = kBitsPerSample,
            .filter_num = static_cast<uint8_t>(sizeof(filters) / sizeof(filters[0])),
            .para = filters,
        };
        esp_ae_eq_handle_t eq_handle = nullptr;
        ret = esp_ae_eq_open(&eq_cfg, &eq_handle);
        if (ret != ESP_AE_ERR_OK || eq_handle == nullptr) {
            ESP_LOGW(TAG, "Failed to open EQ: %d", ret);
            Close();
            return false;
        }
        eq_handle_ = eq_handle;
    }

    if (config_.enable_drc) {
        esp_ae_drc_curve_point curve_points[] = {
            {0.0f, -3.0f},
            {-18.0f, -15.0f},
            {-60.0f, -55.0f},
            {-100.0f, -100.0f},
        };
        esp_ae_drc_cfg_t drc_cfg = {
            .sample_rate = static_cast<uint32_t>(sample_rate),
            .channel = kChannelCount,
            .bits_per_sample = kBitsPerSample,
            .drc_para = {
                .point = curve_points,
                .point_num = static_cast<uint8_t>(sizeof(curve_points) / sizeof(curve_points[0])),
                .makeup_gain = config_.makeup_gain_db,
                .knee_width = 3.0f,
                .attack_time = 10,
                .release_time = 150,
                .hold_time = 0,
            },
        };
        esp_ae_drc_handle_t drc_handle = nullptr;
        ret = esp_ae_drc_open(&drc_cfg, &drc_handle);
        if (ret != ESP_AE_ERR_OK || drc_handle == nullptr) {
            ESP_LOGW(TAG, "Failed to open DRC: %d", ret);
            Close();
            return false;
        }
        drc_handle_ = drc_handle;
    }

    active_ = (eq_handle_ != nullptr) || (drc_handle_ != nullptr) || config_.enable_limiter;
    ESP_LOGI(TAG, "Initialized speaker enhancer at %d Hz (active=%s)", sample_rate, active_ ? "true" : "false");
    return active_;
}

void SpeakerOutputEnhancer::Process(std::vector<int16_t>& pcm) {
    if (!config_.enabled || pcm.empty()) {
        return;
    }

    if (active_) {
        uint32_t sample_num = static_cast<uint32_t>(pcm.size());
        if (eq_handle_ != nullptr) {
            esp_ae_err_t ret = esp_ae_eq_process(static_cast<esp_ae_eq_handle_t>(eq_handle_), sample_num,
                                                 reinterpret_cast<esp_ae_sample_t>(pcm.data()),
                                                 reinterpret_cast<esp_ae_sample_t>(pcm.data()));
            if (ret != ESP_AE_ERR_OK && !warned_process_failure_) {
                ESP_LOGW(TAG, "EQ process failed: %d", ret);
                warned_process_failure_ = true;
            }
        }
        if (drc_handle_ != nullptr) {
            esp_ae_err_t ret = esp_ae_drc_process(static_cast<esp_ae_drc_handle_t>(drc_handle_), sample_num,
                                                  reinterpret_cast<esp_ae_sample_t>(pcm.data()),
                                                  reinterpret_cast<esp_ae_sample_t>(pcm.data()));
            if (ret != ESP_AE_ERR_OK && !warned_process_failure_) {
                ESP_LOGW(TAG, "DRC process failed: %d", ret);
                warned_process_failure_ = true;
            }
        }
    }

    if (config_.enable_limiter) {
        speaker_output_limiter_apply(pcm, speaker_output_limiter_peak_from_db(config_.limiter_ceiling_db));
    }
}

void SpeakerOutputEnhancer::SetConfig(const Config& config) {
    config_ = config;
    Close();
}

bool SpeakerOutputEnhancer::IsActive() const {
    return active_;
}

int SpeakerOutputEnhancer::sample_rate() const {
    return sample_rate_;
}
```

- [ ] **Step 4: Run the host limiter test again**

Run:

```powershell
g++ -std=c++17 -Wall -Wextra -Werror tests\speaker_output_limiter_test.cc main\audio\speaker_output_limiter.cc -I main\audio -o build\speaker_output_limiter_test.exe; .\build\speaker_output_limiter_test.exe
```

Expected: PASS and prints `speaker_output_limiter tests passed`.

- [ ] **Step 5: Commit Task 2**

Run:

```powershell
git add main\CMakeLists.txt main\audio\speaker_output_enhancer.h main\audio\speaker_output_enhancer.cc
git commit -m "feat: add speaker output enhancer"
```

Expected: commit succeeds.

---

### Task 3: Kconfig Gate and AudioService Playback Integration

**Files:**
- Modify: `main/Kconfig.projbuild`
- Modify: `main/audio/audio_service.h`
- Modify: `main/audio/audio_service.cc`

**Interfaces:**
- Consumes:
  - `SpeakerOutputEnhancer::Initialize(int sample_rate) -> bool`
  - `SpeakerOutputEnhancer::Process(std::vector<int16_t>& pcm) -> void`
- Produces:
  - Playback output path calls enhancer before `AudioCodec::OutputData()`.
  - Feature is compiled only when `CONFIG_SPEAKER_OUTPUT_ENHANCER` is enabled.

- [ ] **Step 1: Add the Kconfig option**

In `main/Kconfig.projbuild`, insert this block after `config USE_AUDIO_PROCESSOR` and before `config USE_DEVICE_AEC`:

```kconfig
config SPEAKER_OUTPUT_ENHANCER
    bool "Enable Speaker Output Enhancer"
    default y
    depends on BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_46
    help
        Enable playback-side EQ, DRC, and software limiting for the Waveshare
        ESP32-S3-Touch-LCD-1.46/1.46C speaker output path.
```

- [ ] **Step 2: Include and hold the enhancer in AudioService**

Modify `main/audio/audio_service.h` includes:

```cpp
#include "processors/audio_debugger.h"
#include "speaker_output_enhancer.h"
#include "wake_word.h"
```

Add this private member after `audio_debugger_`:

```cpp
#if CONFIG_SPEAKER_OUTPUT_ENHANCER
    SpeakerOutputEnhancer speaker_output_enhancer_;
#endif
```

- [ ] **Step 3: Initialize the enhancer with codec output sample rate**

In `AudioService::Initialize(AudioCodec* codec)` in `main/audio/audio_service.cc`, after `codec_->Start();`, add:

```cpp
#if CONFIG_SPEAKER_OUTPUT_ENHANCER
    speaker_output_enhancer_.Initialize(codec_->output_sample_rate());
#endif
```

- [ ] **Step 4: Process playback PCM before codec output**

In `AudioService::AudioOutputTask()` in `main/audio/audio_service.cc`, replace:

```cpp
        codec_->OutputData(task->pcm);
```

with:

```cpp
#if CONFIG_SPEAKER_OUTPUT_ENHANCER
        if (speaker_output_enhancer_.sample_rate() != codec_->output_sample_rate()) {
            speaker_output_enhancer_.Initialize(codec_->output_sample_rate());
        }
        speaker_output_enhancer_.Process(task->pcm);
#endif

        codec_->OutputData(task->pcm);
```

- [ ] **Step 5: Run the host limiter test**

Run:

```powershell
g++ -std=c++17 -Wall -Wextra -Werror tests\speaker_output_limiter_test.cc main\audio\speaker_output_limiter.cc -I main\audio -o build\speaker_output_limiter_test.exe; .\build\speaker_output_limiter_test.exe
```

Expected: PASS and prints `speaker_output_limiter tests passed`.

- [ ] **Step 6: Compile the touched ESP-IDF object if build tree is configured**

Run:

```powershell
ninja -C build esp-idf/main/CMakeFiles/__idf_main.dir/audio/audio_service.cc.obj
```

Expected: exit code 0. If this fails because the build tree needs reconfiguration after adding sources/Kconfig, run the full ESP-IDF build command available in the environment instead:

```powershell
idf.py build
```

Expected: exit code 0. If `idf.py` is not available in the current shell, record that exact limitation in the task report.

- [ ] **Step 7: Commit Task 3**

Run:

```powershell
git add main\Kconfig.projbuild main\audio\audio_service.h main\audio\audio_service.cc
git commit -m "feat: enable speaker output enhancer"
```

Expected: commit succeeds.

---

### Task 4: Verification Notes and Final Checks

**Files:**
- Modify: `docs/update.md`

**Interfaces:**
- Consumes: all previous task outputs.
- Produces: a short implementation note and verified command list in project update docs.

- [ ] **Step 1: Add an update entry**

Prepend this section near the top of `docs/update.md`:

```markdown
### Waveshare ESP32-S3 Touch LCD 1.46/1.46C 扬声器输出增强器

本轮在播放端接入 `SpeakerOutputEnhancer`，对云端 TTS/电话类下行音频和 `PlaySound()` OGG 提示音统一生效。增强器位于 `AudioOutputTask` 与 `AudioCodec::OutputData()` 之间，不改麦克风输入、唤醒词、AEC、VAD、协议或上行 Opus 编码。

实现内容：

- 新增纯软件 limiter，并用主机侧 C++ 测试覆盖 -3 dBFS 峰值换算、正负样本限幅、空 frame 和长度保持。
- 新增播放端 EQ/DRC 封装，使用 `esp_audio_effects` 的 `esp_ae_eq` / `esp_ae_drc`，再追加软件后级 limiter。
- 新增 `CONFIG_SPEAKER_OUTPUT_ENHANCER`，默认仅对 `BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_46` 启用。
- 在 `AudioService::AudioOutputTask()` 输出 PCM 前调用增强器，覆盖 TTS 和提示音播放路径。

验证：

- `g++ -std=c++17 -Wall -Wextra -Werror tests\speaker_output_limiter_test.cc main\audio\speaker_output_limiter.cc -I main\audio -o build\speaker_output_limiter_test.exe; .\build\speaker_output_limiter_test.exe`：通过。
- `ninja -C build esp-idf/main/CMakeFiles/__idf_main.dir/audio/audio_service.cc.obj` 或 `idf.py build`：填写实际执行结果。
```

- [ ] **Step 2: Run host test**

Run:

```powershell
g++ -std=c++17 -Wall -Wextra -Werror tests\speaker_output_limiter_test.cc main\audio\speaker_output_limiter.cc -I main\audio -o build\speaker_output_limiter_test.exe; .\build\speaker_output_limiter_test.exe
```

Expected: PASS and prints `speaker_output_limiter tests passed`.

- [ ] **Step 3: Run formatting whitespace check**

Run:

```powershell
git diff --check
```

Expected: exit code 0. CRLF warnings are acceptable only if Git reports no whitespace errors.

- [ ] **Step 4: Run ESP-IDF compile or record limitation**

Run:

```powershell
ninja -C build esp-idf/main/CMakeFiles/__idf_main.dir/audio/audio_service.cc.obj
```

Expected: exit code 0. If CMake requires regeneration because `speaker_output_enhancer.cc` was added, run:

```powershell
idf.py build
```

Expected: exit code 0. If `idf.py` is unavailable in the current shell, add the limitation to the final report and keep the host test and `git diff --check` evidence.

- [ ] **Step 5: Commit Task 4**

Run:

```powershell
git add docs\update.md
git commit -m "docs: record speaker output enhancer"
```

Expected: commit succeeds.

---

## Self-Review

Spec coverage:

- Playback insertion point is covered by Task 3.
- Waveshare 1.46/1.46C Kconfig gate is covered by Task 3.
- EQ/DRC and software post-limiter are covered by Tasks 1 and 2.
- Conservative gain and -3 dBFS limiter ceiling are covered by Task 2.
- No microphone/wake word/AEC/VAD/protocol/uplink changes are preserved by limiting changes to playback code paths.
- Host-testable PCM-only behavior is covered by Task 1.
- Firmware/build verification and docs are covered by Task 4.

Placeholder scan:

- Each task includes concrete file paths, code blocks, commands, and expected outcomes.

Type consistency:

- `SpeakerOutputEnhancer::Initialize(int sample_rate)` returns `bool` in the header, implementation, and call sites.
- `SpeakerOutputEnhancer::Process(std::vector<int16_t>& pcm)` returns `void` in the header, implementation, and call sites.
- Limiter functions use the same names in tests and implementation: `speaker_output_limiter_peak_from_db` and `speaker_output_limiter_apply`.
