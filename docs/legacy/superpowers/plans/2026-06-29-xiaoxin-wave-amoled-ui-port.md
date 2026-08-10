# Xiaoxin Wave AMOLED UI Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the visible WaveStopwatch `175Amoled` UI language onto the Waveshare ESP32-S3 Touch LCD 1.46C standby screen at 412x412 resolution.

**Architecture:** Keep the existing low-power clock model and power-save lifecycle. Replace the current snake-background standby composition with a LVGL-native Wave AMOLED instrument layer: static labels/blocks/arcs plus dynamic pseudo-audio bars driven by the existing 500ms low-power timer. The first implementation ports visuals only; stopwatch behavior, button state cycling, and real audio sampling remain out of scope until the user chooses how those functions should work.

**Tech Stack:** ESP-IDF, LVGL, existing Waveshare board-local `PaopaoPetDisplay`, existing C/Python source-shape tests.

## Global Constraints

- Target board path: `main/boards/waveshare/esp32-s3-touch-lcd-1.46`.
- Target display size: `DISPLAY_WIDTH == 412` and `DISPLAY_HEIGHT == 412`, matching the Waveshare 1.46C / 1.46 family.
- Reference project path: `D:\Learn\WaveStopwatch-main\WaveStopwatch-main\175Amoled`.
- Port visible UI effects first: black background, right-lower large time, dynamic bar graph, static industrial blocks, runtime letter tiles, small labels, and circular instrument accents.
- Do not port stopwatch behavior yet: no start/pause/reset state machine, no elapsed stopwatch counter, no button-mode loop.
- Do not use the standby snake background in this branch.
- Do not import Arduino/TFT_eSPI font assets into the ESP-IDF project.
- Keep existing POWER wake, dim-backlight enter, restore-backlight exit, and 500ms low-power timer boundaries.

---

## File Structure

- Modify `tests/xiaoxin_low_power_clock_visual_path_test.py`: replace snake/background expectations with Wave AMOLED visual contract expectations.
- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`: replace low-power snake state/draw code with Wave AMOLED state, LVGL objects, layout, and animation updates.
- Keep `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c`: it already formats clock/date/sync fields and provides animation phase.
- Keep `tests/xiaoxin_low_power_clock_model_test.c`: no model behavior change is required unless animation cadence changes.

## Task 1: Lock The Wave Visual Contract In Tests

**Files:**
- Modify: `tests/xiaoxin_low_power_clock_visual_path_test.py`
- Test: `tests/xiaoxin_low_power_clock_visual_path_test.py`

**Interfaces:**
- Consumes: existing source-shape helper `read_source()`.
- Produces: failing tests for the Wave AMOLED UI members and absence of snake members.

- [ ] **Step 1: Replace snake-specific tests with Wave AMOLED tests**

In `tests/xiaoxin_low_power_clock_visual_path_test.py`, remove tests whose names start with `test_low_power_clock_snake_` and add these tests:

```python
def test_low_power_clock_uses_wave_amoled_objects_instead_of_snake():
    source = read_source()
    assert "static constexpr uint8_t k_low_power_wave_bar_count = 20;" in source
    assert "lv_obj_t* low_power_wave_bar_layer_ = nullptr;" in source
    assert "lv_obj_t* low_power_wave_bars_[k_low_power_wave_bar_count] = {};" in source
    assert "uint8_t low_power_wave_bar_levels_[k_low_power_wave_bar_count] = {};" in source
    assert "uint32_t low_power_wave_random_state_" in source
    assert "lv_obj_t* low_power_clock_snake_bg_" not in source
    assert "LowPowerSnakeCell" not in source
    assert "StartNewLowPowerSnakeGameLocked();" not in source


