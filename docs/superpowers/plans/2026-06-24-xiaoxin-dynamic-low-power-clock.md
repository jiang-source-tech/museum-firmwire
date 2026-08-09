# Xiaoxin Dynamic Low-Power Clock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the tiny static low-power clock on the Waveshare ESP32-S3 Touch LCD 1.46 board with a large center `HH:MM` display and subtle cyan/blue moving outer arc.

**Architecture:** Keep the feature board-local. Extend the existing pure C low-power clock model with a deterministic animation phase helper and a `12%` brightness default, then update the existing `PaopaoPetDisplay` low-power overlay to use LVGL labels and arcs instead of the small icon-left layout. Reuse the existing power-save callbacks, wake path, display lock, and low-power clock timer.

**Tech Stack:** ESP-IDF, C/C++, LVGL, existing `PowerSaveTimer`, existing `Backlight`, Python source-shape tests, focused C unit test.

## Global Constraints

- Only modify the Waveshare ESP32-S3 Touch LCD 1.46 board implementation and its focused tests.
- Keep using the existing `PowerSaveTimer` enter/exit callbacks.
- Keep using the existing `low_power_clock_layer_` overlay; do not add image assets.
- Draw the face with LVGL objects: center time label, outer arc, bottom hint.
- Do not call `esp_deep_sleep_start()` for this display mode.
- POWER short press continues to wake through `WakePowerSaveTimer()`.
- Default low-power clock brightness is `12%`.
- Text refresh happens only when the minute changes; arc animation can update every second.
- All LVGL object updates happen while holding the display lock.
- Do not revert unrelated existing workspace changes.

---

## File Structure

- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.h`
  - Owns pure low-power clock constants, snapshot formatting, and animation phase helper declarations.
- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c`
  - Implements time formatting, `12%` brightness default, minute refresh guard, and phase wrapping.
- Modify `tests/xiaoxin_low_power_clock_model_test.c`
  - Verifies brightness, time formatting, invalid time, refresh cadence, and animation phase wrapping.
- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
  - Reworks the low-power clock overlay into large center time plus animated LVGL arc.
- Modify `tests/xiaoxin_low_power_clock_visual_path_test.py`
  - Verifies the new large-time/arc layout, timer cadence, wake path, and removal of the old small icon-left layout.

---

### Task 1: Extend the Pure Clock Model

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.h`
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c`
- Test: `tests/xiaoxin_low_power_clock_model_test.c`

**Interfaces:**
- Consumes: existing `xiaoxin_low_power_clock_model_build(...)` and `xiaoxin_low_power_clock_should_refresh(...)`.
- Produces:
  - `#define XIAOXIN_LOW_POWER_CLOCK_DEFAULT_BRIGHTNESS 12`
  - `#define XIAOXIN_LOW_POWER_CLOCK_ARC_SPAN_DEGREES 76`
  - `uint16_t xiaoxin_low_power_clock_animation_phase(uint32_t tick);`

- [ ] **Step 1: Update the model test**

Replace `tests/xiaoxin_low_power_clock_model_test.c` with:

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
  assert(snapshot.brightness_percent == 12);
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

static void animation_phase_wraps_to_circle_degrees(void) {
  assert(xiaoxin_low_power_clock_animation_phase(0) == 0);
  assert(xiaoxin_low_power_clock_animation_phase(1) == 12);
  assert(xiaoxin_low_power_clock_animation_phase(29) == 348);
  assert(xiaoxin_low_power_clock_animation_phase(30) == 0);
  assert(xiaoxin_low_power_clock_animation_phase(31) == 12);
  assert(XIAOXIN_LOW_POWER_CLOCK_ARC_SPAN_DEGREES == 76);
}

