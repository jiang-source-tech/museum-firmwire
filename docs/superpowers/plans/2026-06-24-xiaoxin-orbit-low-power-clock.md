# Xiaoxin Orbit Low-Power Clock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the Waveshare ESP32-S3 Touch LCD 1.46 low-power clock into a polished "deep-space orbit" always-on-display clock with a large center time, layered animated rings, date, battery, and visible time-sync status.

**Architecture:** Keep clock text formatting in the existing pure C low-power clock model, keep SNTP status in a tiny common module owned by the network layer, and keep LVGL object creation/styling in `esp32-s3-touch-lcd-1.46.cc`. The sleep screen should remain cheap to refresh: text only on minute/status changes, orbit animation once per second.

**Tech Stack:** ESP-IDF, LVGL 9, C/C++, existing pytest path tests, existing C model tests.

## Global Constraints

- Target board: `CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_46`.
- Preserve low-power screen wake behavior: power button wakes through `WakePowerSaveTimer()`.
- Preserve dim sleep brightness: `XIAOXIN_LOW_POWER_CLOCK_DEFAULT_BRIGHTNESS` remains `12`.
- Do not add new generated fonts; use existing `font_puhui_basic_30_4` and LVGL transform scale.
- Keep the first visual priority on large `HH:MM`; date, battery, and sync status are secondary.
- Use TDD: each task starts with a failing test and then the minimal implementation.
- Do not run `idf.py build` as proof unless the shell can resolve `idf.py`.

---

## File Structure

- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.h`
  - Add date, battery, and sync status inputs/outputs for the sleep clock snapshot.
- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c`
  - Format secondary labels: date, battery, sync text, and sync color.
- Modify `tests/xiaoxin_low_power_clock_model_test.c`
  - Add pure model tests for date/battery/sync formatting.
- Create `main/boards/common/time_sync_status.h`
  - Declare a minimal global SNTP status API.
- Create `main/boards/common/time_sync_status.cc`
  - Store whether SNTP is idle, syncing, or synced.
- Modify `main/boards/common/wifi_board.cc`
  - Mark time sync as syncing when SNTP starts and synced in the SNTP callback.
- Modify `main/CMakeLists.txt`
  - Compile `boards/common/time_sync_status.cc`.
- Modify `tests/wifi_config_status_path_test.py`
  - Assert Wi-Fi time sync updates the common status module.
- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
  - Replace the simple sleep clock layout with the orbit AOD layout.
- Modify `tests/xiaoxin_low_power_clock_visual_path_test.py`
  - Assert the orbit objects, styles, positions, and animation paths exist.

---

### Task 1: Extend the Pure Clock Model

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.h`
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c`
- Test: `tests/xiaoxin_low_power_clock_model_test.c`

**Interfaces:**
- Consumes: Existing `xiaoxin_low_power_clock_state_t` and `xiaoxin_low_power_clock_snapshot_t`.
- Produces:
  - `xiaoxin_low_power_clock_sync_state_t`
  - `state.month`, `state.day`, `state.weekday`, `state.battery_known`, `state.battery_percent`, `state.sync_state`
  - `snapshot.date_text`, `snapshot.battery_text`, `snapshot.sync_text`, `snapshot.sync_color_hex`

- [ ] **Step 1: Write the failing model test**

Add these tests to `tests/xiaoxin_low_power_clock_model_test.c`:

```c
static void valid_state_formats_orbit_secondary_text(void) {
  xiaoxin_low_power_clock_state_t state = {
    .time_valid = true,
    .hour = 9,
    .minute = 5,
    .month = 6,
    .day = 24,
    .weekday = 3,
    .battery_known = true,
    .battery_percent = 87,
    .sync_state = XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCED,
  };
  xiaoxin_low_power_clock_snapshot_t snapshot = {};

  xiaoxin_low_power_clock_model_build(&state, &snapshot);

  assert(strcmp(snapshot.time_text, "09:05") == 0);
  assert(strcmp(snapshot.date_text, "WED 06/24") == 0);
  assert(strcmp(snapshot.battery_text, "87%") == 0);
  assert(strcmp(snapshot.sync_text, "SYNC") == 0);
  assert(snapshot.sync_color_hex == 0x26D9FF);
}

static void invalid_time_marks_syncing_orbit_state(void) {
  xiaoxin_low_power_clock_state_t state = {
    .time_valid = false,
    .battery_known = false,
    .sync_state = XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCING,
  };
  xiaoxin_low_power_clock_snapshot_t snapshot = {};

  xiaoxin_low_power_clock_model_build(&state, &snapshot);

  assert(strcmp(snapshot.time_text, "--:--") == 0);
  assert(strcmp(snapshot.date_text, "WAITING") == 0);
  assert(strcmp(snapshot.battery_text, "--%") == 0);
  assert(strcmp(snapshot.sync_text, "NTP") == 0);
  assert(snapshot.sync_color_hex == 0xF5C542);
}
```

Call both functions from `main()`.

- [ ] **Step 2: Run the model test to verify it fails**

Run:

```powershell
if (!(Test-Path .\build)) { New-Item -ItemType Directory -Path .\build | Out-Null }
gcc tests/xiaoxin_low_power_clock_model_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 -o build/xiaoxin_low_power_clock_model_test.exe
```

Expected: compile failure because `XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCED`, `date_text`, `battery_text`, or `sync_color_hex` does not exist.

- [ ] **Step 3: Add model fields and formatting**

Update `xiaoxin_low_power_clock_model.h`:

```c
#define XIAOXIN_LOW_POWER_CLOCK_DATE_MAX 16
#define XIAOXIN_LOW_POWER_CLOCK_BATTERY_MAX 8
#define XIAOXIN_LOW_POWER_CLOCK_SYNC_MAX 8

typedef enum {
  XIAOXIN_LOW_POWER_CLOCK_SYNC_IDLE = 0,
  XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCING,
  XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCED,
} xiaoxin_low_power_clock_sync_state_t;

typedef struct {
  bool time_valid;
  int hour;
  int minute;
  int month;
  int day;
  int weekday;
  bool battery_known;
  int battery_percent;
  xiaoxin_low_power_clock_sync_state_t sync_state;
} xiaoxin_low_power_clock_state_t;

typedef struct {
  char icon_text[8];
  char time_text[XIAOXIN_LOW_POWER_CLOCK_TIME_MAX];
  char date_text[XIAOXIN_LOW_POWER_CLOCK_DATE_MAX];
  char battery_text[XIAOXIN_LOW_POWER_CLOCK_BATTERY_MAX];
  char sync_text[XIAOXIN_LOW_POWER_CLOCK_SYNC_MAX];
  char hint_text[XIAOXIN_LOW_POWER_CLOCK_HINT_MAX];
  uint32_t sync_color_hex;
  uint8_t brightness_percent;
} xiaoxin_low_power_clock_snapshot_t;
```

Update `xiaoxin_low_power_clock_model.c` with helpers:

```c
static const char* weekday_text(int weekday) {
  static const char* names[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  if (weekday < 0 || weekday > 6) {
    return "---";
  }
  return names[weekday];
}

static void build_sync_fields(
  xiaoxin_low_power_clock_sync_state_t sync_state,
  xiaoxin_low_power_clock_snapshot_t* snapshot
) {
  if (sync_state == XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCED) {
    snprintf(snapshot->sync_text, sizeof(snapshot->sync_text), "%s", "SYNC");
    snapshot->sync_color_hex = 0x26D9FF;
  } else if (sync_state == XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCING) {
    snprintf(snapshot->sync_text, sizeof(snapshot->sync_text), "%s", "NTP");
    snapshot->sync_color_hex = 0xF5C542;
  } else {
    snprintf(snapshot->sync_text, sizeof(snapshot->sync_text), "%s", "OFF");
    snapshot->sync_color_hex = 0x7C8794;
  }
}
```

Inside `xiaoxin_low_power_clock_model_build()`:

```c
build_sync_fields(state != 0 ? state->sync_state : XIAOXIN_LOW_POWER_CLOCK_SYNC_IDLE, snapshot);

if (state == 0 || !state->battery_known) {
  snprintf(snapshot->battery_text, sizeof(snapshot->battery_text), "%s", "--%");
} else {
  snprintf(snapshot->battery_text, sizeof(snapshot->battery_text), "%d%%", clamp_int(state->battery_percent, 0, 100));
}

if (state == 0 || !state->time_valid) {
  snprintf(snapshot->time_text, sizeof(snapshot->time_text), "%s", "--:--");
  snprintf(snapshot->date_text, sizeof(snapshot->date_text), "%s", "WAITING");
  return;
}

snprintf(
  snapshot->date_text,
  sizeof(snapshot->date_text),
  "%s %02d/%02d",
  weekday_text(state->weekday),
  clamp_int(state->month, 1, 12),
  clamp_int(state->day, 1, 31)
);
```

- [ ] **Step 4: Run the model test to verify it passes**

Run:

```powershell
gcc tests/xiaoxin_low_power_clock_model_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 -o build/xiaoxin_low_power_clock_model_test.exe; if ($LASTEXITCODE -eq 0) { .\build\xiaoxin_low_power_clock_model_test.exe }
```

Expected: exit code `0`.

---

### Task 2: Add Shared Time Sync Status

**Files:**
- Create: `main/boards/common/time_sync_status.h`
- Create: `main/boards/common/time_sync_status.cc`
- Modify: `main/boards/common/wifi_board.cc`
- Modify: `main/CMakeLists.txt`
- Test: `tests/wifi_config_status_path_test.py`

**Interfaces:**
- Produces:
  - `enum class TimeSyncStatus { Idle, Syncing, Synced };`
  - `void MarkTimeSyncStarted();`
  - `void MarkTimeSyncSucceeded();`
  - `TimeSyncStatus GetTimeSyncStatus();`
- Consumes: `StartTimeSynchronization()` and `OnSntpTimeSync()` in `wifi_board.cc`.

- [ ] **Step 1: Write the failing path test**

Add assertions to `tests/wifi_config_status_path_test.py`:

```python
def test_wifi_time_sync_updates_shared_status():
    source = read_source(WIFI_BOARD_SOURCE)
    cmake = Path("main/CMakeLists.txt").read_text(encoding="utf-8")

    assert '#include "time_sync_status.h"' in source
    assert "MarkTimeSyncStarted();" in source
    assert "MarkTimeSyncSucceeded();" in source
    assert '"boards/common/time_sync_status.cc"' in cmake
```

- [ ] **Step 2: Run the path test to verify it fails**

Run:

```powershell
python -m pytest tests/wifi_config_status_path_test.py -q
```

Expected: failure because `time_sync_status.h` is not included and the new source file is not in CMake.

- [ ] **Step 3: Create the status module**

Create `main/boards/common/time_sync_status.h`:

```cpp
#pragma once

enum class TimeSyncStatus {
    Idle,
    Syncing,
    Synced,
};

void MarkTimeSyncStarted();
void MarkTimeSyncSucceeded();
TimeSyncStatus GetTimeSyncStatus();
```

Create `main/boards/common/time_sync_status.cc`:

```cpp
#include "time_sync_status.h"

static volatile TimeSyncStatus g_time_sync_status = TimeSyncStatus::Idle;

void MarkTimeSyncStarted() {
    if (g_time_sync_status != TimeSyncStatus::Synced) {
        g_time_sync_status = TimeSyncStatus::Syncing;
    }
}

void MarkTimeSyncSucceeded() {
    g_time_sync_status = TimeSyncStatus::Synced;
}

TimeSyncStatus GetTimeSyncStatus() {
    return g_time_sync_status;
}
```

- [ ] **Step 4: Wire Wi-Fi SNTP into the status module**

In `main/boards/common/wifi_board.cc`, add:

```cpp
#include "time_sync_status.h"
```

Inside `OnSntpTimeSync()` before logging:

```cpp
MarkTimeSyncSucceeded();
```

Inside `StartTimeSynchronization()` immediately before `esp_sntp_init();`:

```cpp
MarkTimeSyncStarted();
```

In `main/CMakeLists.txt`, add to the common source list:

```cmake
"boards/common/time_sync_status.cc"
```

- [ ] **Step 5: Run the path test to verify it passes**

Run:

```powershell
python -m pytest tests/wifi_config_status_path_test.py -q
```

Expected: all tests in the file pass.

---

### Task 3: Build the Orbit AOD LVGL Layout

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Test: `tests/xiaoxin_low_power_clock_visual_path_test.py`

**Interfaces:**
- Consumes: `low_power_clock_snapshot_.date_text`, `battery_text`, `sync_text`, `sync_color_hex`.
- Produces new LVGL object fields:
  - `low_power_clock_outer_arc_`
  - `low_power_clock_inner_arc_`
  - `low_power_clock_date_label_`
  - `low_power_clock_battery_label_`
  - `low_power_clock_sync_dot_`
  - `low_power_clock_sync_label_`

- [ ] **Step 1: Write the failing visual path test**

Add assertions to `tests/xiaoxin_low_power_clock_visual_path_test.py`:

```python
def test_low_power_clock_uses_orbit_aod_visual_language():
    source = read_source()

    assert "lv_obj_t* low_power_clock_outer_arc_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_inner_arc_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_date_label_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_battery_label_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_sync_dot_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_sync_label_ = nullptr;" in source
    assert "lv_obj_set_style_arc_color(low_power_clock_outer_arc_, lv_color_hex(0x102A35), LV_PART_MAIN);" in source
    assert "lv_obj_set_style_arc_color(low_power_clock_inner_arc_, lv_color_hex(0x26D9FF), LV_PART_INDICATOR);" in source
    assert "lv_obj_align(low_power_clock_date_label_, LV_ALIGN_TOP_MID, 0, 34);" in source
    assert "lv_obj_align(low_power_clock_battery_label_, LV_ALIGN_BOTTOM_LEFT, 22, -20);" in source
    assert "lv_obj_align(low_power_clock_sync_label_, LV_ALIGN_BOTTOM_RIGHT, -22, -20);" in source
```

- [ ] **Step 2: Run the visual path test to verify it fails**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: failure because the new orbit objects do not exist.

- [ ] **Step 3: Add object fields**

In the `PaopaoPetDisplay` private fields near existing low-power clock fields, replace the single arc field with:

```cpp
lv_obj_t* low_power_clock_outer_arc_ = nullptr;
lv_obj_t* low_power_clock_inner_arc_ = nullptr;
lv_obj_t* low_power_clock_time_label_ = nullptr;
lv_obj_t* low_power_clock_date_label_ = nullptr;
lv_obj_t* low_power_clock_battery_label_ = nullptr;
lv_obj_t* low_power_clock_sync_dot_ = nullptr;
lv_obj_t* low_power_clock_sync_label_ = nullptr;
lv_obj_t* low_power_clock_hint_label_ = nullptr;
```

- [ ] **Step 4: Replace the simple arc with two orbit arcs**

In `InitializeLowPowerClockLayerLocked()`, create the outer arc:

```cpp
low_power_clock_outer_arc_ = lv_arc_create(low_power_clock_layer_);
lv_obj_set_size(low_power_clock_outer_arc_, DISPLAY_WIDTH - 10, DISPLAY_HEIGHT - 10);
lv_obj_center(low_power_clock_outer_arc_);
lv_arc_set_bg_angles(low_power_clock_outer_arc_, 0, 360);
lv_arc_set_angles(low_power_clock_outer_arc_, 0, 360);
lv_obj_remove_style(low_power_clock_outer_arc_, NULL, LV_PART_KNOB);
lv_obj_clear_flag(low_power_clock_outer_arc_, LV_OBJ_FLAG_CLICKABLE);
lv_obj_set_style_arc_width(low_power_clock_outer_arc_, 3, LV_PART_MAIN);
lv_obj_set_style_arc_width(low_power_clock_outer_arc_, 3, LV_PART_INDICATOR);
lv_obj_set_style_arc_color(low_power_clock_outer_arc_, lv_color_hex(0x102A35), LV_PART_MAIN);
lv_obj_set_style_arc_color(low_power_clock_outer_arc_, lv_color_hex(0x163D4A), LV_PART_INDICATOR);
lv_obj_set_style_arc_opa(low_power_clock_outer_arc_, LV_OPA_60, LV_PART_MAIN);
lv_obj_set_style_arc_opa(low_power_clock_outer_arc_, LV_OPA_50, LV_PART_INDICATOR);
```

