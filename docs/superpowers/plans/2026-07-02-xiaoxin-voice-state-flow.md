# Xiaoxin Voice State Flow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Xiaoxin's voice UI follow the explicit flow `Listening -> Thinking -> Speaking`, so the top status text and Paopao GIF cannot disagree during long replies or two-board conversations.

**Architecture:** Add `kDeviceStateThinking` as a first-class application state between listening and speaking. Treat `Application` device state as the authority for voice phase, and make chat subtitle updates display text without independently forcing Paopao into speaking unless the authoritative state is already speaking.

**Tech Stack:** ESP-IDF C/C++, existing `DeviceStateMachine`, `Application` event loop, Waveshare 1.46 Paopao LVGL display, Python `pytest` source-path tests.

## Global Constraints

- Scope is only voice-phase state consistency for Xiaoxin / Waveshare ESP32-S3 Touch LCD 1.46 behavior.
- Preserve the intended voice flow: `Listening` means microphone/input is active, `Thinking` means input ended and the assistant is preparing, `Speaking` means TTS/output is active.
- Do not add new GIF assets.
- Do not rewrite the whole `PaopaoPetDisplay` class.
- Do not make assistant subtitle text the authority for speaking animation.
- Keep manual and wake-word interruption behavior working.

---

## File Structure

- Modify `main/device_state.h`
  - Add `kDeviceStateThinking`.
- Modify `main/device_state_machine.cc`
  - Add the `thinking` state name and valid transitions.
- Modify `main/assets/lang_config.h`
  - Add `Lang::Strings::THINKING = "思考中..."`.
- Modify `main/application.cc`
  - Drive `Listening -> Thinking -> Speaking` from protocol events and handle the new state in `HandleStateChangedEvent()`.
- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
  - Recognize `Lang::Strings::THINKING` in `SetStatus()`.
  - Gate assistant-message speaking GIF updates behind `Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking`.
- Create `tests/xiaoxin_voice_state_flow_path_test.py`
  - Path tests for the new state, transitions, protocol event ordering, and assistant subtitle gating.
- Modify `tests/xiaoxin_pet_mood_integration_path_test.py`
  - Update existing expectations around assistant chat events if they currently assume every assistant message unconditionally dispatches speaking.

---

### Task 1: Add The Thinking Device State

**Files:**
- Modify: `main/device_state.h`
- Modify: `main/device_state_machine.cc`
- Modify: `main/assets/lang_config.h`
- Modify: `main/application.cc`
- Create: `tests/xiaoxin_voice_state_flow_path_test.py`

**Interfaces:**
- Consumes: existing `DeviceState`, `DeviceStateMachine::TransitionTo()`, and `Application::HandleStateChangedEvent()`.
- Produces:
  - `kDeviceStateThinking`
  - `Lang::Strings::THINKING`
  - A state-machine path where `Listening -> Thinking -> Speaking`, `Thinking -> Listening`, and `Thinking -> Idle` are valid.

- [ ] **Step 1: Write the failing path tests**

Create `tests/xiaoxin_voice_state_flow_path_test.py`:

```python
from pathlib import Path


DEVICE_STATE = Path("main/device_state.h")
STATE_MACHINE = Path("main/device_state_machine.cc")
APPLICATION = Path("main/application.cc")
LANG_CONFIG = Path("main/assets/lang_config.h")
PAOPAO_DISPLAY = Path("main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc")


def read_source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"function body not found: {signature}")


def switch_case(source: str, marker: str, next_marker: str) -> str:
    start = source.index(marker)
    end = source.index(next_marker, start)
    return source[start:end]


def test_thinking_state_is_declared_between_listening_and_speaking():
    source = read_source(DEVICE_STATE)

    assert "kDeviceStateListening" in source
    assert "kDeviceStateThinking" in source
    assert "kDeviceStateSpeaking" in source
    assert source.index("kDeviceStateListening") < source.index("kDeviceStateThinking")
    assert source.index("kDeviceStateThinking") < source.index("kDeviceStateSpeaking")


def test_state_machine_names_and_transitions_include_thinking():
    source = read_source(STATE_MACHINE)

    assert '"thinking"' in source

    listening_case = switch_case(source, "case kDeviceStateListening:", "case kDeviceStateThinking:")
    assert "to == kDeviceStateThinking" in listening_case
    assert "to == kDeviceStateSpeaking" in listening_case
    assert "to == kDeviceStateIdle" in listening_case

    thinking_case = switch_case(source, "case kDeviceStateThinking:", "case kDeviceStateSpeaking:")
    assert "to == kDeviceStateSpeaking" in thinking_case
    assert "to == kDeviceStateListening" in thinking_case
    assert "to == kDeviceStateIdle" in thinking_case


def test_lang_config_exposes_thinking_status_text():
    source = read_source(LANG_CONFIG)

    assert 'constexpr const char* THINKING = "思考中...";' in source


def test_application_renders_thinking_state_as_status_and_pet_thinking():
    body = function_body(read_source(APPLICATION), "void Application::HandleStateChangedEvent()")

    thinking_case = switch_case(body, "case kDeviceStateThinking:", "case kDeviceStateSpeaking:")
    assert "display->SetStatus(Lang::Strings::THINKING);" in thinking_case
    assert 'display->SetEmotion("neutral");' in thinking_case
    assert "audio_service_.EnableVoiceProcessing(false);" in thinking_case
    assert "audio_service_.EnableWakeWordDetection(false);" in thinking_case
```

