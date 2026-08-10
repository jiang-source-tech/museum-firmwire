# Xiaoxin Snake Screensaver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a GitHub contribution snake style animated background to the Xiaoxin low-power clock screen while preserving the existing time, date, sync status, and POWER wake hint.

**Architecture:** The low-power clock keeps its existing LVGL overlay and one-second timer. A single background `lv_obj_t` draws the contribution grid and snake in an `LV_EVENT_DRAW_MAIN` callback, so the feature adds one LVGL object instead of hundreds of per-cell objects. Path generation stays local to `PaopaoPetDisplay` and uses fixed grid constants, circular clipping, and text-safe exclusion rectangles.

**Tech Stack:** ESP-IDF, C++ board implementation, LVGL 9.4 draw callbacks (`lv_event_get_layer`, `lv_draw_rect`, `lv_draw_rect_dsc_init`), Python path tests via `pytest`.

## Global Constraints

- Do not replace `main/assets/images/idle.gif`.
- Do not add GIF, WebP, bitmap, or large image assets.
- Do not create hundreds of LVGL child objects for grid cells.
- Default implementation must use one self-drawn LVGL background object.
- Keep the low-power clock timer at one-second animation cadence.
- Keep existing low-power mode entry, exit, and POWER wake behavior unchanged.
- Snake must avoid the center time area, date/sync soft-safe area, and bottom hint area.
- Initialize every `lv_draw_rect_dsc_t` with `lv_draw_rect_dsc_init()` before drawing.
- Leave a code comment near the typewriter path noting concentric rings as the visual fallback if typewriter jumps look poor on hardware.

---

## File Structure

- Modify `tests/xiaoxin_low_power_clock_visual_path_test.py`: add source-level guard tests for the snake background object, draw callback API, path constraints, no per-cell objects, and dimmed arc values.
- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`: add snake grid constants, path helpers, draw callback, one background object member, animation invalidation, and dimmed arc styling.
- No model file changes are required; this feature is visual and does not alter `xiaoxin_low_power_clock_model`.

### Task 1: Add Visual Path Test Guardrails

**Files:**
- Modify: `tests/xiaoxin_low_power_clock_visual_path_test.py`

**Interfaces:**
- Consumes: Current source file text from `read_source()`.
- Produces: Tests that later implementation must satisfy using exact symbols:
  - `low_power_clock_snake_bg_`
  - `LowPowerSnakeDrawEvent`
  - `BuildLowPowerSnakePath`
  - `lv_draw_rect_dsc_init`
  - `lv_obj_invalidate(low_power_clock_snake_bg_)`

- [ ] **Step 1: Add failing tests for snake background architecture**

Append these tests to `tests/xiaoxin_low_power_clock_visual_path_test.py`:

```python
def test_low_power_clock_snake_background_uses_single_drawn_object():
    source = read_source()
    assert "lv_obj_t* low_power_clock_snake_bg_ = nullptr;" in source
    assert "low_power_clock_snake_bg_ = lv_obj_create(low_power_clock_layer_);" in source
    assert "lv_obj_add_event_cb(low_power_clock_snake_bg_, LowPowerSnakeDrawEvent, LV_EVENT_DRAW_MAIN, this);" in source
    assert "lv_event_get_layer(e)" in source
    assert "lv_draw_rect(" in source
    assert "lv_draw_rect_dsc_init(" in source
    assert "lv_canvas_create" not in source
    assert "low_power_clock_snake_cells_" not in source


def test_low_power_clock_snake_background_is_created_before_foreground_labels():
    source = read_source()
    init_section = source[
        source.index("void InitializeLowPowerClockLayerLocked()"):
        source.index("void InitializeCardPagerLayer()", source.index("void InitializeLowPowerClockLayerLocked()"))
    ]

    assert init_section.index("low_power_clock_snake_bg_ = lv_obj_create(low_power_clock_layer_);") < init_section.index("low_power_clock_outer_arc_ = lv_arc_create(low_power_clock_layer_);")
    assert init_section.index("low_power_clock_snake_bg_ = lv_obj_create(low_power_clock_layer_);") < init_section.index("low_power_clock_time_label_ = lv_label_create(low_power_clock_layer_);")
