# Xiaoxin Random Snake Gradient Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Change the low-power clock snake from a fixed path loop to constrained real-time random movement, and render its body with a head-to-tail gradient.

**Architecture:** Keep the existing single custom-drawn LVGL background object. Replace the static path array with snake body, direction, and PRNG state on `PaopaoPetDisplay`; each animation tick advances the snake before invalidating the background. Draw the existing background grid first, then draw the current snake body using color and opacity interpolation by body index.

**Tech Stack:** C++ in `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`, LVGL draw callbacks, Python source-inspection tests in `tests/xiaoxin_low_power_clock_visual_path_test.py`, pytest.

## Global Constraints

- Do not add GIF, WebP, bitmap, or remote configuration resources.
- Keep `low_power_clock_snake_bg_` as a single custom-drawn LVGL object.
- Do not create per-cell LVGL objects such as `low_power_clock_snake_cells_`.
- Snake cells must stay inside the circular visible area and the existing snake safe area.
- Do not reintroduce `k_low_power_snake_time_safe` constants.
- Preserve the existing low-power clock entry, exit, timer, and foreground label behavior.
- The working tree already has unrelated low-power clock label changes; do not revert them.

---

### Task 1: Lock The Random-Snake Contract In Tests

**Files:**
- Modify: `tests/xiaoxin_low_power_clock_visual_path_test.py`

**Interfaces:**
- Consumes: existing source-inspection helper `read_source()`.
- Produces: tests that require `LowPowerSnakeDirection`, snake body state, random state, movement helpers, and gradient helper names.

- [ ] **Step 1: Write the failing tests**

Replace `test_low_power_clock_snake_path_clips_circle_and_bottom_hint_but_not_time` with:

```python
def test_low_power_clock_snake_moves_with_random_direction_state():
    source = read_source()
    state_section = source[
        source.index("lv_obj_t* low_power_clock_layer_ = nullptr;"):
        source.index("uint8_t settings_item_count_ = 0;")
    ]

    assert "enum class LowPowerSnakeDirection" in source
    assert "LowPowerSnakeCell low_power_clock_snake_body_[k_low_power_snake_length] = {};" in state_section
    assert "LowPowerSnakeDirection low_power_clock_snake_direction_" in state_section
    assert "uint32_t low_power_clock_snake_random_state_" in state_section
    assert "low_power_clock_snake_path_" not in state_section
    assert "low_power_clock_snake_path_count_" not in state_section


def test_low_power_clock_snake_uses_constrained_random_walk_not_fixed_path():
    source = read_source()

    assert "BuildLowPowerSnakePath(" not in source
    assert "AdvanceLowPowerSnakeLocked()" in source
    assert "ResetLowPowerSnakeLocked()" in source
    assert "NextLowPowerSnakeRandomLocked()" in source
    assert "LowPowerSnakeCanMoveTo(" in source
    assert "LowPowerSnakeNextCell(" in source
    assert "LowPowerSnakeCellInCircle(next.col, next.row)" in source
    assert "LowPowerSnakeCellInSnakeSafeArea(next.col, next.row)" in source
    assert "k_low_power_snake_time_safe" not in source


def test_low_power_clock_snake_body_uses_gradient_color():
    source = read_source()

    assert "LowPowerSnakeMixColor(" in source
    assert "LowPowerSnakeBodyColor(" in source
    assert "LowPowerSnakeBodyOpa(" in source
    assert "LowPowerSnakeBodyColor(body_index)" in source
    assert "LowPowerSnakeBodyOpa(body_index)" in source
    assert "is_head ? lv_color_hex(0x56D364)" not in source
    assert "bright_body ? lv_color_hex(0x2F9E5D) : lv_color_hex(0x24734A)" not in source
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q`

Expected: FAIL. The failure should mention missing `LowPowerSnakeDirection`, the removed fixed path contract, or missing gradient helpers.

- [ ] **Step 3: Commit is not allowed yet**

Do not commit after red. Continue to Task 2.

---

### Task 2: Replace Fixed Path With Constrained Random Movement

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Test: `tests/xiaoxin_low_power_clock_visual_path_test.py`

**Interfaces:**
- Consumes: `LowPowerSnakeCell`, `LowPowerSnakeCellInCircle(uint8_t col, uint8_t row)`, `LowPowerSnakeCellInSnakeSafeArea(uint8_t col, uint8_t row)`.
- Produces:
  - `enum class LowPowerSnakeDirection : uint8_t`
  - `LowPowerSnakeCell LowPowerSnakeNextCell(LowPowerSnakeCell cell, LowPowerSnakeDirection direction)`
  - `uint32_t NextLowPowerSnakeRandomLocked()`
  - `void ResetLowPowerSnakeLocked()`
  - `bool LowPowerSnakeCanMoveTo(const LowPowerSnakeCell& next) const`
  - `void AdvanceLowPowerSnakeLocked()`

- [ ] **Step 1: Remove the fixed path builder**

Delete the entire `static uint16_t BuildLowPowerSnakePath(LowPowerSnakeCell* path, uint16_t max_count)` function. Keep `LowPowerSnakeCellInCircle` and `LowPowerSnakeCellInSnakeSafeArea`.

