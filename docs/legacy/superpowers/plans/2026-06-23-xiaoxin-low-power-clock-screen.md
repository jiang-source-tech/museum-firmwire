# Xiaoxin Low-Power Clock Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a low-power clock screen for the Waveshare ESP32-S3 Touch LCD 1.46 board that shows an alarm icon on the left, the current `HH:MM` time beside it, and `按下 POWER 键唤醒` at the bottom while the device is in power-save display mode.

**Architecture:** Keep the feature board-local and small. Add a pure C model for formatting the clock-screen text and refresh cadence, then add a hidden LVGL overlay inside `PaopaoPetDisplay` that is shown by the existing `PowerSaveTimer::OnEnterSleepMode` callback and hidden by `OnExitSleepMode`. Use the existing `PWR_BUTTON_GPIO` callback path to call `WakePowerSaveTimer()`, and lower the TFT backlight while the overlay is visible.

**Tech Stack:** ESP-IDF, C/C++, LVGL, existing `PowerSaveTimer`, existing `Backlight`, existing Python source-shape tests, existing C unit-test style.

## Global Constraints

- Do not implement real `esp_deep_sleep_start()` for this screen; this is a pseudo-sleep low-power display mode.
- The visible layout is icon-left/time-right: `LV_SYMBOL_BELL 14:32` or a fallback icon if the selected LVGL font cannot render the symbol.
- Bottom hint text must be exactly `按下 POWER 键唤醒`.
- Do not show seconds and do not animate the screen.
- Refresh the clock no more often than once per minute after the initial render.
- Default low-power display brightness should be `8%`.
- Restore the user's previous/permanent brightness when leaving the low-power clock screen.
- POWER short press wakes the device by using the existing `WakePowerSaveTimer()` path.
- Keep touch/BOOT wake behavior unchanged unless a later task explicitly narrows it.

---

## File Structure

- Create `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.h`
  - Owns pure data structures and formatting helpers for the low-power clock screen.
- Create `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c`
  - Formats `HH:MM`, returns icon/hint strings, and decides minute-based refresh.
- Leave `main/CMakeLists.txt` unchanged unless local verification proves otherwise.
  - The board build already collects `*.c` files from this board directory via `file(GLOB ...)`.
- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
  - Adds LVGL overlay objects and power-save enter/exit methods on `PaopaoPetDisplay`.
  - Wires the existing `PowerSaveTimer` callbacks to the new overlay.
  - Applies temporary 8% brightness in low-power clock mode and restores brightness on wake.
- Create `tests/xiaoxin_low_power_clock_model_test.c`
  - Verifies formatting, fallback time, icon/hint text, and minute refresh cadence.
- Modify or create `tests/xiaoxin_low_power_clock_visual_path_test.py`
  - Source-shape tests for LVGL overlay wiring, icon-left layout, bottom hint, brightness behavior, and POWER wake path.
- Modify `tests/xiaoxin_settings_path_test.py`
  - Updates the existing power-save callback expectations so they do not conflict with the new clock-screen callbacks.

---

### Task 1: Add the pure low-power clock model

**Files:**
- Create: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.h`
- Create: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c`
- Create: `tests/xiaoxin_low_power_clock_model_test.c`

**Interfaces:**
- Consumes: plain time fields from the board display layer.
- Produces:
  - `void xiaoxin_low_power_clock_model_build(const xiaoxin_low_power_clock_state_t* state, xiaoxin_low_power_clock_snapshot_t* snapshot);`
  - `bool xiaoxin_low_power_clock_should_refresh(uint8_t previous_minute, uint8_t current_minute);`
  - `#define XIAOXIN_LOW_POWER_CLOCK_DEFAULT_BRIGHTNESS 8`

- [ ] **Step 1: Write the failing model test**

Add `tests/xiaoxin_low_power_clock_model_test.c`:

```c
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.h"

static void valid_time_formats_as_hh_mm(void) {
  xiaoxin_low_power_clock_state_t state = {
    .time_valid = true,
    .hour = 9,
    .minute = 5,
  };
  xiaoxin_low_power_clock_snapshot_t snapshot = {};

  xiaoxin_low_power_clock_model_build(&state, &snapshot);

  assert(strcmp(snapshot.time_text, "09:05") == 0);
  assert(strcmp(snapshot.icon_text, XIAOXIN_LOW_POWER_CLOCK_ICON_TEXT) == 0);
  assert(strcmp(snapshot.hint_text, "按下 POWER 键唤醒") == 0);
  assert(snapshot.brightness_percent == XIAOXIN_LOW_POWER_CLOCK_DEFAULT_BRIGHTNESS);
}

static void invalid_time_uses_placeholder(void) {
  xiaoxin_low_power_clock_state_t state = {
    .time_valid = false,
    .hour = 14,
    .minute = 32,
  };
  xiaoxin_low_power_clock_snapshot_t snapshot = {};

  xiaoxin_low_power_clock_model_build(&state, &snapshot);

  assert(strcmp(snapshot.time_text, "--:--") == 0);
}

static void time_fields_are_clamped_to_displayable_range(void) {
  xiaoxin_low_power_clock_state_t state = {
    .time_valid = true,
    .hour = 128,
    .minute = -3,
  };
  xiaoxin_low_power_clock_snapshot_t snapshot = {};

  xiaoxin_low_power_clock_model_build(&state, &snapshot);

  assert(strcmp(snapshot.time_text, "23:00") == 0);
}

static void refresh_only_when_minute_changes(void) {
  assert(!xiaoxin_low_power_clock_should_refresh(32, 32));
  assert(xiaoxin_low_power_clock_should_refresh(32, 33));
  assert(xiaoxin_low_power_clock_should_refresh(59, 0));
}

int main(void) {
  valid_time_formats_as_hh_mm();
  invalid_time_uses_placeholder();
  time_fields_are_clamped_to_displayable_range();
  refresh_only_when_minute_changes();
  return 0;
}
```

- [ ] **Step 2: Run the new test and verify it fails**

Run:

```powershell
gcc tests/xiaoxin_low_power_clock_model_test.c `
  main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c `
  -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 `
  -o build/xiaoxin_low_power_clock_model_test.exe
```

Expected: FAIL because the new header/source files do not exist yet.

- [ ] **Step 3: Add the model header**

Create `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XIAOXIN_LOW_POWER_CLOCK_TIME_MAX 8
#define XIAOXIN_LOW_POWER_CLOCK_HINT_MAX 32
#define XIAOXIN_LOW_POWER_CLOCK_DEFAULT_BRIGHTNESS 8
#define XIAOXIN_LOW_POWER_CLOCK_ICON_TEXT "\xEF\x83\xB3"

typedef struct {
  bool time_valid;
  int hour;
  int minute;
} xiaoxin_low_power_clock_state_t;

typedef struct {
  char icon_text[8];
  char time_text[XIAOXIN_LOW_POWER_CLOCK_TIME_MAX];
  char hint_text[XIAOXIN_LOW_POWER_CLOCK_HINT_MAX];
  uint8_t brightness_percent;
} xiaoxin_low_power_clock_snapshot_t;

void xiaoxin_low_power_clock_model_build(
  const xiaoxin_low_power_clock_state_t* state,
  xiaoxin_low_power_clock_snapshot_t* snapshot
);

bool xiaoxin_low_power_clock_should_refresh(
  uint8_t previous_minute,
  uint8_t current_minute
);

#ifdef __cplusplus
}
#endif
```

Note: `XIAOXIN_LOW_POWER_CLOCK_ICON_TEXT` uses the same UTF-8 byte sequence as this repo's `FONT_AWESOME_BELL`. If visual QA shows the board font renders it poorly, replace only this macro with a known-supported icon string and keep the left-of-time layout unchanged.

- [ ] **Step 4: Add the model implementation**

Create `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c`:

```c
#include "xiaoxin_low_power_clock_model.h"

#include <stdio.h>
#include <string.h>

static int clamp_int(int value, int min, int max) {
  if (value < min) {
    return min;
  }
  if (value > max) {
    return max;
  }
  return value;
}