```

- [ ] **Step 2: Add failing tests for path constraints and animation invalidation**

Append these tests to the same file:

```python
def test_low_power_clock_snake_path_clips_circle_and_text_safe_areas():
    source = read_source()

    assert "k_low_power_snake_screen_center = 206" in source
    assert "k_low_power_snake_visible_radius = 198" in source
    assert "LowPowerSnakeCellInCircle" in source
    assert "dx * dx + dy * dy <= k_low_power_snake_visible_radius * k_low_power_snake_visible_radius" in source
    assert "LowPowerSnakeCellInSnakeSafeArea" in source
    assert "k_low_power_snake_time_safe_x1 = 82" in source
    assert "k_low_power_snake_time_safe_y2 = 245" in source
    assert "k_low_power_snake_bottom_safe_y = 326" in source
    assert "BuildLowPowerSnakePath" in source
    assert "concentric ring" in source


def test_low_power_clock_snake_animation_invalidates_background_only():
    source = read_source()
    animation_section = source[
        source.index("void RefreshLowPowerClockAnimationLocked()"):
        source.index("void RefreshLowPowerClockScreenFromTimer()")
    ]

    assert "low_power_clock_snake_tick_++" in animation_section
    assert "lv_obj_invalidate(low_power_clock_snake_bg_);" in animation_section
    assert "lv_obj_create(low_power_clock_layer_)" not in animation_section
```

- [ ] **Step 3: Update existing arc visual tests for dimmed colors**

In `test_low_power_clock_uses_large_center_time_and_arc_layout`, replace the old bright inner arc assertion:

```python
assert "lv_obj_set_style_arc_color(low_power_clock_inner_arc_, lv_color_hex(0x26D9FF), LV_PART_INDICATOR);" in source
```

with:

```python
assert "lv_obj_set_style_arc_color(low_power_clock_inner_arc_, lv_color_hex(0x1C6B4A), LV_PART_INDICATOR);" in source
assert "lv_obj_set_style_arc_opa(low_power_clock_inner_arc_, LV_OPA_55, LV_PART_INDICATOR);" in source
```

In `test_low_power_clock_uses_orbit_aod_visual_language`, replace:

```python
assert "lv_obj_set_style_arc_color(low_power_clock_outer_arc_, lv_color_hex(0x102A35), LV_PART_MAIN);" in source
assert "lv_obj_set_style_arc_color(low_power_clock_inner_arc_, lv_color_hex(0x26D9FF), LV_PART_INDICATOR);" in source
```

with:

```python
assert "lv_obj_set_style_arc_color(low_power_clock_outer_arc_, lv_color_hex(0x071015), LV_PART_MAIN);" in source
assert "lv_obj_set_style_arc_opa(low_power_clock_outer_arc_, LV_OPA_35, LV_PART_MAIN);" in source
assert "lv_obj_set_style_arc_color(low_power_clock_inner_arc_, lv_color_hex(0x1C6B4A), LV_PART_INDICATOR);" in source
assert "lv_obj_set_style_arc_opa(low_power_clock_inner_arc_, LV_OPA_55, LV_PART_INDICATOR);" in source
```

- [ ] **Step 4: Run test to verify it fails**

Run:

```powershell
pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: FAIL, with missing `low_power_clock_snake_bg_`, `LowPowerSnakeDrawEvent`, path helpers, and dimmed arc values.

- [ ] **Step 5: Commit failing tests**

```powershell
git add tests/xiaoxin_low_power_clock_visual_path_test.py
git commit -m "test: cover xiaoxin snake screensaver background"
```

### Task 2: Add Snake Grid Constants and Path Helpers

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`

**Interfaces:**
- Consumes: `DISPLAY_WIDTH`, `DISPLAY_HEIGHT`, `PaopaoPetDisplay`.
- Produces:
  - `struct LowPowerSnakeCell { uint8_t col; uint8_t row; };`
  - `BuildLowPowerSnakePath(LowPowerSnakeCell* path, uint16_t max_count) -> uint16_t`
  - `LowPowerSnakeCellInCircle(uint8_t col, uint8_t row) -> bool`
  - `LowPowerSnakeCellInSnakeSafeArea(uint8_t col, uint8_t row) -> bool`

- [ ] **Step 1: Add constants and cell type near low-power clock constants**

Add this block near the other board-level constants, before `class PaopaoPetDisplay`:

```cpp
static constexpr int16_t k_low_power_snake_cell_size = 12;
static constexpr int16_t k_low_power_snake_cell_gap = 6;
static constexpr int16_t k_low_power_snake_pitch =
    k_low_power_snake_cell_size + k_low_power_snake_cell_gap;