- [ ] **Step 2: Add direction helpers near `LowPowerSnakeCell`**

Add:

```cpp
enum class LowPowerSnakeDirection : uint8_t {
    Right,
    Down,
    Left,
    Up,
};

static LowPowerSnakeDirection LowPowerSnakeTurnLeft(LowPowerSnakeDirection direction) {
    return static_cast<LowPowerSnakeDirection>((static_cast<uint8_t>(direction) + 3U) % 4U);
}

static LowPowerSnakeDirection LowPowerSnakeTurnRight(LowPowerSnakeDirection direction) {
    return static_cast<LowPowerSnakeDirection>((static_cast<uint8_t>(direction) + 1U) % 4U);
}

static LowPowerSnakeCell LowPowerSnakeNextCell(
    LowPowerSnakeCell cell,
    LowPowerSnakeDirection direction
) {
    switch (direction) {
        case LowPowerSnakeDirection::Right:
            cell.col++;
            break;
        case LowPowerSnakeDirection::Down:
            cell.row++;
            break;
        case LowPowerSnakeDirection::Left:
            cell.col--;
            break;
        case LowPowerSnakeDirection::Up:
            cell.row--;
            break;
    }
    return cell;
}
```

- [ ] **Step 3: Replace member state**

Replace:

```cpp
LowPowerSnakeCell low_power_clock_snake_path_[k_low_power_snake_path_max] = {};
uint16_t low_power_clock_snake_path_count_ = 0;
```

with:

```cpp
LowPowerSnakeCell low_power_clock_snake_body_[k_low_power_snake_length] = {};
LowPowerSnakeDirection low_power_clock_snake_direction_ = LowPowerSnakeDirection::Right;
uint32_t low_power_clock_snake_random_state_ = 0xA5A55A5AU;
bool low_power_clock_snake_ready_ = false;
```

- [ ] **Step 4: Add random and movement methods inside `PaopaoPetDisplay` private section**

Add these methods near `DrawLowPowerSnakeBackground`:

```cpp
uint32_t NextLowPowerSnakeRandomLocked() {
    uint32_t state = low_power_clock_snake_random_state_;
    if (state == 0) {
        state = 0xA5A55A5AU ^ low_power_clock_snake_tick_;
    }
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    low_power_clock_snake_random_state_ = state;
    return state;
}

bool LowPowerSnakeBodyContains(const LowPowerSnakeCell& cell, uint8_t ignore_from) const {
    for (uint8_t i = 0; i < ignore_from; ++i) {
        if (low_power_clock_snake_body_[i].col == cell.col &&
            low_power_clock_snake_body_[i].row == cell.row) {
            return true;
        }
    }
    return false;
}

bool LowPowerSnakeCanMoveTo(const LowPowerSnakeCell& next) const {
    if (next.col >= k_low_power_snake_cols || next.row >= k_low_power_snake_rows) {
        return false;
    }
    if (!LowPowerSnakeCellInCircle(next.col, next.row) ||
        !LowPowerSnakeCellInSnakeSafeArea(next.col, next.row)) {
        return false;
    }
    return !LowPowerSnakeBodyContains(next, k_low_power_snake_length - 1U);
}

void ResetLowPowerSnakeLocked() {
    const uint32_t offset = NextLowPowerSnakeRandomLocked() % k_low_power_snake_path_max;
    for (uint16_t attempt = 0; attempt < k_low_power_snake_path_max; ++attempt) {
        const uint16_t index = (uint16_t)((offset + attempt) % k_low_power_snake_path_max);
        const LowPowerSnakeCell head = {
            static_cast<uint8_t>(index % k_low_power_snake_cols),
            static_cast<uint8_t>(index / k_low_power_snake_cols),
        };
        if (!LowPowerSnakeCellInCircle(head.col, head.row) ||
            !LowPowerSnakeCellInSnakeSafeArea(head.col, head.row)) {
            continue;
        }

        low_power_clock_snake_body_[0] = head;
        for (uint8_t i = 1; i < k_low_power_snake_length; ++i) {
            low_power_clock_snake_body_[i] = head;
        }
        low_power_clock_snake_direction_ =
            static_cast<LowPowerSnakeDirection>(NextLowPowerSnakeRandomLocked() % 4U);
        low_power_clock_snake_ready_ = true;
        return;
    }
    low_power_clock_snake_ready_ = false;
}

void AdvanceLowPowerSnakeLocked() {
    if (!low_power_clock_snake_ready_) {
        ResetLowPowerSnakeLocked();
        return;
    }

    LowPowerSnakeDirection candidates[3] = {
        low_power_clock_snake_direction_,
        LowPowerSnakeTurnLeft(low_power_clock_snake_direction_),
        LowPowerSnakeTurnRight(low_power_clock_snake_direction_),
    };
    const uint8_t first = (uint8_t)(NextLowPowerSnakeRandomLocked() % 3U);
    for (uint8_t i = 0; i < 3U; ++i) {
        const LowPowerSnakeDirection direction = candidates[(first + i) % 3U];
        const LowPowerSnakeCell next = LowPowerSnakeNextCell(low_power_clock_snake_body_[0], direction);
        if (!LowPowerSnakeCanMoveTo(next)) {
            continue;
        }
        for (uint8_t body = k_low_power_snake_length - 1U; body > 0; --body) {
            low_power_clock_snake_body_[body] = low_power_clock_snake_body_[body - 1U];
        }
        low_power_clock_snake_body_[0] = next;
        low_power_clock_snake_direction_ = direction;
        return;
    }

    ResetLowPowerSnakeLocked();
}
```