- [ ] **Step 2: Run the failing tests**

Run:

```powershell
python -m pytest tests/xiaoxin_voice_state_flow_path_test.py -q
```

Expected: fail because `kDeviceStateThinking`, `"thinking"`, and `Lang::Strings::THINKING` do not exist yet.

- [ ] **Step 3: Add `kDeviceStateThinking`**

Change `main/device_state.h` enum to:

```cpp
enum DeviceState {
    kDeviceStateUnknown,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateThinking,
    kDeviceStateSpeaking,
    kDeviceStateUpgrading,
    kDeviceStateActivating,
    kDeviceStateAudioTesting,
    kDeviceStateFatalError
};
```

- [ ] **Step 4: Add state-machine name and transitions**

In `main/device_state_machine.cc`, update `STATE_STRINGS`:

```cpp
static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "wifi_configuring",
    "idle",
    "connecting",
    "listening",
    "thinking",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state"
};
```

Update the listening and thinking transition cases:

```cpp
        case kDeviceStateListening:
            // Can go to thinking after user input ends, speaking on direct TTS, or idle on stop
            return to == kDeviceStateThinking ||
                   to == kDeviceStateSpeaking ||
                   to == kDeviceStateIdle;

        case kDeviceStateThinking:
            // Can go to speaking when TTS starts, listening for continuous mode, or idle on cancel/stop
            return to == kDeviceStateSpeaking ||
                   to == kDeviceStateListening ||
                   to == kDeviceStateIdle;

        case kDeviceStateSpeaking:
            // Can go to listening or idle
            return to == kDeviceStateListening ||
                   to == kDeviceStateIdle;
```

- [ ] **Step 5: Add the Thinking status string**

In `main/assets/lang_config.h`, add this near `LISTENING` and `SPEAKING`:

```cpp
        constexpr const char* THINKING = "思考中...";
```

- [ ] **Step 6: Render the Thinking state in `Application`**

In `Application::HandleStateChangedEvent()` in `main/application.cc`, insert this case between listening and speaking:

```cpp
        case kDeviceStateThinking:
            display->SetStatus(Lang::Strings::THINKING);
            display->SetEmotion("neutral");
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
```

- [ ] **Step 7: Run the tests**

Run:

```powershell
python -m pytest tests/xiaoxin_voice_state_flow_path_test.py -q
```

Expected: all tests in `tests/xiaoxin_voice_state_flow_path_test.py` pass.

- [ ] **Step 8: Commit**

```powershell
git add main/device_state.h main/device_state_machine.cc main/assets/lang_config.h main/application.cc tests/xiaoxin_voice_state_flow_path_test.py
git commit -m "feat: add xiaoxin thinking voice state"
```

---

### Task 2: Drive Listening To Thinking From STT/Input Completion

**Files:**
- Modify: `main/application.cc`
- Modify: `tests/xiaoxin_voice_state_flow_path_test.py`

**Interfaces:**
- Consumes: `kDeviceStateThinking` from Task 1.
- Produces: protocol event flow where STT text transitions from `Listening` to `Thinking` before assistant TTS starts.

- [ ] **Step 1: Add failing tests for protocol event ordering**

Append these tests to `tests/xiaoxin_voice_state_flow_path_test.py`:

