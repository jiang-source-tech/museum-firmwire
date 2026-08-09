# Xiaoxin Low Battery Shutdown Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Use the existing GPIO8 / ADC1_CH7 battery divider and GPIO7 power latch to show a low-battery warning, then actively power off before brownout reboot loops.

**Architecture:** Battery decisions come from `xiaoxin_battery_state_update()` snapshots, not board-local LOW / CRITICAL voltage thresholds. The board periodically samples ADC, feeds the battery state machine, reports battery level from the latest reliable snapshot, shows LOW once via `low_edge`, and starts a cancelable CRITICAL shutdown via `critical_edge`. Startup protection is a separate early guard: `RuntimeHealthProtectionRecommended()` is the immediate NVS-based signal, and the state machine takes over only after it has enough runtime samples.

**Tech Stack:** ESP-IDF C++, FreeRTOS, `esp_timer`, ADC oneshot calibration, existing C battery/power/runtime-health models, Python `pytest` source-path tests, C model tests compiled with `gcc`.

## Global Constraints

- Hardware target is Waveshare `ESP32-S3-Touch-LCD-1.46C`.
- Do not add a fuel gauge, PMIC, or new hardware dependency.
- Do not implement a "critical survival mode"; LOW may warn, but it must not stop the pet, animation, Wi-Fi, or audio as the core experience.
- LOW / CRITICAL thresholds are the existing state-machine percent thresholds: LOW at `<=20%` (about `3700mV`), CRITICAL at `<=10%` (about `3600mV`), with existing confirmation windows.
- Board code must not define a parallel LOW / CRITICAL voltage decision path.
- Trigger LOW UI from `snapshot.low_edge`, not repeated `state == LOW` polling.
- Trigger CRITICAL shutdown from `snapshot.critical_edge`, not repeated `state == CRITICAL` polling.
- Low-battery shutdown must release `PWR_Control_PIN`; `esp_deep_sleep_start()` is best-effort after the rail is released.
- Low-battery shutdown must not call `esp_restart()`.
- USB/external power must not trigger low-battery shutdown, and a CRITICAL warning window must cancel shutdown if external power or recovery is seen before GPIO7 is released.
- In startup protection, `RuntimeHealthProtectionRecommended()` is the fast early signal; state-machine CRITICAL confirmation cannot be required before full application startup decisions.

---

## File Structure

- `tests/xiaoxin_battery_state_test.c`: C unit coverage for UNKNOWN -> CRITICAL and one-shot edge semantics used by board code.
- `tests/xiaoxin_power_control_test.c`: C unit coverage for a semantic shutdown request API shared by manual and low-battery shutdown.
- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_power_control.h`: declare `xiaoxin_power_control_request_shutdown()`.
- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_power_control.c`: implement the semantic shutdown request API.
- `tests/xiaoxin_low_battery_shutdown_path_test.py`: board source-path tests for runtime ADC monitor wiring, LOW notification, CRITICAL shutdown, cancelation, startup protection, and diagnostics.
- `tests/xiaoxin_power_latch_path_test.py`: replace the old "battery reporting disabled" expectation with snapshot-backed `GetBatteryLevel()` behavior.
- `tests/xiaoxin_runtime_health_path_test.py`: change runtime-health expectation from "not wired into board" to "used by startup protection".
- `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`: include battery state, add monitor fields/timers, sample ADC periodically, wire LOW/CRITICAL behavior, startup guard, `GetBatteryLevel()`, and `battery` console command.
- `main/boards/common/runtime_health_model.h`: add low-battery shutdown diagnostic fields and pure model API.
- `main/boards/common/runtime_health_model.c`: record low-battery shutdown count, voltage, and startup/runtime stage.
- `main/boards/common/runtime_health.h`: declare `RuntimeHealthRecordLowBatteryShutdown(int voltage_mv, bool startup_stage)`.
- `main/boards/common/runtime_health.cc`: persist and expose low-battery shutdown diagnostics.
- `tests/xiaoxin_runtime_health_model_test.c`: C unit coverage for low-battery shutdown diagnostics.
- `docs/xiaoxin-serial-debug-commands.zh-CN.md`: document the `battery` command as USB-attached auxiliary diagnostics, and document `runtime_health` as the post-shutdown way to read persisted low-battery records.

---

### Task 1: Lock State-Machine Edge Semantics

**Files:**
- Modify: `tests/xiaoxin_battery_state_test.c`

**Interfaces:**
- Consumes: `xiaoxin_battery_state_update(...)`, `xiaoxin_battery_state_snapshot(...)`.
- Produces: Verified expectations that board code may rely on `low_edge`, `critical_edge`, and `recovered_edge` as one-shot events.

- [ ] **Step 1: Add failing C tests**

In `tests/xiaoxin_battery_state_test.c`, add these functions after `startup_sustained_critical_enters_critical()`:

```c
static void unknown_can_enter_critical_without_low_first(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);

  uint32_t now_ms = 1000;
  xiaoxin_battery_snapshot_t snapshot = feed(
    &ctx,
    3600,
    XIAOXIN_BATTERY_LOAD_IDLE,
    now_ms
  );
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_UNKNOWN);

  for (; snapshot.state != XIAOXIN_BATTERY_STATE_CRITICAL && now_ms <= 60000; now_ms += 1000) {
    snapshot = feed(&ctx, 3600, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }

  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_CRITICAL);
  assert(snapshot.critical_edge);
  assert(!snapshot.low_edge);
}

static void critical_edge_is_one_shot_until_recovery(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  enter_battery_from_startup(&ctx);

  uint32_t now_ms = 18000;
  xiaoxin_battery_snapshot_t snapshot = feed(
    &ctx,
    3600,
    XIAOXIN_BATTERY_LOAD_IDLE,
    now_ms
  );
  for (; snapshot.state != XIAOXIN_BATTERY_STATE_CRITICAL && now_ms <= 90000; now_ms += 1000) {
    snapshot = feed(&ctx, 3600, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }

  assert(snapshot.state == XIAOXIN_BATTERY_STATE_CRITICAL);
  assert(snapshot.critical_edge);

  snapshot = feed(&ctx, 3600, XIAOXIN_BATTERY_LOAD_IDLE, now_ms + 1000);
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_CRITICAL);
  assert(!snapshot.critical_edge);
  assert(!snapshot.low_edge);

  for (; snapshot.state != XIAOXIN_BATTERY_STATE_LOW && now_ms <= 130000; now_ms += 1000) {
    snapshot = feed(&ctx, 3680, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }

  assert(snapshot.state == XIAOXIN_BATTERY_STATE_LOW);
  assert(snapshot.recovered_edge);
}
```

Then call them from `main()` immediately after `startup_sustained_critical_enters_critical();`:

```c
  unknown_can_enter_critical_without_low_first();
  critical_edge_is_one_shot_until_recovery();
```

- [ ] **Step 2: Run the state test**

Run:

```powershell
if (!(Test-Path build)) { New-Item -ItemType Directory build | Out-Null }
gcc -std=c11 -Wall -Wextra -Werror `
  tests\xiaoxin_battery_state_test.c `
  main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_battery_state.c `
  main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_battery_level.c `
  -I main\boards\waveshare\esp32-s3-touch-lcd-1.46 `
  -o build\xiaoxin_battery_state_test.exe