def test_low_power_clock_ports_wave_reference_static_effects():
    source = read_source()
    init_section = source[
        source.index("void InitializeLowPowerClockLayerLocked()"):
        source.index("void InitializeCardPagerLayer()", source.index("void InitializeLowPowerClockLayerLocked()"))
    ]

    assert "low_power_wave_bar_layer_ = lv_obj_create(low_power_clock_layer_);" in init_section
    assert "InitializeLowPowerWaveBarsLocked();" in init_section
    assert "InitializeLowPowerWaveReferenceLabelsLocked(hint_font);" in init_section
    assert "InitializeLowPowerWaveReferenceBlocksLocked();" in init_section
    assert 'lv_label_set_text(low_power_clock_brand_label_, "AMOLED");' in init_section
    assert 'lv_label_set_text(low_power_clock_hours_label_, "*HRS*");' in init_section
    assert 'lv_label_set_text(low_power_clock_mode_label_, "runtime");' in init_section
    assert 'lv_label_set_text(low_power_clock_title_label_, "VOLOS TIME");' in init_section
    assert 'lv_label_set_text(low_power_clock_micro_label_, "mil");' in init_section
    assert 'lv_label_set_text(low_power_clock_probe_label_, "CAN YOU READ THIS");' in init_section


def test_low_power_clock_places_time_in_wave_reference_lower_right():
    source = read_source()

    assert "RefreshLowPowerTimeLabelLocked(low_power_clock_time_glow_label_, low_power_clock_snapshot_.time_text, LV_ALIGN_BOTTOM_RIGHT, -38, -96);" in source
    assert "RefreshLowPowerTimeLabelLocked(low_power_clock_time_label_, low_power_clock_snapshot_.time_text, LV_ALIGN_BOTTOM_RIGHT, -38, -96);" in source
    assert "lv_obj_align(low_power_clock_time_label_, LV_ALIGN_CENTER, 0, -10);" not in source


def test_low_power_clock_wave_animation_updates_bars_and_instrument_arcs():
    source = read_source()
    animation_section = source[
        source.index("void RefreshLowPowerClockAnimationLocked()"):
        source.index("void RefreshLowPowerClockScreenFromTimer()")
    ]

    assert "UpdateLowPowerWaveBarsLocked();" in animation_section
    assert "lv_arc_set_rotation(low_power_clock_inner_arc_, start);" in animation_section
    assert "lv_arc_set_rotation(low_power_clock_outer_arc_, (start + 180) % 360);" in animation_section
    assert "AdvanceLowPowerSnakeLocked();" not in animation_section
    assert "lv_obj_invalidate(low_power_clock_snake_bg_);" not in animation_section
```

- [ ] **Step 2: Run the focused visual test and verify RED**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: FAIL because `low_power_wave_bar_layer_`, Wave labels, lower-right time alignment, and Wave animation calls do not exist yet.

- [ ] **Step 3: Commit the failing visual contract only if implementation will be delegated**

If implementing inline in the same session, do not commit the failing tests separately. Continue to Task 2 and commit after green.

## Task 2: Replace Snake State With Wave AMOLED State

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Test: `tests/xiaoxin_low_power_clock_visual_path_test.py`

**Interfaces:**
- Consumes: existing `low_power_clock_layer_`, `low_power_clock_outer_arc_`, `low_power_clock_inner_arc_`, clock labels, and timer.
- Produces: Wave state names expected by Task 1 tests.

- [ ] **Step 1: Remove standby snake members and add Wave members**

In the `PaopaoPetDisplay` private member block, replace the low-power snake members with:

```cpp
    lv_obj_t* low_power_wave_bar_layer_ = nullptr;
    lv_obj_t* low_power_wave_bars_[k_low_power_wave_bar_count] = {};
    lv_obj_t* low_power_clock_brand_label_ = nullptr;
    lv_obj_t* low_power_clock_hours_label_ = nullptr;
    lv_obj_t* low_power_clock_mode_label_ = nullptr;
    lv_obj_t* low_power_clock_title_label_ = nullptr;
    lv_obj_t* low_power_clock_micro_panel_ = nullptr;
    lv_obj_t* low_power_clock_micro_label_ = nullptr;
    lv_obj_t* low_power_clock_probe_label_ = nullptr;
    lv_obj_t* low_power_clock_left_red_dash_ = nullptr;
    lv_obj_t* low_power_clock_left_gray_panel_ = nullptr;
    lv_obj_t* low_power_clock_center_rule_ = nullptr;
    lv_obj_t* low_power_clock_center_stem_ = nullptr;
    lv_obj_t* low_power_clock_blue_top_block_ = nullptr;
    lv_obj_t* low_power_clock_blue_bottom_block_ = nullptr;
    lv_obj_t* low_power_clock_top_dial_ = nullptr;
    uint8_t low_power_wave_bar_levels_[k_low_power_wave_bar_count] = {};
    uint32_t low_power_wave_random_state_ = 0xA5A55A5AU;
