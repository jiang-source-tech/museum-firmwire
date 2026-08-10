# Xiaoxin Real Situation Pet GIF Mapping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Xiaoxin play `tired.gif` when the battery state machine enters the one-bar low-battery state, while preserving the existing WiFi, voice, idle, service emotion, and local interaction mappings.

**Architecture:** Reuse the existing battery state machine edge (`xiaoxin_battery_snapshot_t.low_edge`) and the existing pet mood pipeline (`PAOPAO_PET_MOOD_EVENT_BATTERY_LOW -> PAOPAO_PET_TRIGGER_SERVICE_TIRED -> PAOPAO_PET_STATE_TIRED`). The display/board layer only connects the real battery edge into the already-tested mood module; it does not add new GIF assets, charging detection, recovery feedback, or a new snapshot type.

**Tech Stack:** ESP-IDF C/C++, LVGL display layer, C unit tests, Python source-path tests, Markdown/YAML requirements docs.

## Global Constraints

- Do not add new GIF assets.
- Do not add charging detection; `power_source == external` is only an external-power inference and must not be presented as "charging".
- Do not add battery recovery happy feedback.
- Do not add 0-bar critical-battery pet mapping in this task.
- Do not add energy, intimacy, long-term mood, or a new lightweight pet-state snapshot.
- Ordinary real-situation mood suggestions must not interrupt speaking, thinking, listening/waiting, failing, or sleeping protected states.
- Preserve existing low-battery notification behavior.
- Ignore unrelated dirty worktree files; stage only files touched by this plan.

---

## File Structure

- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
  - Responsibility: connect `snapshot.low_edge` from `HandleBatterySnapshot()` into the existing pet mood pipeline.
- Modify `tests/xiaoxin_pet_mood_integration_path_test.py`
  - Responsibility: lock the source-level integration contract for low battery -> pet mood event and preserve protected-state behavior.
- Modify `docs/xiaoxin-feature-roadmap.zh-CN.md`
  - Responsibility: update the roadmap to say one-bar low battery now maps to tired, while charging/recovery/0-bar critical remain out of scope.
- Modify `docs/visualization/xiaoxin-feature-map.yaml`
  - Responsibility: keep the visual feature map consistent with the roadmap.
- Modify `docs/xiaoxin-pet-emotion-gif-mapping.zh-CN.md`
  - Responsibility: document that low battery uses `tired.gif` through the existing trigger chain.

---

### Task 1: Wire Low Battery Edge Into Pet Mood

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Modify: `tests/xiaoxin_pet_mood_integration_path_test.py`

**Interfaces:**
- Consumes: `xiaoxin_battery_snapshot_t.low_edge`, `PaopaoPetDisplay::ShowLowBatteryNotification()`, `PaopaoPetDisplay::DispatchPetMoodEvent(paopao_pet_mood_event_t, paopao_pet_trigger_event_t = PAOPAO_PET_TRIGGER_NONE)`.
- Produces: A runtime path from one-bar low battery to `PAOPAO_PET_MOOD_EVENT_BATTERY_LOW`.

- [ ] **Step 1: Replace the obsolete removed-path test with a failing integration test**

In `tests/xiaoxin_pet_mood_integration_path_test.py`, replace this test:

```python
def test_pet_mood_battery_edge_runtime_path_is_removed():
    source = read_source()

    assert "void SyncPetMoodDeviceStateLocked()" not in source
    assert "battery_snapshot_.low_edge" not in source
    assert "battery_snapshot_.critical_edge" not in source
    assert "battery_snapshot_.recovered_edge" not in source
```

with this test:

```python
def test_low_battery_edge_notifies_and_dispatches_tired_mood():
    body = function_body(
        source=read_source(),
        signature="void HandleBatterySnapshot(const xiaoxin_battery_snapshot_t& snapshot)"
    )

    assert "if (snapshot.low_edge && display != nullptr)" in body
    low_edge_start = body.index("if (snapshot.low_edge && display != nullptr)")
    low_edge_block = body[low_edge_start: body.index("}", low_edge_start)]

    assert "display->ShowLowBatteryNotification();" in low_edge_block
    assert "display->DispatchPetMoodEvent(PAOPAO_PET_MOOD_EVENT_BATTERY_LOW);" in low_edge_block
    assert "PAOPAO_PET_MOOD_EVENT_BATTERY_RECOVERED" not in body
    assert "snapshot.critical_edge" not in body
```

