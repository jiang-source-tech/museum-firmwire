# Runtime Health Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a commercial-product-style runtime health summary so battery brownout or power-loss reboot loops can be diagnosed from the device, serial console, and MCP system info.

**Architecture:** Reuse the existing `boot_diagnostics` trace for boot-stage replay, and add a new structured `runtime_health` module for counters, reset reason, uptime checkpoints, and short-run streaks. The UI and diagnostics surfaces read a snapshot from this module; they do not parse the boot trace string.

**Tech Stack:** ESP-IDF, FreeRTOS, NVS, `esp_timer_get_time()`, `esp_reset_reason()`, LVGL settings overlay, local C tests with `gcc`, Python source-path tests with `pytest`.

## Global Constraints

- First implementation is observability-first: do not block normal boot until the counters have been validated on real hardware.
- Runtime checkpoints use NVS sparingly: save once at 60 seconds, then every 300 seconds.
- Runtime state is approximate by design; if a brownout happens before the first checkpoint, report the previous run as `<1分钟`.
- Store structured counters in a new NVS namespace, not inside the existing `boot_diag` trace strings.
- Keep the about page compact enough for the 1.46-inch round screen: show only firmware, current run, previous run, last reset, and brownout count.
- Leave `boot_diagnostics` in place for detailed boot-stage trace replay.

---

## File Structure