Create the inner animated arc:

```cpp
low_power_clock_inner_arc_ = lv_arc_create(low_power_clock_layer_);
lv_obj_set_size(low_power_clock_inner_arc_, DISPLAY_WIDTH - 28, DISPLAY_HEIGHT - 28);
lv_obj_center(low_power_clock_inner_arc_);
lv_arc_set_bg_angles(low_power_clock_inner_arc_, 0, 360);
lv_arc_set_angles(low_power_clock_inner_arc_, 0, XIAOXIN_LOW_POWER_CLOCK_ARC_SPAN_DEGREES);
lv_obj_remove_style(low_power_clock_inner_arc_, NULL, LV_PART_KNOB);
lv_obj_clear_flag(low_power_clock_inner_arc_, LV_OBJ_FLAG_CLICKABLE);
lv_obj_set_style_arc_width(low_power_clock_inner_arc_, 5, LV_PART_MAIN);
lv_obj_set_style_arc_width(low_power_clock_inner_arc_, 7, LV_PART_INDICATOR);
lv_obj_set_style_arc_color(low_power_clock_inner_arc_, lv_color_hex(0x071015), LV_PART_MAIN);
lv_obj_set_style_arc_color(low_power_clock_inner_arc_, lv_color_hex(0x26D9FF), LV_PART_INDICATOR);
lv_obj_set_style_arc_opa(low_power_clock_inner_arc_, LV_OPA_30, LV_PART_MAIN);
lv_obj_set_style_arc_opa(low_power_clock_inner_arc_, LV_OPA_COVER, LV_PART_INDICATOR);
lv_obj_set_style_arc_rounded(low_power_clock_inner_arc_, true, LV_PART_INDICATOR);
```

- [ ] **Step 5: Add date, battery, and sync footer labels**

After the time label setup, create:

```cpp
low_power_clock_date_label_ = lv_label_create(low_power_clock_layer_);
lv_obj_set_style_text_color(low_power_clock_date_label_, lv_color_hex(0x75AFC0), 0);
lv_obj_set_style_text_opa(low_power_clock_date_label_, LV_OPA_80, 0);
if (hint_font != nullptr) {
    lv_obj_set_style_text_font(low_power_clock_date_label_, hint_font, 0);
}
lv_obj_align(low_power_clock_date_label_, LV_ALIGN_TOP_MID, 0, 34);

low_power_clock_battery_label_ = lv_label_create(low_power_clock_layer_);
lv_obj_set_style_text_color(low_power_clock_battery_label_, lv_color_hex(0x8BE7B1), 0);
lv_obj_set_style_text_opa(low_power_clock_battery_label_, LV_OPA_80, 0);
if (hint_font != nullptr) {
    lv_obj_set_style_text_font(low_power_clock_battery_label_, hint_font, 0);
}
lv_obj_align(low_power_clock_battery_label_, LV_ALIGN_BOTTOM_LEFT, 22, -20);

low_power_clock_sync_dot_ = lv_obj_create(low_power_clock_layer_);
lv_obj_remove_style_all(low_power_clock_sync_dot_);
lv_obj_set_size(low_power_clock_sync_dot_, 6, 6);
lv_obj_set_style_radius(low_power_clock_sync_dot_, LV_RADIUS_CIRCLE, 0);
lv_obj_set_style_bg_opa(low_power_clock_sync_dot_, LV_OPA_COVER, 0);
lv_obj_align(low_power_clock_sync_dot_, LV_ALIGN_BOTTOM_RIGHT, -64, -24);

low_power_clock_sync_label_ = lv_label_create(low_power_clock_layer_);
lv_obj_set_style_text_color(low_power_clock_sync_label_, lv_color_hex(0x75AFC0), 0);
lv_obj_set_style_text_opa(low_power_clock_sync_label_, LV_OPA_80, 0);
if (hint_font != nullptr) {
    lv_obj_set_style_text_font(low_power_clock_sync_label_, hint_font, 0);
}
lv_obj_align(low_power_clock_sync_label_, LV_ALIGN_BOTTOM_RIGHT, -22, -20);
```

