# Xiaoxin Snake Fruit Growth Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add red fruit, fruit-seeking movement, eight-fruit growth, and a capped long-lived snake to the low-power standby snake screensaver.

**Architecture:** Keep the existing single LVGL custom-drawn background object and extend its local snake state. The snake body buffer grows from a fixed initial-length array to a max-length array, while movement, drawing, collision, and reset logic use `low_power_clock_snake_length_`.

**Tech Stack:** ESP-IDF C++, LVGL draw callbacks, existing `pytest` source-level tests.

## Global Constraints

- Red fruit must appear only inside the circular visible grid and must not overlap the snake body.
- Snake grows by 1 cell after eating 8 fruits.
- Initial length is 9 cells.
- Maximum length is 24 cells.
- At maximum length, fruit eating continues but length does not grow and no automatic new game starts.
- Leaving standby and entering standby again starts a new game with initial length and zero fruit count.
- Runtime trap recovery may reset snake position and direction, but preserves current length unless full-body generation is impossible.
- Do not add touch control, score UI, win/fail UI, remote configuration, image assets, GIF assets, or font assets.
- Preserve `low_power_clock_snake_bg_` as the single LVGL snake background object.

---

## File Structure

- Modify `tests/xiaoxin_low_power_clock_visual_path_test.py`: extend the existing source-level standby snake tests with assertions for fruit state, growth constants, capped length, fruit generation, fruit-seeking movement, and new-game reset on standby entry.
- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`: add constants, state fields, fruit generation/drawing helpers, current-length movement, growth handling, and standby-entry reset.

No new source files are needed because the existing display implementation already keeps this screensaver state local to `PaopaoPetDisplay`.

### Task 1: Lock Fruit And Growth Behavior With Tests

**Files:**
- Modify: `tests/xiaoxin_low_power_clock_visual_path_test.py`
- Test: `tests/xiaoxin_low_power_clock_visual_path_test.py`

**Interfaces:**
- Consumes: existing `read_source()` helper and existing low-power snake tests.
- Produces: source-level assertions that require the implementation names used in Task 2:
  `k_low_power_snake_initial_length`, `k_low_power_snake_max_length`,
  `k_low_power_snake_fruits_per_growth`, `low_power_clock_snake_length_`,
  `low_power_clock_snake_fruit_`, `low_power_clock_snake_fruit_ready_`,
  `low_power_clock_snake_fruit_count_`, `StartNewLowPowerSnakeGameLocked()`,
  `GenerateLowPowerSnakeFruitLocked()`, `LowPowerSnakeDistanceToFruit()`,
  `HandleLowPowerSnakeFruitLocked()`, and `DrawLowPowerSnakeFruitLocked()`.

- [ ] **Step 1: Write the failing tests**

Append these tests after `test_low_power_clock_snake_moves_with_random_direction_state()` and before `test_low_power_clock_snake_uses_constrained_random_walk_not_fixed_path()`:

```python
def test_low_power_clock_snake_has_fruit_growth_and_capped_length_state():
    source = read_source()
    state_section = source[
        source.index("lv_obj_t* low_power_clock_layer_ = nullptr;"):
        source.index("uint8_t settings_item_count_ = 0;")
    ]

    assert "static constexpr uint8_t k_low_power_snake_initial_length = 9;" in source
    assert "static constexpr uint8_t k_low_power_snake_max_length = 24;" in source
    assert "static constexpr uint8_t k_low_power_snake_fruits_per_growth = 8;" in source
    assert "LowPowerSnakeCell low_power_clock_snake_body_[k_low_power_snake_max_length] = {};" in state_section
    assert "uint8_t low_power_clock_snake_length_ = k_low_power_snake_initial_length;" in state_section
    assert "LowPowerSnakeCell low_power_clock_snake_fruit_" in state_section
    assert "bool low_power_clock_snake_fruit_ready_ = false;" in state_section
    assert "uint8_t low_power_clock_snake_fruit_count_ = 0;" in state_section


def test_low_power_clock_snake_starts_new_game_when_standby_enters():
    source = read_source()
    show_section = source[
        source.index("void ShowLowPowerClockScreen()"):
        source.index("void HideLowPowerClockScreen()")
    ]
    new_game_section = source[
        source.index("void StartNewLowPowerSnakeGameLocked()"):
        source.index("void AdvanceLowPowerSnakeLocked()")
    ]

    assert "StartNewLowPowerSnakeGameLocked();" in show_section
    assert "low_power_clock_snake_length_ = k_low_power_snake_initial_length;" in new_game_section
    assert "low_power_clock_snake_fruit_count_ = 0;" in new_game_section
    assert "low_power_clock_snake_fruit_ready_ = false;" in new_game_section
    assert "ResetLowPowerSnakeLocked(k_low_power_snake_initial_length);" in new_game_section
    assert "GenerateLowPowerSnakeFruitLocked();" in new_game_section