.\build\xiaoxin_battery_state_test.exe
```

Expected: the test should pass if the existing implementation already supports the spec. If it fails, the failure identifies a state-machine mismatch that must be fixed in `xiaoxin_battery_state.c` before board wiring starts.

- [ ] **Step 3: Fix only if the new tests fail**

If `unknown_can_enter_critical_without_low_first` fails, inspect `desired_state_for()` in `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_battery_state.c` and make the UNKNOWN branch choose CRITICAL before LOW:

```c
if (ctx->state == XIAOXIN_BATTERY_STATE_UNKNOWN) {
  if (percent <= k_low_to_critical_percent) {
    return XIAOXIN_BATTERY_STATE_CRITICAL;
  }
  if (percent <= k_normal_to_low_percent) {
    return XIAOXIN_BATTERY_STATE_LOW;
  }
  return XIAOXIN_BATTERY_STATE_NORMAL;
}
```

If `critical_edge_is_one_shot_until_recovery` fails, ensure the code that builds the snapshot sets edges only from `previous_state` to `ctx->state` transitions:

```c
snapshot.low_edge =
  previous_state != XIAOXIN_BATTERY_STATE_LOW &&
  ctx->state == XIAOXIN_BATTERY_STATE_LOW;
snapshot.critical_edge =
  previous_state != XIAOXIN_BATTERY_STATE_CRITICAL &&
  ctx->state == XIAOXIN_BATTERY_STATE_CRITICAL;
snapshot.recovered_edge =
  previous_state == XIAOXIN_BATTERY_STATE_CRITICAL &&
  ctx->state == XIAOXIN_BATTERY_STATE_LOW;
```

- [ ] **Step 4: Run the state test again**

Run the command from Step 2 again.

Expected: executable prints `xiaoxin_battery_state tests passed`.

- [ ] **Step 5: Commit**

```powershell
git add tests\xiaoxin_battery_state_test.c main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_battery_state.c
git commit -m "test: lock low battery state edges"
```

If `xiaoxin_battery_state.c` did not change, stage only `tests\xiaoxin_battery_state_test.c`.

---

### Task 2: Add Semantic Shutdown Request API

**Files:**
- Modify: `tests/xiaoxin_power_control_test.c`
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_power_control.h`
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_power_control.c`
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`

**Interfaces:**
- Produces: `void xiaoxin_power_control_request_shutdown(xiaoxin_power_control_t* power);`
- Board code may call this from manual long press and low-battery shutdown without pretending every shutdown came from a long press.

- [ ] **Step 1: Add failing power-control test**

In `tests/xiaoxin_power_control_test.c`, add:

```c
static void request_shutdown_matches_long_press_latch_behavior(void) {
    xiaoxin_power_control_t power;
    xiaoxin_power_control_init(&power);

    xiaoxin_power_control_request_shutdown(&power);

    assert(!xiaoxin_power_control_power_hold(&power));
    assert(!xiaoxin_power_control_backlight_on(&power));
    assert(xiaoxin_power_control_shutdown_requested(&power));

    xiaoxin_power_control_request_shutdown(&power);

    assert(!xiaoxin_power_control_power_hold(&power));
    assert(!xiaoxin_power_control_backlight_on(&power));
    assert(xiaoxin_power_control_shutdown_requested(&power));
}
```

Call it from `main()` after `long_press_requests_shutdown_instead_of_toggling();`:

```c
    request_shutdown_matches_long_press_latch_behavior();
```

- [ ] **Step 2: Run the power-control test and verify it fails**

Run:

```powershell
if (!(Test-Path build)) { New-Item -ItemType Directory build | Out-Null }
gcc -std=c11 -Wall -Wextra -Werror `
  tests\xiaoxin_power_control_test.c `
  main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_power_control.c `
  -I main\boards\waveshare\esp32-s3-touch-lcd-1.46 `
  -o build\xiaoxin_power_control_test.exe
.\build\xiaoxin_power_control_test.exe
```

Expected: compile fails with `xiaoxin_power_control_request_shutdown` undeclared.

- [ ] **Step 3: Add the API**

In `xiaoxin_power_control.h`, add after `xiaoxin_power_control_handle_long_press(...)`:

```c
void xiaoxin_power_control_request_shutdown(xiaoxin_power_control_t* power);
```

In `xiaoxin_power_control.c`, change `xiaoxin_power_control_handle_long_press()` to delegate:

```c
void xiaoxin_power_control_request_shutdown(xiaoxin_power_control_t* power) {
  if (power == 0) {
    return;
  }

  power->shutdown_requested = true;
  power->backlight_on = false;
  power->power_hold = false;
}

void xiaoxin_power_control_handle_long_press(xiaoxin_power_control_t* power) {
  xiaoxin_power_control_request_shutdown(power);
}
```

- [ ] **Step 4: Use the semantic API in board power-off**

In `esp32-s3-touch-lcd-1.46.cc`, change `RequestPowerOff()` from:

```cpp
        xiaoxin_power_control_handle_long_press(&power_control_);
```

to:

```cpp
        xiaoxin_power_control_request_shutdown(&power_control_);
```

Keep the existing log text if the path is still manually triggered; Task 4 adds low-battery-specific logging.

- [ ] **Step 5: Run tests**

Run:

```powershell
.\build\xiaoxin_power_control_test.exe
python -m pytest tests\xiaoxin_power_latch_path_test.py -q
```

Expected: C test prints `xiaoxin_power_control tests passed`; Python path test still passes.

- [ ] **Step 6: Commit**

```powershell
git add `
  tests\xiaoxin_power_control_test.c `
  main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_power_control.h `
  main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_power_control.c `
  main\boards\waveshare\esp32-s3-touch-lcd-1.46\esp32-s3-touch-lcd-1.46.cc
git commit -m "refactor: add semantic power shutdown request"
```

---

### Task 3: Add Board Path Tests for Runtime Battery Monitor

**Files:**
- Create: `tests/xiaoxin_low_battery_shutdown_path_test.py`
- Modify: `tests/xiaoxin_power_latch_path_test.py`
- Modify: `tests/xiaoxin_notification_visual_path_test.py` if its negative include assertion conflicts with this feature.

**Interfaces:**
- Produces static expectations for:
  - `xiaoxin_battery_state.h` inclusion.
  - `ReadBatteryVoltageMv(int* voltage_mv)`.
  - `StartBatteryMonitor()`.
  - `HandleBatterySnapshot(const xiaoxin_battery_snapshot_t& snapshot)`.
  - `BeginLowBatteryShutdown(bool startup_stage)`.
  - `CancelLowBatteryShutdownIfRecovered(const xiaoxin_battery_snapshot_t& snapshot)`.
  - snapshot-backed `GetBatteryLevel(...)`.

- [ ] **Step 1: Create failing path test file**

Create `tests/xiaoxin_low_battery_shutdown_path_test.py`:

```python
from pathlib import Path


BOARD_SOURCE = Path(
    "main/boards/waveshare/esp32-s3-touch-lcd-1.46/"
    "esp32-s3-touch-lcd-1.46.cc"
)


def read_source() -> str:
    return BOARD_SOURCE.read_text(encoding="utf-8")


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
                return source[brace + 1:index]
    raise AssertionError(f"function body not found: {signature}")


def assert_ordered(source: str, *needles: str):
    cursor = 0
    for needle in needles:
        index = source.find(needle, cursor)
        assert index != -1, f"missing ordered fragment: {needle}"
        cursor = index + len(needle)


def test_board_wires_adc_samples_into_battery_state_machine():
    source = read_source()
    constructor = function_body(source, "CustomBoard()")
    monitor = function_body(source, "void RefreshBatteryStateFromTimer()")
    reader = function_body(source, "bool ReadBatteryVoltageMv(int* voltage_mv)")

    assert '#include "xiaoxin_battery_state.h"' in source
    assert "xiaoxin_battery_context_t battery_context_" in source
    assert "xiaoxin_battery_snapshot_t battery_snapshot_" in source
    assert "esp_timer_handle_t battery_monitor_timer_" in source
    assert "battery_adc_available_" in reader
    assert "adc_oneshot_read(" in reader
    assert "adc_cali_raw_to_voltage(" in reader
    assert "pin_voltage_mv * k_battery_voltage_divider" in reader
    assert "xiaoxin_battery_state_update(" in monitor
    assert "XIAOXIN_BATTERY_LOAD_IDLE" in monitor
    assert "HandleBatterySnapshot(battery_snapshot_);" in monitor
    assert "StartBatteryMonitor();" in constructor
    assert_ordered(constructor, "InitializeSpd2010Display();", "StartBatteryMonitor();")


def test_low_edge_shows_one_time_warning_without_stopping_core_tasks():
    source = read_source()
    handler = function_body(source, "void HandleBatterySnapshot(const xiaoxin_battery_snapshot_t& snapshot)")

    assert "snapshot.low_edge" in handler
    assert 'ShowNotification("电量低，请尽快充电"' in handler
    assert "InitializeMotion()" not in handler
    assert "StartNetwork()" not in handler
    assert "SetEnabled(false)" not in handler


def test_critical_edge_starts_cancelable_shutdown_without_restart():
    source = read_source()
    handler = function_body(source, "void HandleBatterySnapshot(const xiaoxin_battery_snapshot_t& snapshot)")
    begin = function_body(source, "void BeginLowBatteryShutdown(bool startup_stage)")
    finish = function_body(source, "void FinishLowBatteryShutdown()")
    cancel = function_body(source, "void CancelLowBatteryShutdownIfRecovered(const xiaoxin_battery_snapshot_t& snapshot)")

    assert "snapshot.critical_edge" in handler
    assert "BeginLowBatteryShutdown(false);" in handler
    assert 'ShowNotification("电量不足，即将关机"' in begin
    assert "esp_timer_start_once(low_battery_shutdown_timer_" in begin
    assert "snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL" in cancel
    assert "snapshot.recovered_edge" in cancel
    assert "low_battery_shutdown_pending_ = false;" in cancel
    assert "RuntimeHealthForceCheckpoint();" in finish
    assert "xiaoxin_power_control_request_shutdown(&power_control_);" in finish
    assert "gpio_set_level(PWR_Control_PIN, xiaoxin_power_control_power_hold(&power_control_));" in finish
    assert "esp_restart()" not in begin
    assert "esp_restart()" not in finish


def test_get_battery_level_reports_only_reliable_snapshot_percent():
    source = read_source()
    body = function_body(source, "virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override")

    assert "battery_has_snapshot_" in body
    assert "battery_snapshot_.percent_reliable" in body
    assert "level = battery_snapshot_.display_percent;" in body
    assert "charging = battery_snapshot_.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL;" in body
    assert "discharging = battery_snapshot_.power_source == XIAOXIN_BATTERY_POWER_BATTERY;" in body
    assert "return true;" in body


def test_startup_protection_uses_runtime_health_before_full_app_work():
    source = read_source()
    constructor = function_body(source, "CustomBoard()")
    start_network = function_body(source, "void StartNetwork() override")

    assert "startup_low_battery_protection_" in source
    assert "RuntimeHealthProtectionRecommended()" in constructor
    assert_ordered(
        constructor,
        "RuntimeHealthStart(on_battery_);",
        "startup_low_battery_protection_ = on_battery_ && RuntimeHealthProtectionRecommended();",
        "InitializeSpd2010Display();",
        "HandleStartupLowBatteryProtection();",
    )
    assert "if (startup_low_battery_protection_)" in start_network
    assert "return;" in start_network


def test_battery_console_command_prints_last_sample_and_state():
    source = read_source()
    body = function_body(source, "void InitializeDebugConsole()")

    assert 'const esp_console_cmd_t battery_cmd = {' in body
    assert '.command = "battery"' in body
    assert "battery_snapshot_.state" in body
    assert "battery_snapshot_.power_source" in body
    assert "last_battery_voltage_mv_" in body
    assert "esp_console_cmd_register(&battery_cmd)" in body
```

- [ ] **Step 2: Update old battery reporting path test**

In `tests/xiaoxin_power_latch_path_test.py`, replace `test_runtime_battery_level_reporting_is_disabled_but_boot_power_detection_remains()` with:

```python
def test_runtime_battery_level_reporting_uses_reliable_state_snapshot_and_boot_detection_remains():
    source = read_source()
    runtime_battery = function_body(source, "virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override")
    boot_detection = function_body(source, "void DetectPowerSourceEarly()")

    assert "battery_has_snapshot_" in runtime_battery
    assert "battery_snapshot_.percent_reliable" in runtime_battery
    assert "level = battery_snapshot_.display_percent;" in runtime_battery
    assert "charging = battery_snapshot_.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL;" in runtime_battery
    assert "discharging = battery_snapshot_.power_source == XIAOXIN_BATTERY_POWER_BATTERY;" in runtime_battery
    assert "return false;" in runtime_battery
    assert "return true;" in runtime_battery

    assert "InitializeBatteryAdc();" in boot_detection
    assert "adc_oneshot_read(" in boot_detection
    assert "on_battery_ = voltage_mv <= k_external_power_voltage_mv;" in boot_detection
```

- [ ] **Step 3: Remove now-invalid negative include assertion**

If `tests/xiaoxin_notification_visual_path_test.py` still contains:

```python
assert '#include "xiaoxin_battery_state.h"' not in source
```

delete that assertion. This feature intentionally includes the battery state header in the board file.

- [ ] **Step 4: Run path tests and verify they fail for missing implementation**

Run:

```powershell
python -m pytest `
  tests\xiaoxin_low_battery_shutdown_path_test.py `
  tests\xiaoxin_power_latch_path_test.py `
  tests\xiaoxin_notification_visual_path_test.py `
  -q
```

Expected: `xiaoxin_low_battery_shutdown_path_test.py` and the edited battery reporting test fail because the board is not wired yet. Any unrelated notification visual test failures should be fixed only if caused by the deleted negative assertion.

- [ ] **Step 5: Commit failing tests**

```powershell
git add `
  tests\xiaoxin_low_battery_shutdown_path_test.py `
  tests\xiaoxin_power_latch_path_test.py `
  tests\xiaoxin_notification_visual_path_test.py