```python
def test_stt_text_moves_listening_to_thinking_before_user_subtitle():
    body = function_body(read_source(APPLICATION), "void Application::InitializeProtocol()")
    stt_section = switch_case(body, 'strcmp(type->valuestring, "stt") == 0', 'strcmp(type->valuestring, "llm") == 0')

    assert "SetDeviceState(kDeviceStateThinking);" in stt_section
    assert 'display->SetChatMessage("user", message.c_str());' in stt_section
    assert stt_section.index("SetDeviceState(kDeviceStateThinking);") < stt_section.index('display->SetChatMessage("user", message.c_str());')


def test_tts_start_moves_thinking_to_speaking_and_tts_stop_leaves_speaking():
    body = function_body(read_source(APPLICATION), "void Application::InitializeProtocol()")
    tts_section = switch_case(body, 'strcmp(type->valuestring, "tts") == 0', 'strcmp(type->valuestring, "stt") == 0')

    assert "SetDeviceState(kDeviceStateSpeaking);" in tts_section
    assert "if (GetDeviceState() == kDeviceStateSpeaking)" in tts_section
    assert "SetDeviceState(kDeviceStateListening);" in tts_section
    assert "SetDeviceState(kDeviceStateIdle);" in tts_section
```

- [ ] **Step 2: Run the failing tests**

Run:

```powershell
python -m pytest tests/xiaoxin_voice_state_flow_path_test.py::test_stt_text_moves_listening_to_thinking_before_user_subtitle tests/xiaoxin_voice_state_flow_path_test.py::test_tts_start_moves_thinking_to_speaking_and_tts_stop_leaves_speaking -q
```

Expected: the STT test fails because the STT handler currently only updates the user subtitle.

- [ ] **Step 3: Update the STT scheduled task**

In `main/application.cc`, change the STT schedule block to:

```cpp
                Schedule([this, display, message = NormalizeXiaoxinDeviceName(std::string(text->valuestring))]() {
                    if (GetDeviceState() == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateThinking);
                    }
                    display->SetChatMessage("user", message.c_str());
                });
```

- [ ] **Step 4: Run the focused tests**

Run:

```powershell
python -m pytest tests/xiaoxin_voice_state_flow_path_test.py::test_stt_text_moves_listening_to_thinking_before_user_subtitle tests/xiaoxin_voice_state_flow_path_test.py::test_tts_start_moves_thinking_to_speaking_and_tts_stop_leaves_speaking -q
```

Expected: both tests pass.

- [ ] **Step 5: Commit**

```powershell
git add main/application.cc tests/xiaoxin_voice_state_flow_path_test.py
git commit -m "fix: move xiaoxin to thinking after stt"
```

---

### Task 3: Stop Assistant Subtitles From Forcing Speaking GIF

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Modify: `tests/xiaoxin_voice_state_flow_path_test.py`
- Modify: `tests/xiaoxin_pet_mood_integration_path_test.py`

**Interfaces:**
- Consumes: `Application::GetInstance().GetDeviceState()`.
- Produces: assistant subtitles can stream without changing Paopao to speaking unless `Application` is already in `kDeviceStateSpeaking`.

- [ ] **Step 1: Add failing tests for status mapping and assistant gating**

Append these tests to `tests/xiaoxin_voice_state_flow_path_test.py`:

```python
def test_paopao_status_recognizes_thinking_status():
    body = function_body(read_source(PAOPAO_DISPLAY), "virtual void SetStatus(const char* status) override")

    assert "StatusEquals(status, Lang::Strings::THINKING)" in body
    assert "PAOPAO_PET_TRIGGER_THINKING" in body
    assert "PAOPAO_PET_BEHAVIOR_VOICE_THINKING" in body


def test_assistant_subtitle_only_sets_speaking_pet_when_device_is_speaking():
    body = function_body(read_source(PAOPAO_DISPLAY), "virtual void SetChatMessage(const char* role, const char* content) override")
    assistant_section = switch_case(body, 'std::strcmp(role, "assistant") == 0', "}")

    assert "Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking" in assistant_section
    assert "PAOPAO_PET_TRIGGER_SPEAKING" in assistant_section
```

- [ ] **Step 2: Run the failing tests**

Run:

```powershell
python -m pytest tests/xiaoxin_voice_state_flow_path_test.py::test_paopao_status_recognizes_thinking_status tests/xiaoxin_voice_state_flow_path_test.py::test_assistant_subtitle_only_sets_speaking_pet_when_device_is_speaking -q
```

Expected: both tests fail because thinking status is not recognized and assistant subtitle still unconditionally dispatches speaking.

- [ ] **Step 3: Map Thinking status to Paopao thinking GIF**

In `SetStatus()` in `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`, change the thinking branch to:

```cpp
        } else if (StatusEquals(status, Lang::Strings::THINKING) ||
                   Contains(status, "Thinking") || Contains(status, "thinking")) {
            DispatchPetVoiceState(
                PAOPAO_PET_TRIGGER_THINKING,
                PAOPAO_PET_BEHAVIOR_VOICE_THINKING
            );
```