static constexpr int16_t k_low_power_snake_origin_x = 17;
static constexpr int16_t k_low_power_snake_origin_y = 17;
static constexpr uint8_t k_low_power_snake_cols = 22;
static constexpr uint8_t k_low_power_snake_rows = 22;
static constexpr uint16_t k_low_power_snake_path_max =
    k_low_power_snake_cols * k_low_power_snake_rows;
static constexpr uint8_t k_low_power_snake_length = 9;
static constexpr int16_t k_low_power_snake_screen_center = 206;
static constexpr int16_t k_low_power_snake_visible_radius = 198;
static constexpr int16_t k_low_power_snake_time_safe_x1 = 82;
static constexpr int16_t k_low_power_snake_time_safe_x2 = 330;
static constexpr int16_t k_low_power_snake_time_safe_y1 = 135;
static constexpr int16_t k_low_power_snake_time_safe_y2 = 245;
static constexpr int16_t k_low_power_snake_soft_safe_x1 = 86;
static constexpr int16_t k_low_power_snake_soft_safe_x2 = 326;
static constexpr int16_t k_low_power_snake_soft_safe_y1 = 246;
static constexpr int16_t k_low_power_snake_soft_safe_y2 = 292;
static constexpr int16_t k_low_power_snake_bottom_safe_y = 326;

struct LowPowerSnakeCell {
    uint8_t col;
    uint8_t row;
};
```

- [ ] **Step 2: Add coordinate and safety helpers**

Add this code below the constants:

```cpp
static int16_t LowPowerSnakeCellX(uint8_t col) {
    return (int16_t)(k_low_power_snake_origin_x + col * k_low_power_snake_pitch);
}

static int16_t LowPowerSnakeCellY(uint8_t row) {
    return (int16_t)(k_low_power_snake_origin_y + row * k_low_power_snake_pitch);
}

static bool LowPowerSnakeRectOverlaps(
    int16_t x,
    int16_t y,
    int16_t x1,
    int16_t y1,
    int16_t x2,
    int16_t y2
) {
    const int16_t cell_x2 = (int16_t)(x + k_low_power_snake_cell_size);
    const int16_t cell_y2 = (int16_t)(y + k_low_power_snake_cell_size);
    return x < x2 && cell_x2 > x1 && y < y2 && cell_y2 > y1;
}

static bool LowPowerSnakeCellInCircle(uint8_t col, uint8_t row) {
    const int16_t cell_cx = (int16_t)(LowPowerSnakeCellX(col) + k_low_power_snake_cell_size / 2);
    const int16_t cell_cy = (int16_t)(LowPowerSnakeCellY(row) + k_low_power_snake_cell_size / 2);
    const int32_t dx = cell_cx - k_low_power_snake_screen_center;
    const int32_t dy = cell_cy - k_low_power_snake_screen_center;
    return dx * dx + dy * dy <=
        k_low_power_snake_visible_radius * k_low_power_snake_visible_radius;
}

static bool LowPowerSnakeCellInSnakeSafeArea(uint8_t col, uint8_t row) {
    const int16_t x = LowPowerSnakeCellX(col);
    const int16_t y = LowPowerSnakeCellY(row);
    if (LowPowerSnakeRectOverlaps(
            x,
            y,
            k_low_power_snake_time_safe_x1,
            k_low_power_snake_time_safe_y1,
            k_low_power_snake_time_safe_x2,
            k_low_power_snake_time_safe_y2
        )) {
        return false;
    }
    if (LowPowerSnakeRectOverlaps(
            x,
            y,
            k_low_power_snake_soft_safe_x1,
            k_low_power_snake_soft_safe_y1,
            k_low_power_snake_soft_safe_x2,
            k_low_power_snake_soft_safe_y2
        )) {
        return false;
    }
    return y < k_low_power_snake_bottom_safe_y;
}
```

- [ ] **Step 3: Add typewriter path builder with visual fallback comment**

Add this code below the safety helpers:

```cpp
static uint16_t BuildLowPowerSnakePath(LowPowerSnakeCell* path, uint16_t max_count) {
    if (path == nullptr || max_count == 0) {
        return 0;
    }

    uint16_t count = 0;
    for (uint8_t row = 0; row < k_low_power_snake_rows; ++row) {
        const bool reverse = (row % 2U) != 0U;
        for (uint8_t i = 0; i < k_low_power_snake_cols; ++i) {
            const uint8_t col = reverse ? (uint8_t)(k_low_power_snake_cols - 1U - i) : i;
            if (!LowPowerSnakeCellInCircle(col, row) ||
                !LowPowerSnakeCellInSnakeSafeArea(col, row)) {
                continue;
            }
            if (count >= max_count) {
                return count;
            }
            path[count++] = {col, row};
        }
    }

    // If typewriter jumps around the center read poorly on hardware, use a concentric ring path.
    return count;
}
```

- [ ] **Step 4: Add member storage to `PaopaoPetDisplay`**

In the private member list near existing low-power clock fields, add:

```cpp
    lv_obj_t* low_power_clock_snake_bg_ = nullptr;
    LowPowerSnakeCell low_power_clock_snake_path_[k_low_power_snake_path_max] = {};
    uint16_t low_power_clock_snake_path_count_ = 0;
    uint32_t low_power_clock_snake_tick_ = 0;