```

- [ ] **Step 2: Remove snake helper code**

Delete helpers whose names start with `LowPowerSnake`, plus:

```cpp
DrawLowPowerSnakeCell(...)
NextLowPowerSnakeRandomLocked()
GenerateLowPowerSnakeFruitLocked()
ResetLowPowerSnakeLocked(...)
StartNewLowPowerSnakeGameLocked()
HandleLowPowerSnakeFruitLocked(...)
AdvanceLowPowerSnakeLocked()
DrawLowPowerSnakeFruitLocked(...)
DrawLowPowerSnakeBackground(...)
LowPowerSnakeDrawEvent(...)
```

- [ ] **Step 3: Add Wave constants near existing low-power constants**

Add:

```cpp
static constexpr uint8_t k_low_power_wave_bar_count = 20;
static constexpr int16_t k_low_power_wave_bar_w = 4;
static constexpr int16_t k_low_power_wave_bar_gap = 6;
static constexpr int16_t k_low_power_wave_bar_unit_h = 4;
static constexpr uint8_t k_low_power_wave_bar_min_level = 1;
static constexpr uint8_t k_low_power_wave_bar_max_level = 12;
```

- [ ] **Step 4: Run the focused visual test and verify expected failures remain**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: still FAIL, now mostly because creation/layout/update helpers are not implemented yet.

## Task 3: Build The 412x412 Wave Standby Layout

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Test: `tests/xiaoxin_low_power_clock_visual_path_test.py`

**Interfaces:**
- Consumes: Wave members from Task 2.
- Produces: `InitializeLowPowerWaveBarsLocked`, `InitializeLowPowerWaveReferenceLabelsLocked`, `InitializeLowPowerWaveReferenceBlocksLocked`, and lower-right time alignment.

- [ ] **Step 1: Extend time label alignment helper**

Change `RefreshLowPowerTimeLabelLocked` signature to:

```cpp
    static void RefreshLowPowerTimeLabelLocked(
        lv_obj_t* label,
        const char* text,
        lv_align_t align,
        int16_t x_offset,
        int16_t y_offset
    )
```

Inside the helper, replace the final align call with:

```cpp
        lv_obj_align(label, align, x_offset, y_offset);
```

- [ ] **Step 2: Update refresh call sites for right-lower Wave time**

In `RefreshLowPowerClockScreenLocked`, set:

```cpp
        RefreshLowPowerTimeLabelLocked(low_power_clock_time_glow_label_, low_power_clock_snapshot_.time_text, LV_ALIGN_BOTTOM_RIGHT, -38, -96);
        RefreshLowPowerTimeLabelLocked(low_power_clock_time_label_, low_power_clock_snapshot_.time_text, LV_ALIGN_BOTTOM_RIGHT, -38, -96);