- [ ] **Step 4: Gate assistant subtitle speaking dispatch**

In `SetChatMessage()` in the same file, change the assistant branch to:

```cpp
        } else if (std::strcmp(role, "assistant") == 0) {
            if (Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking) {
                DispatchPetVoiceState(
                    PAOPAO_PET_TRIGGER_SPEAKING,
                    PAOPAO_PET_BEHAVIOR_VOICE_SPEAKING,
                    PAOPAO_PET_MOOD_EVENT_ASSISTANT_REPLY
                );
            } else {
                DispatchPetMoodEvent(PAOPAO_PET_MOOD_EVENT_ASSISTANT_REPLY);
            }
        }
```

- [ ] **Step 5: Update the existing mood integration test if needed**

If `tests/xiaoxin_pet_mood_integration_path_test.py::test_status_and_chat_events_update_mood_without_bypassing_trigger_state` fails because it expects unconditional assistant speaking dispatch, replace its assistant-specific assertions with:

```python
    assert "PAOPAO_PET_MOOD_EVENT_ASSISTANT_REPLY" in chat_body
    assert "Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking" in chat_body
```

- [ ] **Step 6: Run the focused tests**

Run:

```powershell
python -m pytest tests/xiaoxin_voice_state_flow_path_test.py tests/xiaoxin_pet_mood_integration_path_test.py -q
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc tests/xiaoxin_voice_state_flow_path_test.py tests/xiaoxin_pet_mood_integration_path_test.py
git commit -m "fix: gate xiaoxin assistant speaking animation"
```

---

### Task 4: Verify Full Voice State Flow

**Files:**
- Test: `tests/xiaoxin_voice_state_flow_path_test.py`
- Test: `tests/xiaoxin_pet_mood_integration_path_test.py`
- Test: `tests/xiaoxin_boot_listening_toggle_test.py`

**Interfaces:**
- Consumes: all tasks above.
- Produces: verified state-flow behavior and a manual hardware logging checklist.

- [ ] **Step 1: Run focused Python tests**

Run:

```powershell
python -m pytest tests/xiaoxin_voice_state_flow_path_test.py tests/xiaoxin_pet_mood_integration_path_test.py tests/xiaoxin_boot_listening_toggle_test.py -q
```

Expected: all selected tests pass.

- [ ] **Step 2: Run the broader Xiaoxin path test subset**

Run:

```powershell
python -m pytest tests/xiaoxin_*_path_test.py tests/xiaoxin_*_integration_path_test.py -q
```

Expected: all collected path tests pass.

- [ ] **Step 3: Build the firmware**

Run the repo's normal ESP-IDF build command from this workspace:

```powershell
idf.py build
```

Expected: build completes successfully and generates `build/ai_pet.bin`.

- [ ] **Step 4: Hardware log verification**

Flash two boards and reproduce the board-to-board conversation. In serial logs, confirm this order for a normal turn:

```text
State: idle -> listening
Playing Paopao GIF state=waiting.gif
State: listening -> thinking
Playing Paopao GIF state=thinking.gif
State: thinking -> speaking
Playing Paopao GIF state=speaking_fixed.gif
State: speaking -> listening
Playing Paopao GIF state=waiting.gif
```

Also confirm this negative case after `State: speaking -> listening`:

```text
No later assistant sentence_start causes Playing Paopao GIF state=speaking_fixed.gif unless a new State: ... -> speaking appears first.
```

- [ ] **Step 5: Commit verification notes if a docs file is used**

If a hardware verification note is added, save it as `docs/troubleshooting/xiaoxin-voice-state-flow.zh-CN.md` and commit:

```powershell
git add docs/troubleshooting/xiaoxin-voice-state-flow.zh-CN.md
git commit -m "docs: record xiaoxin voice state verification"
```

If no hardware note is added, do not create a docs commit.

---

## Self-Review

- Spec coverage: Task 1 adds the `Thinking` state and top status rendering. Task 2 drives `Listening -> Thinking -> Speaking` from protocol events. Task 3 prevents assistant subtitle updates from overriding the authoritative device state. Task 4 verifies tests, build, and real two-board logs.
- Placeholder scan: The plan contains no `TBD`, `TODO`, or unspecified implementation steps.
- Type consistency: `kDeviceStateThinking`, `Lang::Strings::THINKING`, `PAOPAO_PET_TRIGGER_THINKING`, and `PAOPAO_PET_BEHAVIOR_VOICE_THINKING` are consistently used across tests and implementation steps.