- [ ] **Step 6: Run the visual path test to verify it passes**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: all tests in the file pass.

---

### Task 4: Feed Date, Battery, and Sync State into the Sleep Clock

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Test: `tests/xiaoxin_low_power_clock_visual_path_test.py`

**Interfaces:**
- Consumes:
  - `battery_snapshot_.estimated_percent`
  - `battery_snapshot_.state`
  - `GetTimeSyncStatus()`
  - `localtime_r()`
- Produces: fully populated `xiaoxin_low_power_clock_state_t`.

- [ ] **Step 1: Write the failing visual path test**

Add:

```python
def test_low_power_clock_state_includes_date_battery_and_sync_status():
    source = read_source()
    build_state_section = source[
        source.index("static xiaoxin_low_power_clock_state_t BuildLowPowerClockState("):
        source.index("void RefreshLowPowerClockScreenLocked(bool force)")
    ]

    assert "battery_snapshot_" in build_state_section
    assert "GetTimeSyncStatus()" in build_state_section
    assert "state.month = timeinfo.tm_mon + 1;" in build_state_section
    assert "state.day = timeinfo.tm_mday;" in build_state_section
    assert "state.weekday = timeinfo.tm_wday;" in build_state_section
    assert "state.battery_percent = battery_snapshot_.estimated_percent;" in build_state_section
```

- [ ] **Step 2: Run the visual path test to verify it fails**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: failure because `BuildLowPowerClockState()` is still static and does not read battery or sync state.

- [ ] **Step 3: Include sync status and make state building an instance method**

At the top of `esp32-s3-touch-lcd-1.46.cc`, add:

```cpp
#include "time_sync_status.h"
```

Change:

```cpp
static xiaoxin_low_power_clock_state_t BuildLowPowerClockState()
```

to:

```cpp
xiaoxin_low_power_clock_state_t BuildLowPowerClockState()
```

Inside it, after valid `localtime_r()`:

```cpp
state.month = timeinfo.tm_mon + 1;
state.day = timeinfo.tm_mday;
state.weekday = timeinfo.tm_wday;
```

After time handling, add:

```cpp
state.battery_known = battery_snapshot_.state != XIAOXIN_BATTERY_STATE_UNKNOWN;
state.battery_percent = battery_snapshot_.estimated_percent;

const TimeSyncStatus sync_status = GetTimeSyncStatus();
if (sync_status == TimeSyncStatus::Synced) {
    state.sync_state = XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCED;
} else if (sync_status == TimeSyncStatus::Syncing) {
    state.sync_state = XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCING;
} else {
    state.sync_state = XIAOXIN_LOW_POWER_CLOCK_SYNC_IDLE;
}
```

- [ ] **Step 4: Update refresh logic to set all labels and sync dot**

In `RefreshLowPowerClockScreenLocked(bool force)`, after setting `time_text`, add:

```cpp
lv_label_set_text(low_power_clock_date_label_, low_power_clock_snapshot_.date_text);
lv_label_set_text(low_power_clock_battery_label_, low_power_clock_snapshot_.battery_text);
lv_label_set_text(low_power_clock_sync_label_, low_power_clock_snapshot_.sync_text);
lv_obj_set_style_bg_color(low_power_clock_sync_dot_, lv_color_hex(low_power_clock_snapshot_.sync_color_hex), 0);
```

- [ ] **Step 5: Run the visual path test to verify it passes**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: all tests in the file pass.

---