git commit -m "test: specify low battery shutdown board wiring"
```

---

### Task 4: Wire Runtime ADC Monitor and Battery Level Reporting

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`

**Interfaces:**
- Consumes: `xiaoxin_battery_state_update(...)`, `xiaoxin_battery_state_snapshot(...)`.
- Produces:
  - `bool ReadBatteryVoltageMv(int* voltage_mv);`
  - `void StartBatteryMonitor();`
  - `void RefreshBatteryStateFromTimer();`
  - `void HandleBatterySnapshot(const xiaoxin_battery_snapshot_t& snapshot);`
  - snapshot-backed `GetBatteryLevel(...)`.

- [ ] **Step 1: Add include and constants**

In the `extern "C"` include block, add:

```cpp
#include "xiaoxin_battery_state.h"
```

Near the existing battery constants, change the external-power threshold into a shared constant and add monitor intervals:

```cpp
static constexpr int k_external_power_voltage_mv = 4500;
static constexpr uint64_t k_battery_monitor_interval_us = 2 * 1000 * 1000;
static constexpr uint8_t k_battery_runtime_sample_count = 4;
```

In `DetectPowerSourceEarly()`, replace:

```cpp
        on_battery_ = voltage_mv <= 4500;
```

with:

```cpp
        on_battery_ = voltage_mv <= k_external_power_voltage_mv;
```

- [ ] **Step 2: Add board fields**

In `CustomBoard` private fields, after `battery_adc_available_`, add:

```cpp
    xiaoxin_battery_context_t battery_context_ = {};
    xiaoxin_battery_snapshot_t battery_snapshot_ = {};
    bool battery_context_initialized_ = false;
    bool battery_has_snapshot_ = false;
    int last_battery_voltage_mv_ = 0;
    uint32_t last_battery_sample_ms_ = 0;
    esp_timer_handle_t battery_monitor_timer_ = nullptr;
```

- [ ] **Step 3: Add ADC read helper**

Add this method after `InitializeBatteryAdc()`:

```cpp
    bool ReadBatteryVoltageMv(int* voltage_mv) {
        if (voltage_mv == nullptr) {
            return false;
        }
        if (!battery_adc_available_) {
            return false;
        }

        int voltage_sum = 0;
        uint8_t sample_count = 0;
        for (uint8_t i = 0; i < k_battery_runtime_sample_count; ++i) {
            int raw_value = 0;
            int pin_voltage_mv = 0;
            if (adc_oneshot_read(battery_adc_handle_, k_battery_adc_channel, &raw_value) != ESP_OK) {
                continue;
            }
            if (adc_cali_raw_to_voltage(battery_adc_cali_handle_, raw_value, &pin_voltage_mv) != ESP_OK) {
                continue;
            }
            voltage_sum += pin_voltage_mv * k_battery_voltage_divider;
            sample_count++;
        }

        if (sample_count == 0) {
            return false;
        }

        *voltage_mv = voltage_sum / sample_count;
        return true;
    }
```

- [ ] **Step 4: Add monitor methods**

Add these methods after `ReadBatteryVoltageMv(...)`:

```cpp
    uint32_t BatteryNowMs() const {
        return (uint32_t)(esp_timer_get_time() / 1000);
    }

    xiaoxin_battery_power_hint_t CurrentBatteryPowerHint(int voltage_mv) const {
        if (voltage_mv > k_external_power_voltage_mv) {
            return XIAOXIN_BATTERY_POWER_HINT_EXTERNAL;
        }
        if (on_battery_) {
            return XIAOXIN_BATTERY_POWER_HINT_BATTERY;
        }
        return XIAOXIN_BATTERY_POWER_HINT_UNKNOWN;
    }

    void StartBatteryMonitor() {
        if (battery_monitor_timer_ != nullptr) {
            return;
        }

        const uint32_t now_ms = BatteryNowMs();
        xiaoxin_battery_state_init(&battery_context_, now_ms);
        battery_snapshot_ = xiaoxin_battery_state_snapshot(&battery_context_);
        battery_context_initialized_ = true;
        battery_has_snapshot_ = true;

        const esp_timer_create_args_t battery_timer_args = {
            .callback = [](void* arg) {
                static_cast<CustomBoard*>(arg)->RefreshBatteryStateFromTimer();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "battery_monitor",
            .skip_unhandled_events = true,
        };

        esp_err_t err = esp_timer_create(&battery_timer_args, &battery_monitor_timer_);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Battery monitor timer create failed: %s", esp_err_to_name(err));
            return;
        }

        RefreshBatteryStateFromTimer();
        err = esp_timer_start_periodic(battery_monitor_timer_, k_battery_monitor_interval_us);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Battery monitor timer start failed: %s", esp_err_to_name(err));
        }
    }

    void RefreshBatteryStateFromTimer() {
        if (!battery_context_initialized_) {
            xiaoxin_battery_state_init(&battery_context_, BatteryNowMs());
            battery_context_initialized_ = true;
        }

        int voltage_mv = 0;
        const bool sample_valid = ReadBatteryVoltageMv(&voltage_mv);
        const uint32_t now_ms = BatteryNowMs();
        if (sample_valid) {
            last_battery_voltage_mv_ = voltage_mv;
            last_battery_sample_ms_ = now_ms;
        }

        battery_snapshot_ = xiaoxin_battery_state_update(
            &battery_context_,
            voltage_mv,
            sample_valid,
            sample_valid ? CurrentBatteryPowerHint(voltage_mv) : XIAOXIN_BATTERY_POWER_HINT_UNKNOWN,
            XIAOXIN_BATTERY_LOAD_IDLE,
            now_ms
        );
        battery_has_snapshot_ = true;
        HandleBatterySnapshot(battery_snapshot_);
    }

    void HandleBatterySnapshot(const xiaoxin_battery_snapshot_t& snapshot) {
        if (snapshot.low_edge && display_ != nullptr) {
            display_->ShowNotification("电量低，请尽快充电", 3000);
        }
    }
```

- [ ] **Step 5: Start the monitor after display is ready**

In the constructor, after the backlight stage and before touch initialization, add:

```cpp
        StartBatteryMonitor();
```

The surrounding order should be:

```cpp
        InitializeSpd2010Display();

        ESP_LOGI(TAG, "[BOOT] Stage 7/11: Backlight restore (%s)", on_battery_ ? "battery-splash" : "normal");
        BootDiagnosticsMark("board_backlight_ready");
        if (on_battery_) {
            GetBacklight()->SetBrightness(k_boot_splash_brightness_percent, false);
            vTaskDelay(pdMS_TO_TICKS(200));
        } else {
            GetBacklight()->RestoreBrightness();
        }
        StartBatteryMonitor();
```

- [ ] **Step 6: Implement `GetBatteryLevel()`**

Replace the existing body with:

```cpp
    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        charging = false;
        discharging = false;

        if (!battery_has_snapshot_ || !battery_snapshot_.percent_reliable) {
            return false;
        }

        level = battery_snapshot_.display_percent;
        charging = battery_snapshot_.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL;
        discharging = battery_snapshot_.power_source == XIAOXIN_BATTERY_POWER_BATTERY;
        return true;
    }
```