void xiaoxin_low_power_clock_model_build(
  const xiaoxin_low_power_clock_state_t* state,
  xiaoxin_low_power_clock_snapshot_t* snapshot
) {
  if (snapshot == 0) {
    return;
  }

  memset(snapshot, 0, sizeof(*snapshot));
  snprintf(snapshot->icon_text, sizeof(snapshot->icon_text), "%s", XIAOXIN_LOW_POWER_CLOCK_ICON_TEXT);
  snprintf(snapshot->hint_text, sizeof(snapshot->hint_text), "%s", "按下 POWER 键唤醒");
  snapshot->brightness_percent = XIAOXIN_LOW_POWER_CLOCK_DEFAULT_BRIGHTNESS;

  if (state == 0 || !state->time_valid) {
    snprintf(snapshot->time_text, sizeof(snapshot->time_text), "%s", "--:--");
    return;
  }

  const int hour = clamp_int(state->hour, 0, 23);
  const int minute = clamp_int(state->minute, 0, 59);
  snprintf(snapshot->time_text, sizeof(snapshot->time_text), "%02d:%02d", hour, minute);
}

bool xiaoxin_low_power_clock_should_refresh(
  uint8_t previous_minute,
  uint8_t current_minute
) {
  return previous_minute != current_minute;
}
```

- [ ] **Step 5: Confirm the model source is picked up by the firmware build**

Do not add the model source manually to `main/CMakeLists.txt` unless verification proves the existing board source glob misses it. The current board build already collects board-local `*.c` files:

```cmake
file(GLOB BOARD_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${MANUFACTURER}/${BOARD_TYPE}/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${MANUFACTURER}/${BOARD_TYPE}/*.c
)
```

- [ ] **Step 6: Run the model test and verify it passes**

Run:

```powershell
gcc tests/xiaoxin_low_power_clock_model_test.c `
  main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c `
  -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 `
  -o build/xiaoxin_low_power_clock_model_test.exe; `
  .\build\xiaoxin_low_power_clock_model_test.exe
```

Expected: command exits with code `0`.

- [ ] **Step 7: Commit**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.h `
  main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c `
  tests/xiaoxin_low_power_clock_model_test.c
git commit -m "feat: add low power clock model"
```

---

### Task 2: Add the LVGL low-power clock overlay

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Create: `tests/xiaoxin_low_power_clock_visual_path_test.py`

**Interfaces:**
- Consumes:
  - `xiaoxin_low_power_clock_model_build(...)`
  - `xiaoxin_low_power_clock_should_refresh(...)`
  - existing `DisplayLockGuard`
  - existing `Backlight::SetBrightness(...)` and `Backlight::RestoreBrightness()`
- Produces:
  - `void PaopaoPetDisplay::ShowLowPowerClockScreen()`
  - `void PaopaoPetDisplay::HideLowPowerClockScreen()`
  - `void PaopaoPetDisplay::RefreshLowPowerClockScreenLocked(bool force)`

- [ ] **Step 1: Write the visual-path test**

Create `tests/xiaoxin_low_power_clock_visual_path_test.py`:

```python
from pathlib import Path

SOURCE = Path("main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc")


def read_source():
    return SOURCE.read_text(encoding="utf-8")


def test_low_power_clock_objects_exist_and_are_hidden_by_default():
    source = read_source()
    assert "lv_obj_t* low_power_clock_layer_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_icon_label_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_time_label_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_hint_label_ = nullptr;" in source
    assert "lv_obj_add_flag(low_power_clock_layer_, LV_OBJ_FLAG_HIDDEN);" in source


def test_low_power_clock_uses_icon_left_time_right_layout():
    source = read_source()
    assert "lv_obj_align(low_power_clock_icon_label_, LV_ALIGN_TOP_MID" in source
    assert "lv_obj_align_to(low_power_clock_time_label_, low_power_clock_icon_label_, LV_ALIGN_OUT_RIGHT_MID" in source
    assert "lv_obj_set_style_text_font(low_power_clock_icon_label_, icon_font, 0);" in source
    assert "lv_label_set_text(low_power_clock_hint_label_, low_power_clock_snapshot_.hint_text);" in source
    assert "LV_ALIGN_BOTTOM_MID" in source


def test_low_power_clock_enters_with_dim_backlight_and_exits_with_restore():
    source = read_source()
    assert "ShowLowPowerClockScreen()" in source
    assert "HideLowPowerClockScreen()" in source
    assert "backlight->SetBrightness(low_power_clock_snapshot_.brightness_percent, false);" in source
    assert "backlight->RestoreBrightness();" in source


def test_power_button_still_wakes_power_save_timer():
    source = read_source()
    power_section = source[source.index("// Power Button"):]
    assert "self->WakePowerSaveTimer();" in power_section
```

- [ ] **Step 2: Run the visual-path test and verify it fails**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: FAIL because the overlay objects do not exist yet.

- [ ] **Step 3: Include the model header**

In `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`, add:

```cpp
extern "C" {
#include "xiaoxin_low_power_clock_model.h"
}
```

- [ ] **Step 4: Add display members**

Inside `PaopaoPetDisplay`, near the other LVGL object members, add:

```cpp
    lv_obj_t* low_power_clock_layer_ = nullptr;
    lv_obj_t* low_power_clock_icon_label_ = nullptr;
    lv_obj_t* low_power_clock_time_label_ = nullptr;
    lv_obj_t* low_power_clock_hint_label_ = nullptr;
    xiaoxin_low_power_clock_snapshot_t low_power_clock_snapshot_ = {};
    uint8_t low_power_clock_last_minute_ = 0xff;
    bool low_power_clock_visible_ = false;
```

- [ ] **Step 5: Create the overlay during display setup**

In the display setup path where the other overlays are created, add an `InitializeLowPowerClockLayerLocked()` helper:

```cpp
    void InitializeLowPowerClockLayerLocked() {
        lv_obj_t* screen = lv_screen_active();
        auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        const lv_font_t* icon_font = lvgl_theme != nullptr ? lvgl_theme->icon_font()->font() : nullptr;

        low_power_clock_layer_ = lv_obj_create(screen);
        lv_obj_remove_style_all(low_power_clock_layer_);
        lv_obj_set_size(low_power_clock_layer_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lv_obj_set_style_bg_color(low_power_clock_layer_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(low_power_clock_layer_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(low_power_clock_layer_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(low_power_clock_layer_, LV_OBJ_FLAG_HIDDEN);

        low_power_clock_icon_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_icon_label_, lv_color_hex(0xF6FAFF), 0);
        lv_obj_set_style_text_opa(low_power_clock_icon_label_, LV_OPA_COVER, 0);
        if (icon_font != nullptr) {
            lv_obj_set_style_text_font(low_power_clock_icon_label_, icon_font, 0);
        }
        lv_obj_align(low_power_clock_icon_label_, LV_ALIGN_TOP_MID, -42, 54);

        low_power_clock_time_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_time_label_, lv_color_hex(0xF6FAFF), 0);
        lv_obj_set_style_text_opa(low_power_clock_time_label_, LV_OPA_COVER, 0);
        lv_obj_align_to(low_power_clock_time_label_, low_power_clock_icon_label_, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

        low_power_clock_hint_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_hint_label_, lv_color_hex(0x9AA4B2), 0);
        lv_obj_set_style_text_opa(low_power_clock_hint_label_, LV_OPA_COVER, 0);
        lv_obj_align(low_power_clock_hint_label_, LV_ALIGN_BOTTOM_MID, 0, -18);
    }
```

Call `InitializeLowPowerClockLayerLocked()` once after the main screen/card/status layers exist.

- [ ] **Step 6: Add time capture and refresh helpers**

Add these methods to `PaopaoPetDisplay`:

```cpp
    static xiaoxin_low_power_clock_state_t BuildLowPowerClockState() {
        xiaoxin_low_power_clock_state_t state = {};
        time_t now = 0;
        time(&now);

        struct tm timeinfo = {};
        if (now > 24 * 60 * 60 &&
            localtime_r(&now, &timeinfo) != nullptr &&
            timeinfo.tm_year >= 120) {
            state.time_valid = true;
            state.hour = timeinfo.tm_hour;
            state.minute = timeinfo.tm_min;
        }
        return state;
    }

    void RefreshLowPowerClockScreenLocked(bool force) {
        if (low_power_clock_layer_ == nullptr) {
            return;
        }

        const xiaoxin_low_power_clock_state_t state = BuildLowPowerClockState();
        const uint8_t current_minute = state.time_valid ? (uint8_t)state.minute : 0xff;
        if (!force &&
            !xiaoxin_low_power_clock_should_refresh(low_power_clock_last_minute_, current_minute)) {
            return;
        }

        low_power_clock_last_minute_ = current_minute;
        xiaoxin_low_power_clock_model_build(&state, &low_power_clock_snapshot_);

        lv_label_set_text(low_power_clock_icon_label_, low_power_clock_snapshot_.icon_text);
        lv_label_set_text(low_power_clock_time_label_, low_power_clock_snapshot_.time_text);
        lv_label_set_text(low_power_clock_hint_label_, low_power_clock_snapshot_.hint_text);
    }
```

- [ ] **Step 7: Add public show/hide methods**

Add these public methods to `PaopaoPetDisplay`:

```cpp
    void ShowLowPowerClockScreen() {
        DisplayLockGuard lock(this);
        low_power_clock_visible_ = true;
        low_power_clock_last_minute_ = 0xff;
        RefreshLowPowerClockScreenLocked(true);
        if (low_power_clock_layer_ != nullptr) {
            lv_obj_remove_flag(low_power_clock_layer_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(low_power_clock_layer_);
        }

        auto backlight = Board::GetInstance().GetBacklight();
        if (backlight != nullptr) {
            backlight->SetBrightness(low_power_clock_snapshot_.brightness_percent, false);
        }
    }

    void HideLowPowerClockScreen() {
        DisplayLockGuard lock(this);
        low_power_clock_visible_ = false;
        low_power_clock_last_minute_ = 0xff;
        if (low_power_clock_layer_ != nullptr) {
            lv_obj_add_flag(low_power_clock_layer_, LV_OBJ_FLAG_HIDDEN);
        }

        auto backlight = Board::GetInstance().GetBacklight();
        if (backlight != nullptr) {
            backlight->RestoreBrightness();
        }
    }
```

- [ ] **Step 8: Refresh only when visible**

In the existing periodic display refresh/update path, add:

```cpp
        if (low_power_clock_visible_) {
            RefreshLowPowerClockScreenLocked(false);
        }
```

Place this behind an existing display lock. If no suitable minute-level display refresh exists, add an `esp_timer` dedicated to this overlay with a 10-second period and keep the model guard so labels only change when the minute changes.

- [ ] **Step 9: Run the visual-path test and verify it passes**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: all tests in this file pass.

- [ ] **Step 10: Commit**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc `
  tests/xiaoxin_low_power_clock_visual_path_test.py
git commit -m "feat: add low power clock overlay"
```

---

### Task 3: Wire the power-save timer to the low-power clock screen

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Modify: `tests/xiaoxin_low_power_clock_visual_path_test.py`
- Modify: `tests/xiaoxin_settings_path_test.py`

**Interfaces:**
- Consumes:
  - `PaopaoPetDisplay::ShowLowPowerClockScreen()`
  - `PaopaoPetDisplay::HideLowPowerClockScreen()`
- Produces:
  - Existing power-save timeout enters the pseudo-sleep clock screen.
  - Existing POWER short press exits pseudo-sleep via `WakePowerSaveTimer()`.

- [ ] **Step 1: Replace the existing display power-save callbacks**

In `InitializePowerSaveTimer()`, replace:

```cpp
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
        });
```

with:

```cpp
        power_save_timer_->OnEnterSleepMode([this]() {
            auto* display = static_cast<PaopaoPetDisplay*>(display_);
            if (display != nullptr) {
                display->ShowLowPowerClockScreen();
            }
        });
        power_save_timer_->OnExitSleepMode([this]() {
            auto* display = static_cast<PaopaoPetDisplay*>(display_);
            if (display != nullptr) {
                display->HideLowPowerClockScreen();
            }
        });
```

- [ ] **Step 2: Add callback-path assertions to the visual-path test**

Add this test to `tests/xiaoxin_low_power_clock_visual_path_test.py`:

```python
def test_power_save_timer_callbacks_show_and_hide_clock_screen():
    source = read_source()
    assert "display->ShowLowPowerClockScreen();" in source
    assert "display->HideLowPowerClockScreen();" in source
    assert "GetDisplay()->SetPowerSaveMode(true);" not in source
    assert "GetDisplay()->SetPowerSaveMode(false);" not in source
```

- [ ] **Step 3: Keep the existing shutdown behavior**

Leave this callback unchanged:

```cpp
        power_save_timer_->OnShutdownRequest([this]() {
            RequestPowerOff();
        });
```

This means the device still powers off after the existing timeout if nobody wakes it.

- [ ] **Step 4: Ensure POWER short press still wakes**

Keep this existing code in the POWER button single-click callback:

```cpp
            self->WakePowerSaveTimer();
```

Do not add a separate overlay-only wake flag; the existing timer exit callback should be the single source of truth.

- [ ] **Step 5: Update the existing settings-path power-save expectations**

In `tests/xiaoxin_settings_path_test.py`, update `test_power_save_setting_is_only_enabled_when_timer_is_initialized()` so it expects the low-power clock callbacks instead of the old generic display power-save callbacks:

```python
    assert "ShowLowPowerClockScreen()" in source
    assert "HideLowPowerClockScreen()" in source
    assert "SetPowerSaveMode(true)" not in source
    assert "SetPowerSaveMode(false)" not in source
```

- [ ] **Step 6: Run callback-path tests**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py tests/xiaoxin_settings_path_test.py -q
```

Expected: all selected tests pass.

- [ ] **Step 7: Commit**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc `
  tests/xiaoxin_low_power_clock_visual_path_test.py `
  tests/xiaoxin_settings_path_test.py
git commit -m "feat: show clock screen in power save mode"
```

---

### Task 4: Verify firmware build and focused regression suite

**Files:**
- No new files.

**Interfaces:**
- Consumes all prior tasks.
- Produces confidence that the plan works in the existing board build.

- [ ] **Step 1: Run focused C model tests**

Run:

```powershell
gcc tests/xiaoxin_low_power_clock_model_test.c `
  main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c `
  -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 `
  -o build/xiaoxin_low_power_clock_model_test.exe; `
  .\build\xiaoxin_low_power_clock_model_test.exe
```

Expected: command exits with code `0`.

- [ ] **Step 2: Run focused Python path tests**

Run:

```powershell
python -m pytest `
  tests/xiaoxin_low_power_clock_visual_path_test.py `
  tests/xiaoxin_settings_path_test.py `
  tests/xiaoxin_notification_visual_path_test.py `
  -q
```

Expected: all selected tests pass.

- [ ] **Step 3: Build the selected board firmware**

Run:

```powershell
idf.py build
```

Expected: build succeeds for `CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_46=y`.

- [ ] **Step 4: Manual device smoke test**

Flash and observe the board:

```powershell
idf.py flash monitor
```

Expected manual behavior:

1. Let the device sit idle until the existing 60-second power-save timer fires.
2. Screen changes to a black low-power clock page.
3. Icon appears on the left of the time.
4. Time appears as `HH:MM`.
5. Bottom text reads `按下 POWER 键唤醒`.
6. Backlight visibly dims but remains readable indoors.
7. POWER short press wakes back to the normal UI and restores previous brightness.
8. Long press POWER still requests power off.

- [ ] **Step 5: Commit verification-only updates if any were needed**

If verification required test expectation tweaks, commit them:

```powershell
git add tests main
git commit -m "test: cover low power clock screen integration"
```

If no files changed, skip this commit.

---

## Self-Review

- Spec coverage: The plan covers the requested time display, icon on the left, bottom POWER wake hint, dim TFT pseudo-sleep behavior, minute refresh, POWER wake, and brightness restore.
- Placeholder scan: The plan has no placeholder markers, mojibake strings, or unspecified implementation steps.
- Type consistency: The model types are named `xiaoxin_low_power_clock_state_t` and `xiaoxin_low_power_clock_snapshot_t` consistently across tasks.
- Scope check: This is one board-local feature and does not require a cross-board display API change.