### Task 5: Animate the Orbit Clock Without Refreshing Text Too Often

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c`
- Test: `tests/xiaoxin_low_power_clock_visual_path_test.py`
- Test: `tests/xiaoxin_low_power_clock_model_test.c`

**Interfaces:**
- Consumes: existing `xiaoxin_low_power_clock_animation_phase(uint32_t tick)`.
- Produces: inner arc rotation and subtle outer arc phase.

- [ ] **Step 1: Write the failing animation path test**

Add:

```python
def test_low_power_clock_orbit_animation_updates_two_rings():
    source = read_source()
    animation_section = source[
        source.index("void RefreshLowPowerClockAnimationLocked()"):
        source.index("void RefreshLowPowerClockScreenFromTimer()")
    ]

    assert "lv_arc_set_rotation(low_power_clock_inner_arc_, start);" in animation_section
    assert "lv_arc_set_rotation(low_power_clock_outer_arc_, (start + 180) % 360);" in animation_section
    assert "lv_obj_set_style_opa(low_power_clock_sync_dot_, dot_opa, 0);" in animation_section
```

- [ ] **Step 2: Run the visual path test to verify it fails**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: failure because only one arc is animated today.

- [ ] **Step 3: Update animation implementation**

Replace the current animation function body with:

```cpp
void RefreshLowPowerClockAnimationLocked() {
    if (low_power_clock_inner_arc_ == nullptr || low_power_clock_outer_arc_ == nullptr) {
        return;
    }

    const uint16_t start = xiaoxin_low_power_clock_animation_phase(low_power_clock_animation_tick_++);
    lv_arc_set_rotation(low_power_clock_inner_arc_, start);
    lv_arc_set_rotation(low_power_clock_outer_arc_, (start + 180) % 360);

    if (low_power_clock_sync_dot_ != nullptr) {
        const lv_opa_t dot_opa = (low_power_clock_animation_tick_ % 2U) == 0U ? LV_OPA_COVER : LV_OPA_60;
        lv_obj_set_style_opa(low_power_clock_sync_dot_, dot_opa, 0);
    }
}
```

- [ ] **Step 4: Run tests to verify animation still passes**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
gcc tests/xiaoxin_low_power_clock_model_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 -o build/xiaoxin_low_power_clock_model_test.exe; if ($LASTEXITCODE -eq 0) { .\build\xiaoxin_low_power_clock_model_test.exe }
```

Expected: visual path tests pass and C model test exits `0`.

---

### Task 6: Final Verification

**Files:**
- Verify all files touched in Tasks 1-5.

**Interfaces:**
- Consumes: all task outputs.
- Produces: evidence that the orbit clock work is safe to flash.

- [ ] **Step 1: Run targeted tests**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py tests/xiaoxin_settings_path_test.py tests/wifi_config_status_path_test.py -q
```

Expected: targeted pytest suite passes.

- [ ] **Step 2: Run all Python tests**

Run:

```powershell
python -m pytest tests -q
```

Expected: all Python tests pass.

- [ ] **Step 3: Run C model tests**

Run:

```powershell
gcc tests/xiaoxin_low_power_clock_model_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 -o build/xiaoxin_low_power_clock_model_test.exe; if ($LASTEXITCODE -eq 0) { .\build\xiaoxin_low_power_clock_model_test.exe }
```

Expected: exit code `0`.

- [ ] **Step 4: Check diff hygiene**

Run:

```powershell
git diff --check
```

Expected: exit code `0`.

- [ ] **Step 5: Attempt firmware build only if ESP-IDF is available**

Run:

```powershell
idf.py build
```

Expected when ESP-IDF is configured: build exits `0`.

Expected in the current Codex PowerShell environment: `idf.py` may not be recognized. If that happens, report the environment limitation and do not claim firmware build success.

---

## Self-Review

- Spec coverage: The plan covers the selected A direction: large center time, two orbit rings, date, battery, sync status, and dynamic motion.
- Placeholder scan: The plan contains no placeholder implementation steps.
- Type consistency: `TimeSyncStatus`, `xiaoxin_low_power_clock_sync_state_t`, and new snapshot field names are defined before use.