- [ ] **Step 2: Run the source-path test and verify it fails**

Run:

```powershell
python -m pytest tests/xiaoxin_pet_mood_integration_path_test.py::test_low_battery_edge_notifies_and_dispatches_tired_mood -q
```

Expected: FAIL because `display->DispatchPetMoodEvent(PAOPAO_PET_MOOD_EVENT_BATTERY_LOW);` is not yet in `HandleBatterySnapshot()`.

- [ ] **Step 3: Implement the low-battery mood dispatch**

In `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`, change `HandleBatterySnapshot()` from:

```cpp
if (snapshot.low_edge && display != nullptr) {
    display->ShowLowBatteryNotification();
}
```

to:

```cpp
if (snapshot.low_edge && display != nullptr) {
    display->ShowLowBatteryNotification();
    display->DispatchPetMoodEvent(PAOPAO_PET_MOOD_EVENT_BATTERY_LOW);
}
```

Do not add handling for `snapshot.critical_edge`, `snapshot.recovered_edge`, or `battery_snapshot_.power_source`.

- [ ] **Step 4: Run the focused integration test and verify it passes**

Run:

```powershell
python -m pytest tests/xiaoxin_pet_mood_integration_path_test.py::test_low_battery_edge_notifies_and_dispatches_tired_mood -q
```

Expected: PASS.

- [ ] **Step 5: Run the pet mood and protected-state tests**

Run:

```powershell
python -m pytest tests/xiaoxin_pet_mood_integration_path_test.py -q
```

Expected: PASS, including the existing protected-state helper test.

Run:

```powershell
gcc -std=c11 -Wall -Wextra -I. tests/paopao_pet_mood_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_mood.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_emotion.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_trigger.c -o build/paopao_pet_mood_test.exe
.\build\paopao_pet_mood_test.exe
```

Expected: `paopao_pet_mood tests passed`.

Run:

```powershell
gcc -std=c11 -Wall -Wextra -I. tests/paopao_pet_trigger_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_trigger.c -o build/paopao_pet_trigger_test.exe
.\build\paopao_pet_trigger_test.exe
```

Expected: `paopao pet trigger tests passed`.

- [ ] **Step 6: Commit Task 1**

Stage only the files from this task:

```powershell
git add -- main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc tests/xiaoxin_pet_mood_integration_path_test.py
git commit -m "feat: map low battery to tired pet mood"
```

Expected: commit succeeds and does not stage unrelated dirty files.

---

### Task 2: Sync Requirements Documentation

**Files:**
- Modify: `docs/xiaoxin-feature-roadmap.zh-CN.md`
- Modify: `docs/visualization/xiaoxin-feature-map.yaml`
- Modify: `docs/xiaoxin-pet-emotion-gif-mapping.zh-CN.md`

**Interfaces:**
- Consumes: Task 1 behavior: `snapshot.low_edge -> PAOPAO_PET_MOOD_EVENT_BATTERY_LOW -> SERVICE_TIRED -> PAOPAO_PET_STATE_TIRED -> tired.gif`.
- Produces: Requirements docs that match the implemented low-battery mapping and explicitly exclude charging, recovery, and critical-battery mapping from this task.

- [ ] **Step 1: Update the pet roadmap status**

In `docs/xiaoxin-feature-roadmap.zh-CN.md`, update the `P1：宠物情绪系统` section so the implemented list includes this bullet:

```markdown
- 一格低电量边沿已接入宠物 mood，稳定进入低电状态时复用 `tired.gif` 表现疲惫。
```

In the same section, keep the not-yet-complete list explicit with these bullets:

```markdown
- 0 格严重低电量、充电识别和电量恢复反馈尚未纳入宠物表现；当前硬件/固件只把一格低电作为可靠宠物映射。
- 情绪系统目前仍接近“事件到动画建议”，还没有长期能量值、心情值、亲密度或连续性人格状态。
```

If an older bullet says low battery is not connected to pet behavior, replace that older statement instead of adding a contradiction.

- [ ] **Step 2: Update the feature map**

In `docs/visualization/xiaoxin-feature-map.yaml`, under the `pet_mood` feature:

Add to `implemented`:

```yaml
      - 一格低电量边沿已接入宠物 mood，稳定进入低电状态时复用 tired.gif 表现疲惫。
```

Add or preserve under `gaps`:

```yaml
      - 0 格严重低电量、充电识别和电量恢复反馈尚未纳入宠物表现。
      - 长期能量值、心情值、亲密度或连续性人格状态尚未落地。
```

Do not mark charging behavior as implemented.

- [ ] **Step 3: Update the GIF mapping doc**

In `docs/xiaoxin-pet-emotion-gif-mapping.zh-CN.md`, update the `tired.gif` mapping row or nearby explanation so it says:

```markdown
| `tired.gif` | 服务端 emotion 包含 `tired`, `weak`, `low_battery`；本机电量稳定进入一格低电状态时 |
```

Add this note near the mapping table:

```markdown
说明：当前本机电量映射只覆盖一格低电到 `tired.gif`。0 格严重低电、充电识别和电量恢复反馈暂不作为宠物 GIF 触发条件。
```

- [ ] **Step 4: Run documentation consistency checks**

Run:

```powershell
rg -n "充电.*已|恢复.*happy|0 格.*已|严重低电.*已" docs/xiaoxin-feature-roadmap.zh-CN.md docs/visualization/xiaoxin-feature-map.yaml docs/xiaoxin-pet-emotion-gif-mapping.zh-CN.md
```

Expected: no result that claims charging, recovery, or 0-bar critical low battery is implemented.

Run:

```powershell
rg -n "一格低电|tired\\.gif|BATTERY_LOW|low_battery" docs/xiaoxin-feature-roadmap.zh-CN.md docs/visualization/xiaoxin-feature-map.yaml docs/xiaoxin-pet-emotion-gif-mapping.zh-CN.md
```

Expected: results include the new one-bar low-battery mapping in all three docs.

- [ ] **Step 5: Run the focused regression suite again**

Run:

```powershell
python -m pytest tests/xiaoxin_pet_mood_integration_path_test.py tests/xiaoxin_low_battery_shutdown_path_test.py tests/xiaoxin_notification_visual_path_test.py -q
```

Expected: PASS.

Run:

```powershell
gcc -std=c11 -Wall -Wextra -I. tests/paopao_pet_mood_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_mood.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_emotion.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_trigger.c -o build/paopao_pet_mood_test.exe
.\build\paopao_pet_mood_test.exe
```

Expected: `paopao_pet_mood tests passed`.

- [ ] **Step 6: Commit Task 2**

Stage only the files from this task:

```powershell
git add -- docs/xiaoxin-feature-roadmap.zh-CN.md docs/visualization/xiaoxin-feature-map.yaml docs/xiaoxin-pet-emotion-gif-mapping.zh-CN.md
git commit -m "docs: sync pet mood situation mapping requirements"
```

Expected: commit succeeds and does not stage unrelated dirty files.

---

## Self-Review Notes

- Spec coverage: Task 1 implements the one-bar low-battery -> tired path and preserves protected-state behavior through existing tests. Task 2 syncs roadmap, feature map, and GIF mapping docs.
- Exclusions covered: charging detection, recovery feedback, 0-bar critical mapping, new GIF assets, and long-term personality state are explicitly excluded in the plan and docs steps.
- Type consistency: The plan uses existing names from the codebase: `xiaoxin_battery_snapshot_t.low_edge`, `PAOPAO_PET_MOOD_EVENT_BATTERY_LOW`, `PAOPAO_PET_TRIGGER_SERVICE_TIRED`, `PAOPAO_PET_STATE_TIRED`, and `tired.gif`.