- Create `main/boards/common/runtime_health_model.h`: pure C data types and logic for applying a boot event, checkpoint policy, reset label mapping, and duration formatting.
- Create `main/boards/common/runtime_health_model.c`: pure logic with no ESP headers, suitable for local `gcc` tests.
- Create `main/boards/common/runtime_health.h`: C ABI for the ESP/NVS runtime-health service.
- Create `main/boards/common/runtime_health.cc`: ESP adapter that reads/writes NVS, maps `esp_reset_reason()`, exposes snapshots, and checkpoints uptime.
- Modify `main/CMakeLists.txt`: compile `runtime_health_model.c` and `runtime_health.cc`.
- Modify `main/system_info.h` and `main/system_info.cc`: add `GetUptimeSeconds()` as the single source for current uptime.
- Modify `main/application.cc`: call checkpoint logic from the existing 1-second clock tick and flush before software reboot.
- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`: start runtime health after early power-source detection, flush before soft power-off, show summary in About, and add a serial command.
- Modify `main/boards/common/board.cc`: add runtime health JSON under `runtime_health`.
- Create `tests/xiaoxin_runtime_health_model_test.c`: local unit tests for short-run streaks, counters, checkpoint cadence, and duration formatting.
- Modify `tests/xiaoxin_settings_path_test.py`: assert the About page uses runtime-health summary fields.
- Create `tests/xiaoxin_runtime_health_path_test.py`: assert the ESP adapter is compiled and wired to boot, clock tick, reboot, power-off, MCP JSON, and serial console.

## Task 1: Pure Runtime-Health Model

**Files:**
- Create: `main/boards/common/runtime_health_model.h`
- Create: `main/boards/common/runtime_health_model.c`
- Test: `tests/xiaoxin_runtime_health_model_test.c`

**Interfaces:**
- Consumes: plain C values only.
- Produces:
  - `xiaoxin_runtime_health_record_t`
  - `xiaoxin_runtime_health_snapshot_t`
  - `xiaoxin_runtime_health_apply_boot(...)`
  - `xiaoxin_runtime_health_should_checkpoint(...)`
  - `xiaoxin_runtime_health_format_duration(...)`
  - `xiaoxin_runtime_health_reset_label(...)`

- [ ] **Step 1: Write the failing test**

```c
static void brownout_short_runs_increment_streak(void) {
  xiaoxin_runtime_health_record_t record = {0};
  record.current_checkpoint_sec = 42;
  record.current_on_battery = true;

  xiaoxin_runtime_health_apply_boot(
    &record,
    XIAOXIN_RUNTIME_RESET_BROWNOUT,
    true,
    60
  );

  assert(record.boot_count == 1);
  assert(record.brownout_count == 1);
  assert(record.last_runtime_sec == 42);
  assert(record.short_run_streak == 1);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
gcc -std=c11 -Wall -Wextra -I. tests/xiaoxin_runtime_health_model_test.c main/boards/common/runtime_health_model.c -o build/xiaoxin_runtime_health_model_test.exe
```

Expected: fail because `runtime_health_model.h` does not exist.

- [ ] **Step 3: Implement the model header**

Define:

```c
typedef enum {
  XIAOXIN_RUNTIME_RESET_UNKNOWN = 0,
  XIAOXIN_RUNTIME_RESET_POWERON,
  XIAOXIN_RUNTIME_RESET_BROWNOUT,
  XIAOXIN_RUNTIME_RESET_SOFTWARE,
  XIAOXIN_RUNTIME_RESET_PANIC,
  XIAOXIN_RUNTIME_RESET_WATCHDOG,
  XIAOXIN_RUNTIME_RESET_DEEPSLEEP,
} xiaoxin_runtime_reset_kind_t;

typedef struct {
  uint32_t boot_count;
  uint32_t brownout_count;
  uint32_t poweron_count;
  uint32_t software_reset_count;
  uint32_t watchdog_count;
  uint32_t panic_count;
  uint32_t short_run_streak;
  uint32_t last_runtime_sec;
  uint32_t max_runtime_sec;
  uint32_t current_checkpoint_sec;
  bool current_on_battery;
  bool previous_on_battery;
  xiaoxin_runtime_reset_kind_t last_reset_kind;
} xiaoxin_runtime_health_record_t;
```

- [ ] **Step 4: Implement the model logic**

Rules:

```c
bool unstable_reset =
  reset_kind == XIAOXIN_RUNTIME_RESET_BROWNOUT ||
  reset_kind == XIAOXIN_RUNTIME_RESET_POWERON;

if (record->current_checkpoint_sec > record->max_runtime_sec) {
  record->max_runtime_sec = record->current_checkpoint_sec;
}
record->last_runtime_sec = record->current_checkpoint_sec;
record->previous_on_battery = record->current_on_battery;
record->current_checkpoint_sec = 0;
record->current_on_battery = current_on_battery;
record->boot_count++;
record->last_reset_kind = reset_kind;

if (unstable_reset &&
    record->previous_on_battery &&
    record->last_runtime_sec < short_runtime_threshold_sec) {
  record->short_run_streak++;
} else {
  record->short_run_streak = 0;
}
```

Checkpoint policy:

```c
bool xiaoxin_runtime_health_should_checkpoint(uint32_t last_saved_sec, uint32_t now_sec) {
  if (now_sec < 60) {
    return false;
  }
  if (last_saved_sec < 60) {
    return true;
  }
  return now_sec - last_saved_sec >= 300;
}
```

- [ ] **Step 5: Run the model test**

Run:

```powershell
gcc -std=c11 -Wall -Wextra -I. tests/xiaoxin_runtime_health_model_test.c main/boards/common/runtime_health_model.c -o build/xiaoxin_runtime_health_model_test.exe
.\build\xiaoxin_runtime_health_model_test.exe
```

Expected: `xiaoxin_runtime_health_model tests passed`.

## Task 2: ESP/NVS Runtime-Health Service

**Files:**
- Create: `main/boards/common/runtime_health.h`
- Create: `main/boards/common/runtime_health.cc`
- Modify: `main/CMakeLists.txt`
- Test: `tests/xiaoxin_runtime_health_path_test.py`

**Interfaces:**
- Consumes:
  - `SystemInfo::GetUptimeSeconds()`
  - `esp_reset_reason()`
  - NVS namespace `runtime_health`
- Produces:
  - `void RuntimeHealthStart(bool on_battery);`
  - `void RuntimeHealthMaybeCheckpoint(void);`
  - `void RuntimeHealthForceCheckpoint(void);`
  - `bool RuntimeHealthReadSnapshot(xiaoxin_runtime_health_snapshot_t* out);`

- [ ] **Step 1: Write the source-path test**

Assert:

```python
assert '"boards/common/runtime_health.cc"' in cmake
assert '"boards/common/runtime_health_model.c"' in cmake
assert 'static constexpr const char* k_namespace = "runtime_health";' in source
assert "esp_reset_reason()" in source
assert "nvs_get_u32" in source
assert "nvs_set_u32" in source
assert "RuntimeHealthStart(bool on_battery)" in header
assert "RuntimeHealthMaybeCheckpoint(void)" in header
assert "RuntimeHealthReadSnapshot" in header
```

- [ ] **Step 2: Implement NVS keys**

Use these exact keys:

```cpp
static constexpr const char* k_namespace = "runtime_health";
static constexpr const char* k_boot_count_key = "boot_count";
static constexpr const char* k_brownout_count_key = "brownout_count";
static constexpr const char* k_poweron_count_key = "poweron_count";
static constexpr const char* k_software_reset_count_key = "soft_count";
static constexpr const char* k_watchdog_count_key = "wdt_count";
static constexpr const char* k_panic_count_key = "panic_count";
static constexpr const char* k_short_run_streak_key = "short_streak";
static constexpr const char* k_last_runtime_sec_key = "last_sec";
static constexpr const char* k_max_runtime_sec_key = "max_sec";
static constexpr const char* k_current_checkpoint_sec_key = "cur_sec";
static constexpr const char* k_current_on_battery_key = "cur_bat";
static constexpr const char* k_previous_on_battery_key = "prev_bat";
static constexpr const char* k_last_reset_kind_key = "last_reset";
```

- [ ] **Step 3: Map ESP reset reasons**

Use:

```cpp
static xiaoxin_runtime_reset_kind_t MapResetReason(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:
            return XIAOXIN_RUNTIME_RESET_POWERON;
        case ESP_RST_BROWNOUT:
            return XIAOXIN_RUNTIME_RESET_BROWNOUT;
        case ESP_RST_SW:
            return XIAOXIN_RUNTIME_RESET_SOFTWARE;
        case ESP_RST_PANIC:
            return XIAOXIN_RUNTIME_RESET_PANIC;
        case ESP_RST_TASK_WDT:
        case ESP_RST_INT_WDT:
        case ESP_RST_WDT:
            return XIAOXIN_RUNTIME_RESET_WATCHDOG;
        case ESP_RST_DEEPSLEEP:
            return XIAOXIN_RUNTIME_RESET_DEEPSLEEP;
        default:
            return XIAOXIN_RUNTIME_RESET_UNKNOWN;
    }
}
```

- [ ] **Step 4: Implement checkpointing**

`RuntimeHealthMaybeCheckpoint()` should:

```cpp
const uint32_t now_sec = SystemInfo::GetUptimeSeconds();
if (!xiaoxin_runtime_health_should_checkpoint(s_record.current_checkpoint_sec, now_sec)) {
    return;
}
s_record.current_checkpoint_sec = now_sec;
PersistRecord();
```

- [ ] **Step 5: Run the path test**

Run:

```powershell
python -m pytest tests\xiaoxin_runtime_health_path_test.py -q
```

Expected: all tests pass.

## Task 3: Wire Startup, Tick, Reboot, and Power-Off

**Files:**
- Modify: `main/system_info.h`
- Modify: `main/system_info.cc`
- Modify: `main/application.cc`
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Test: `tests/xiaoxin_runtime_health_path_test.py`

**Interfaces:**
- Consumes:
  - `RuntimeHealthStart(bool on_battery)`
  - `RuntimeHealthMaybeCheckpoint()`
  - `RuntimeHealthForceCheckpoint()`
- Produces:
  - `SystemInfo::GetUptimeSeconds()`
  - runtime-health start after `DetectPowerSourceEarly()`
  - graceful checkpoint before software restart and soft power-off

- [ ] **Step 1: Add uptime API**

Add to `system_info.h`:

```cpp
static uint32_t GetUptimeSeconds();
```

Add to `system_info.cc`:

```cpp
uint32_t SystemInfo::GetUptimeSeconds() {
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}
```

- [ ] **Step 2: Start runtime health during board construction**

In `CustomBoard()` after `DetectPowerSourceEarly();` and before `BootDiagnosticsStart(on_battery_);`:

```cpp
RuntimeHealthStart(on_battery_);
```

- [ ] **Step 3: Checkpoint from the existing clock tick**

In `Application::Run()`, inside `if (bits & MAIN_EVENT_CLOCK_TICK)` after `clock_ticks_++;`:

```cpp
RuntimeHealthMaybeCheckpoint();
```

- [ ] **Step 4: Flush before controlled reboot**

In `Application::Reboot()`, before `esp_restart();`:

```cpp
RuntimeHealthForceCheckpoint();
```

- [ ] **Step 5: Flush before soft power-off**

In `CustomBoard::RequestPowerOff()`, before `gpio_set_level(PWR_Control_PIN, ...)`:

```cpp
RuntimeHealthForceCheckpoint();
```

- [ ] **Step 6: Run tests**

Run:

```powershell
python -m pytest tests\xiaoxin_runtime_health_path_test.py -q
```

Expected: all runtime-health wiring assertions pass.

## Task 4: About Page, Serial Command, and MCP JSON

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Modify: `main/boards/common/board.cc`
- Modify: `tests/xiaoxin_settings_path_test.py`
- Modify: `tests/xiaoxin_runtime_health_path_test.py`

**Interfaces:**
- Consumes:
  - `RuntimeHealthReadSnapshot(...)`
  - `xiaoxin_runtime_health_format_duration(...)`
  - `xiaoxin_runtime_health_reset_label(...)`
- Produces:
  - About page text with runtime-health summary.
  - Serial command `runtime_health`.
  - JSON object `runtime_health` in `Board::GetSystemInfoJson()`.

- [ ] **Step 1: Update About page text**

The body should fit this shape:

```cpp
std::snprintf(
    text,
    sizeof(text),
    "小芯 D151\n固件 %s\n本次 %s\n上次 %s\n重启 %s\n欠压 %lu次",
    app_desc != nullptr ? app_desc->version : "-",
    current_duration,
    last_duration,
    reset_label,
    (unsigned long)snapshot.brownout_count
);
```

- [ ] **Step 2: Add serial command**

Add an `esp_console_cmd_t runtime_health_cmd` in `InitializeDebugConsole()` that prints:

```text
runtime: current=<duration> last=<duration> max=<duration> reset=<label> brownout=<count> short_streak=<count> battery=<0|1>
```

- [ ] **Step 3: Add MCP/system JSON**

In `Board::GetSystemInfoJson()`, before `"board":`, add:

```cpp
json += R"("runtime_health":{)";
json += R"("current_runtime_sec":)" + std::to_string(snapshot.current_runtime_sec) + R"(,)";
json += R"("last_runtime_sec":)" + std::to_string(snapshot.last_runtime_sec) + R"(,)";
json += R"("max_runtime_sec":)" + std::to_string(snapshot.max_runtime_sec) + R"(,)";
json += R"("last_reset":")" + std::string(xiaoxin_runtime_health_reset_label(snapshot.last_reset_kind)) + R"(",)";
json += R"("brownout_count":)" + std::to_string(snapshot.brownout_count) + R"(,)";
json += R"("short_run_streak":)" + std::to_string(snapshot.short_run_streak);
json += R"(},)";
```

- [ ] **Step 4: Run UI/path tests**

Run:

```powershell
python -m pytest tests\xiaoxin_settings_path_test.py tests\xiaoxin_runtime_health_path_test.py -q
```

Expected: all tests pass.

## Task 5: Conservative Protection Signal

**Files:**
- Modify: `main/boards/common/runtime_health_model.h`
- Modify: `main/boards/common/runtime_health_model.c`
- Modify: `main/boards/common/runtime_health.h`
- Modify: `main/boards/common/runtime_health.cc`
- Test: `tests/xiaoxin_runtime_health_model_test.c`

**Interfaces:**
- Consumes: `short_run_streak`, `current_on_battery`, `last_reset_kind`.
- Produces:
  - `bool RuntimeHealthProtectionRecommended(void);`
  - `bool xiaoxin_runtime_health_protection_recommended(const xiaoxin_runtime_health_record_t* record);`

- [ ] **Step 1: Add model test**

```c
static void protection_requires_three_short_battery_unstable_boots(void) {
  xiaoxin_runtime_health_record_t record = {0};
  record.short_run_streak = 2;
  record.current_on_battery = true;
  record.last_reset_kind = XIAOXIN_RUNTIME_RESET_BROWNOUT;
  assert(!xiaoxin_runtime_health_protection_recommended(&record));

  record.short_run_streak = 3;
  assert(xiaoxin_runtime_health_protection_recommended(&record));

  record.current_on_battery = false;
  assert(!xiaoxin_runtime_health_protection_recommended(&record));
}
```

- [ ] **Step 2: Implement the helper**

```c
bool xiaoxin_runtime_health_protection_recommended(const xiaoxin_runtime_health_record_t* record) {
  if (record == 0) {
    return false;
  }
  const bool unstable =
    record->last_reset_kind == XIAOXIN_RUNTIME_RESET_BROWNOUT ||
    record->last_reset_kind == XIAOXIN_RUNTIME_RESET_POWERON;
  return record->current_on_battery && unstable && record->short_run_streak >= 3;
}
```

- [ ] **Step 3: Expose ESP wrapper**

```cpp
bool RuntimeHealthProtectionRecommended(void) {
    return xiaoxin_runtime_health_protection_recommended(&s_record);
}
```

- [ ] **Step 4: Do not gate boot yet**

Keep this task as a signal only. The next product decision is whether to use it to delay Wi-Fi/audio, show a low-battery-only screen, or only surface a warning.

- [ ] **Step 5: Run model tests**

Run:

```powershell
gcc -std=c11 -Wall -Wextra -I. tests/xiaoxin_runtime_health_model_test.c main/boards/common/runtime_health_model.c -o build/xiaoxin_runtime_health_model_test.exe
.\build\xiaoxin_runtime_health_model_test.exe
```

Expected: `xiaoxin_runtime_health_model tests passed`.

## Task 6: Documentation and Manual Verification

**Files:**
- Modify: `docs/xiaoxin-feature-roadmap.zh-CN.md`
- Modify: `docs/xiaoxin-serial-debug-commands.zh-CN.md`

**Interfaces:**
- Consumes: implemented UI, serial, and MCP behavior.
- Produces: operator-facing notes for diagnosing battery reboot loops.

- [ ] **Step 1: Document device-facing behavior**

Add:

```markdown
关于页显示本次运行、上次运行、最近重启原因和欠压次数。运行时间来自 NVS checkpoint，首次 checkpoint 为 60 秒，之后每 5 分钟保存一次；因此掉电前最后一段运行时间是近似值。
```

- [ ] **Step 2: Document serial command**

Add:

```markdown
`runtime_health`：打印本次运行、上次运行、最长运行、最近重启原因、欠压次数、短运行连续次数和当前供电判断。用于排查电池供电下反复重启。
```

- [ ] **Step 3: Run focused verification**

Run:

```powershell
gcc -std=c11 -Wall -Wextra -I. tests/xiaoxin_runtime_health_model_test.c main/boards/common/runtime_health_model.c -o build/xiaoxin_runtime_health_model_test.exe
.\build\xiaoxin_runtime_health_model_test.exe
python -m pytest tests\xiaoxin_settings_path_test.py tests\xiaoxin_runtime_health_path_test.py tests\xiaoxin_boot_diagnostics_path_test.py -q
```

Expected:

```text
xiaoxin_runtime_health_model tests passed
```

and pytest exits with all selected tests passing.

## Self-Review

- Spec coverage: the plan covers uptime display, previous-run recovery through checkpoints, reset reason classification, brownout counters, short-run streak detection, serial diagnostics, MCP JSON, and a future protection signal.
- Placeholder scan: no unresolved placeholder phrases or open-ended handling steps remain.
- Type consistency: all produced interfaces use the `RuntimeHealth...` ESP wrapper names and the `xiaoxin_runtime_health_...` pure model names consistently.