```

- [ ] **Step 3: Create Wave bar objects**

Add:

```cpp
    void InitializeLowPowerWaveBarsLocked() {
        if (low_power_clock_layer_ == nullptr || low_power_wave_bar_layer_ != nullptr) {
            return;
        }
        low_power_wave_bar_layer_ = lv_obj_create(low_power_clock_layer_);
        lv_obj_remove_style_all(low_power_wave_bar_layer_);
        lv_obj_set_size(low_power_wave_bar_layer_, 142, 64);
        lv_obj_align(low_power_wave_bar_layer_, LV_ALIGN_TOP_MID, 22, 84);
        lv_obj_clear_flag(low_power_wave_bar_layer_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(low_power_wave_bar_layer_, LV_OBJ_FLAG_CLICKABLE);

        for (uint8_t i = 0; i < k_low_power_wave_bar_count; ++i) {
            low_power_wave_bar_levels_[i] = (uint8_t)(1U + (i * 5U) % k_low_power_wave_bar_max_level);
            low_power_wave_bars_[i] = lv_obj_create(low_power_wave_bar_layer_);
            lv_obj_remove_style_all(low_power_wave_bars_[i]);
            lv_obj_set_style_bg_color(low_power_wave_bars_[i], lv_color_hex(0x8B949E), 0);
            lv_obj_set_style_bg_opa(low_power_wave_bars_[i], LowPowerClockOpaPercent(52), 0);
            lv_obj_set_style_radius(low_power_wave_bars_[i], 1, 0);
        }
        UpdateLowPowerWaveBarsLocked();
    }
```

- [ ] **Step 4: Create Wave labels**

Add:

```cpp
    void InitializeLowPowerWaveReferenceLabelsLocked(const lv_font_t* hint_font) {
        low_power_clock_brand_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_brand_label_, lv_color_hex(0x7C8794), 0);
        lv_obj_set_style_text_opa(low_power_clock_brand_label_, LV_OPA_70, 0);
        if (hint_font != nullptr) {
            lv_obj_set_style_text_font(low_power_clock_brand_label_, hint_font, 0);
        }
        lv_label_set_text(low_power_clock_brand_label_, "AMOLED");
        lv_obj_align(low_power_clock_brand_label_, LV_ALIGN_TOP_RIGHT, -54, 122);

        low_power_clock_hours_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_hours_label_, lv_color_hex(0x7C8794), 0);
        lv_obj_set_style_text_opa(low_power_clock_hours_label_, LV_OPA_60, 0);
        if (hint_font != nullptr) {
            lv_obj_set_style_text_font(low_power_clock_hours_label_, hint_font, 0);
        }
        lv_label_set_text(low_power_clock_hours_label_, "*HRS*");
        lv_obj_align(low_power_clock_hours_label_, LV_ALIGN_BOTTOM_MID, -28, -54);

        low_power_clock_mode_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_mode_label_, lv_color_hex(0xC9D1D9), 0);
        lv_obj_set_style_text_opa(low_power_clock_mode_label_, LV_OPA_70, 0);
        if (hint_font != nullptr) {
            lv_obj_set_style_text_font(low_power_clock_mode_label_, hint_font, 0);
        }
        lv_label_set_text(low_power_clock_mode_label_, "runtime");
        lv_obj_align(low_power_clock_mode_label_, LV_ALIGN_TOP_MID, 42, 20);

        low_power_clock_title_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_title_label_, lv_color_hex(0xB8C7D1), 0);
        lv_obj_set_style_text_opa(low_power_clock_title_label_, LV_OPA_75, 0);
        if (hint_font != nullptr) {
            lv_obj_set_style_text_font(low_power_clock_title_label_, hint_font, 0);
        }
        lv_label_set_text(low_power_clock_title_label_, "VOLOS TIME");
        lv_obj_align(low_power_clock_title_label_, LV_ALIGN_CENTER, 54, -28);

        low_power_clock_probe_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_probe_label_, lv_color_hex(0x66707A), 0);
        lv_obj_set_style_text_opa(low_power_clock_probe_label_, LV_OPA_50, 0);
        if (hint_font != nullptr) {
            lv_obj_set_style_text_font(low_power_clock_probe_label_, hint_font, 0);
        }
        lv_label_set_text(low_power_clock_probe_label_, "CAN YOU READ THIS");
        lv_obj_align(low_power_clock_probe_label_, LV_ALIGN_RIGHT_MID, -30, -44);
    }