- [ ] **Step 7: Run focused tests**

Run:

```powershell
python -m pytest `
  tests\xiaoxin_low_battery_shutdown_path_test.py::test_board_wires_adc_samples_into_battery_state_machine `
  tests\xiaoxin_low_battery_shutdown_path_test.py::test_low_edge_shows_one_time_warning_without_stopping_core_tasks `
  tests\xiaoxin_low_battery_shutdown_path_test.py::test_get_battery_level_reports_only_reliable_snapshot_percent `
  tests\xiaoxin_power_latch_path_test.py::test_runtime_battery_level_reporting_uses_reliable_state_snapshot_and_boot_detection_remains `
  -q
```

Expected: these tests pass. Other low-battery shutdown path tests still fail until Task 5 and Task 6.

- [ ] **Step 8: Commit**

```powershell
git add `
  main\boards\waveshare\esp32-s3-touch-lcd-1.46\esp32-s3-touch-lcd-1.46.cc `
  tests\xiaoxin_low_battery_shutdown_path_test.py `
  tests\xiaoxin_power_latch_path_test.py `
  tests\xiaoxin_notification_visual_path_test.py
git commit -m "feat: report battery state from runtime ADC monitor"
```

---

### Task 5: Add Cancelable CRITICAL Shutdown

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`

**Interfaces:**
- Consumes: `xiaoxin_power_control_request_shutdown(...)`, `RuntimeHealthForceCheckpoint()`.
- Produces:
  - `void BeginLowBatteryShutdown(bool startup_stage);`
  - `void CancelLowBatteryShutdownIfRecovered(const xiaoxin_battery_snapshot_t& snapshot);`
  - `void FinishLowBatteryShutdown();`

- [ ] **Step 1: Add shutdown constants and fields**

Near battery monitor constants, add:

```cpp
static constexpr uint64_t k_low_battery_shutdown_delay_us = 3 * 1000 * 1000;
```

In `CustomBoard` fields near `battery_monitor_timer_`, add:

```cpp
    esp_timer_handle_t low_battery_shutdown_timer_ = nullptr;
    bool low_battery_shutdown_pending_ = false;
    bool low_battery_shutdown_startup_stage_ = false;
```

- [ ] **Step 2: Add cancel and shutdown methods**

Add after `HandleBatterySnapshot(...)`:

```cpp
    void CancelLowBatteryShutdownIfRecovered(const xiaoxin_battery_snapshot_t& snapshot) {
        if (!low_battery_shutdown_pending_) {
            return;
        }

        const bool external_power = snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL;
        const bool recovered = snapshot.recovered_edge;
        if (!external_power && !recovered) {
            return;
        }

        if (low_battery_shutdown_timer_ != nullptr) {
            esp_timer_stop(low_battery_shutdown_timer_);
        }
        low_battery_shutdown_pending_ = false;
        low_battery_shutdown_startup_stage_ = false;
        ESP_LOGI(TAG, "Low battery shutdown canceled: external=%d recovered=%d", external_power ? 1 : 0, recovered ? 1 : 0);
        if (display_ != nullptr) {
            display_->ShowNotification("已接入电源", 2000);
        }
    }

    void BeginLowBatteryShutdown(bool startup_stage) {
        if (low_battery_shutdown_pending_ || xiaoxin_power_control_shutdown_requested(&power_control_)) {
            return;
        }

        low_battery_shutdown_pending_ = true;
        low_battery_shutdown_startup_stage_ = startup_stage;
        if (display_ != nullptr) {
            display_->ShowNotification("电量不足，即将关机", 3000);
        }

        if (low_battery_shutdown_timer_ == nullptr) {
            const esp_timer_create_args_t shutdown_timer_args = {
                .callback = [](void* arg) {
                    static_cast<CustomBoard*>(arg)->FinishLowBatteryShutdown();
                },
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "low_battery_off",
                .skip_unhandled_events = true,
            };
            esp_err_t err = esp_timer_create(&shutdown_timer_args, &low_battery_shutdown_timer_);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Low battery shutdown timer create failed: %s", esp_err_to_name(err));
                FinishLowBatteryShutdown();
                return;
            }
        } else {
            esp_timer_stop(low_battery_shutdown_timer_);
        }

        esp_err_t err = esp_timer_start_once(low_battery_shutdown_timer_, k_low_battery_shutdown_delay_us);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Low battery shutdown timer start failed: %s", esp_err_to_name(err));
            FinishLowBatteryShutdown();
        }
    }

    void FinishLowBatteryShutdown() {
        if (!low_battery_shutdown_pending_) {
            return;
        }

        low_battery_shutdown_pending_ = false;
        RuntimeHealthForceCheckpoint();
        xiaoxin_power_control_request_shutdown(&power_control_);
        GetBacklight()->SetBrightness(0);
        gpio_set_level(PWR_Control_PIN, xiaoxin_power_control_power_hold(&power_control_));
        ESP_LOGI(TAG, "Low battery shutdown: power hold released (startup=%d voltage=%dmV)",
            low_battery_shutdown_startup_stage_ ? 1 : 0,
            last_battery_voltage_mv_);

        if (power_off_task_ == nullptr) {
            xTaskCreatePinnedToCore(
                PowerOffTask,
                "pwr_off",
                3072,
                this,
                4,
                &power_off_task_,
                1
            );
        }
    }
```

- [ ] **Step 3: Call cancel and CRITICAL edge from the snapshot handler**

Replace `HandleBatterySnapshot(...)` with:

```cpp
    void HandleBatterySnapshot(const xiaoxin_battery_snapshot_t& snapshot) {
        CancelLowBatteryShutdownIfRecovered(snapshot);

        if (snapshot.low_edge && display_ != nullptr) {
            display_->ShowNotification("电量低，请尽快充电", 3000);
        }

        if (snapshot.critical_edge) {
            BeginLowBatteryShutdown(false);
        }
    }
```

- [ ] **Step 4: Preserve manual shutdown path**

Keep `RequestPowerOff()` for manual PWR key shutdown. It should continue to release GPIO7 immediately. After Task 2 it should contain:

```cpp
        xiaoxin_power_control_request_shutdown(&power_control_);
        GetBacklight()->SetBrightness(0);
        RuntimeHealthForceCheckpoint();
        gpio_set_level(PWR_Control_PIN, xiaoxin_power_control_power_hold(&power_control_));
```

Do not add `esp_restart()` anywhere in low-battery shutdown code.

- [ ] **Step 5: Run focused shutdown tests**

Run:

```powershell
python -m pytest `
  tests\xiaoxin_low_battery_shutdown_path_test.py::test_critical_edge_starts_cancelable_shutdown_without_restart `
  tests\xiaoxin_low_battery_shutdown_path_test.py::test_low_edge_shows_one_time_warning_without_stopping_core_tasks `
  -q