def test_low_power_clock_snake_generates_safe_red_fruit():
    source = read_source()
    fruit_section = source[
        source.index("bool GenerateLowPowerSnakeFruitLocked()"):
        source.index("static uint16_t LowPowerSnakeDistanceToFruit(")
    ]
    draw_section = source[
        source.index("void DrawLowPowerSnakeBackground(lv_event_t* e)"):
        source.index("xiaoxin_low_power_clock_state_t BuildLowPowerClockState()")
    ]

    assert "LowPowerSnakeCellInCircle(candidate.col, candidate.row)" in fruit_section
    assert "LowPowerSnakeBodyContains(candidate, low_power_clock_snake_length_)" in fruit_section
    assert "low_power_clock_snake_fruit_ = candidate;" in fruit_section
    assert "low_power_clock_snake_fruit_ready_ = true;" in fruit_section
    assert "low_power_clock_snake_fruit_ready_ = false;" in fruit_section
    assert "DrawLowPowerSnakeFruitLocked(layer);" in draw_section
    assert "lv_color_hex(0xFF4D4D)" in source


def test_low_power_clock_snake_moves_toward_fruit_and_grows_only_until_cap():
    source = read_source()
    advance_section = source[
        source.index("void AdvanceLowPowerSnakeLocked()"):
        source.index("void DrawLowPowerSnakeBackground(lv_event_t* e)")
    ]
    fruit_section = source[
        source.index("void HandleLowPowerSnakeFruitLocked("):
        source.index("void DrawLowPowerSnakeBackground(lv_event_t* e)")
    ]

    assert "LowPowerSnakeDistanceToFruit(next)" in advance_section
    assert "fruit_distance" in advance_section
    assert "HandleLowPowerSnakeFruitLocked(next);" in advance_section
    assert "low_power_clock_snake_fruit_count_++;" in fruit_section
    assert "low_power_clock_snake_fruit_count_ >= k_low_power_snake_fruits_per_growth" in fruit_section
    assert "low_power_clock_snake_length_ < k_low_power_snake_max_length" in fruit_section
    assert "low_power_clock_snake_length_++;" in fruit_section
    assert "low_power_clock_snake_fruit_count_ -= k_low_power_snake_fruits_per_growth;" in fruit_section
    assert "low_power_clock_snake_fruit_count_ = k_low_power_snake_fruits_per_growth;" in fruit_section
    assert "GenerateLowPowerSnakeFruitLocked();" in fruit_section
    assert "StartNewLowPowerSnakeGameLocked();" not in fruit_section
