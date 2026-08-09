# Xiaoxin Boot Audio Animation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep the boot splash/animation visible through the `Boot: Audio` phase, then dismiss it after audio startup completes and before Wi-Fi startup status is shown.

**Architecture:** Reuse the existing `Display::CompleteBootSplash()` interface and the existing Xiaoxin 1.46 boot splash implementation. The application layer will stop exposing `Boot: Audio` as a visible status and will treat it as a diagnostic log stage; the splash dismissal point moves to immediately after `audio_service_.Start()` returns.

**Tech Stack:** ESP-IDF C++ application code, existing LVGL-backed display abstraction, Python path tests with `pytest`.

## Global Constraints

- Do not add a new display interface; reuse `Display::CompleteBootSplash()`.
- Do not make Wi-Fi connection success the boot animation completion condition.
- Do not make audio initialization asynchronous.
- Do not change `audio_service_.Initialize()` or `audio_service_.Start()` semantics.
- Do not redesign the boot page visuals.
- Keep `BootDiagnosticsMark("app_audio_start")` for diagnostics.
- Do not show `display->SetStatus("Boot: Audio")` in the normal user-visible startup path.
- Place `display->CompleteBootSplash()` after `audio_service_.Start()` and before `BootDiagnosticsMark("app_network_start")`.

---

## File Structure

- Modify `tests/xiaoxin_boot_diagnostics_path_test.py`: update the existing application boot path test so it locks the new order and rejects user-visible `Boot: Audio`.
- Modify `main/application.cc`: remove the visible `Boot: Audio` status update, add a diagnostic log line, and complete the boot splash after audio startup.

No new source files, display interfaces, or board-specific display code are needed.

### Task 1: Lock the Boot Audio Splash Handoff in Tests

**Files:**
- Modify: `tests/xiaoxin_boot_diagnostics_path_test.py`
- Test: `tests/xiaoxin_boot_diagnostics_path_test.py`

**Interfaces:**
- Consumes: Existing helper `function_body(source: str, signature: str) -> str`
- Produces: A path test that requires `display->CompleteBootSplash();` after `audio_service_.Start();` and before `BootDiagnosticsMark("app_network_start");`

- [ ] **Step 1: Update the failing test**

Replace the existing function `test_application_boot_records_visible_ui_audio_network_and_protocol_stages` in `tests/xiaoxin_boot_diagnostics_path_test.py` with this full function:

```python
def test_application_boot_records_ui_audio_network_and_protocol_stages():
    source = read_source(APPLICATION_SOURCE)
    initialize = function_body(source, "void Application::Initialize()")
    activation = function_body(source, "void Application::ActivationTask()")
    protocol = function_body(source, "void Application::InitializeProtocol()")

    assert '#include "boot_diagnostics.h"' in source
    assert 'BootDiagnosticsMark("app_initialize_start")' in initialize
    assert 'BootDiagnosticsMark("app_ui_ready")' in initialize
    assert 'BootDiagnosticsMark("app_audio_start")' in initialize
    assert 'BootDiagnosticsMark("app_network_start")' in initialize
    assert 'ESP_LOGI(TAG, "Boot: Audio");' in initialize
    assert 'display->SetStatus("Boot: Audio");' not in initialize
    assert 'display->SetStatus("Boot: Wi-Fi");' in initialize
    assert 'display->CompleteBootSplash();' in initialize
    assert initialize.index('BootDiagnosticsMark("app_audio_start")') < initialize.index(
        'ESP_LOGI(TAG, "Boot: Audio");'
    )
    assert initialize.index('ESP_LOGI(TAG, "Boot: Audio");') < initialize.index(
        "auto codec = board.GetAudioCodec();"
    )
    assert initialize.index("auto codec = board.GetAudioCodec();") < initialize.index(
        "audio_service_.Initialize(codec);"
    )
    assert initialize.index("audio_service_.Initialize(codec);") < initialize.index(
        "audio_service_.Start();"
    )
    assert initialize.index("audio_service_.Start();") < initialize.index(
        "display->CompleteBootSplash();"
    )
    assert initialize.index("display->CompleteBootSplash();") < initialize.index(
        'BootDiagnosticsMark("app_network_start")'
    )
    assert initialize.index('BootDiagnosticsMark("app_network_start")') < initialize.index(
        'display->SetStatus("Boot: Wi-Fi");'
    )
    assert 'BootDiagnosticsMark("activation_task_start")' in activation
    assert 'BootDiagnosticsMark("protocol_initialize_start")' in protocol
    assert 'BootDiagnosticsMark("protocol_start_done")' in protocol
```

- [ ] **Step 2: Run the targeted test and verify it fails for the intended reason**

Run:

```powershell
python -m pytest tests/xiaoxin_boot_diagnostics_path_test.py::test_application_boot_records_ui_audio_network_and_protocol_stages -q
```

Expected result before implementation:

```text
FAILED tests/xiaoxin_boot_diagnostics_path_test.py::test_application_boot_records_ui_audio_network_and_protocol_stages
AssertionError: assert 'ESP_LOGI(TAG, "Boot: Audio");' in initialize
```

The exact assertion line may differ if pytest reports the later `display->SetStatus("Boot: Audio");` rejection first, but the failure must be caused by the old visible `Boot: Audio` path still being present.

- [ ] **Step 3: Commit the failing test**

```powershell
git add -- tests/xiaoxin_boot_diagnostics_path_test.py
git commit -m "test: lock boot audio splash handoff"
```

### Task 2: Move Boot Audio From Visible Status to Diagnostic Log

**Files:**
- Modify: `main/application.cc`
- Test: `tests/xiaoxin_boot_diagnostics_path_test.py`

**Interfaces:**
- Consumes: Existing `Display::CompleteBootSplash()`
- Produces: `Application::Initialize()` sequence where Audio startup logs `Boot: Audio`, completes audio service startup, dismisses boot splash, then starts Wi-Fi status flow.

- [ ] **Step 1: Implement the minimal application change**

In `main/application.cc`, find this block inside `Application::Initialize()`:

```cpp
// Setup the audio service
BootDiagnosticsMark("app_audio_start");
display->SetStatus("Boot: Audio");
display->UpdateStatusBar(true);
auto codec = board.GetAudioCodec();
audio_service_.Initialize(codec);
audio_service_.Start();
```

Replace it with:

```cpp
// Setup the audio service
BootDiagnosticsMark("app_audio_start");
ESP_LOGI(TAG, "Boot: Audio");
auto codec = board.GetAudioCodec();
audio_service_.Initialize(codec);
audio_service_.Start();
display->CompleteBootSplash();
```

- [ ] **Step 2: Run the targeted boot diagnostics path test**

Run:

```powershell
python -m pytest tests/xiaoxin_boot_diagnostics_path_test.py::test_application_boot_records_ui_audio_network_and_protocol_stages -q
```

Expected result:

```text
1 passed
```

- [ ] **Step 3: Run the full boot diagnostics path test file**

Run:

```powershell
python -m pytest tests/xiaoxin_boot_diagnostics_path_test.py -q
```

Expected result:

```text
14 passed
```

If the count differs because more tests have been added, the acceptable result is all tests in `tests/xiaoxin_boot_diagnostics_path_test.py` passing with zero failures.

- [ ] **Step 4: Run adjacent boot splash asset/path checks**

Run:

```powershell
python -m pytest tests/xiaoxin_boot_gif_asset_test.py tests/xiaoxin_boot_diagnostics_path_test.py -q
```

Expected result:

```text
all selected tests pass with zero failures
```

- [ ] **Step 5: Commit the implementation**

```powershell
git add -- main/application.cc
git commit -m "fix: keep boot splash through audio startup"
```

### Task 3: Final Verification and Handoff

**Files:**
- No source changes expected.
- Verify: `main/application.cc`
- Verify: `tests/xiaoxin_boot_diagnostics_path_test.py`

**Interfaces:**
- Consumes: Commits from Task 1 and Task 2
- Produces: Verified branch ready for review or hardware testing

- [ ] **Step 1: Inspect final boot sequence**

Run:

```powershell
python - <<'PY'
from pathlib import Path
source = Path("main/application.cc").read_text(encoding="utf-8")
start = source.index('// Setup the audio service')
end = source.index('// Update the status bar immediately', start)
print(source[start:end])
PY
```

Expected output must include this order:

```cpp
BootDiagnosticsMark("app_audio_start");
ESP_LOGI(TAG, "Boot: Audio");
auto codec = board.GetAudioCodec();
audio_service_.Initialize(codec);
audio_service_.Start();
display->CompleteBootSplash();
```

Expected output must not include:

```cpp
display->SetStatus("Boot: Audio");
```

- [ ] **Step 2: Check working tree status**

Run:

```powershell
git status --short --branch
```

Expected result:

```text
## codex/boot-audio-animation-spec
```

If the branch line includes ahead/behind counts, that is acceptable. There must be no unstaged or staged file changes.

- [ ] **Step 3: Record hardware verification notes**

When hardware is available, verify these two manual paths:

```text
USB power: boot splash appears, no visible Boot: Audio, Wi-Fi status appears after splash exits.
Battery power: boot splash remains visible during the formerly visible Boot: Audio interval, then exits before Wi-Fi scanning status.
```

If hardware is not available in the implementation session, report that firmware path tests passed and hardware verification remains pending.