```

Expected: both pass.

- [ ] **Step 6: Commit**

```powershell
git add main\boards\waveshare\esp32-s3-touch-lcd-1.46\esp32-s3-touch-lcd-1.46.cc
git commit -m "feat: shut down on critical battery state"
```

---

### Task 6: Persist Low-Battery Shutdown Diagnostics

**Files:**
- Modify: `tests/xiaoxin_runtime_health_model_test.c`
- Modify: `tests/xiaoxin_runtime_health_path_test.py`
- Modify: `main/boards/common/runtime_health_model.h`
- Modify: `main/boards/common/runtime_health_model.c`
- Modify: `main/boards/common/runtime_health.h`
- Modify: `main/boards/common/runtime_health.cc`
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Modify: `tests/xiaoxin_low_battery_shutdown_path_test.py`

**Interfaces:**
- Produces:
  - `void xiaoxin_runtime_health_record_low_battery_shutdown(xiaoxin_runtime_health_record_t* record, uint32_t voltage_mv, bool startup_stage);`
  - `void RuntimeHealthRecordLowBatteryShutdown(int voltage_mv, bool startup_stage);`
  - snapshot fields `low_battery_shutdown_count`, `last_low_battery_shutdown_voltage_mv`, `last_low_battery_shutdown_startup_stage`.
  - `FinishLowBatteryShutdown()` records the low-battery shutdown before checkpointing.

- [ ] **Step 1: Add failing model test**

In `tests/xiaoxin_runtime_health_model_test.c`, add:

```c
static void low_battery_shutdown_diagnostics_are_recorded(void) {
  xiaoxin_runtime_health_record_t record = {0};

  xiaoxin_runtime_health_record_low_battery_shutdown(&record, 3590, true);

  assert(record.low_battery_shutdown_count == 1);
  assert(record.last_low_battery_shutdown_voltage_mv == 3590);
  assert(record.last_low_battery_shutdown_startup_stage);

  xiaoxin_runtime_health_record_low_battery_shutdown(&record, 3620, false);

  assert(record.low_battery_shutdown_count == 2);
  assert(record.last_low_battery_shutdown_voltage_mv == 3620);
  assert(!record.last_low_battery_shutdown_startup_stage);
}
```

Call it from `main()` before `puts(...)`:

```c
  low_battery_shutdown_diagnostics_are_recorded();
```

- [ ] **Step 2: Run model test and verify it fails**

Run:

```powershell
if (!(Test-Path build)) { New-Item -ItemType Directory build | Out-Null }
gcc -std=c11 -Wall -Wextra -Werror `
  tests\xiaoxin_runtime_health_model_test.c `
  main\boards\common\runtime_health_model.c `
  -I main\boards\common `
  -o build\xiaoxin_runtime_health_model_test.exe
.\build\xiaoxin_runtime_health_model_test.exe
```

Expected: compile fails because the new fields and function are missing.

- [ ] **Step 3: Extend runtime health model header**

In `main/boards/common/runtime_health_model.h`, add these fields to `xiaoxin_runtime_health_record_t`:

```c
  uint32_t low_battery_shutdown_count;
  uint32_t last_low_battery_shutdown_voltage_mv;
  bool last_low_battery_shutdown_startup_stage;
```

Add the same fields to `xiaoxin_runtime_health_snapshot_t`.

Declare the pure model API:

```c
void xiaoxin_runtime_health_record_low_battery_shutdown(
  xiaoxin_runtime_health_record_t* record,
  uint32_t voltage_mv,
  bool startup_stage
);
```

- [ ] **Step 4: Implement pure model API**

In `runtime_health_model.c`, add:

```c
void xiaoxin_runtime_health_record_low_battery_shutdown(
  xiaoxin_runtime_health_record_t* record,
  uint32_t voltage_mv,
  bool startup_stage
) {
  if (record == 0) {
    return;
  }

  record->low_battery_shutdown_count++;
  record->last_low_battery_shutdown_voltage_mv = voltage_mv;
  record->last_low_battery_shutdown_startup_stage = startup_stage;
}
```

In `xiaoxin_runtime_health_snapshot_from_record(...)`, copy the new fields:

```c
  snapshot->low_battery_shutdown_count = record->low_battery_shutdown_count;
  snapshot->last_low_battery_shutdown_voltage_mv = record->last_low_battery_shutdown_voltage_mv;
  snapshot->last_low_battery_shutdown_startup_stage = record->last_low_battery_shutdown_startup_stage;
```

- [ ] **Step 5: Extend ESP runtime-health service**

In `runtime_health.h`, declare:

```c
void RuntimeHealthRecordLowBatteryShutdown(int voltage_mv, bool startup_stage);
```

In `runtime_health.cc`, add NVS keys near existing keys:

```cpp
static constexpr const char* k_low_battery_shutdown_count_key = "lb_count";
static constexpr const char* k_last_low_battery_voltage_key = "lb_mv";
static constexpr const char* k_last_low_battery_stage_key = "lb_startup";
```

In the load path that reads NVS into `s_record`, add:

```cpp
    nvs_get_u32(handle, k_low_battery_shutdown_count_key, &s_record.low_battery_shutdown_count);
    nvs_get_u32(handle, k_last_low_battery_voltage_key, &s_record.last_low_battery_shutdown_voltage_mv);
    uint32_t startup_stage = 0;
    nvs_get_u32(handle, k_last_low_battery_stage_key, &startup_stage);
    s_record.last_low_battery_shutdown_startup_stage = startup_stage != 0;
```

In the save path, add:

```cpp
    nvs_set_u32(handle, k_low_battery_shutdown_count_key, s_record.low_battery_shutdown_count);
    nvs_set_u32(handle, k_last_low_battery_voltage_key, s_record.last_low_battery_shutdown_voltage_mv);
    nvs_set_u32(handle, k_last_low_battery_stage_key, s_record.last_low_battery_shutdown_startup_stage ? 1 : 0);
```

Add the public function:

```cpp
void RuntimeHealthRecordLowBatteryShutdown(int voltage_mv, bool startup_stage) {
    xiaoxin_runtime_health_record_low_battery_shutdown(
        &s_record,
        voltage_mv > 0 ? (uint32_t)voltage_mv : 0,
        startup_stage
    );
    PersistRecord();
}
```

- [ ] **Step 6: Update runtime health path test**

In `tests/xiaoxin_runtime_health_path_test.py`, add assertions to `test_runtime_health_service_is_compiled_and_uses_esp_nvs()`:

```python
    assert "RuntimeHealthRecordLowBatteryShutdown" in header
    assert "k_low_battery_shutdown_count_key" in source
    assert "k_last_low_battery_voltage_key" in source
    assert "k_last_low_battery_stage_key" in source
```

Add a new test:

```python
def test_runtime_health_records_low_battery_shutdown_diagnostics():
    header = read_source(RUNTIME_HEALTH_HEADER)
    source = read_source(RUNTIME_HEALTH_SOURCE)

    assert "void RuntimeHealthRecordLowBatteryShutdown(int voltage_mv, bool startup_stage);" in header
    body = function_body(source, "void RuntimeHealthRecordLowBatteryShutdown(int voltage_mv, bool startup_stage)")
    assert "xiaoxin_runtime_health_record_low_battery_shutdown(" in body
    assert "voltage_mv > 0 ? (uint32_t)voltage_mv : 0" in body
    assert "PersistRecord();" in body