```

- [ ] **Step 5: Create Wave blocks and micro panel**

Add:

```cpp
    static lv_obj_t* CreateLowPowerBlock(lv_obj_t* parent, int16_t w, int16_t h, uint32_t color_hex, lv_opa_t opa) {
        lv_obj_t* obj = lv_obj_create(parent);
        lv_obj_remove_style_all(obj);
        lv_obj_set_size(obj, w, h);
        lv_obj_set_style_bg_color(obj, lv_color_hex(color_hex), 0);
        lv_obj_set_style_bg_opa(obj, opa, 0);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        return obj;
    }

    void InitializeLowPowerWaveReferenceBlocksLocked() {
        low_power_clock_left_red_dash_ = CreateLowPowerBlock(low_power_clock_layer_, 22, 4, 0xD04141, LV_OPA_COVER);
        lv_obj_align(low_power_clock_left_red_dash_, LV_ALIGN_LEFT_MID, 48, -10);

        low_power_clock_left_gray_panel_ = CreateLowPowerBlock(low_power_clock_layer_, 46, 28, 0x30363D, LowPowerClockOpaPercent(82));
        lv_obj_align(low_power_clock_left_gray_panel_, LV_ALIGN_LEFT_MID, 0, 28);

        low_power_clock_micro_label_ = lv_label_create(low_power_clock_left_gray_panel_);
        lv_obj_set_style_text_color(low_power_clock_micro_label_, lv_color_hex(0xC9D1D9), 0);
        lv_obj_set_style_text_opa(low_power_clock_micro_label_, LV_OPA_70, 0);
        lv_label_set_text(low_power_clock_micro_label_, "mil");
        lv_obj_center(low_power_clock_micro_label_);

        low_power_clock_center_rule_ = CreateLowPowerBlock(low_power_clock_layer_, 106, 3, 0x57606A, LowPowerClockOpaPercent(62));
        lv_obj_align(low_power_clock_center_rule_, LV_ALIGN_CENTER, 54, 26);

        low_power_clock_center_stem_ = CreateLowPowerBlock(low_power_clock_layer_, 3, 32, 0x57606A, LowPowerClockOpaPercent(62));
        lv_obj_align(low_power_clock_center_stem_, LV_ALIGN_CENTER, -10, 36);

        low_power_clock_blue_top_block_ = CreateLowPowerBlock(low_power_clock_layer_, 36, 120, 0x01052A, LowPowerClockOpaPercent(88));
        lv_obj_align(low_power_clock_blue_top_block_, LV_ALIGN_TOP_MID, -66, 0);

        low_power_clock_blue_bottom_block_ = CreateLowPowerBlock(low_power_clock_layer_, 36, 14, 0x01052A, LowPowerClockOpaPercent(88));
        lv_obj_align(low_power_clock_blue_bottom_block_, LV_ALIGN_BOTTOM_MID, -66, -86);

        low_power_clock_top_dial_ = lv_arc_create(low_power_clock_layer_);
        lv_obj_set_size(low_power_clock_top_dial_, 56, 56);
        lv_obj_align(low_power_clock_top_dial_, LV_ALIGN_TOP_RIGHT, -36, 48);
        lv_arc_set_bg_angles(low_power_clock_top_dial_, 0, 360);
        lv_arc_set_angles(low_power_clock_top_dial_, 0, 96);
        lv_obj_remove_style(low_power_clock_top_dial_, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(low_power_clock_top_dial_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(low_power_clock_top_dial_, 8, LV_PART_MAIN);
        lv_obj_set_style_arc_width(low_power_clock_top_dial_, 6, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(low_power_clock_top_dial_, lv_color_hex(0x01052A), LV_PART_MAIN);
        lv_obj_set_style_arc_color(low_power_clock_top_dial_, lv_color_hex(0x1B4965), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(low_power_clock_top_dial_, LowPowerClockOpaPercent(86), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(low_power_clock_top_dial_, LowPowerClockOpaPercent(80), LV_PART_INDICATOR);
    }
```

- [ ] **Step 6: Wire new helpers in `InitializeLowPowerClockLayerLocked`**

Replace the snake initialization call with:

```cpp
        InitializeLowPowerWaveReferenceBlocksLocked();
        InitializeLowPowerWaveBarsLocked();
```

After hint label creation, call:

```cpp
        InitializeLowPowerWaveReferenceLabelsLocked(hint_font);
```

- [ ] **Step 7: Run focused visual test and continue to expected animation failures**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: FAIL only on missing `UpdateLowPowerWaveBarsLocked` and animation wiring if earlier steps are correct.

## Task 4: Animate Bars And Instrument Arcs

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Test: `tests/xiaoxin_low_power_clock_visual_path_test.py`

**Interfaces:**
- Consumes: `low_power_wave_bars_`, `low_power_wave_bar_levels_`, `low_power_wave_random_state_`.
- Produces: `UpdateLowPowerWaveBarsLocked()`.

- [ ] **Step 1: Add deterministic Wave random helper**

Add:

```cpp
    uint32_t NextLowPowerWaveRandomLocked() {
        uint32_t state = low_power_wave_random_state_;
        if (state == 0) {
            state = 0xA5A55A5AU ^ low_power_clock_animation_tick_;
        }
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        low_power_wave_random_state_ = state;
        return state;
    }
```

- [ ] **Step 2: Add bar update helper**

Add:

```cpp
    void UpdateLowPowerWaveBarsLocked() {
        if (low_power_wave_bar_layer_ == nullptr) {
            return;
        }
        for (uint8_t i = 0; i < k_low_power_wave_bar_count; ++i) {
            if (low_power_wave_bars_[i] == nullptr) {
                continue;
            }
            const uint8_t level = (uint8_t)(
                k_low_power_wave_bar_min_level +
                (NextLowPowerWaveRandomLocked() % k_low_power_wave_bar_max_level)
            );
            low_power_wave_bar_levels_[i] = level;
            const int16_t h = (int16_t)(level * k_low_power_wave_bar_unit_h);
            const int16_t x = (int16_t)(i * k_low_power_wave_bar_gap);
            const int16_t y = (int16_t)(56 - h);
            lv_obj_set_size(low_power_wave_bars_[i], k_low_power_wave_bar_w, h);
            lv_obj_set_pos(low_power_wave_bars_[i], x, y);
        }
    }
```

- [ ] **Step 3: Seed Wave random state when entering standby**

In `ShowLowPowerClockScreen`, replace snake reset lines with:

```cpp
        low_power_wave_random_state_ =
            0xA5A55A5AU ^ low_power_clock_animation_tick_ ^ (uint32_t)esp_timer_get_time();
```

- [ ] **Step 4: Update animation function**

In `RefreshLowPowerClockAnimationLocked`, remove snake movement/invalidation and add:

```cpp
        UpdateLowPowerWaveBarsLocked();
        if (low_power_clock_top_dial_ != nullptr) {
            lv_arc_set_rotation(low_power_clock_top_dial_, (start + 42) % 360);
        }
```

Keep:

```cpp
        lv_arc_set_rotation(low_power_clock_inner_arc_, start);
        lv_arc_set_rotation(low_power_clock_outer_arc_, (start + 180) % 360);
```

- [ ] **Step 5: Run focused visual test and verify GREEN**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: PASS.

## Task 5: Verify, Clean Up, And Commit

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Modify: `tests/xiaoxin_low_power_clock_visual_path_test.py`
- Test: focused tests listed below

**Interfaces:**
- Consumes: all previous tasks.
- Produces: committed implementation.

- [ ] **Step 1: Run model test**

Run:

```powershell
if (!(Test-Path build)) { New-Item -ItemType Directory build | Out-Null }
gcc tests/xiaoxin_low_power_clock_model_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.c -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 -o build/xiaoxin_low_power_clock_model_test.exe
.\build\xiaoxin_low_power_clock_model_test.exe
```

Expected: exit code 0.

- [ ] **Step 2: Run visual path tests**

Run:

```powershell
python -m pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: PASS.

- [ ] **Step 3: Run nearby regression tests**

Run:

```powershell
python -m pytest tests/xiaoxin_power_latch_path_test.py tests/xiaoxin_power_control_test.c tests/xiaoxin_settings_path_test.py -q
```

Expected: Python tests pass; if `xiaoxin_power_control_test.c` is not a pytest test in this environment, run the existing project command used for C tests or report the limitation.

- [ ] **Step 4: Inspect diff**

Run:

```powershell
git diff --check
git diff --stat
git diff -- main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc tests/xiaoxin_low_power_clock_visual_path_test.py
```

Expected: no whitespace errors; diff only touches standby Wave UI and matching tests.

- [ ] **Step 5: Commit**

Run:

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc tests/xiaoxin_low_power_clock_visual_path_test.py
git commit -m "feat: port wave amoled standby visuals"
```

Expected: commit succeeds.