```

Update the existing `test_low_power_clock_snake_moves_with_random_direction_state()` assertion from:

```python
assert "LowPowerSnakeCell low_power_clock_snake_body_[k_low_power_snake_length] = {};" in state_section
```

to:

```python
assert "LowPowerSnakeCell low_power_clock_snake_body_[k_low_power_snake_max_length] = {};" in state_section
assert "uint8_t low_power_clock_snake_length_ = k_low_power_snake_initial_length;" in state_section
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```powershell
pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: FAIL with missing strings such as `k_low_power_snake_max_length`, `GenerateLowPowerSnakeFruitLocked()`, or `StartNewLowPowerSnakeGameLocked()`.

- [ ] **Step 3: Commit the failing tests**

```powershell
git add tests/xiaoxin_low_power_clock_visual_path_test.py
git commit -m "test: specify snake fruit growth screensaver"
```

### Task 2: Implement Fruit, Growth, And Capped Length

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Test: `tests/xiaoxin_low_power_clock_visual_path_test.py`

**Interfaces:**
- Consumes: test names and required implementation symbols from Task 1.
- Produces:
  - `StartNewLowPowerSnakeGameLocked()` resets the standby snake lifecycle.
  - `GenerateLowPowerSnakeFruitLocked()` creates safe fruit positions.
  - `HandleLowPowerSnakeFruitLocked(const LowPowerSnakeCell& next)` handles eating, capped growth, and fruit refresh.
  - `DrawLowPowerSnakeFruitLocked(lv_layer_t* layer)` draws the red fruit.
  - Movement, drawing, body containment, and reachability use `low_power_clock_snake_length_`.

- [ ] **Step 1: Replace fixed-length constants with initial/max/growth constants**

In the constants near `k_low_power_snake_path_max`, replace:

```cpp
static constexpr uint8_t k_low_power_snake_length = 9;
static constexpr int16_t k_low_power_snake_screen_center = 206;
```

with:

```cpp
static constexpr uint8_t k_low_power_snake_initial_length = 9;
static constexpr uint8_t k_low_power_snake_max_length = 24;
static constexpr uint8_t k_low_power_snake_fruits_per_growth = 8;
static constexpr int16_t k_low_power_snake_screen_center = 206;
```

Replace:

```cpp
static constexpr uint16_t k_low_power_snake_safe_space_min = k_low_power_snake_length + 4U;
```

with:

```cpp
static constexpr uint16_t k_low_power_snake_safe_space_min = k_low_power_snake_initial_length + 4U;
```

- [ ] **Step 2: Update state fields**

Replace:

```cpp
LowPowerSnakeCell low_power_clock_snake_body_[k_low_power_snake_length] = {};
LowPowerSnakeDirection low_power_clock_snake_direction_ = LowPowerSnakeDirection::Right;
uint32_t low_power_clock_snake_random_state_ = 0xA5A55A5AU;
bool low_power_clock_snake_ready_ = false;
```

with:

```cpp
LowPowerSnakeCell low_power_clock_snake_body_[k_low_power_snake_max_length] = {};
uint8_t low_power_clock_snake_length_ = k_low_power_snake_initial_length;
LowPowerSnakeCell low_power_clock_snake_fruit_ = {};
bool low_power_clock_snake_fruit_ready_ = false;
uint8_t low_power_clock_snake_fruit_count_ = 0;
LowPowerSnakeDirection low_power_clock_snake_direction_ = LowPowerSnakeDirection::Right;
uint32_t low_power_clock_snake_random_state_ = 0xA5A55A5AU;
bool low_power_clock_snake_ready_ = false;
```

- [ ] **Step 3: Make color and opacity use current length**

Change the signatures:

```cpp
static lv_color_t LowPowerSnakeBodyColor(uint8_t body_index)
static lv_opa_t LowPowerSnakeBodyOpa(uint8_t body_index)
```

to:

```cpp
static lv_color_t LowPowerSnakeBodyColor(uint8_t body_index, uint8_t snake_length)
static lv_opa_t LowPowerSnakeBodyOpa(uint8_t body_index, uint8_t snake_length)
```

Use this implementation shape:

```cpp
static lv_color_t LowPowerSnakeBodyColor(uint8_t body_index, uint8_t snake_length) {
    const uint8_t max_index = snake_length > 1U ? (uint8_t)(snake_length - 1U) : 1U;
    const uint8_t half = max_index / 2U;
    if (body_index <= half) {
        return LowPowerSnakeMixColor(0x7CFFB2, 0x32D6A0, body_index, half == 0U ? 1U : half);
    }
    return LowPowerSnakeMixColor(
        0x32D6A0,
        0x1E7EA3,
        (uint8_t)(body_index - half),
        (uint8_t)(max_index - half)
    );
}