```

- [ ] **Step 7: Wire diagnostics into low-battery shutdown**

In `tests/xiaoxin_low_battery_shutdown_path_test.py`, add this assertion to `test_critical_edge_starts_cancelable_shutdown_without_restart()` after `assert "low_battery_shutdown_pending_ = false;" in cancel`:

```python
    assert "RuntimeHealthRecordLowBatteryShutdown(" in finish
```

In `FinishLowBatteryShutdown()` in `esp32-s3-touch-lcd-1.46.cc`, insert this line immediately before `RuntimeHealthForceCheckpoint();`:

```cpp
        RuntimeHealthRecordLowBatteryShutdown(last_battery_voltage_mv_, low_battery_shutdown_startup_stage_);
```

- [ ] **Step 8: Run diagnostics tests**

Run:

```powershell
.\build\xiaoxin_runtime_health_model_test.exe
python -m pytest tests\xiaoxin_runtime_health_path_test.py -q
python -m pytest tests\xiaoxin_low_battery_shutdown_path_test.py::test_critical_edge_starts_cancelable_shutdown_without_restart -q
```

Expected: C model test prints `xiaoxin_runtime_health_model tests passed`; Python runtime-health and low-battery critical path tests pass.

- [ ] **Step 9: Commit**

```powershell
git add `
  tests\xiaoxin_runtime_health_model_test.c `
  tests\xiaoxin_runtime_health_path_test.py `
  tests\xiaoxin_low_battery_shutdown_path_test.py `
  main\boards\common\runtime_health_model.h `
  main\boards\common\runtime_health_model.c `
  main\boards\common\runtime_health.h `
  main\boards\common\runtime_health.cc `
  main\boards\waveshare\esp32-s3-touch-lcd-1.46\esp32-s3-touch-lcd-1.46.cc
git commit -m "feat: record low battery shutdown diagnostics"
```

---

### Task 7: Add Startup Protection Guard

**Files:**
- Modify: `tests/xiaoxin_runtime_health_path_test.py`
- Modify: `tests/xiaoxin_low_battery_shutdown_path_test.py`
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`

**Interfaces:**
- Consumes: `RuntimeHealthProtectionRecommended()`, `BeginLowBatteryShutdown(true)`.
- Produces:
  - `bool startup_low_battery_protection_`.
  - `void HandleStartupLowBatteryProtection();`
  - `StartNetwork()` guard while startup protection is active.

- [ ] **Step 1: Update runtime health path expectation**

In `tests/xiaoxin_runtime_health_path_test.py`, rename `test_runtime_health_exposes_protection_signal_without_gating_boot()` to:

```python
def test_runtime_health_protection_signal_gates_xiaoxin_startup_only():
```

Replace the last two assertions:

```python
    assert "RuntimeHealthProtectionRecommended" not in application
    assert "RuntimeHealthProtectionRecommended" not in board
```

with:

```python
    assert "RuntimeHealthProtectionRecommended" not in application
    assert "startup_low_battery_protection_ = on_battery_ && RuntimeHealthProtectionRecommended();" in board
    constructor = function_body(board, "CustomBoard()")
    assert_ordered(
        constructor,
        "RuntimeHealthStart(on_battery_);",
        "startup_low_battery_protection_ = on_battery_ && RuntimeHealthProtectionRecommended();",
        "InitializeSpd2010Display();",
        "HandleStartupLowBatteryProtection();",
    )
```

- [ ] **Step 2: Add startup field and handler**

In `CustomBoard` fields near `on_battery_`, add:

```cpp
    bool startup_low_battery_protection_ = false;
```

Add this method near other low-battery methods:

```cpp
    void HandleStartupLowBatteryProtection() {
        if (!startup_low_battery_protection_) {
            return;
        }

        ESP_LOGW(TAG, "Runtime health recommends startup low battery protection");
        if (display_ != nullptr) {
            display_->ShowNotification("电量不足，请充电后再启动", 3000);
        }
        BeginLowBatteryShutdown(true);
    }
```

- [ ] **Step 3: Wire guard into constructor**

After `RuntimeHealthStart(on_battery_);`, add:

```cpp
        startup_low_battery_protection_ = on_battery_ && RuntimeHealthProtectionRecommended();
```

After `StartBatteryMonitor();`, add:

```cpp
        HandleStartupLowBatteryProtection();
        if (startup_low_battery_protection_) {
            BootDiagnosticsMark("board_startup_low_battery_protection");
            BootDiagnosticsFlush();
            ESP_LOGW(TAG, "[BOOT] Startup low battery protection active; skipping full board init");
            return;
        }
```

This location intentionally comes after minimal display/backlight setup and before touch, buttons, power-save timer, IMU, debug console, Wi-Fi, and audio paths.

- [ ] **Step 4: Guard `StartNetwork()`**

Change `StartNetwork()` to:

```cpp
    void StartNetwork() override {
        if (startup_low_battery_protection_) {
            ESP_LOGW(TAG, "Skipping network startup during low battery protection");
            return;
        }

        if (on_battery_) {
            vTaskDelay(pdMS_TO_TICKS(300));
        }

        WifiBoard::StartNetwork();
    }
```

- [ ] **Step 5: Run startup tests**

Run:

```powershell
python -m pytest `
  tests\xiaoxin_low_battery_shutdown_path_test.py::test_startup_protection_uses_runtime_health_before_full_app_work `
  tests\xiaoxin_runtime_health_path_test.py::test_runtime_health_protection_signal_gates_xiaoxin_startup_only `
  -q
```

Expected: both pass.

- [ ] **Step 6: Commit**

```powershell
git add `
  tests\xiaoxin_runtime_health_path_test.py `
  tests\xiaoxin_low_battery_shutdown_path_test.py `
  main\boards\waveshare\esp32-s3-touch-lcd-1.46\esp32-s3-touch-lcd-1.46.cc
git commit -m "feat: gate startup after repeated low battery resets"
```

---

### Task 8: Add Serial Battery Diagnostics

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Modify: `docs/xiaoxin-serial-debug-commands.zh-CN.md` if present

**Interfaces:**
- Produces `battery` console command.
- Command prints last ADC voltage, sample age, state, power source, display percent, display level, and pending shutdown flag.
- `battery` is not a pure-battery validation path because USB Serial/JTAG powers the board.
- `runtime_health` prints persisted low-battery shutdown count, last voltage, and startup/runtime stage so the operator can read the previous low-battery shutdown after reconnecting USB.

- [ ] **Step 1: Add label helpers**

Add these static helpers near other file-level helpers:

```cpp
static const char* BatteryStateLabel(xiaoxin_battery_state_t state) {
    switch (state) {
        case XIAOXIN_BATTERY_STATE_NORMAL:
            return "normal";
        case XIAOXIN_BATTERY_STATE_LOW:
            return "low";
        case XIAOXIN_BATTERY_STATE_CRITICAL:
            return "critical";
        case XIAOXIN_BATTERY_STATE_UNKNOWN:
        default:
            return "unknown";
    }
}

static const char* BatteryPowerSourceLabel(xiaoxin_battery_power_source_t source) {
    switch (source) {
        case XIAOXIN_BATTERY_POWER_BATTERY:
            return "battery";
        case XIAOXIN_BATTERY_POWER_EXTERNAL:
            return "external";
        case XIAOXIN_BATTERY_POWER_UNKNOWN:
        default:
            return "unknown";
    }
}
```

