# 扬声器输出增强器设计

## 背景

当前固件的播放链路集中在 `AudioService`：

- 云端 TTS/电话类下行音频进入 `audio_decode_queue_`。
- `OpusCodecTask` 解码为 16-bit PCM，必要时重采样到 codec 输出采样率。
- `AudioOutputTask` 从 `audio_playback_queue_` 取 PCM 并调用 `AudioCodec::OutputData()`。
- `PlaySound()` 播放的 OGG 提示音也会 demux 成 Opus 包，再走同一解码和播放队列。

因此第一版增强应放在 `AudioOutputTask` 和 `AudioCodec::OutputData()` 之间，对所有扬声器播放内容统一生效，同时不影响麦克风采集、唤醒词、AEC、VAD 和上行 Opus 编码。

## 目标

在小尺寸手表喇叭上提升提示音、TTS 和电话类人声的听感响度，避免只是粗暴增大音量导致削波、破音或低频浪费。

第一版目标：

- 削减小喇叭难以有效重放的低频能量。
- 适度突出人声和提示音可懂度频段。
- 压缩动态范围，让较小的人声细节更容易听见。
- 对最终 PCM 做限幅，避免增强后超过 `int16_t` 安全范围。
- 保持实现可关闭、参数集中，方便后续实机调音。

## 非目标

- 不改麦克风输入链路。
- 不改唤醒词、语音识别、服务端协议和 AEC 行为。
- 不引入 SpeexDSP、WebRTC APM 或 npm/Web Audio 依赖。
- 不在第一版做离线重制 OGG 素材；离线素材处理可作为后续补充。
- 不实现复杂的多板级自动声学校准。

## 方案

新增一个播放端模块 `SpeakerOutputEnhancer`，由 `AudioService` 持有并在播放前调用：

```text
Opus/OGG packet
  -> Opus decoder
  -> optional output resampler
  -> SpeakerOutputEnhancer::Process()
  -> AudioCodec::OutputData()
  -> codec volume / board output boost
  -> I2S / speaker
```

模块职责：

- 初始化并封装乐鑫 `esp_audio_effects` 中的 EQ / DRC 能力，并在其后追加软件 limiter；ALC 仅作为 DRC 实测不合适时的后续备选。
- 在输出采样率变化时按采样率重新配置处理器。
- 对每个 PCM frame 原地处理，减少额外分配。
- 如果效果器初始化失败，自动退回到轻量软件后级限幅路径，保证播放不中断。
- 暴露一个小的参数结构体，集中管理默认调音参数。

第一版采用保守的单通道配置，因为当前下行 Opus 解码和输出重采样路径按 mono PCM 处理。

`AudioTask` 中的 `timestamp` 等元数据只服务于 server AEC 和队列追踪，增强器不读取、不修改这些字段。增强器只接收 `task->pcm`，保持协议状态和播放元数据与音色处理解耦。

### 与 codec 音量级的关系

`SpeakerOutputEnhancer` 位于 `AudioService` 播放队列和 `AudioCodec::OutputData()` 之间，因此它处理的是 16-bit PCM。部分 codec 实现之后还会有自己的输出增益级。当前 `NoAudioCodec::Write()` 会将增强后的 `int16_t` PCM 乘以：

```text
pow(output_volume / 100.0, 2) * 65536 * output_boost
```

再写入 32-bit I2S buffer。当前 Waveshare 1.46/1.46C 板级还通过 `output_boost_` 提高 I2S 扬声器输出响度。

第一版削波策略要遵守两点：

- enhancer 的 limiter 只保证自己的 `int16_t` 输出不过载，并给后续 codec 增益留 headroom。
- 不在 enhancer 中再做大幅全局 makeup gain；响度主要来自低频削减、人声频段 EQ、适度动态压缩和 codec 既有音量/boost。

实现时先保持插入点不变，因为这样能覆盖所有播放来源且不改各 codec 的硬件写入逻辑。如果实机发现 `output_boost_` 后仍有削波，应优先下调 enhancer makeup gain 或 limiter ceiling；必要时再把 NoAudioCodec 的最终 32-bit 写入限幅也纳入同一调音任务，而不是让两个增益级各自抢响度。

## API 草图

实现阶段建议把 ESP-IDF 依赖封装在 `SpeakerOutputEnhancer` 内，`AudioService` 只看到一个小接口：

```cpp
class SpeakerOutputEnhancer {
public:
    struct Config {
        bool enabled = true;
        bool enable_eq = true;
        bool enable_drc = true;
        bool enable_limiter = true;  // Software post-limiter after EQ/DRC.
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
};
```

`Initialize()` 使用 `bool` 返回值，避免把 `esp_audio_effects` 的 `esp_ae_err_t` 暴露给调用方；内部记录具体错误码。`Process()` 使用 `void` 返回值：播放路径优先不断声，内部失败时记录日志并 fallback 到软件 limiter-only 或 bypass。需要诊断时通过日志和 `IsActive()` 观察状态。

## 初始调音

默认参数以“更响但不刺耳”为目标，具体数值需要实机确认后再微调：