```

- [ ] **Step 5: Run test to verify partial progress**

Run:

```powershell
pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: still FAIL, but failures should move from missing constants/helpers to missing draw callback, object creation, invalidation, and arc dimming.

- [ ] **Step 6: Commit helper implementation**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc
git commit -m "feat: add xiaoxin snake screensaver path helpers"
```

### Task 3: Add LVGL Draw Callback and Animation Invalidation

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`

**Interfaces:**
- Consumes: Path helpers from Task 2.
- Produces:
  - `static void LowPowerSnakeDrawEvent(lv_event_t* e)`
  - `void DrawLowPowerSnakeBackground(lv_event_t* e)`
  - `void InitializeLowPowerSnakeBackgroundLocked()`
  - Existing `RefreshLowPowerClockAnimationLocked()` invalidates the snake background once per tick.

- [ ] **Step 1: Add color helpers**

Inside `PaopaoPetDisplay` private methods, before `BuildLowPowerClockState()`, add:

```cpp
    static lv_color_t LowPowerSnakeBaseColor(uint8_t col, uint8_t row) {
        const uint8_t level = (uint8_t)((col * 17U + row * 31U + 7U) % 5U);
        switch (level) {
            case 0:
                return lv_color_hex(0x0B1A13);
            case 1:
                return lv_color_hex(0x10261B);
            case 2:
                return lv_color_hex(0x143322);
            case 3:
                return lv_color_hex(0x1B4D33);
            default:
                return lv_color_hex(0x10261B);
        }
    }

    static lv_opa_t LowPowerSnakeBaseOpa(uint8_t col, uint8_t row) {
        const uint8_t level = (uint8_t)((col * 17U + row * 31U + 7U) % 5U);
        switch (level) {
            case 0:
                return LV_OPA_50;
            case 1:
                return LV_OPA_55;
            case 2:
                return LV_OPA_60;
            case 3:
                return LV_OPA_70;
            default:
                return LV_OPA_55;
        }
    }
```

- [ ] **Step 2: Add rectangle drawing helper**

Add this method below the color helpers:

```cpp
    static void DrawLowPowerSnakeCell(
        lv_layer_t* layer,
        uint8_t col,
        uint8_t row,
        lv_color_t color,
        lv_opa_t opa,
        int16_t radius
    ) {
        if (layer == nullptr) {
            return;
        }

        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = color;
        dsc.bg_opa = opa;
        dsc.radius = radius;

        const int16_t x = LowPowerSnakeCellX(col);
        const int16_t y = LowPowerSnakeCellY(row);
        const lv_area_t area = {
            .x1 = x,
            .y1 = y,
            .x2 = (lv_coord_t)(x + k_low_power_snake_cell_size - 1),
            .y2 = (lv_coord_t)(y + k_low_power_snake_cell_size - 1),
        };
        lv_draw_rect(layer, &dsc, &area);
    }
```

- [ ] **Step 3: Add background draw method**

Add this method below `DrawLowPowerSnakeCell`:

```cpp
    void DrawLowPowerSnakeBackground(lv_event_t* e) {
        lv_layer_t* layer = lv_event_get_layer(e);
        if (layer == nullptr) {
            return;
        }

        for (uint8_t row = 0; row < k_low_power_snake_rows; ++row) {
            for (uint8_t col = 0; col < k_low_power_snake_cols; ++col) {
                if (!LowPowerSnakeCellInCircle(col, row)) {
                    continue;
                }
                DrawLowPowerSnakeCell(
                    layer,
                    col,
                    row,
                    LowPowerSnakeBaseColor(col, row),
                    LowPowerSnakeBaseOpa(col, row),
                    3
                );
            }
        }

        if (low_power_clock_snake_path_count_ < k_low_power_snake_length + 4U) {
            return;
        }

        const uint16_t head =
            (uint16_t)(low_power_clock_snake_tick_ % low_power_clock_snake_path_count_);
        for (uint8_t i = k_low_power_snake_length; i > 0; --i) {
            const uint8_t body_index = (uint8_t)(i - 1U);
            const uint16_t path_index = (uint16_t)(
                (head + low_power_clock_snake_path_count_ - body_index) %
                low_power_clock_snake_path_count_
            );
            const LowPowerSnakeCell cell = low_power_clock_snake_path_[path_index];
            const bool is_head = body_index == 0;
            const bool bright_body = body_index < 4;
            DrawLowPowerSnakeCell(
                layer,
                cell.col,
                cell.row,
                is_head ? lv_color_hex(0x56D364)
                        : (bright_body ? lv_color_hex(0x2F9E5D) : lv_color_hex(0x24734A)),
                is_head ? LV_OPA_95 : (bright_body ? LV_OPA_85 : LV_OPA_75),
                4
            );
        }
    }
```

- [ ] **Step 4: Add static event trampoline**

Add this static method near other static event methods:

```cpp
    static void LowPowerSnakeDrawEvent(lv_event_t* e) {
        auto* self = e != nullptr ? static_cast<PaopaoPetDisplay*>(lv_event_get_user_data(e)) : nullptr;
        if (self != nullptr) {
            self->DrawLowPowerSnakeBackground(e);
        }
    }
```

- [ ] **Step 5: Add background initialization method**

Add this method before `InitializeLowPowerClockLayerLocked()`:

```cpp
    void InitializeLowPowerSnakeBackgroundLocked() {
        if (low_power_clock_layer_ == nullptr || low_power_clock_snake_bg_ != nullptr) {
            return;
        }

        low_power_clock_snake_path_count_ =
            BuildLowPowerSnakePath(low_power_clock_snake_path_, k_low_power_snake_path_max);
        low_power_clock_snake_tick_ = 0;

        low_power_clock_snake_bg_ = lv_obj_create(low_power_clock_layer_);
        if (low_power_clock_snake_bg_ == nullptr) {
            return;
        }
        lv_obj_remove_style_all(low_power_clock_snake_bg_);
        lv_obj_set_size(low_power_clock_snake_bg_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lv_obj_clear_flag(low_power_clock_snake_bg_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(low_power_clock_snake_bg_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(low_power_clock_snake_bg_, LowPowerSnakeDrawEvent, LV_EVENT_DRAW_MAIN, this);
    }
```

- [ ] **Step 6: Call initialization before arcs and labels**

In `InitializeLowPowerClockLayerLocked()`, after hiding `low_power_clock_layer_` and before `low_power_clock_outer_arc_ = lv_arc_create(...)`, add:

```cpp
        InitializeLowPowerSnakeBackgroundLocked();
```

- [ ] **Step 7: Invalidate background from animation tick**

In `RefreshLowPowerClockAnimationLocked()`, after incrementing `low_power_clock_animation_tick_`, add:

```cpp
        low_power_clock_snake_tick_++;
        if (low_power_clock_snake_bg_ != nullptr) {
            lv_obj_invalidate(low_power_clock_snake_bg_);
        }
```

Keep the existing arc rotation and sync dot opacity behavior.

- [ ] **Step 8: Run tests**

Run:

```powershell
pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: FAIL only on arc dimming tests if Task 4 has not been done yet; otherwise PASS.

- [ ] **Step 9: Commit draw callback integration**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc
git commit -m "feat: draw xiaoxin snake screensaver background"
```