- [ ] **Step 2: Register `battery` command**

In `InitializeDebugConsole()`, before `runtime_health_cmd`, add:

```cpp
        const esp_console_cmd_t battery_cmd = {
            .command = "battery",
            .help = "Print battery monitor state",
            .hint = nullptr,
            .func = [](int argc, char** argv) -> int {
                (void)argc;
                (void)argv;
                auto* self = CustomBoard::Instance();
                if (self == nullptr) {
                    printf("battery: board is not ready\n");
                    return 0;
                }

                const uint32_t now_ms = self->BatteryNowMs();
                const uint32_t age_ms = self->last_battery_sample_ms_ == 0
                    ? 0
                    : now_ms - self->last_battery_sample_ms_;
                printf(
                    "battery: voltage=%dmV age=%lums state=%s source=%s percent=%d level=%u reliable=%d shutdown_pending=%d\n",
                    self->last_battery_voltage_mv_,
                    (unsigned long)age_ms,
                    BatteryStateLabel(self->battery_snapshot_.state),
                    BatteryPowerSourceLabel(self->battery_snapshot_.power_source),
                    self->battery_snapshot_.display_percent,
                    (unsigned)self->battery_snapshot_.display_level,
                    self->battery_snapshot_.percent_reliable ? 1 : 0,
                    self->low_battery_shutdown_pending_ ? 1 : 0
                );
                return 0;
            },
        };
        ESP_ERROR_CHECK(esp_console_cmd_register(&battery_cmd));
```

- [ ] **Step 3: Update command docs if the file exists**

If `docs/xiaoxin-serial-debug-commands.zh-CN.md` exists, add:

```markdown
### `battery`

打印小新 1.46C 当前电池监测状态：最近 ADC 电压、样本年龄、状态机状态、电源来源、显示百分比、显示档位、百分比是否可靠，以及低电关机是否正在等待执行。

注意：USB Serial/JTAG 接入会让板子同时获得 USB/外部供电，因此 `battery` 只能作为 USB 接入后的辅助诊断，不能用来验收纯电池低电关机。纯电池低电关机后重新插 USB，应使用 `runtime_health` 查看上一次低电主动关机记录。
```

If the file does not exist, do not create a new documentation tree for this task.

- [ ] **Step 4: Run command path test**

Run:

```powershell
python -m pytest tests\xiaoxin_low_battery_shutdown_path_test.py::test_battery_console_command_prints_last_sample_and_state -q
```

Expected: passes.

- [ ] **Step 5: Commit**

```powershell
git add main\boards\waveshare\esp32-s3-touch-lcd-1.46\esp32-s3-touch-lcd-1.46.cc
if (Test-Path docs\xiaoxin-serial-debug-commands.zh-CN.md) { git add docs\xiaoxin-serial-debug-commands.zh-CN.md }
git commit -m "feat: add battery serial diagnostics"
```

---

### Task 9: Full Verification and Hardware Checklist

**Files:**
- No source files required unless verification exposes defects.

**Interfaces:**
- Consumes all tasks above.
- Produces final confidence that tests, build, and hardware behavior match the spec.

- [ ] **Step 1: Run all C model tests touched by this feature**

Run:

```powershell
.\build\xiaoxin_battery_state_test.exe
.\build\xiaoxin_power_control_test.exe
.\build\xiaoxin_runtime_health_model_test.exe
```

Expected: each executable prints its success message.

- [ ] **Step 2: Run focused Python tests**

Run:

```powershell
python -m pytest `
  tests\xiaoxin_low_battery_shutdown_path_test.py `
  tests\xiaoxin_power_latch_path_test.py `
  tests\xiaoxin_runtime_health_path_test.py `
  tests\xiaoxin_notification_visual_path_test.py `
  -q
```

Expected: all selected tests pass.

- [ ] **Step 3: Run full Python suite**

Run:

```powershell
python -m pytest tests -q
```

Expected: all tests pass. If unrelated existing tests fail, capture the exact failing test names and confirm they are unrelated before proceeding.

- [ ] **Step 4: Run whitespace check**

Run:

```powershell
git diff --check
```

Expected: no output and exit code `0`.

- [ ] **Step 5: Run ESP-IDF build**

Run in an ESP-IDF-enabled shell:

```powershell
idf.py build
```

Expected: firmware build succeeds. If the current PowerShell cannot resolve `idf.py`, record the exact command failure in the final report and do not claim firmware build success.

- [ ] **Step 6: Hardware validation**

On the Waveshare 1.46C board with a bench supply or known low battery:

```text
1. Battery around normal range: board starts, pet/UI/Wi-Fi behavior remains normal.
2. Battery reaches LOW state around existing state-machine 20% mapping: one visible "电量低，请尽快充电" warning appears.
3. LOW state persists: warning does not repeat every 2s monitor tick.
4. Battery reaches CRITICAL state around existing state-machine 10% mapping: "电量不足，即将关机" appears for about 3s.
5. No USB inserted during CRITICAL window: GPIO7 power hold is released, backlight goes dark, board powers off, and firmware does not call `esp_restart()`.
6. USB inserted during CRITICAL warning before GPIO7 release: shutdown is canceled if external power is confirmed by the monitor before `FinishLowBatteryShutdown()`.
7. Repeated brownout history on battery: `RuntimeHealthProtectionRecommended()` causes minimal display warning and skips full startup work before shutdown.
8. After a low-battery power-off, reconnect USB and run serial `runtime_health`: it prints the persisted low-battery shutdown count, last shutdown voltage, and whether the shutdown happened during startup or runtime.
9. With USB connected, serial `battery` command prints the current voltage, state, source, percent, level, reliable flag, and shutdown pending flag; this is auxiliary USB-attached diagnostics, not pure-battery validation.
```

- [ ] **Step 7: Final commit if verification fixes were needed**

If Step 1-6 required fixes, commit them with the actual changed paths. For example, if only the board file and low-battery path test changed:

```powershell
git add `
  main\boards\waveshare\esp32-s3-touch-lcd-1.46\esp32-s3-touch-lcd-1.46.cc `
  tests\xiaoxin_low_battery_shutdown_path_test.py
git commit -m "fix: stabilize low battery shutdown verification"
```

---

## Self-Review Notes

- Spec coverage: LOW warning, CRITICAL active power-off, startup protection, `GetBatteryLevel()`, USB cancelation, runtime health relationship, and diagnostics all have tasks.
- Threshold consistency: no task introduces board-local LOW / CRITICAL voltage thresholds; the only board voltage threshold is the existing external-power separator `k_external_power_voltage_mv`.
- Confirmation timing: runtime LOW/CRITICAL timing remains inside `xiaoxin_battery_state.c`; startup protection uses runtime health because state-machine confirmation is too slow during early boot.
- Recovery behavior: LOW recovery and CRITICAL cancelation use state-machine `recovered_edge` and external power source from snapshots.
- Remaining risk: CRITICAL-window USB cancelation depends on monitor cadence and external-power detection speed. Hardware validation must check whether the 3s window is enough on the real board; if not, adjust the shutdown delay or add a dedicated external-power hint from hardware before changing battery thresholds.
