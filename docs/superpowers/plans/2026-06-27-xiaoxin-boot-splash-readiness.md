# Xiaoxin Boot Splash Readiness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Xiaoxin 1.46 boot splash disappear once the normal UI is renderable, and make the boot GIF show only logo content over a black screen.

**Architecture:** Keep the behavior local to the Xiaoxin 1.46 display class. Reuse the existing boot splash timer, but reschedule it to a 5 second reveal delay once `PaopaoPetDisplay::SetupUI()` has prepared the first normal UI frame. Fix transparency at the `boot.gif` asset level and guard it with a Python asset test.

**Tech Stack:** ESP-IDF C++, LVGL, existing Python `pytest` path tests, Pillow for GIF asset inspection/transformation.

## Global Constraints

- Do not change Wi-Fi, OTA, or activation flow behavior.
- Do not change normal desktop pet GIF rendering logic.
- Do not add a desktop simulator in this task.
- Preserve the existing black `boot_splash_layer_` background.
- Preserve existing `CompleteBootSplash()` call sites as idempotent fallbacks.
- Current working tree contains unrelated firmware changes; stage and commit only files touched by each task.

---

## File Structure

- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
  - Owns Xiaoxin 1.46 boot splash timing and display-specific UI readiness.
- Modify: `main/assets/images/boot.gif`
  - Stores the boot animation. Its background must become transparent.
- Modify: `tests/xiaoxin_boot_diagnostics_path_test.py`
  - Existing source-level boot path guardrails. Add checks that the splash reveal is scheduled from UI readiness.
- Create: `tests/xiaoxin_boot_gif_asset_test.py`
  - Asset-level guardrail that `boot.gif` has transparent corners and visible logo pixels.

---

### Task 1: Reschedule Boot Splash Exit When Normal UI Is Ready

**Files:**
- Modify: `tests/xiaoxin_boot_diagnostics_path_test.py`
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`

**Interfaces:**
- Consumes: existing `ShowBootSplashLocked()`, `HideBootSplashFromTimer()`, `CompleteBootSplash()`, and `boot_splash_timer_`.
- Produces: `ScheduleBootSplashRevealAfterUiReadyLocked()` private method and `k_boot_splash_ui_ready_reveal_delay_ms` constant.

- [ ] **Step 1: Add the failing source test**

Append assertions to `test_application_completes_boot_splash_when_ui_can_be_revealed()` or add a new test in `tests/xiaoxin_boot_diagnostics_path_test.py`:

```python
def test_xiaoxin_boot_splash_reveals_after_normal_ui_first_frame_is_ready():
    source = read_source(BOARD_SOURCE)
    setup_ui = function_body(source, "virtual void SetupUI() override")
    reveal = function_body(source, "void ScheduleBootSplashRevealAfterUiReadyLocked()")

    assert "k_boot_splash_ui_ready_reveal_delay_ms" in source
    assert "ScheduleBootSplashRevealAfterUiReadyLocked();" in setup_ui
    assert setup_ui.index("PlayGifState(current_state_);") < setup_ui.index("ShowBootSplashLocked();")
    assert setup_ui.index("ShowBootSplashLocked();") < setup_ui.index(
        "ScheduleBootSplashRevealAfterUiReadyLocked();"
    )
    assert "boot_splash_wait_for_ready_ = false;" in reveal
    assert "esp_timer_stop(boot_splash_timer_);" in reveal
    assert "esp_timer_start_once(boot_splash_timer_, k_boot_splash_ui_ready_reveal_delay_ms * 1000ULL)" in reveal
    assert "HideBootSplashLocked();" in reveal
```

- [ ] **Step 2: Run the failing test**

Run:

```powershell
python -m pytest tests/xiaoxin_boot_diagnostics_path_test.py::test_xiaoxin_boot_splash_reveals_after_normal_ui_first_frame_is_ready -q
```

Expected: FAIL because `ScheduleBootSplashRevealAfterUiReadyLocked()` and `k_boot_splash_ui_ready_reveal_delay_ms` do not exist.

- [ ] **Step 3: Add the short UI-ready reveal delay constant**

In `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`, near the existing boot constants:

```cpp
static constexpr uint32_t k_boot_splash_ui_ready_reveal_delay_ms = 5000;
```

- [ ] **Step 4: Schedule reveal after the boot splash is shown**

In `PaopaoPetDisplay::SetupUI()`, immediately after `ShowBootSplashLocked();`:

```cpp
        ShowBootSplashLocked();
        ScheduleBootSplashRevealAfterUiReadyLocked();
```

Keep this after `PlayGifState(current_state_)` so the normal pet frame is already prepared below the splash.

- [ ] **Step 5: Implement the reveal helper**

Add this private method near `ShowBootSplashLocked()` and `HideBootSplashLocked()`:

```cpp
    void ScheduleBootSplashRevealAfterUiReadyLocked() {
        if (!boot_splash_visible_) {
            return;
        }
        if (boot_splash_timer_ == nullptr) {
            HideBootSplashLocked();
            return;
        }

        boot_splash_wait_for_ready_ = false;
        esp_timer_stop(boot_splash_timer_);
        esp_err_t err = esp_timer_start_once(
            boot_splash_timer_,
            k_boot_splash_ui_ready_reveal_delay_ms * 1000ULL
        );
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Boot splash UI-ready reveal timer start failed: %s", esp_err_to_name(err));
            HideBootSplashLocked();
        }
    }