static lv_opa_t LowPowerSnakeBodyOpa(uint8_t body_index, uint8_t snake_length) {
    const uint8_t max_index = snake_length > 1U ? (uint8_t)(snake_length - 1U) : 1U;
    const uint8_t start = 96U;
    const uint8_t end = 64U;
    const uint8_t percent = (uint8_t)(start - ((start - end) * body_index) / max_index);
    return LowPowerClockOpaPercent(percent);
}
```

Update draw calls to:

```cpp
LowPowerSnakeBodyColor(body_index, low_power_clock_snake_length_)
LowPowerSnakeBodyOpa(body_index, low_power_clock_snake_length_)
```

- [ ] **Step 4: Make body collision and movement use current length**

Keep:

```cpp
bool LowPowerSnakeBodyContains(const LowPowerSnakeCell& cell, uint8_t ignore_from) const
```

but replace fixed-length callers:

```cpp
return !LowPowerSnakeBodyContains(next, k_low_power_snake_length - 1U);
return LowPowerSnakeBodyContains(cell, k_low_power_snake_length - 1U);
for (uint8_t body = k_low_power_snake_length - 1U; body > 0; --body) {
```

with:

```cpp
return !LowPowerSnakeBodyContains(next, low_power_clock_snake_length_ - 1U);
return LowPowerSnakeBodyContains(cell, low_power_clock_snake_length_ - 1U);
for (uint8_t body = low_power_clock_snake_length_ - 1U; body > 0; --body) {
```

- [ ] **Step 5: Add fruit distance and generation helpers**

Add these helpers after `MoveLowPowerSnakeLocked()`:

```cpp
static uint16_t LowPowerSnakeDistanceToFruit(
    const LowPowerSnakeCell& cell,
    const LowPowerSnakeCell& fruit
) {
    const int16_t dc = (int16_t)cell.col - (int16_t)fruit.col;
    const int16_t dr = (int16_t)cell.row - (int16_t)fruit.row;
    return (uint16_t)((dc < 0 ? -dc : dc) + (dr < 0 ? -dr : dr));
}

uint16_t LowPowerSnakeDistanceToFruit(const LowPowerSnakeCell& cell) const {
    return LowPowerSnakeDistanceToFruit(cell, low_power_clock_snake_fruit_);
}

bool GenerateLowPowerSnakeFruitLocked() {
    const uint32_t offset = NextLowPowerSnakeRandomLocked() % k_low_power_snake_path_max;
    for (uint16_t attempt = 0; attempt < k_low_power_snake_path_max; ++attempt) {
        const uint16_t index = (uint16_t)((offset + attempt) % k_low_power_snake_path_max);
        const LowPowerSnakeCell candidate = {
            static_cast<uint8_t>(index % k_low_power_snake_cols),
            static_cast<uint8_t>(index / k_low_power_snake_cols),
        };
        if (!LowPowerSnakeCellInCircle(candidate.col, candidate.row) ||
            LowPowerSnakeBodyContains(candidate, low_power_clock_snake_length_)) {
            continue;
        }
        low_power_clock_snake_fruit_ = candidate;
        low_power_clock_snake_fruit_ready_ = true;
        return true;
    }
    low_power_clock_snake_fruit_ready_ = false;
    return false;
}
```

- [ ] **Step 6: Change reset and add new-game initialization**

Change:

```cpp
void ResetLowPowerSnakeLocked() {
```

to:

```cpp
void ResetLowPowerSnakeLocked(uint8_t target_length) {
    if (target_length < k_low_power_snake_initial_length) {
        target_length = k_low_power_snake_initial_length;
    }
    if (target_length > k_low_power_snake_max_length) {
        target_length = k_low_power_snake_max_length;
    }
    low_power_clock_snake_length_ = target_length;
```

Inside reset, replace:

```cpp
for (uint8_t i = 1; i < k_low_power_snake_length; ++i) {
```

with:

```cpp
for (uint8_t i = 1; i < low_power_clock_snake_length_; ++i) {
```

After `ResetLowPowerSnakeLocked(uint8_t target_length)`, add:

```cpp
void StartNewLowPowerSnakeGameLocked() {
    low_power_clock_snake_length_ = k_low_power_snake_initial_length;
    low_power_clock_snake_fruit_count_ = 0;
    low_power_clock_snake_fruit_ready_ = false;
    ResetLowPowerSnakeLocked(k_low_power_snake_initial_length);
    if (low_power_clock_snake_ready_) {
        GenerateLowPowerSnakeFruitLocked();
    }
}
```

Update existing calls:

```cpp
ResetLowPowerSnakeLocked();
```

to:

```cpp
ResetLowPowerSnakeLocked(low_power_clock_snake_length_);
GenerateLowPowerSnakeFruitLocked();
```

where the reset is runtime trap recovery.

- [ ] **Step 7: Handle fruit eating and capped growth**

Add this helper before `AdvanceLowPowerSnakeLocked()`:

```cpp
void HandleLowPowerSnakeFruitLocked(const LowPowerSnakeCell& next) {
    if (!low_power_clock_snake_fruit_ready_ ||
        next.col != low_power_clock_snake_fruit_.col ||
        next.row != low_power_clock_snake_fruit_.row) {
        return;
    }

    low_power_clock_snake_fruit_count_++;
    if (low_power_clock_snake_fruit_count_ >= k_low_power_snake_fruits_per_growth) {
        if (low_power_clock_snake_length_ < k_low_power_snake_max_length) {
            low_power_clock_snake_length_++;
            low_power_clock_snake_fruit_count_ -= k_low_power_snake_fruits_per_growth;
        } else {
            low_power_clock_snake_fruit_count_ = k_low_power_snake_fruits_per_growth;
        }
    }
    GenerateLowPowerSnakeFruitLocked();
}
```

- [ ] **Step 8: Bias movement toward fruit**

In `AdvanceLowPowerSnakeLocked()`, ensure fruit exists after readiness:

```cpp
if (!low_power_clock_snake_fruit_ready_) {
    GenerateLowPowerSnakeFruitLocked();
}
```

Track candidate fruit distance:

```cpp
bool has_best_direction = false;
LowPowerSnakeDirection best_direction = low_power_clock_snake_direction_;
uint16_t best_score = 0;
uint16_t best_fruit_distance = UINT16_MAX;
```

Inside the candidate loop after `const uint16_t score = LowPowerSnakeReachableSpaceAfterMove(next);`, add:

```cpp
const uint16_t fruit_distance =
    low_power_clock_snake_fruit_ready_ ? LowPowerSnakeDistanceToFruit(next) : UINT16_MAX;
```

Replace best-direction selection with:

```cpp
if (!has_best_direction ||
    fruit_distance < best_fruit_distance ||
    (fruit_distance == best_fruit_distance && score > best_score)) {
    best_score = score;
    best_fruit_distance = fruit_distance;
    best_direction = direction;
    has_best_direction = true;
}
```

After a successful move, call fruit handling with the new head:

```cpp
if (has_safe_direction) {
    MoveLowPowerSnakeLocked(safe_direction);
    HandleLowPowerSnakeFruitLocked(low_power_clock_snake_body_[0]);
} else if (has_best_direction) {
    MoveLowPowerSnakeLocked(best_direction);
    HandleLowPowerSnakeFruitLocked(low_power_clock_snake_body_[0]);
} else {
    ResetLowPowerSnakeLocked(low_power_clock_snake_length_);
    GenerateLowPowerSnakeFruitLocked();
}
```

- [ ] **Step 9: Draw fruit before snake body**

Add this helper before `DrawLowPowerSnakeBackground()`:

```cpp
void DrawLowPowerSnakeFruitLocked(lv_layer_t* layer) {
    if (!low_power_clock_snake_fruit_ready_) {
        return;
    }
    DrawLowPowerSnakeCell(
        layer,
        low_power_clock_snake_fruit_.col,
        low_power_clock_snake_fruit_.row,
        lv_color_hex(0xFF4D4D),
        LowPowerClockOpaPercent(92),
        3
    );
}
```

In `DrawLowPowerSnakeBackground(lv_event_t* e)`, after the base grid loop and before the ready check, add:

```cpp
DrawLowPowerSnakeFruitLocked(layer);
```

Replace the body draw loop:

```cpp
for (uint8_t i = k_low_power_snake_length; i > 0; --i) {
```

with:

```cpp
for (uint8_t i = low_power_clock_snake_length_; i > 0; --i) {
```

- [ ] **Step 10: Start new game on standby entry**

In `ShowLowPowerClockScreen()`, replace:

```cpp
low_power_clock_snake_redraw_pending_ = false;
RefreshLowPowerClockScreenLocked(true);
```

with:

```cpp
low_power_clock_snake_redraw_pending_ = false;
StartNewLowPowerSnakeGameLocked();
RefreshLowPowerClockScreenLocked(true);
```

- [ ] **Step 11: Run tests to verify implementation**

Run:

```powershell
pytest tests/xiaoxin_low_power_clock_visual_path_test.py -q
```

Expected: PASS.

- [ ] **Step 12: Run a broader sanity check if available**

Run:

```powershell
pytest tests -q
```

Expected: PASS or only unrelated pre-existing failures. If ESP-IDF is configured locally, also run:

```powershell
idf.py build
```

- [ ] **Step 13: Commit implementation**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc tests/xiaoxin_low_power_clock_visual_path_test.py
git commit -m "feat: add fruit growth to standby snake"
```

## Self-Review

- Spec coverage: the plan covers fruit placement, fruit refresh, eight-fruit growth, max length, no max-length auto-new-game, standby-entry new game, trap recovery, single LVGL background object, and source-level tests.
- Placeholder scan: no placeholder steps remain; every code step includes the concrete symbol names and snippets needed for implementation.
- Type consistency: the planned state names and helper names match across test and implementation tasks.