int main(void) {
  valid_time_formats_as_hh_mm();
  invalid_time_uses_placeholder();
  time_fields_are_clamped_to_displayable_range();
  refresh_only_when_minute_changes();
  animation_phase_wraps_to_circle_degrees();
  return 0;
}
```

- [ ] **Step 2: Run the model test and verify it fails**

Run:

```powershell
gcc tests/xiaoxin_low_power_clock_model_test.c `
  main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c `
  -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 `
  -o build/xiaoxin_low_power_clock_model_test.exe; `
  .\build\xiaoxin_low_power_clock_model_test.exe
```

Expected: FAIL because `xiaoxin_low_power_clock_animation_phase` and the new arc constant do not exist yet, or because brightness is still `8`.

- [ ] **Step 3: Update the model header**

In `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.h`, change the constants and declarations to include:

```c
#define XIAOXIN_LOW_POWER_CLOCK_TIME_MAX 8
#define XIAOXIN_LOW_POWER_CLOCK_HINT_MAX 32
#define XIAOXIN_LOW_POWER_CLOCK_DEFAULT_BRIGHTNESS 12
#define XIAOXIN_LOW_POWER_CLOCK_ICON_TEXT "\xEF\x83\xB3"
#define XIAOXIN_LOW_POWER_CLOCK_ARC_SPAN_DEGREES 76
```

Add this declaration after `xiaoxin_low_power_clock_should_refresh(...)`:

```c
uint16_t xiaoxin_low_power_clock_animation_phase(uint32_t tick);
```

- [ ] **Step 4: Implement the animation helper**

In `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c`, keep the existing formatting logic and add:

```c
uint16_t xiaoxin_low_power_clock_animation_phase(uint32_t tick) {
  return (uint16_t)((tick % 30U) * 12U);
}
```

- [ ] **Step 5: Run the model test and verify it passes**

Run:

```powershell
gcc tests/xiaoxin_low_power_clock_model_test.c `
  main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c `
  -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 `
  -o build/xiaoxin_low_power_clock_model_test.exe; `
  .\build\xiaoxin_low_power_clock_model_test.exe
```

Expected: command exits with code `0`.

- [ ] **Step 6: Commit**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.h `
  main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c `
  tests/xiaoxin_low_power_clock_model_test.c
git commit -m "feat: add low power clock animation model"
```

---

### Task 2: Rebuild the LVGL Clock Face

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Test: `tests/xiaoxin_low_power_clock_visual_path_test.py`

**Interfaces:**
- Consumes:
  - `xiaoxin_low_power_clock_model_build(...)`
  - `xiaoxin_low_power_clock_animation_phase(uint32_t tick)`
  - `XIAOXIN_LOW_POWER_CLOCK_ARC_SPAN_DEGREES`
- Produces:
  - `lv_obj_t* low_power_clock_arc_ = nullptr;`
  - `uint32_t low_power_clock_animation_tick_ = 0;`
  - `void RefreshLowPowerClockAnimationLocked();`

- [ ] **Step 1: Update visual path tests for the new layout**

In `tests/xiaoxin_low_power_clock_visual_path_test.py`, replace the old icon-left layout test with:

```python
def test_low_power_clock_uses_large_center_time_and_arc_layout():
    source = read_source()
    assert "lv_obj_t* low_power_clock_arc_ = nullptr;" in source
    assert "lv_arc_create(low_power_clock_layer_)" in source
    assert "lv_obj_set_size(low_power_clock_arc_, DISPLAY_WIDTH - 14, DISPLAY_HEIGHT - 14);" in source
    assert "lv_arc_set_bg_angles(low_power_clock_arc_, 0, 360);" in source
    assert "lv_obj_remove_style(low_power_clock_arc_, NULL, LV_PART_KNOB);" in source
    assert "lv_obj_set_style_arc_color(low_power_clock_arc_, lv_color_hex(0x26D9FF), LV_PART_INDICATOR);" in source
    assert "lv_obj_align(low_power_clock_time_label_, LV_ALIGN_CENTER, 0, -10);" in source
    assert "lv_obj_set_style_text_font(low_power_clock_time_label_, clock_font, 0);" in source
    assert "lv_obj_align(low_power_clock_hint_label_, LV_ALIGN_BOTTOM_MID, 0, -18);" in source


def test_low_power_clock_no_longer_uses_small_icon_left_layout():
    source = read_source()
    assert "lv_obj_t* low_power_clock_icon_label_ = nullptr;" not in source
    assert "lv_obj_align_to(low_power_clock_time_label_, low_power_clock_icon_label_, LV_ALIGN_OUT_RIGHT_MID" not in source
    assert "lv_label_set_text(low_power_clock_icon_label_" not in source
```