```

- [ ] **Step 6: Run the focused test**

Run:

```powershell
python -m pytest tests/xiaoxin_boot_diagnostics_path_test.py::test_xiaoxin_boot_splash_reveals_after_normal_ui_first_frame_is_ready -q
```

Expected: PASS.

- [ ] **Step 7: Run existing boot path tests**

Run:

```powershell
python -m pytest tests/xiaoxin_boot_diagnostics_path_test.py tests/xiaoxin_power_latch_path_test.py -q
```

Expected: PASS. If an existing guard expects battery splash to wait until activation, update it to expect the new UI-ready reveal while keeping the `CompleteBootSplash()` fallback checks.

- [ ] **Step 8: Commit Task 1**

```powershell
git add -- main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc tests/xiaoxin_boot_diagnostics_path_test.py
git commit -m "fix: reveal xiaoxin boot splash after ui readiness"
```

---

### Task 2: Make Boot GIF Background Transparent

**Files:**
- Create: `tests/xiaoxin_boot_gif_asset_test.py`
- Modify: `main/assets/images/boot.gif`

**Interfaces:**
- Consumes: `main/assets/images/boot.gif`.
- Produces: a GIF whose corner pixels are transparent and whose logo pixels remain visible.

- [ ] **Step 1: Add the failing asset test**

Create `tests/xiaoxin_boot_gif_asset_test.py`:

```python
from pathlib import Path

from PIL import Image, ImageSequence


BOOT_GIF = Path("main/assets/images/boot.gif")


def test_boot_gif_has_transparent_background_and_visible_logo_pixels():
    image = Image.open(BOOT_GIF)
    frames = [frame.convert("RGBA") for frame in ImageSequence.Iterator(image)]

    assert frames, "boot.gif must contain at least one frame"
    assert len(frames) >= 2, "boot.gif should remain animated"

    width, height = frames[0].size
    corner_points = [
        (0, 0),
        (width - 1, 0),
        (0, height - 1),
        (width - 1, height - 1),
    ]

    for frame in frames[: min(3, len(frames))]:
        for point in corner_points:
            assert frame.getpixel(point)[3] == 0

    visible_pixels = sum(
        1
        for frame in frames
        for pixel in frame.getdata()
        if pixel[3] > 0
    )
    total_pixels = len(frames) * width * height

    assert visible_pixels > 0
    assert visible_pixels < total_pixels * 0.75
```

- [ ] **Step 2: Run the failing asset test**

Run:

```powershell
python -m pytest tests/xiaoxin_boot_gif_asset_test.py -q
```

Expected: FAIL because current `boot.gif` has opaque near-white corner pixels.

- [ ] **Step 3: Convert near-white background pixels to transparent**

Run this one-off asset conversion:

```powershell
@'
from pathlib import Path
from PIL import Image, ImageSequence

path = Path("main/assets/images/boot.gif")
image = Image.open(path)
frames = []
durations = []

for frame in ImageSequence.Iterator(image):
    rgba = frame.convert("RGBA")
    pixels = []
    for r, g, b, a in rgba.getdata():
        if r >= 245 and g >= 245 and b >= 245:
            pixels.append((r, g, b, 0))
        else:
            pixels.append((r, g, b, a))
    rgba.putdata(pixels)
    frames.append(rgba)
    durations.append(frame.info.get("duration", image.info.get("duration", 200)))

save_kwargs = {
    "save_all": True,
    "append_images": frames[1:],
    "duration": durations,
    "loop": image.info.get("loop", 0),
    "disposal": 2,
    "transparency": 0,
}
frames[0].save(path, **save_kwargs)
'@ | python -
```

- [ ] **Step 4: Verify the asset test passes**

Run:

```powershell
python -m pytest tests/xiaoxin_boot_gif_asset_test.py -q
```

Expected: PASS.

- [ ] **Step 5: Commit Task 2**

```powershell
git add -- main/assets/images/boot.gif tests/xiaoxin_boot_gif_asset_test.py
git commit -m "fix: make xiaoxin boot gif background transparent"
```

---

### Task 3: Build and Final Verification

**Files:**
- No new source files.
- Reads generated build output under `build/`.

**Interfaces:**
- Consumes: changes from Tasks 1 and 2.
- Produces: rebuilt firmware with updated boot splash logic and embedded transparent GIF.

- [ ] **Step 1: Run focused Python tests**

Run:

```powershell
python -m pytest tests/xiaoxin_boot_diagnostics_path_test.py tests/xiaoxin_power_latch_path_test.py tests/xiaoxin_boot_gif_asset_test.py -q
```

Expected: PASS.

- [ ] **Step 2: Build firmware**

Run:

```powershell
idf.py build
```

Expected: build completes successfully and updates `build/ai_pet.bin`.

- [ ] **Step 3: Check build output**

Run:

```powershell
Get-ChildItem -LiteralPath build -File | Where-Object { $_.Name -in @("ai_pet.bin","flash_args","flasher_args.json") } | Select-Object Name,Length,LastWriteTime
```

Expected: `ai_pet.bin` exists with a fresh timestamp.

- [ ] **Step 4: Commit verification-only changes if any**

If build artifacts are tracked and changed, stage only intended tracked artifacts. Otherwise do not commit generated files.

```powershell
git status --short
```

Expected: no unexpected source changes beyond the two task commits.

---

## Self-Review

- Spec coverage: Task 1 covers UI-ready boot splash exit independent of Wi-Fi/OTA/activation. Task 2 covers transparent GIF background over the existing black splash layer. Task 3 covers tests and build verification.
- Placeholder scan: no forbidden placeholder terms remain.
- Type consistency: `ScheduleBootSplashRevealAfterUiReadyLocked()` and `k_boot_splash_ui_ready_reveal_delay_ms` are named consistently across tests and implementation steps.