### Task 4: Dim Low-Power Clock Arcs and Verify

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`

**Interfaces:**
- Consumes: Existing `low_power_clock_outer_arc_` and `low_power_clock_inner_arc_`.
- Produces: Dimmed arc colors and opacities from the approved design.

- [ ] **Step 1: Update outer arc style**

In `InitializeLowPowerClockLayerLocked()`, replace the outer arc style block:

```cpp
        lv_obj_set_style_arc_width(low_power_clock_outer_arc_, 3, LV_PART_MAIN);
        lv_obj_set_style_arc_width(low_power_clock_outer_arc_, 3, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(low_power_clock_outer_arc_, lv_color_hex(0x102A35), LV_PART_MAIN);
        lv_obj_set_style_arc_color(low_power_clock_outer_arc_, lv_color_hex(0x163D4A), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(low_power_clock_outer_arc_, LV_OPA_60, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(low_power_clock_outer_arc_, LV_OPA_50, LV_PART_INDICATOR);
```

with:

```cpp
        lv_obj_set_style_arc_width(low_power_clock_outer_arc_, 2, LV_PART_MAIN);
        lv_obj_set_style_arc_width(low_power_clock_outer_arc_, 2, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(low_power_clock_outer_arc_, lv_color_hex(0x071015), LV_PART_MAIN);
        lv_obj_set_style_arc_color(low_power_clock_outer_arc_, lv_color_hex(0x123428), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(low_power_clock_outer_arc_, LV_OPA_35, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(low_power_clock_outer_arc_, LV_OPA_35, LV_PART_INDICATOR);
```

- [ ] **Step 2: Update inner arc style**

Replace the inner arc style block:

```cpp
        lv_obj_set_style_arc_width(low_power_clock_inner_arc_, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_width(low_power_clock_inner_arc_, 7, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(low_power_clock_inner_arc_, lv_color_hex(0x071015), LV_PART_MAIN);
        lv_obj_set_style_arc_color(low_power_clock_inner_arc_, lv_color_hex(0x26D9FF), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(low_power_clock_inner_arc_, LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(low_power_clock_inner_arc_, LV_OPA_COVER, LV_PART_INDICATOR);
```

with:

```cpp
        lv_obj_set_style_arc_width(low_power_clock_inner_arc_, 3, LV_PART_MAIN);
        lv_obj_set_style_arc_width(low_power_clock_inner_arc_, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(low_power_clock_inner_arc_, lv_color_hex(0x050A0C), LV_PART_MAIN);
        lv_obj_set_style_arc_color(low_power_clock_inner_arc_, lv_color_hex(0x1C6B4A), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(low_power_clock_inner_arc_, LV_OPA_20, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(low_power_clock_inner_arc_, LV_OPA_55, LV_PART_INDICATOR);
```

- [ ] **Step 3: Run focused Python tests**

Run:

```powershell
pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: PASS.

- [ ] **Step 4: Run low-power model test**

Run:

```powershell
.\build\xiaoxin_low_power_clock_model_test.exe
```

Expected: command exits with status 0 and no assertion failure.

- [ ] **Step 5: Run firmware build**

Run:

```powershell
idf.py build
```

Expected: build succeeds. If the compiler cannot see LVGL draw declarations transitively, add the minimal LVGL include used by this codebase and rerun the build.

- [ ] **Step 6: Commit final implementation**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc tests/xiaoxin_low_power_clock_visual_path_test.py
git commit -m "feat: add xiaoxin snake low-power screensaver"
```

## Self-Review

Spec coverage:

- Single self-drawn object is covered by Tasks 1 and 3.
- Circular clipping and text-safe exclusions are covered by Tasks 1 and 2.
- Snake path, length, loop behavior, and typewriter fallback comment are covered by Task 2.
- LVGL draw descriptor initialization is covered by Tasks 1 and 3.
- One-second animation invalidation is covered by Tasks 1 and 3.
- Dimmed arcs are covered by Tasks 1 and 4.
- No asset additions and no idle GIF replacement are covered by global constraints.

Placeholder scan:

- The plan avoids unfinished placeholder language.
- The only conditional item is the build include fallback in Task 4 Step 5, tied to a concrete compiler failure mode.

Type consistency:

- `low_power_clock_snake_bg_`, `low_power_clock_snake_tick_`, and `low_power_clock_snake_path_count_` are introduced once and used consistently.
- `LowPowerSnakeDrawEvent`, `DrawLowPowerSnakeBackground`, `InitializeLowPowerSnakeBackgroundLocked`, and `BuildLowPowerSnakePath` names match across tests and implementation tasks.