Remove the old `test_low_power_clock_uses_fallback_icon_when_bell_glyph_is_missing` test entirely. Keep the existing tests that verify show/hide, power-save callbacks, wake path, and display-lock timer behavior.

- [ ] **Step 2: Run the visual path test and verify it fails**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: FAIL because `low_power_clock_arc_`, large center time layout, and icon removal are not implemented yet.

- [ ] **Step 3: Replace old icon member with arc and tick members**

In `PaopaoPetDisplay`, replace:

```cpp
    lv_obj_t* low_power_clock_icon_label_ = nullptr;
```

with:

```cpp
    lv_obj_t* low_power_clock_arc_ = nullptr;
```

Remove:

```cpp
    const char* low_power_clock_icon_text_ = XIAOXIN_LOW_POWER_CLOCK_ICON_TEXT;
```

Add near `low_power_clock_last_minute_`:

```cpp
    uint32_t low_power_clock_animation_tick_ = 0;
```

- [ ] **Step 4: Remove icon glyph fallback helpers**

Remove these exact methods from `PaopaoPetDisplay`:

```cpp
    static bool LowPowerClockFontHasBell(const lv_font_t* font) {
        if (font == nullptr || font->get_glyph_dsc == nullptr) {
            return false;
        }

        lv_font_glyph_dsc_t glyph_dsc = {};
        return font->get_glyph_dsc(font, &glyph_dsc, 0xf0f3, 0);
    }

    static const char* LowPowerClockIconTextForFont(const lv_font_t* font) {
        return LowPowerClockFontHasBell(font) ? XIAOXIN_LOW_POWER_CLOCK_ICON_TEXT : "*";
    }
```

The dynamic design does not render a bell icon, so font glyph fallback is no longer needed.

- [ ] **Step 5: Rewrite low-power layer initialization**

Replace `InitializeLowPowerClockLayerLocked()` with:

```cpp
    void InitializeLowPowerClockLayerLocked() {
        lv_obj_t* screen = lv_screen_active();
        auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        const lv_font_t* clock_font = lvgl_theme != nullptr && lvgl_theme->text_font() != nullptr
            ? lvgl_theme->text_font()->font()
            : nullptr;

        low_power_clock_layer_ = lv_obj_create(screen);
        lv_obj_remove_style_all(low_power_clock_layer_);
        lv_obj_set_size(low_power_clock_layer_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lv_obj_set_style_bg_color(low_power_clock_layer_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(low_power_clock_layer_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(low_power_clock_layer_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(low_power_clock_layer_, LV_OBJ_FLAG_HIDDEN);

        low_power_clock_arc_ = lv_arc_create(low_power_clock_layer_);
        lv_obj_set_size(low_power_clock_arc_, DISPLAY_WIDTH - 14, DISPLAY_HEIGHT - 14);
        lv_obj_center(low_power_clock_arc_);
        lv_arc_set_bg_angles(low_power_clock_arc_, 0, 360);
        lv_arc_set_angles(low_power_clock_arc_, 0, XIAOXIN_LOW_POWER_CLOCK_ARC_SPAN_DEGREES);
        lv_obj_remove_style(low_power_clock_arc_, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(low_power_clock_arc_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(low_power_clock_arc_, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_width(low_power_clock_arc_, 6, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(low_power_clock_arc_, lv_color_hex(0x112B36), LV_PART_MAIN);
        lv_obj_set_style_arc_color(low_power_clock_arc_, lv_color_hex(0x26D9FF), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(low_power_clock_arc_, LV_OPA_50, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(low_power_clock_arc_, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(low_power_clock_arc_, true, LV_PART_INDICATOR);

        low_power_clock_time_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_time_label_, lv_color_hex(0xF6FAFF), 0);
        lv_obj_set_style_text_opa(low_power_clock_time_label_, LV_OPA_COVER, 0);
        if (clock_font != nullptr) {
            lv_obj_set_style_text_font(low_power_clock_time_label_, clock_font, 0);
        }
        lv_obj_set_style_text_letter_space(low_power_clock_time_label_, 0, 0);
        lv_obj_align(low_power_clock_time_label_, LV_ALIGN_CENTER, 0, -10);

        low_power_clock_hint_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_hint_label_, lv_color_hex(0x75AFC0), 0);
        lv_obj_set_style_text_opa(low_power_clock_hint_label_, LV_OPA_70, 0);
        if (clock_font != nullptr) {
            lv_obj_set_style_text_font(low_power_clock_hint_label_, clock_font, 0);
        }
        lv_label_set_text(low_power_clock_hint_label_, "POWER 唤醒");
        lv_obj_align(low_power_clock_hint_label_, LV_ALIGN_BOTTOM_MID, 0, -18);
    }
```