- [ ] **Step 5: Seed and advance from existing lifecycle**

In the low-power clock layer initialization, replace any `BuildLowPowerSnakePath(...)` call with:

```cpp
low_power_clock_snake_random_state_ =
    0xA5A55A5AU ^ low_power_clock_animation_tick_ ^ (uint32_t)esp_timer_get_time();
ResetLowPowerSnakeLocked();
```

In `RefreshLowPowerClockAnimationLocked()`, call:

```cpp
AdvanceLowPowerSnakeLocked();
low_power_clock_snake_tick_++;
```

before invalidating `low_power_clock_snake_bg_`.

- [ ] **Step 6: Update drawing loop to use snake body**

Replace the old `head/path_index` drawing block with:

```cpp
if (!low_power_clock_snake_ready_) {
    return;
}

for (uint8_t i = k_low_power_snake_length; i > 0; --i) {
    const uint8_t body_index = (uint8_t)(i - 1U);
    const LowPowerSnakeCell cell = low_power_clock_snake_body_[body_index];
    DrawLowPowerSnakeCell(
        layer,
        cell.col,
        cell.row,
        LowPowerSnakeBodyColor(body_index),
        LowPowerSnakeBodyOpa(body_index),
        4
    );
}
```

- [ ] **Step 7: Run tests to verify Task 2 partially passes**

Run: `pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q`

Expected: remaining FAIL only for missing gradient helpers if Task 3 is not done yet.

---

### Task 3: Render Snake Body With Gradient Helpers

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Test: `tests/xiaoxin_low_power_clock_visual_path_test.py`

**Interfaces:**
- Consumes: snake body drawing loop from Task 2.
- Produces:
  - `static lv_color_t LowPowerSnakeMixColor(uint32_t from_hex, uint32_t to_hex, uint8_t step, uint8_t max_step)`
  - `static lv_color_t LowPowerSnakeBodyColor(uint8_t body_index)`
  - `static lv_opa_t LowPowerSnakeBodyOpa(uint8_t body_index)`

- [ ] **Step 1: Add gradient helpers**

Add after `LowPowerSnakeBaseOpa`:

```cpp
static uint8_t LowPowerSnakeMixChannel(
    uint8_t from,
    uint8_t to,
    uint8_t step,
    uint8_t max_step
) {
    if (max_step == 0) {
        return from;
    }
    const int16_t delta = (int16_t)to - (int16_t)from;
    return (uint8_t)((int16_t)from + (delta * step) / max_step);
}

static lv_color_t LowPowerSnakeMixColor(
    uint32_t from_hex,
    uint32_t to_hex,
    uint8_t step,
    uint8_t max_step
) {
    const uint8_t r = LowPowerSnakeMixChannel((from_hex >> 16) & 0xffU, (to_hex >> 16) & 0xffU, step, max_step);
    const uint8_t g = LowPowerSnakeMixChannel((from_hex >> 8) & 0xffU, (to_hex >> 8) & 0xffU, step, max_step);
    const uint8_t b = LowPowerSnakeMixChannel(from_hex & 0xffU, to_hex & 0xffU, step, max_step);
    return lv_color_make(r, g, b);
}

static lv_color_t LowPowerSnakeBodyColor(uint8_t body_index) {
    constexpr uint8_t half = k_low_power_snake_length / 2U;
    if (body_index <= half) {
        return LowPowerSnakeMixColor(0x7CFFB2, 0x32D6A0, body_index, half);
    }
    return LowPowerSnakeMixColor(
        0x32D6A0,
        0x1E7EA3,
        (uint8_t)(body_index - half),
        (uint8_t)(k_low_power_snake_length - 1U - half)
    );
}

static lv_opa_t LowPowerSnakeBodyOpa(uint8_t body_index) {
    constexpr uint8_t max_index = k_low_power_snake_length - 1U;
    const uint8_t start = 96U;
    const uint8_t end = 64U;
    const uint8_t percent = (uint8_t)(start - ((start - end) * body_index) / max_index);
    return LowPowerClockOpaPercent(percent);
}
```

- [ ] **Step 2: Run the focused test**

Run: `pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q`

Expected: PASS.

- [ ] **Step 3: Run a compile-oriented verification if available**

Run: `idf.py build`

Expected: build succeeds. If ESP-IDF is unavailable in the shell, record the exact failure and continue with pytest evidence.

- [ ] **Step 4: Commit implementation**

```bash
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc tests/xiaoxin_low_power_clock_visual_path_test.py
git commit -m "feat: randomize low power snake"
```

Do not stage unrelated files.