- High-pass：150 Hz，Q 0.7，0 dB，用于减少无效低频和振膜行程。
- Low-shelf：220 Hz，Q 0.7，-2.5 dB，给小喇叭进一步减负。
- Presence boost：2.1 kHz，Q 1.0，+2.5 dB，提升人声存在感。
- Clarity boost：3.8 kHz，Q 1.0，+1.5 dB，提升提示音穿透力。
- DRC：轻到中等压缩，attack 8-12 ms，release 120-180 ms，hold 0 ms，knee 3 dB。`esp_ae_drc` 使用曲线点而不是 ratio 字段，初始曲线建议为 `{0, -3}`、`{-18, -15}`、`{-60, -55}`、`{-100, -100}`，表达“峰值留余量、主体轻压缩、低电平少抬升”的保守起点。
- Makeup gain：从 +1.5 dB 起步，不超过 +3 dB；若板级 `output_boost_` 已启用，优先保持 +1.5 dB 或更低。
- Limiter：最终峰值先限制在约 -3 dBFS，给 `NoAudioCodec::Write()` 的音量和 `output_boost_` 留余量；实机确认无削波后再评估是否提高到 -2 dBFS。

参数文件不分散到业务代码里。若使用 `esp_audio_effects` API 时某些滤波器模式或曲线点需要更具体的结构体配置，实现阶段以组件头文件和示例为准，但语义保持上述目标。

短时提示音需要单独试听。`PlaySound()` 的启动成功音、popup 和错误提示音通常很短，DRC attack 过慢会让开头变弱，attack 过快又可能削掉瞬态。第一版不按音频来源分流，所有播放内容走同一增强链路；如果实机发现短 OGG 提示音变钝，后续再为 `AudioTask` 增加播放来源枚举，并让提示音走更轻的 DRC 或只走 EQ/limiter。

## 配置与降级

新增编译期配置建议：

- `CONFIG_SPEAKER_OUTPUT_ENHANCER`：放在 `main/Kconfig.projbuild` 的音频配置区，靠近 `USE_AUDIO_PROCESSOR` / `USE_AUDIO_DEBUGGER`。
- 默认先 `y`，并直接 `depends on BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_46`。当前项目实际目标为 Waveshare ESP32-S3-Touch-LCD-1.46/1.46C，对应仓库配置 `BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_46`；第一版不默认改变其他板子的声音。

新增运行时行为：

- `SpeakerOutputEnhancer::Initialize(sample_rate)` 失败时记录日志，并进入 bypass 或软件 limiter-only 模式。
- `Process()` 接收空 buffer 时直接返回。
- 如果输出采样率与已初始化采样率不同，重新初始化处理器。这是防御性设计；当前 Waveshare 1.46/1.46C 板级配置的输出采样率为 24000 Hz，正常运行时不应频繁触发。
- `ResetDecoder()` 不需要重置增强器状态；增强器只处理播放端连续 PCM，不持有协议状态。

## 资源预算

第一版不能假设 EQ + DRC 免费。实现和验收要记录：

- 每个 60 ms PCM frame 的处理耗时，目标是在 `AudioOutputTask` 中保持明显低于 60 ms，避免播放队列堆积。
- enhancer 初始化后的额外 heap 占用，尤其是 `esp_audio_effects` handle 和内部 delay / smoothing buffer。
- `AudioOutputTask` 当前优先级为 4，第一版不主动调高任务优先级；只有实测出现输出 underrun 或队列积压时再调整。
- 如果完整 EQ + DRC 开销过高，降级顺序为：降低 EQ band 数量，关闭 DRC 保留 limiter，最后完全 bypass。

## 错误处理

- 效果器打开失败：不阻断音频，打印一次错误日志，继续播放原始 PCM。
- 效果器处理失败：打印限频警告，当前 frame 走软件限幅，后续 frame 继续尝试或进入 bypass。
- PCM 溢出：所有软件增益或 fallback 路径都使用饱和裁剪到 `int16_t` 范围。
- 采样率异常：跳过效果器，只做限幅。

## 测试策略

主机侧单元测试优先覆盖不依赖 ESP-IDF 二进制库的部分：

- 软件限幅不会溢出 `int16_t`。
- 空数据、正常语音幅度、超大幅度输入都能稳定返回。
- 增强器 disabled/bypass 路径不改变 frame 长度。
- `SpeakerOutputEnhancer` 主机侧单元测试只覆盖 PCM-only 行为；它不接收 `AudioTask`，因此不测试 `timestamp` 等播放元数据。

需要固件或实机测试的部分：

- `esp_audio_effects` EQ / DRC 参数能成功初始化。
- EQ / DRC 处理后的听感和削波风险。
- 处理耗时、heap 占用和播放队列是否积压。
- codec 音量与 `output_boost_` 叠加后的最大音量表现。

固件构建验证：

- 至少编译 `audio_service.cc` 相关目标，确认新增头文件、组件依赖和 Kconfig 配置可用。
- 如果当前 shell 没有 ESP-IDF 环境，则记录无法完整 `idf.py build` 的原因。

实机验证：

- 播放启动成功音、popup、错误提示音，确认无爆音。
- 播放 TTS 中文人声，确认主观响度提升且齿音不过度。
- 在最大音量下确认没有持续削波或明显破音。
- 切换聆听/说话状态，确认麦克风、唤醒词和对话状态不回归。

## 后续可选增强

- 为 Waveshare 1.46 目标板单独保存一组实机调音参数。
- 增加设置页中的“响度增强”开关。
- 用 FFmpeg/SoX 批量生成手表喇叭版 OGG 提示音，减少实时处理负担。
- 如果未来硬件换成 smart amp，保留软件 EQ/DRC 作为前级或改为板级可选。