- [ ] **Step 6: Remove icon text updates**

In `RefreshLowPowerClockScreenLocked(bool force)`, remove:

```cpp
        lv_label_set_text(low_power_clock_icon_label_, low_power_clock_icon_text_);
```

Keep:

```cpp
        lv_label_set_text(low_power_clock_time_label_, low_power_clock_snapshot_.time_text);
        lv_label_set_text(low_power_clock_hint_label_, low_power_clock_snapshot_.hint_text);
```

Use the model snapshot as the single source of truth for the hint text:

```cpp
        lv_label_set_text(low_power_clock_hint_label_, low_power_clock_snapshot_.hint_text);
```

- [ ] **Step 7: Add animation refresh helper**

Add this method in `PaopaoPetDisplay` near the low-power clock refresh methods:

```cpp
    void RefreshLowPowerClockAnimationLocked() {
        if (low_power_clock_arc_ == nullptr) {
            return;
        }

        const uint16_t start = xiaoxin_low_power_clock_animation_phase(low_power_clock_animation_tick_++);
        const uint16_t end = (uint16_t)((start + XIAOXIN_LOW_POWER_CLOCK_ARC_SPAN_DEGREES) % 360U);
        lv_arc_set_angles(low_power_clock_arc_, start, end);
    }
```

- [ ] **Step 8: Initialize animation state on show**

In `ShowLowPowerClockScreen()`, after resetting `low_power_clock_last_minute_`, add:

```cpp
        low_power_clock_animation_tick_ = 0;
```

After `RefreshLowPowerClockScreenLocked(true);`, add:

```cpp
        RefreshLowPowerClockAnimationLocked();
```

- [ ] **Step 9: Run the visual path test and verify it passes**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: all tests in this file pass.

- [ ] **Step 10: Commit**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc `
  tests/xiaoxin_low_power_clock_visual_path_test.py
git commit -m "feat: add dynamic low power clock face"
```

---

### Task 3: Drive Animation From the Low-Power Timer

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Modify: `tests/xiaoxin_low_power_clock_visual_path_test.py`

**Interfaces:**
- Consumes:
  - `RefreshLowPowerClockScreenLocked(bool force)`
  - `RefreshLowPowerClockAnimationLocked()`
- Produces:
  - One-second low-power arc animation while the clock is visible.
  - Minute-gated text refresh remains unchanged.

- [ ] **Step 1: Update visual tests for one-second animation cadence**

In `tests/xiaoxin_low_power_clock_visual_path_test.py`, update `test_low_power_clock_refresh_uses_dedicated_timer_with_display_lock` so the timer section asserts:

```python
    assert "esp_timer_start_periodic(low_power_clock_timer_, 1000 * 1000)" in source
```

Add these assertions to the `timer_refresh_section` checks:

```python
    assert "RefreshLowPowerClockAnimationLocked();" in timer_refresh_section
    assert "RefreshLowPowerClockScreenLocked(false);" in timer_refresh_section
```

- [ ] **Step 2: Run the visual path test and verify it fails**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: FAIL because the timer still runs every 10 seconds or does not update the arc.

- [ ] **Step 3: Update the timer callback path**

In `RefreshLowPowerClockScreenFromTimer()`, keep the display lock and visibility guard, then call animation before the minute-gated text refresh:

```cpp
    void RefreshLowPowerClockScreenFromTimer() {
        DisplayLockGuard lock(this);
        if (!low_power_clock_visible_) {
            return;
        }

        RefreshLowPowerClockAnimationLocked();
        RefreshLowPowerClockScreenLocked(false);
    }
```

- [ ] **Step 4: Change the low-power clock timer period**

In `StartLowPowerClockRefreshTimer()`, replace:

```cpp
            ESP_ERROR_CHECK(esp_timer_start_periodic(low_power_clock_timer_, 10 * 1000 * 1000));
```

with:

```cpp
            ESP_ERROR_CHECK(esp_timer_start_periodic(low_power_clock_timer_, 1000 * 1000));
```

- [ ] **Step 5: Run the visual path test and verify it passes**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: all tests in this file pass.

- [ ] **Step 6: Commit**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc `
  tests/xiaoxin_low_power_clock_visual_path_test.py
git commit -m "feat: animate low power clock arc"
```

---

### Task 4: Focused Regression Verification

**Files:**
- No new files.

**Interfaces:**
- Consumes all prior tasks.
- Produces confidence that the dynamic clock did not regress low-power entry, settings, or wake behavior.

- [ ] **Step 1: Run the model test**

Run:

```powershell
gcc tests/xiaoxin_low_power_clock_model_test.c `
  main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c `
  -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 `
  -o build/xiaoxin_low_power_clock_model_test.exe; `
  .\build\xiaoxin_low_power_clock_model_test.exe
```

Expected: command exits with code `0`.

- [ ] **Step 2: Run focused Python tests**

Run:

```powershell
python -m pytest `
  tests/xiaoxin_low_power_clock_visual_path_test.py `
  tests/xiaoxin_settings_path_test.py `
  -q
```

Expected: all selected tests pass.

- [ ] **Step 3: Run a firmware build if the ESP-IDF environment is available**

Run:

```powershell
idf.py build
```

Expected: build succeeds. If `idf.py` is unavailable in this shell, record that verification gap in the final response.

- [ ] **Step 4: Manual device smoke test**

Flash and observe the device:

```powershell
idf.py flash monitor
```

Expected manual behavior:

1. Let the device sit idle until the 60-second power-save timer fires.
2. The screen changes to a black low-power clock page.
3. Large `HH:MM` appears near the center and is the first thing visible.
4. The outer cyan/blue arc moves gently once per second.
5. Bottom hint reads `POWER 唤醒` and does not crowd the time.
6. Backlight is readable at `12%` and restores after wake.
7. POWER short press wakes back to the normal UI.

- [ ] **Step 5: Commit verification-only updates if needed**

If verification required test expectation fixes, run:

```powershell
git add tests main
git commit -m "test: cover dynamic low power clock"
```

If no files changed, skip this commit.

---

## Self-Review

- Spec coverage: The plan covers large center time, black background, cyan/blue outer arc, gentle animation, `12%` brightness, existing wake path, existing overlay reuse, no image assets, and focused tests.
- Placeholder scan: No placeholder steps are present; each code-changing step includes exact snippets or file replacements.
- Type consistency: `xiaoxin_low_power_clock_animation_phase(uint32_t tick)`, `low_power_clock_arc_`, and `low_power_clock_animation_tick_` are introduced before use and used consistently.
