# Xiaoxin Notification Heads-Up Implementation Plan

> **Superseded on 2026-07-12.** The product decision now keeps ordinary notifications in the card-based notification center without a heads-up overlay. Do not execute this historical plan. See `docs/xiaoxin-notification-delivery-policy.zh-CN.md`.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a unified visual heads-up banner for every Xiaoxin notification, complete short-lived notification TTL cleanup, and connect OTA status to the notification center.

**Architecture:** Keep notification storage and TTL in `xiaoxin_card_pager`, add a small pure C heads-up queue model for deterministic tests, and keep LVGL object creation in the Waveshare 1.46 display file. OTA enters the notification center through a generic `Display` notification bridge so `Application` does not depend on board-specific Xiaoxin types.

**Tech Stack:** ESP-IDF, LVGL 9, C/C++, existing C unit tests compiled with `gcc`, existing Python source-path tests with `pytest`.

## Global Constraints

- Target board: `CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_46`.
- All notification types use the same heads-up visual strength, size, animation, and display duration.
- First implementation is visual only: no sound, vibration, or speech playback.
- Heads-up position is a fixed top banner.
- Heads-up motion is top-down: start above the screen, slide down to the fixed top position, stay for 3 seconds, then slide upward out of view while fading.
- Heads-up queue overflow policy is FIFO for queued notifications: keep the active banner, drop the oldest queued banner, append the newest banner.
- Chat reply compatibility events do not enter the notification center and must not trigger heads-up.
- Notification card time display is documented as a follow-up and is not part of this implementation.
- Keep notification business rules out of LVGL rendering code where a small pure C model can own them.
- `PaopaoPetDisplay::NowMs()` already exists and returns `esp_timer_get_time() / 1000`.
- `PaopaoPetDisplay::RemoveNotificationEventLocked()` already exists near the notification upsert helper and removes notifications by `xiaoxin_notification_event_type_t`.
- Heads-up banner visual style: light frosted glass. Four-layer composite — gradient translucent panel, asymmetric rim light edges, noise-texture overlay, diffuse drop shadow. Tag rendered as a small capsule badge. No real-time background blur (infeasible without GPU). Dark text on light glass.

---

## File Structure

- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_card_pager.h`
  - Add notification expiry storage and pure APIs for upserting with a timestamp, expiring TTL notifications, and finding a notification by type.
- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_card_pager.c`
  - Compute `expires_at_ms` from `ttl_ms`, keep expiry data stable through sorting, and remove expired notifications.
- Modify `tests/xiaoxin_card_pager_test.c`
  - Add TTL and find-by-type unit tests.
- Create `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_notification_heads_up.h`
  - Declare the pure queue model for unified heads-up banners.
- Create `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_notification_heads_up.c`
  - Implement fixed-duration queueing, same-content dedupe, overflow behavior, and snapshots.
- Create `tests/xiaoxin_notification_heads_up_test.c`
  - Cover queue order, 3-second duration, dedupe, overflow, and empty-body behavior.
- Modify `main/CMakeLists.txt`
  - Compile the new heads-up model for the Waveshare 1.46 board.
- Create `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_notification_heads_up_glass_texture.c`
  - Embed a 250×58 grayscale noise texture (LVGL I8 format, ~14.5 KB C array) for glass surface simulation.
- Create `tools/generate_glass_texture.py`
  - Python script to generate the Perlin-like noise texture PNG for LVGL image conversion.
- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
  - Create the frosted-glass top banner LVGL objects (gradient panel, rim light, noise overlay, shadow, tag capsule), enqueue successful notification upserts, tick TTL and heads-up state, and keep the banner above low-power clock/settings/card layers.
- Modify `tests/xiaoxin_notification_visual_path_test.py`
  - Assert heads-up fields, LVGL layout, queue calls, timer calls, and low-power foreground ordering.
- Modify `main/display/display.h`
  - Add a generic display notification bridge.
- Modify `main/display/display.cc`
  - Add default logging/no-op bridge behavior.
- Modify `main/application.cc`
  - Convert OTA new-version/start/failure states to generic display notifications.
- Modify `tests/ota_url_config_test.py`
  - Add source-path assertions for OTA notification bridge calls.
- Already updated before implementation planning: `docs/visualization/xiaoxin-feature-map.yaml`, `docs/visualization/README.zh-CN.md`, and `tests/xiaoxin_docs_visualization_test.py`
  - The YAML visualization already links this implementation plan and preserves follow-up reminders for notification card time plus future sound/vibration.

---

### Task 1: Add TTL Lifecycle to the Notification Store

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_card_pager.h`
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_card_pager.c`
- Test: `tests/xiaoxin_card_pager_test.c`

**Interfaces:**
- Consumes: `xiaoxin_notification_event_t.ttl_ms`
- Produces:
  - `int64_t notification_expires_at_ms[XIAOXIN_CARD_NOTIFICATION_MAX]`
  - `bool xiaoxin_card_pager_notification_upsert_event_at(xiaoxin_card_pager_t*, const xiaoxin_notification_event_t*, int64_t now_ms)`
  - `uint8_t xiaoxin_card_pager_notification_expire(xiaoxin_card_pager_t*, int64_t now_ms)`
  - `const xiaoxin_card_item_t* xiaoxin_card_pager_notification_find_by_type(const xiaoxin_card_pager_t*, xiaoxin_notification_event_type_t)`

- [ ] **Step 1: Write failing TTL tests**

Add these functions to `tests/xiaoxin_card_pager_test.c` before `main()`:

```c
static void notification_ttl_expire_removes_only_expired_items(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t voice = {
        .type = XIAOXIN_NOTIFICATION_EVENT_VOICE_RECOGNITION_FAILED,
        .body = "没听清，请再说一次",
        .ttl_ms = 8000,
    };
    const xiaoxin_notification_event_t wifi = {
        .type = XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED,
        .body = "WiFi 已断开，正在重新连接",
    };

    assert(xiaoxin_card_pager_notification_upsert_event_at(&pager, &voice, 1000));
    assert(xiaoxin_card_pager_notification_upsert_event_at(&pager, &wifi, 1000));
    assert(xiaoxin_card_pager_notification_count(&pager) == 2);

    assert(xiaoxin_card_pager_notification_expire(&pager, 8999) == 0);
    assert(xiaoxin_card_pager_notification_count(&pager) == 2);

    assert(xiaoxin_card_pager_notification_expire(&pager, 9000) == 1);
    assert(xiaoxin_card_pager_notification_count(&pager) == 1);
    assert(xiaoxin_card_pager_notification_find_by_type(&pager, XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED) != NULL);
    assert(xiaoxin_card_pager_notification_find_by_type(&pager, XIAOXIN_NOTIFICATION_EVENT_VOICE_RECOGNITION_FAILED) == NULL);
}

static void notification_find_by_type_survives_priority_sort(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t voice = {
        .type = XIAOXIN_NOTIFICATION_EVENT_VOICE_RECOGNITION_FAILED,
        .body = "没听清，请再说一次",
    };
    const xiaoxin_notification_event_t reminder = {
        .type = XIAOXIN_NOTIFICATION_EVENT_REMINDER,
        .body = "15 分钟后 高等数学 @ 3教 204",
    };

    assert(xiaoxin_card_pager_notification_upsert_event_at(&pager, &voice, 2000));
    assert(xiaoxin_card_pager_notification_upsert_event_at(&pager, &reminder, 2000));

    const xiaoxin_card_item_t* found =
        xiaoxin_card_pager_notification_find_by_type(&pager, XIAOXIN_NOTIFICATION_EVENT_VOICE_RECOGNITION_FAILED);
    assert(found != NULL);
    assert(strcmp(found->title, "语音识别失败") == 0);
    assert(strcmp(found->body, "没听清，请再说一次") == 0);
}
```

Call both from `main()`:

```c
    notification_ttl_expire_removes_only_expired_items();
    notification_find_by_type_survives_priority_sort();
```

- [ ] **Step 2: Run the C test and verify it fails**

Run:

```powershell
gcc tests\xiaoxin_card_pager_test.c main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_card_pager.c -I main\boards\waveshare\esp32-s3-touch-lcd-1.46 -o build\xiaoxin_card_pager_test.exe
```

Expected: compile failure mentioning `xiaoxin_card_pager_notification_upsert_event_at`, `xiaoxin_card_pager_notification_expire`, or `xiaoxin_card_pager_notification_find_by_type`.

- [ ] **Step 3: Add the public fields and function declarations**

In `xiaoxin_card_pager.h`, add the expiry storage after `notification_types`:

```c
  int64_t notification_expires_at_ms[XIAOXIN_CARD_NOTIFICATION_MAX];
```

Add declarations after `xiaoxin_card_pager_notification_upsert_event()`:

```c
bool xiaoxin_card_pager_notification_upsert_event_at(
  xiaoxin_card_pager_t* pager,
  const xiaoxin_notification_event_t* event,
  int64_t now_ms
);
uint8_t xiaoxin_card_pager_notification_expire(
  xiaoxin_card_pager_t* pager,
  int64_t now_ms
);
const xiaoxin_card_item_t* xiaoxin_card_pager_notification_find_by_type(
  const xiaoxin_card_pager_t* pager,
  xiaoxin_notification_event_type_t type
);
```

- [ ] **Step 4: Implement expiry storage**

In `notification_clear_slot()` set:

```c
  pager->notification_expires_at_ms[slot] = 0;
```

In `notification_swap_slots()` swap expiry values with the other slot data:

```c
  const int64_t expires_tmp = pager->notification_expires_at_ms[a];
  pager->notification_expires_at_ms[a] = pager->notification_expires_at_ms[b];
  pager->notification_expires_at_ms[b] = expires_tmp;
```

In `notification_shift_left()` copy expiry while shifting:

```c
    pager->notification_expires_at_ms[i] = pager->notification_expires_at_ms[i + 1];
```

Add a private helper near `notification_index_for_type()`:

```c
static int64_t notification_expiry_from_ttl(uint32_t ttl_ms, int64_t now_ms) {
  if (ttl_ms == 0) {
    return 0;
  }
  return now_ms + (int64_t)ttl_ms;
}
```

- [ ] **Step 5: Add timestamped upsert and keep the old API compatible**

Replace the body of `xiaoxin_card_pager_notification_upsert_event()` with:

```c
  return xiaoxin_card_pager_notification_upsert_event_at(pager, event, 0);
```

Create `xiaoxin_card_pager_notification_upsert_event_at()` by moving the old upsert body into the new function and adding this line after `ttl_ms` is assigned:

```c
  pager->notification_expires_at_ms[index] =
    notification_expiry_from_ttl(pager->notification_items[index].ttl_ms, now_ms);
```

Transition note: until Task 4 changes the board runtime to call `_upsert_event_at(..., NowMs())`, legacy callers that use `xiaoxin_card_pager_notification_upsert_event()` store TTL expiry relative to `0`. Do not add a runtime expiry timer before Task 4. Existing legacy callers with `ttl_ms == 0` are unaffected.

- [ ] **Step 6: Add expire and find APIs**

Add to `xiaoxin_card_pager.c` after the upsert functions:

```c
uint8_t xiaoxin_card_pager_notification_expire(
  xiaoxin_card_pager_t* pager,
  int64_t now_ms
) {
  if (pager == NULL) {
    return 0;
  }

  uint8_t removed = 0;
  uint8_t index = 0;
  while (index < pager->notification_count) {
    const int64_t expires_at = pager->notification_expires_at_ms[index];
    if (expires_at > 0 && now_ms >= expires_at) {
      notification_shift_left(pager, index);
      removed++;
      continue;
    }
    index++;
  }
  return removed;
}

const xiaoxin_card_item_t* xiaoxin_card_pager_notification_find_by_type(
  const xiaoxin_card_pager_t* pager,
  xiaoxin_notification_event_type_t type
) {
  const int8_t index = notification_index_for_type(pager, type);
  if (index < 0) {
    return NULL;
  }
  return &pager->notification_items[(uint8_t)index];
}
```

- [ ] **Step 7: Run the C test and verify it passes**

Run:

```powershell
gcc tests\xiaoxin_card_pager_test.c main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_card_pager.c -I main\boards\waveshare\esp32-s3-touch-lcd-1.46 -o build\xiaoxin_card_pager_test.exe; if ($LASTEXITCODE -eq 0) { .\build\xiaoxin_card_pager_test.exe }
```

Expected: output includes `xiaoxin_card_pager tests passed`.

- [ ] **Step 8: Commit**

```powershell
git add main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_card_pager.h main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_card_pager.c tests\xiaoxin_card_pager_test.c
git commit -m "feat: add xiaoxin notification ttl lifecycle"
```

---

### Task 2: Add a Pure Heads-Up Queue Model

**Files:**
- Create: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_notification_heads_up.h`
- Create: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_notification_heads_up.c`
- Modify: `main/CMakeLists.txt`
- Test: `tests/xiaoxin_notification_heads_up_test.c`

**Interfaces:**
- Consumes: `xiaoxin_card_item_t`
- Produces:
  - `XIAOXIN_NOTIFICATION_HEADS_UP_DURATION_MS` = `3000`
  - `xiaoxin_notification_heads_up_t`
  - `xiaoxin_notification_heads_up_init()`
  - `xiaoxin_notification_heads_up_enqueue()`
  - `xiaoxin_notification_heads_up_tick()`
  - `xiaoxin_notification_heads_up_snapshot()`

- [ ] **Step 1: Write the failing model test**

Create `tests/xiaoxin_notification_heads_up_test.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_card_pager.h"
#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_notification_heads_up.h"

static xiaoxin_card_item_t item(const char* title, const char* body, const char* tag) {
    xiaoxin_card_item_t value = {
        .title = title,
        .body = body,
        .detail = NULL,
        .tag = tag,
        .priority = 1,
        .ttl_ms = 0,
    };
    return value;
}

static void enqueue_starts_visible_banner_for_three_seconds(void) {
    xiaoxin_notification_heads_up_t model;
    xiaoxin_notification_heads_up_init(&model);

    xiaoxin_card_item_t voice = item("语音识别失败", "刚才没听清", "语音");
    assert(xiaoxin_notification_heads_up_enqueue(&model, &voice, 1000));

    xiaoxin_notification_heads_up_snapshot_t snapshot = {};
    assert(xiaoxin_notification_heads_up_snapshot(&model, &snapshot));
    assert(strcmp(snapshot.title, "语音识别失败") == 0);
    assert(strcmp(snapshot.body, "刚才没听清") == 0);
    assert(strcmp(snapshot.tag, "语音") == 0);

    assert(xiaoxin_notification_heads_up_tick(&model, 3999));
    assert(xiaoxin_notification_heads_up_snapshot(&model, &snapshot));

    assert(!xiaoxin_notification_heads_up_tick(&model, 4000));
    assert(!xiaoxin_notification_heads_up_snapshot(&model, &snapshot));
}

static void queued_notifications_show_in_order(void) {
    xiaoxin_notification_heads_up_t model;
    xiaoxin_notification_heads_up_init(&model);

    xiaoxin_card_item_t wifi = item("WiFi 断开", "请检查网络", "网络");
    xiaoxin_card_item_t ota = item("OTA 更新", "发现新版本", "系统");
    assert(xiaoxin_notification_heads_up_enqueue(&model, &wifi, 5000));
    assert(xiaoxin_notification_heads_up_enqueue(&model, &ota, 5100));

    xiaoxin_notification_heads_up_snapshot_t snapshot = {};
    assert(xiaoxin_notification_heads_up_snapshot(&model, &snapshot));
    assert(strcmp(snapshot.title, "WiFi 断开") == 0);

    assert(xiaoxin_notification_heads_up_tick(&model, 8000));
    assert(xiaoxin_notification_heads_up_snapshot(&model, &snapshot));
    assert(strcmp(snapshot.title, "OTA 更新") == 0);
}

static void duplicate_visible_content_is_not_queued_again(void) {
    xiaoxin_notification_heads_up_t model;
    xiaoxin_notification_heads_up_init(&model);

    xiaoxin_card_item_t first = item("WiFi 断开", "请检查网络", "网络");
    xiaoxin_card_item_t same = item("WiFi 断开", "请检查网络", "网络");
    assert(xiaoxin_notification_heads_up_enqueue(&model, &first, 1000));
    assert(!xiaoxin_notification_heads_up_enqueue(&model, &same, 1100));
}

static void overflow_drops_oldest_queued_banner(void) {
    xiaoxin_notification_heads_up_t model;
    xiaoxin_notification_heads_up_init(&model);

    xiaoxin_card_item_t active = item("低电量", "请尽快充电", "电量");
    xiaoxin_card_item_t first = item("WiFi 断开", "第一条排队", "网络");
    xiaoxin_card_item_t second = item("OTA 更新", "第二条排队", "系统");
    xiaoxin_card_item_t third = item("上课提醒", "第三条排队", "课程");
    xiaoxin_card_item_t newest = item("语音识别失败", "最新排队", "语音");

    assert(xiaoxin_notification_heads_up_enqueue(&model, &active, 1000));
    assert(xiaoxin_notification_heads_up_enqueue(&model, &first, 1100));
    assert(xiaoxin_notification_heads_up_enqueue(&model, &second, 1200));
    assert(xiaoxin_notification_heads_up_enqueue(&model, &third, 1300));
    assert(xiaoxin_notification_heads_up_enqueue(&model, &newest, 1400));

    xiaoxin_notification_heads_up_snapshot_t snapshot = {};
    assert(xiaoxin_notification_heads_up_snapshot(&model, &snapshot));
    assert(strcmp(snapshot.title, "低电量") == 0);

    assert(xiaoxin_notification_heads_up_tick(&model, 4000));
    assert(xiaoxin_notification_heads_up_snapshot(&model, &snapshot));
    assert(strcmp(snapshot.title, "OTA 更新") == 0);

    assert(xiaoxin_notification_heads_up_tick(&model, 7000));
    assert(xiaoxin_notification_heads_up_snapshot(&model, &snapshot));
    assert(strcmp(snapshot.title, "上课提醒") == 0);

    assert(xiaoxin_notification_heads_up_tick(&model, 10000));
    assert(xiaoxin_notification_heads_up_snapshot(&model, &snapshot));
    assert(strcmp(snapshot.title, "语音识别失败") == 0);
}

static void empty_body_snapshot_keeps_title_only(void) {
    xiaoxin_notification_heads_up_t model;
    xiaoxin_notification_heads_up_init(&model);

    xiaoxin_card_item_t low = item("低电量", "", "电量");
    assert(xiaoxin_notification_heads_up_enqueue(&model, &low, 1000));

    xiaoxin_notification_heads_up_snapshot_t snapshot = {};
    assert(xiaoxin_notification_heads_up_snapshot(&model, &snapshot));
    assert(strcmp(snapshot.title, "低电量") == 0);
    assert(strcmp(snapshot.body, "") == 0);
}

int main(void) {
    enqueue_starts_visible_banner_for_three_seconds();
    queued_notifications_show_in_order();
    duplicate_visible_content_is_not_queued_again();
    overflow_drops_oldest_queued_banner();
    empty_body_snapshot_keeps_title_only();
    puts("xiaoxin_notification_heads_up tests passed");
    return 0;
}
```

- [ ] **Step 2: Run the model test and verify it fails**

Run:

```powershell
gcc tests\xiaoxin_notification_heads_up_test.c main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_notification_heads_up.c -I main\boards\waveshare\esp32-s3-touch-lcd-1.46 -o build\xiaoxin_notification_heads_up_test.exe
```

Expected: compile failure because the header/source do not exist.

- [ ] **Step 3: Create the header**

Create `xiaoxin_notification_heads_up.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "xiaoxin_card_pager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XIAOXIN_NOTIFICATION_HEADS_UP_QUEUE_MAX 3
#define XIAOXIN_NOTIFICATION_HEADS_UP_DURATION_MS 3000

typedef struct {
  char title[XIAOXIN_CARD_NOTIFICATION_TITLE_MAX];
  char body[XIAOXIN_CARD_NOTIFICATION_BODY_MAX];
  char tag[XIAOXIN_CARD_NOTIFICATION_TAG_MAX];
} xiaoxin_notification_heads_up_entry_t;

typedef struct {
  bool visible;
  int64_t visible_until_ms;
  uint8_t queue_count;
  xiaoxin_notification_heads_up_entry_t active;
  xiaoxin_notification_heads_up_entry_t queue[XIAOXIN_NOTIFICATION_HEADS_UP_QUEUE_MAX];
} xiaoxin_notification_heads_up_t;

typedef struct {
  bool visible;
  const char* title;
  const char* body;
  const char* tag;
} xiaoxin_notification_heads_up_snapshot_t;

void xiaoxin_notification_heads_up_init(xiaoxin_notification_heads_up_t* model);
bool xiaoxin_notification_heads_up_enqueue(
  xiaoxin_notification_heads_up_t* model,
  const xiaoxin_card_item_t* item,
  int64_t now_ms
);
bool xiaoxin_notification_heads_up_tick(
  xiaoxin_notification_heads_up_t* model,
  int64_t now_ms
);
bool xiaoxin_notification_heads_up_snapshot(
  const xiaoxin_notification_heads_up_t* model,
  xiaoxin_notification_heads_up_snapshot_t* snapshot
);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Create the implementation**

Create `xiaoxin_notification_heads_up.c`:

```c
#include "xiaoxin_notification_heads_up.h"

#include <stdio.h>
#include <string.h>

static void copy_text(char* dest, size_t dest_size, const char* text) {
  if (dest == NULL || dest_size == 0) {
    return;
  }
  snprintf(dest, dest_size, "%s", text != NULL ? text : "");
}

static void copy_entry(
  xiaoxin_notification_heads_up_entry_t* dest,
  const xiaoxin_card_item_t* item
) {
  copy_text(dest->title, sizeof(dest->title), item != NULL ? item->title : "");
  copy_text(dest->body, sizeof(dest->body), item != NULL ? item->body : "");
  copy_text(dest->tag, sizeof(dest->tag), item != NULL ? item->tag : "");
}

static bool entry_same_item(
  const xiaoxin_notification_heads_up_entry_t* entry,
  const xiaoxin_card_item_t* item
) {
  if (entry == NULL || item == NULL) {
    return false;
  }
  return strcmp(entry->title, item->title != NULL ? item->title : "") == 0 &&
         strcmp(entry->body, item->body != NULL ? item->body : "") == 0 &&
         strcmp(entry->tag, item->tag != NULL ? item->tag : "") == 0;
}

static void start_active(
  xiaoxin_notification_heads_up_t* model,
  const xiaoxin_card_item_t* item,
  int64_t now_ms
) {
  copy_entry(&model->active, item);
  model->visible = true;
  model->visible_until_ms = now_ms + XIAOXIN_NOTIFICATION_HEADS_UP_DURATION_MS;
}

static void promote_next(xiaoxin_notification_heads_up_t* model, int64_t now_ms) {
  if (model == NULL || model->queue_count == 0) {
    if (model != NULL) {
      model->visible = false;
      model->visible_until_ms = 0;
    }
    return;
  }

  xiaoxin_card_item_t item = {
    .title = model->queue[0].title,
    .body = model->queue[0].body,
    .detail = NULL,
    .tag = model->queue[0].tag,
    .priority = 0,
    .ttl_ms = 0,
  };
  for (uint8_t i = 0; (uint8_t)(i + 1) < model->queue_count; ++i) {
    model->queue[i] = model->queue[i + 1];
  }
  model->queue_count--;
  start_active(model, &item, now_ms);
}

void xiaoxin_notification_heads_up_init(xiaoxin_notification_heads_up_t* model) {
  if (model == NULL) {
    return;
  }
  memset(model, 0, sizeof(*model));
}

bool xiaoxin_notification_heads_up_enqueue(
  xiaoxin_notification_heads_up_t* model,
  const xiaoxin_card_item_t* item,
  int64_t now_ms
) {
  if (model == NULL || item == NULL || item->title == NULL || item->title[0] == '\0') {
    return false;
  }

  if (model->visible && entry_same_item(&model->active, item)) {
    return false;
  }
  for (uint8_t i = 0; i < model->queue_count; ++i) {
    if (entry_same_item(&model->queue[i], item)) {
      return false;
    }
  }

  if (!model->visible) {
    start_active(model, item, now_ms);
    return true;
  }

  if (model->queue_count >= XIAOXIN_NOTIFICATION_HEADS_UP_QUEUE_MAX) {
    for (uint8_t i = 0; (uint8_t)(i + 1) < XIAOXIN_NOTIFICATION_HEADS_UP_QUEUE_MAX; ++i) {
      model->queue[i] = model->queue[i + 1];
    }
    model->queue_count = XIAOXIN_NOTIFICATION_HEADS_UP_QUEUE_MAX - 1;
  }
  copy_entry(&model->queue[model->queue_count], item);
  model->queue_count++;
  return true;
}

bool xiaoxin_notification_heads_up_tick(
  xiaoxin_notification_heads_up_t* model,
  int64_t now_ms
) {
  if (model == NULL) {
    return false;
  }
  if (!model->visible) {
    promote_next(model, now_ms);
    return model->visible;
  }
  if (now_ms < model->visible_until_ms) {
    return true;
  }
  promote_next(model, now_ms);
  return model->visible;
}

bool xiaoxin_notification_heads_up_snapshot(
  const xiaoxin_notification_heads_up_t* model,
  xiaoxin_notification_heads_up_snapshot_t* snapshot
) {
  if (model == NULL || snapshot == NULL || !model->visible) {
    return false;
  }
  snapshot->visible = true;
  snapshot->title = model->active.title;
  snapshot->body = model->active.body;
  snapshot->tag = model->active.tag;
  return true;
}
```

- [ ] **Step 5: Compile the new model in the firmware**

In `main/CMakeLists.txt`, add the new source next to `xiaoxin_low_power_clock_model.c`:

```cmake
        ${CMAKE_CURRENT_SOURCE_DIR}/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_notification_heads_up.c
```

- [ ] **Step 6: Run the model test and verify it passes**

Run:

```powershell
gcc tests\xiaoxin_notification_heads_up_test.c main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_notification_heads_up.c -I main\boards\waveshare\esp32-s3-touch-lcd-1.46 -o build\xiaoxin_notification_heads_up_test.exe; if ($LASTEXITCODE -eq 0) { .\build\xiaoxin_notification_heads_up_test.exe }
```

Expected: output includes `xiaoxin_notification_heads_up tests passed`.

- [ ] **Step 7: Commit**

```powershell
git add main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_notification_heads_up.h main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_notification_heads_up.c main\CMakeLists.txt tests\xiaoxin_notification_heads_up_test.c
git commit -m "feat: add xiaoxin notification heads-up queue"
```

---

## Frosted-Glass Visual Design（Task 3 视觉参考）

**目标：** Apple 浅色毛玻璃（frosted glass）风格。ESP32-S3 无 GPU，LVGL 9 无实时背景模糊——用四层静态叠加模拟。

### 变更总览

| 属性 | 旧值（深色玻璃） | 新值（浅色毛玻璃） |
|---|---|---|
| 底色 | `0x111827` | `0xF4F6F9` |
| 底色透明度 | `LV_OPA_90` (35%) | `static_cast<lv_opa_t>(180)` (70%) |
| 渐变 | 无 | 顶部 `0xF8FAFB` → 底部 `0xEDF0F5` |
| 圆角 | 16 | 22 |
| 尺寸 | 250×58 | 不变 |
| 上边缘位置 | `y=18` | 不变 |
| 标题颜色 | `0xFFFFFF` | `0x111827` |
| 正文颜色 | `0xCBD5E1` | `0x4A5568` |
| 标签 | 纯文字 `0x9BE7FF` | 小胶囊 `0x3182CE` 底色 15% + 蓝色文字 |
| 边框 | 四边 `0xFFFFFF` 20% | 上/左 `0xFFFFFF` 60% + 下/右 `0xCDD3DC` 25% |

### 四层结构

```
┌─ 第 3 层: 噪点纹理 ────────────────┐ ← lv_image, I8 灰度, 透明度 12%
│  ┌─ 第 2 层: 边缘光泽 ──────────┐  │
│  │  ┌─ 第 1 层: 渐变面板 ───┐  │  │
│  │  │  [语音] 标题           │  │  │
│  │  │  正文                  │  │  │
│  │  └───────────────────────┘  │  │
│  │   ↑上/左白边     ↓下/右暗边   │  │
│  └─────────────────────────────┘  │
└────────────────────────────────────┘
     第 4 层: 弥散阴影 (shadow, 黑色 4%, 宽 20px, y 偏移 3px)
```

**第 1 层（渐变面板）：** `0xF4F6F9` @ 70% + 垂直渐变 `0xF8FAFB→0xEDF0F5`，渐变在底部 50% 区域生效（`bg_main_stop=0`, `bg_grad_stop=128`），圆角 22。

**第 2 层（rim light）：** 两个叠加矩形，各设不同边框侧。`border_side` 不可用时回退为四侧同色（视觉差异极小）。

**第 3 层（噪点纹理）：** 250×58 I8 灰度图，Perlin 噪点 + 高斯模糊，中灰基底 + 微小偏移，`lv_image` 叠在面板上方 12% 透明度。Python 脚本生成 PNG → LVGL 在线转换器转 C 数组（~14.5KB）。

**第 4 层（阴影）：** `0x000000` @ 4%，宽度 20px，向下偏移 3px。

### 标签胶囊

标签从纯文字改为 `lv_obj_create` 小胶囊：底色 `0x3182CE` @ 15%，圆角 6，水平内边距 6，垂直 2，无边线。标签文字作为子 `lv_label`，颜色 `0x3182CE`。布局上标题和正文从 `x=56` 改为 `x=48`。

### 不变部分

`xiaoxin_card_pager` / `xiaoxin_notification_heads_up` / `display.h` / `display.cc` / `application.cc` —— 零改动。纯渲染层变更。

### 风险

- `lv_obj_set_style_border_side()` 可能不存在于旧版 LVGL 9 → 回退为四侧同色
- `bg_grad` API 小版本差异 → 实现时验证 `LV_GRAD_DIR_VER` / `bg_main_stop` / `bg_grad_stop`
- 纹理转换依赖在线工具 → 备选：构建流程中集成 Python 脚本直接输出 I8 二进制 C 数组

---

### Task 3: Render the Frosted-Glass Heads-Up Banner

**Files:**
- Create: `tools/generate_glass_texture.py`
- Create: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_notification_heads_up_glass_texture.c`
- Modify: `main/CMakeLists.txt`
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Test: `tests/xiaoxin_notification_visual_path_test.py`

**Interfaces:**
- Consumes: `xiaoxin_notification_heads_up_t`, `xiaoxin_notification_heads_up_snapshot_t`
- Produces:
  - `notification_heads_up_layer_`
  - `notification_heads_up_rim_top_left_`, `notification_heads_up_rim_bottom_right_`
  - `notification_heads_up_texture_overlay_`
  - `notification_heads_up_tag_capsule_`, `notification_heads_up_tag_label_`
  - `notification_heads_up_title_label_`
  - `notification_heads_up_body_label_`
  - `notification_heads_up_model_`
  - `RefreshNotificationHeadsUpLocked()`
  - Colors: glass panel `0xF4F6F9` @ 70% + vertical gradient `0xF8FAFB` → `0xEDF0F5`, rim top/left `0xFFFFFF` @ 60%, rim bottom/right `0xCDD3DC` @ 25%, title `0x111827`, body `0x4A5568`, tag `0x3182CE`

- [ ] **Step 1: Generate the glass texture Python script**

Create `tools/generate_glass_texture.py`:

```python
"""Generate a 250×58 grayscale noise texture for frosted-glass surface simulation."""
import numpy as np
from pathlib import Path

W, H = 250, 58
OUT = Path(__file__).resolve().parents[1] / "main" / "boards" / "waveshare" / "esp32-s3-touch-lcd-1.46" / "glass_texture.png"

def gaussian_kernel(size: int, sigma: float) -> np.ndarray:
    ax = np.arange(-size // 2 + 1., size // 2 + 1.)
    xx, yy = np.meshgrid(ax, ax)
    kernel = np.exp(-(xx**2 + yy**2) / (2. * sigma**2))
    return kernel / kernel.sum()

def convolve2d(img: np.ndarray, kernel: np.ndarray) -> np.ndarray:
    kh, kw = kernel.shape
    pad_h, pad_w = kh // 2, kw // 2
    padded = np.pad(img.astype(np.float32), ((pad_h, pad_h), (pad_w, pad_w)), mode='reflect')
    result = np.zeros_like(img, dtype=np.float32)
    for y in range(img.shape[0]):
        for x in range(img.shape[1]):
            result[y, x] = np.sum(padded[y:y+kh, x:x+kw] * kernel)
    return result

def main() -> None:
    np.random.seed(42)
    noise = np.random.randint(0, 255, (H, W), dtype=np.uint8).astype(np.float32)
    kernel = gaussian_kernel(5, sigma=0.8)
    blurred = convolve2d(noise, kernel)
    amplitude = 10.0
    centered = 128.0 + (blurred - 128.0) * (amplitude / 128.0)
    result = np.clip(centered, 118, 138).astype(np.uint8)

    from PIL import Image
    img = Image.fromarray(result, mode='L')
    OUT.parent.mkdir(parents=True, exist_ok=True)
    img.save(str(OUT))
    print(f"Glass texture written to {OUT}  ({W}x{H}, 8-bit grayscale)")

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run the script and convert to LVGL C array**

Run:

```powershell
pip install pillow numpy
python tools/generate_glass_texture.py
```

Expected: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/glass_texture.png` created (250×58, 8-bit grayscale).

Then convert the PNG to an LVGL I8 C array. Use LVGL's online image converter at `https://lvgl.io/tools/imageconverter` with these settings:
- Color format: `I8` (8-bit grayscale, no alpha)
- Output format: C array
- Dithering: off
- Name: `xiaoxin_heads_up_glass_texture`

Save the output as `main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_notification_heads_up_glass_texture.c`. The file will be roughly 14.5 KB.

If the online converter is unavailable, fall back to embedding the raw PNG bytes and using LVGL's runtime PNG decoder (`lv_image_set_src(img, "A:glass_texture.png")`) if the board's LVGL build includes PNG support. Document which path was taken.

- [ ] **Step 3: Compile the texture in the firmware**

In `main/CMakeLists.txt`, add the texture source next to `xiaoxin_notification_heads_up.c`:

```cmake
        ${CMAKE_CURRENT_SOURCE_DIR}/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_notification_heads_up_glass_texture.c
```

- [ ] **Step 4: Write the failing visual path test**

Add to `tests/xiaoxin_notification_visual_path_test.py`:

```python
def test_notification_heads_up_uses_frosted_glass_banner_visuals():
    source = read_source()
    init_body = function_body(source, "void InitializeNotificationHeadsUpLayerLocked()")
    raise_body = function_body(source, "void RaiseOverlayObjects()")
    foreground_body = function_body(source, "void RaiseNotificationHeadsUpLayerLocked()")
    show_body = function_body(source, "void ShowNotificationHeadsUpLocked()")
    hide_body = function_body(source, "void HideNotificationHeadsUpLocked()")

    # Model and fields
    assert '#include "xiaoxin_notification_heads_up.h"' in source
    assert "xiaoxin_notification_heads_up_t notification_heads_up_model_ = {};" in source

    # Top-level objects
    assert "static constexpr int16_t k_notification_heads_up_hidden_y = -70;" in source
    assert "static constexpr int16_t k_notification_heads_up_visible_y = 18;" in source
    assert "lv_obj_t* notification_heads_up_layer_ = nullptr;" in source
    assert "lv_obj_t* notification_heads_up_title_label_ = nullptr;" in source
    assert "lv_obj_t* notification_heads_up_body_label_ = nullptr;" in source
    assert "lv_obj_t* notification_heads_up_tag_label_ = nullptr;" in source

    # Glass layers
    assert "lv_obj_t* notification_heads_up_rim_top_left_ = nullptr;" in source
    assert "lv_obj_t* notification_heads_up_rim_bottom_right_ = nullptr;" in source
    assert "lv_obj_t* notification_heads_up_texture_overlay_ = nullptr;" in source
    assert "lv_obj_t* notification_heads_up_tag_capsule_ = nullptr;" in source
    assert '#include "xiaoxin_notification_heads_up_glass_texture.c"' in source

    # Glass panel styling
    assert "lv_obj_set_size(notification_heads_up_layer_, 250, 58);" in init_body
    assert "lv_obj_set_style_radius(notification_heads_up_layer_, 22, 0);" in init_body
    assert "lv_obj_set_style_bg_color(notification_heads_up_layer_, lv_color_hex(0xF4F6F9), 0);" in init_body
    assert "lv_obj_set_style_bg_opa(notification_heads_up_layer_, static_cast<lv_opa_t>(180), 0);" in init_body
    assert "lv_obj_set_style_bg_grad_color(notification_heads_up_layer_, lv_color_hex(0xEDF0F5), 0);" in init_body
    assert "lv_obj_set_style_bg_grad_dir(notification_heads_up_layer_, LV_GRAD_DIR_VER, 0);" in init_body
    assert "lv_obj_align(notification_heads_up_layer_, LV_ALIGN_TOP_MID, 0, k_notification_heads_up_hidden_y);" in init_body

    # Rim light edges
    assert "lv_obj_set_style_border_color(notification_heads_up_rim_top_left_, lv_color_hex(0xFFFFFF), 0);" in init_body
    assert "lv_obj_set_style_border_opa(notification_heads_up_rim_top_left_, static_cast<lv_opa_t>(153), 0);" in init_body
    assert "lv_obj_set_style_border_side(notification_heads_up_rim_top_left_, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0);" in init_body
    assert "lv_obj_set_style_border_color(notification_heads_up_rim_bottom_right_, lv_color_hex(0xCDD3DC), 0);" in init_body
    assert "lv_obj_set_style_border_opa(notification_heads_up_rim_bottom_right_, static_cast<lv_opa_t>(64), 0);" in init_body
    assert "lv_obj_set_style_border_side(notification_heads_up_rim_bottom_right_, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT, 0);" in init_body

    # Drop shadow
    assert "lv_obj_set_style_shadow_color(notification_heads_up_layer_, lv_color_hex(0x000000), 0);" in init_body
    assert "lv_obj_set_style_shadow_opa(notification_heads_up_layer_, LV_OPA_10, 0);" in init_body
    assert "lv_obj_set_style_shadow_width(notification_heads_up_layer_, 20, 0);" in init_body
    assert "lv_obj_set_style_shadow_ofs_y(notification_heads_up_layer_, 3, 0);" in init_body

    # Noise texture overlay
    assert "lv_image_create(notification_heads_up_layer_);" in init_body
    assert "xiaoxin_heads_up_glass_texture" in init_body
    assert "lv_obj_set_style_opa(notification_heads_up_texture_overlay_, static_cast<lv_opa_t>(30), 0);" in init_body

    # Tag capsule
    assert "lv_obj_set_style_bg_color(notification_heads_up_tag_capsule_, lv_color_hex(0x3182CE), 0);" in init_body
    assert "lv_obj_set_style_bg_opa(notification_heads_up_tag_capsule_, static_cast<lv_opa_t>(38), 0);" in init_body
    assert "lv_obj_set_style_radius(notification_heads_up_tag_capsule_, 6, 0);" in init_body

    # Text colors (dark on light glass)
    assert "lv_obj_set_style_text_color(notification_heads_up_title_label_, lv_color_hex(0x111827), 0);" in init_body
    assert "lv_obj_set_style_text_color(notification_heads_up_body_label_, lv_color_hex(0x4A5568), 0);" in init_body
    assert "lv_obj_set_style_text_color(notification_heads_up_tag_label_, lv_color_hex(0x3182CE), 0);" in init_body

    # Foreground in raise paths
    assert "RaiseNotificationHeadsUpLayerLocked();" in raise_body
    assert "lv_obj_move_foreground(notification_heads_up_layer_);" in foreground_body

    # Top-down motion
    assert "lv_anim_set_values(&anim, k_notification_heads_up_hidden_y, k_notification_heads_up_visible_y);" in show_body
    assert "lv_anim_set_values(&anim, k_notification_heads_up_visible_y, k_notification_heads_up_hidden_y);" in hide_body
    assert "lv_anim_set_exec_cb(&fade, NotificationHeadsUpSetOpa);" in show_body
    assert "lv_anim_set_exec_cb(&fade, NotificationHeadsUpSetOpa);" in hide_body
    assert "lv_anim_set_values(&fade, LV_OPA_TRANSP, LV_OPA_COVER);" in show_body
    assert "lv_anim_set_values(&fade, LV_OPA_COVER, LV_OPA_TRANSP);" in hide_body
```

- [ ] **Step 5: Run the path test and verify it fails**

Run:

```powershell
python -m pytest tests/xiaoxin_notification_visual_path_test.py -q
```

Expected: failure because `InitializeNotificationHeadsUpLayerLocked()` and the glass fields do not exist.

- [ ] **Step 6: Include the model, texture, and add fields**

At the top of `esp32-s3-touch-lcd-1.46.cc`, add:

```cpp
#include "xiaoxin_notification_heads_up.h"
#include "xiaoxin_notification_heads_up_glass_texture.c"
```

Near existing notification fields, add:

```cpp
    static constexpr int16_t k_notification_heads_up_hidden_y = -70;
    static constexpr int16_t k_notification_heads_up_visible_y = 18;
    lv_obj_t* notification_heads_up_layer_ = nullptr;
    lv_obj_t* notification_heads_up_rim_top_left_ = nullptr;
    lv_obj_t* notification_heads_up_rim_bottom_right_ = nullptr;
    lv_obj_t* notification_heads_up_texture_overlay_ = nullptr;
    lv_obj_t* notification_heads_up_tag_capsule_ = nullptr;
    lv_obj_t* notification_heads_up_tag_label_ = nullptr;
    lv_obj_t* notification_heads_up_title_label_ = nullptr;
    lv_obj_t* notification_heads_up_body_label_ = nullptr;
    xiaoxin_notification_heads_up_t notification_heads_up_model_ = {};
```

- [ ] **Step 7: Create the frosted-glass banner layer**

Add this method before `InitializeCardPagerLayer()`:

```cpp
    void InitializeNotificationHeadsUpLayerLocked() {
        lv_obj_t* screen = lv_screen_active();
        auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        const lv_font_t* text_font = lvgl_theme != nullptr && lvgl_theme->text_font() != nullptr
            ? lvgl_theme->text_font()->font()
            : nullptr;

        // --- Layer 1: gradient glass panel ---
        notification_heads_up_layer_ = lv_obj_create(screen);
        lv_obj_remove_style_all(notification_heads_up_layer_);
        lv_obj_set_size(notification_heads_up_layer_, 250, 58);
        lv_obj_set_style_radius(notification_heads_up_layer_, 22, 0);
        lv_obj_set_style_bg_color(notification_heads_up_layer_, lv_color_hex(0xF4F6F9), 0);
        lv_obj_set_style_bg_opa(notification_heads_up_layer_, static_cast<lv_opa_t>(180), 0);
        lv_obj_set_style_bg_grad_color(notification_heads_up_layer_, lv_color_hex(0xEDF0F5), 0);
        lv_obj_set_style_bg_grad_dir(notification_heads_up_layer_, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_main_stop(notification_heads_up_layer_, 0, 0);
        lv_obj_set_style_bg_grad_stop(notification_heads_up_layer_, 128, 0);
        lv_obj_set_style_border_width(notification_heads_up_layer_, 0, 0);
        lv_obj_clear_flag(notification_heads_up_layer_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(notification_heads_up_layer_, LV_ALIGN_TOP_MID, 0, k_notification_heads_up_hidden_y);
        lv_obj_set_style_opa(notification_heads_up_layer_, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(notification_heads_up_layer_, LV_OBJ_FLAG_HIDDEN);

        // --- Layer 2: rim light edges ---
        notification_heads_up_rim_top_left_ = lv_obj_create(notification_heads_up_layer_);
        lv_obj_remove_style_all(notification_heads_up_rim_top_left_);
        lv_obj_set_size(notification_heads_up_rim_top_left_, 250, 58);
        lv_obj_set_style_radius(notification_heads_up_rim_top_left_, 22, 0);
        lv_obj_set_style_bg_opa(notification_heads_up_rim_top_left_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(notification_heads_up_rim_top_left_, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_opa(notification_heads_up_rim_top_left_, static_cast<lv_opa_t>(153), 0);
        lv_obj_set_style_border_width(notification_heads_up_rim_top_left_, 1, 0);
        lv_obj_set_style_border_side(notification_heads_up_rim_top_left_, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0);
        lv_obj_clear_flag(notification_heads_up_rim_top_left_, LV_OBJ_FLAG_SCROLLABLE);

        notification_heads_up_rim_bottom_right_ = lv_obj_create(notification_heads_up_layer_);
        lv_obj_remove_style_all(notification_heads_up_rim_bottom_right_);
        lv_obj_set_size(notification_heads_up_rim_bottom_right_, 250, 58);
        lv_obj_set_style_radius(notification_heads_up_rim_bottom_right_, 22, 0);
        lv_obj_set_style_bg_opa(notification_heads_up_rim_bottom_right_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(notification_heads_up_rim_bottom_right_, lv_color_hex(0xCDD3DC), 0);
        lv_obj_set_style_border_opa(notification_heads_up_rim_bottom_right_, static_cast<lv_opa_t>(64), 0);
        lv_obj_set_style_border_width(notification_heads_up_rim_bottom_right_, 1, 0);
        lv_obj_set_style_border_side(notification_heads_up_rim_bottom_right_, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT, 0);
        lv_obj_clear_flag(notification_heads_up_rim_bottom_right_, LV_OBJ_FLAG_SCROLLABLE);

        // --- Layer 3: noise texture overlay ---
        notification_heads_up_texture_overlay_ = lv_image_create(notification_heads_up_layer_);
        lv_image_set_src(notification_heads_up_texture_overlay_, &xiaoxin_heads_up_glass_texture);
        lv_obj_set_size(notification_heads_up_texture_overlay_, 250, 58);
        lv_obj_set_style_opa(notification_heads_up_texture_overlay_, static_cast<lv_opa_t>(30), 0);
        lv_obj_align(notification_heads_up_texture_overlay_, LV_ALIGN_CENTER, 0, 0);

        // --- Layer 4: drop shadow ---
        lv_obj_set_style_shadow_color(notification_heads_up_layer_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(notification_heads_up_layer_, LV_OPA_10, 0);
        lv_obj_set_style_shadow_width(notification_heads_up_layer_, 20, 0);
        lv_obj_set_style_shadow_ofs_y(notification_heads_up_layer_, 3, 0);
        lv_obj_set_style_shadow_ofs_x(notification_heads_up_layer_, 0, 0);

        // --- Tag capsule ---
        notification_heads_up_tag_capsule_ = lv_obj_create(notification_heads_up_layer_);
        lv_obj_set_size(notification_heads_up_tag_capsule_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(notification_heads_up_tag_capsule_, lv_color_hex(0x3182CE), 0);
        lv_obj_set_style_bg_opa(notification_heads_up_tag_capsule_, static_cast<lv_opa_t>(38), 0);
        lv_obj_set_style_radius(notification_heads_up_tag_capsule_, 6, 0);
        lv_obj_set_style_pad_hor(notification_heads_up_tag_capsule_, 6, 0);
        lv_obj_set_style_pad_ver(notification_heads_up_tag_capsule_, 2, 0);
        lv_obj_set_style_border_width(notification_heads_up_tag_capsule_, 0, 0);
        lv_obj_align(notification_heads_up_tag_capsule_, LV_ALIGN_LEFT_MID, 8, 0);

        notification_heads_up_tag_label_ = lv_label_create(notification_heads_up_tag_capsule_);
        lv_obj_set_style_text_color(notification_heads_up_tag_label_, lv_color_hex(0x3182CE), 0);
        lv_obj_set_style_text_align(notification_heads_up_tag_label_, LV_TEXT_ALIGN_CENTER, 0);
        if (text_font != nullptr) {
            lv_obj_set_style_text_font(notification_heads_up_tag_label_, text_font, 0);
        }

        // --- Title label ---
        notification_heads_up_title_label_ = lv_label_create(notification_heads_up_layer_);
        lv_obj_set_width(notification_heads_up_title_label_, 180);
        lv_obj_set_style_text_color(notification_heads_up_title_label_, lv_color_hex(0x111827), 0);
        lv_obj_set_style_text_opa(notification_heads_up_title_label_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_align(notification_heads_up_title_label_, LV_TEXT_ALIGN_LEFT, 0);
        if (text_font != nullptr) {
            lv_obj_set_style_text_font(notification_heads_up_title_label_, text_font, 0);
        }
        lv_obj_align(notification_heads_up_title_label_, LV_ALIGN_TOP_LEFT, 48, 9);

        // --- Body label ---
        notification_heads_up_body_label_ = lv_label_create(notification_heads_up_layer_);
        lv_obj_set_width(notification_heads_up_body_label_, 180);
        lv_obj_set_style_text_color(notification_heads_up_body_label_, lv_color_hex(0x4A5568), 0);
        lv_obj_set_style_text_opa(notification_heads_up_body_label_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_align(notification_heads_up_body_label_, LV_TEXT_ALIGN_LEFT, 0);
        if (text_font != nullptr) {
            lv_obj_set_style_text_font(notification_heads_up_body_label_, text_font, 0);
        }
        lv_obj_align(notification_heads_up_body_label_, LV_ALIGN_TOP_LEFT, 48, 31);
    }
```

**Fallback note:** If `lv_obj_set_style_border_side()` is unavailable in the current ESP-IDF LVGL 9 build, omit it and set all 4 sides on both rim objects. The visual impact is negligible: a 0xCDD3DC line also appears on top/left at 25% opacity, barely visible on the light glass background.

- [ ] **Step 8: Initialize the model and layer from setup**

In the display setup path where `InitializeCardPagerLayer()` is called, add:

```cpp
        xiaoxin_notification_heads_up_init(&notification_heads_up_model_);
        InitializeNotificationHeadsUpLayerLocked();
```

- [ ] **Step 9: Keep the banner above every other layer**

Add a helper before `RaiseOverlayObjects()`:

```cpp
    void RaiseNotificationHeadsUpLayerLocked() {
        if (notification_heads_up_layer_ != nullptr) {
            lv_obj_move_foreground(notification_heads_up_layer_);
        }
    }
```

In `RaiseOverlayObjects()`, add `RaiseNotificationHeadsUpLayerLocked();` before the `return;` in the `IsCardLayerVisible()` branch, and add another call at the end of the non-card-layer path (same as the old dark-glass task).

- [ ] **Step 10: Add top-down show/hide animation helpers**

Add:

```cpp
    static void NotificationHeadsUpSetY(void* obj, int32_t y) {
        lv_obj_align(static_cast<lv_obj_t*>(obj), LV_ALIGN_TOP_MID, 0, (int16_t)y);
    }

    static void NotificationHeadsUpSetOpa(void* obj, int32_t opa) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), (lv_opa_t)opa, 0);
    }

    void ShowNotificationHeadsUpLocked() {
        if (notification_heads_up_layer_ == nullptr) {
            return;
        }
        lv_anim_del(notification_heads_up_layer_, NotificationHeadsUpSetY);
        lv_anim_del(notification_heads_up_layer_, NotificationHeadsUpSetOpa);
        lv_obj_clear_flag(notification_heads_up_layer_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(notification_heads_up_layer_);

        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, notification_heads_up_layer_);
        lv_anim_set_exec_cb(&anim, NotificationHeadsUpSetY);
        lv_anim_set_values(&anim, k_notification_heads_up_hidden_y, k_notification_heads_up_visible_y);
        lv_anim_set_time(&anim, 180);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_start(&anim);

        lv_anim_t fade;
        lv_anim_init(&fade);
        lv_anim_set_var(&fade, notification_heads_up_layer_);
        lv_anim_set_exec_cb(&fade, NotificationHeadsUpSetOpa);
        lv_anim_set_values(&fade, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_time(&fade, 180);
        lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);
        lv_anim_start(&fade);
    }

    void HideNotificationHeadsUpLocked() {
        if (notification_heads_up_layer_ == nullptr) {
            return;
        }
        lv_anim_del(notification_heads_up_layer_, NotificationHeadsUpSetY);
        lv_anim_del(notification_heads_up_layer_, NotificationHeadsUpSetOpa);

        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, notification_heads_up_layer_);
        lv_anim_set_exec_cb(&anim, NotificationHeadsUpSetY);
        lv_anim_set_values(&anim, k_notification_heads_up_visible_y, k_notification_heads_up_hidden_y);
        lv_anim_set_time(&anim, 160);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
        lv_anim_start(&anim);

        lv_anim_t fade;
        lv_anim_init(&fade);
        lv_anim_set_var(&fade, notification_heads_up_layer_);
        lv_anim_set_exec_cb(&fade, NotificationHeadsUpSetOpa);
        lv_anim_set_values(&fade, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_time(&fade, 160);
        lv_anim_set_path_cb(&fade, lv_anim_path_ease_in);
        lv_anim_start(&fade);
    }
```

Implementation note: this first version does not wait for the fade-out completion before keeping the object in the tree; it becomes transparent and off-screen, and the next show call restarts both animations.

- [ ] **Step 11: Add rendering refresh**

Add:

```cpp
    void RefreshNotificationHeadsUpLocked() {
        if (notification_heads_up_layer_ == nullptr) {
            return;
        }

        xiaoxin_notification_heads_up_snapshot_t snapshot = {};
        if (!xiaoxin_notification_heads_up_snapshot(&notification_heads_up_model_, &snapshot)) {
            HideNotificationHeadsUpLocked();
            return;
        }

        lv_label_set_text(notification_heads_up_title_label_, snapshot.title);
        lv_label_set_text(notification_heads_up_body_label_, snapshot.body);
        lv_label_set_text(notification_heads_up_tag_label_, snapshot.tag != nullptr && snapshot.tag[0] != '\0' ? snapshot.tag : "通知");
        ShowNotificationHeadsUpLocked();
    }
```

- [ ] **Step 12: Run the path test and verify it passes**

Run:

```powershell
python -m pytest tests/xiaoxin_notification_visual_path_test.py -q
```

Expected: `test_notification_heads_up_uses_frosted_glass_banner_visuals` and all other tests in the file pass.

- [ ] **Step 13: Commit**

```powershell
git add tools\generate_glass_texture.py main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_notification_heads_up_glass_texture.c main\boards\waveshare\esp32-s3-touch-lcd-1.46\esp32-s3-touch-lcd-1.46.cc main\CMakeLists.txt tests\xiaoxin_notification_visual_path_test.py
git commit -m "feat: render xiaoxin notification heads-up frosted-glass banner"
```

---

### Task 4: Enqueue Heads-Up and Tick TTL at Runtime

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Test: `tests/xiaoxin_notification_visual_path_test.py`

**Interfaces:**
- Consumes:
  - `xiaoxin_card_pager_notification_upsert_event_at()`
  - `xiaoxin_card_pager_notification_find_by_type()`
  - `xiaoxin_card_pager_notification_expire()`
  - `xiaoxin_notification_heads_up_enqueue()`
  - `xiaoxin_notification_heads_up_tick()`
- Produces:
  - `notification_maintenance_timer_`
  - `RefreshNotificationsFromTimer()`
  - `StartNotificationMaintenanceTimer()`
  - `StopNotificationMaintenanceTimer()`

- [ ] **Step 1: Write the failing runtime path test**

Add:

```python
def test_notification_upsert_enqueues_heads_up_and_ttl_maintenance():
    source = read_source()
    upsert_body = function_body(source, "void UpsertNotificationEventLocked(const xiaoxin_notification_event_t& event)")
    timer_body = function_body(source, "void RefreshNotificationsFromTimer()")

    assert "esp_timer_handle_t notification_maintenance_timer_ = nullptr;" in source
    assert "xiaoxin_card_pager_notification_upsert_event_at(&card_pager_, &event, NowMs())" in upsert_body
    assert "xiaoxin_card_pager_notification_find_by_type(&card_pager_, event.type)" in upsert_body
    assert "xiaoxin_notification_heads_up_enqueue(&notification_heads_up_model_, item, NowMs())" in upsert_body
    assert "StartNotificationMaintenanceTimer();" in upsert_body
    assert "xiaoxin_card_pager_notification_expire(&card_pager_, NowMs())" in timer_body
    assert "xiaoxin_notification_heads_up_tick(&notification_heads_up_model_, NowMs())" in timer_body
    assert "RefreshNotificationHeadsUpLocked();" in timer_body
```

- [ ] **Step 2: Run the path test and verify it fails**

Run:

```powershell
python -m pytest tests/xiaoxin_notification_visual_path_test.py -q
```

Expected: failure because maintenance timer and enqueue calls do not exist.

- [ ] **Step 3: Add the maintenance timer field**

Near `low_power_clock_timer_`, add:

```cpp
    esp_timer_handle_t notification_maintenance_timer_ = nullptr;
```

- [ ] **Step 4: Update upsert to enqueue heads-up**

Replace `UpsertNotificationEventLocked()` body with:

```cpp
    void UpsertNotificationEventLocked(const xiaoxin_notification_event_t& event) {
        if (xiaoxin_card_pager_notification_upsert_event_at(&card_pager_, &event, NowMs())) {
            notification_scroll_y_ = ClampNotificationScrollY(
                notification_scroll_y_,
                xiaoxin_card_pager_notification_count(&card_pager_)
            );
            const xiaoxin_card_item_t* item =
                xiaoxin_card_pager_notification_find_by_type(&card_pager_, event.type);
            if (item != nullptr) {
                xiaoxin_notification_heads_up_enqueue(&notification_heads_up_model_, item, NowMs());
                RefreshNotificationHeadsUpLocked();
                StartNotificationMaintenanceTimer();
            }
            RefreshNotificationPageIfVisibleLocked();
        }
    }
```

- [ ] **Step 5: Add timer callbacks**

Add methods near the low-power clock timer methods:

```cpp
    static void NotificationMaintenanceTimerCallback(void* arg) {
        auto* self = static_cast<PaopaoPetDisplay*>(arg);
        if (self != nullptr) {
            self->RefreshNotificationsFromTimer();
        }
    }

    void RefreshNotificationsFromTimer() {
        DisplayLockGuard lock(this);
        const uint8_t removed = xiaoxin_card_pager_notification_expire(&card_pager_, NowMs());
        const bool heads_up_visible = xiaoxin_notification_heads_up_tick(&notification_heads_up_model_, NowMs());
        if (removed > 0) {
            notification_scroll_y_ = ClampNotificationScrollY(
                notification_scroll_y_,
                xiaoxin_card_pager_notification_count(&card_pager_)
            );
            RefreshNotificationPageIfVisibleLocked();
        }
        RefreshNotificationHeadsUpLocked();
        if (!heads_up_visible && xiaoxin_card_pager_notification_count(&card_pager_) == 0) {
            StopNotificationMaintenanceTimer();
        }
    }

    void EnsureNotificationMaintenanceTimer() {
        if (notification_maintenance_timer_ != nullptr) {
            return;
        }
        const esp_timer_create_args_t timer_args = {
            .callback = &NotificationMaintenanceTimerCallback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "xiaoxin_notify",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &notification_maintenance_timer_));
    }

    void StartNotificationMaintenanceTimer() {
        EnsureNotificationMaintenanceTimer();
        if (notification_maintenance_timer_ != nullptr && !esp_timer_is_active(notification_maintenance_timer_)) {
            ESP_ERROR_CHECK(esp_timer_start_periodic(notification_maintenance_timer_, 250 * 1000));
        }
    }

    void StopNotificationMaintenanceTimer() {
        if (notification_maintenance_timer_ != nullptr && esp_timer_is_active(notification_maintenance_timer_)) {
            ESP_ERROR_CHECK(esp_timer_stop(notification_maintenance_timer_));
        }
    }
```

- [ ] **Step 6: Keep maintenance running for TTL notifications**

In `UpsertNotificationEventLocked()`, keep `StartNotificationMaintenanceTimer();` inside the successful upsert branch exactly as shown. This ensures short TTL notifications expire even if the heads-up banner already hides.

- [ ] **Step 7: Run tests and verify they pass**

Run:

```powershell
python -m pytest tests/xiaoxin_notification_visual_path_test.py -q
gcc tests\xiaoxin_card_pager_test.c main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_card_pager.c -I main\boards\waveshare\esp32-s3-touch-lcd-1.46 -o build\xiaoxin_card_pager_test.exe; if ($LASTEXITCODE -eq 0) { .\build\xiaoxin_card_pager_test.exe }
gcc tests\xiaoxin_notification_heads_up_test.c main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_notification_heads_up.c -I main\boards\waveshare\esp32-s3-touch-lcd-1.46 -o build\xiaoxin_notification_heads_up_test.exe; if ($LASTEXITCODE -eq 0) { .\build\xiaoxin_notification_heads_up_test.exe }
```

Expected: pytest passes, both C tests print their success messages.

- [ ] **Step 8: Commit**

```powershell
git add main\boards\waveshare\esp32-s3-touch-lcd-1.46\esp32-s3-touch-lcd-1.46.cc tests\xiaoxin_notification_visual_path_test.py
git commit -m "feat: enqueue xiaoxin heads-up notifications"
```

---

### Task 5: Add a Generic Display Notification Bridge for OTA

**Files:**
- Modify: `main/display/display.h`
- Modify: `main/display/display.cc`
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Test: `tests/xiaoxin_notification_visual_path_test.py`

**Interfaces:**
- Produces:
  - `virtual bool UpsertNotification(const char* id, const char* title, const char* body, const char* tag, uint32_t priority, uint32_t ttl_ms);`
  - `virtual bool RemoveNotification(const char* id);`
- Consumes existing `PaopaoPetDisplay::RemoveNotificationEventLocked(xiaoxin_notification_event_type_t type)` from the board display file.

- [ ] **Step 1: Write the failing path test**

Add:

```python
def test_display_notification_bridge_maps_ota_to_xiaoxin_notification_center():
    display_header = Path("main/display/display.h").read_text(encoding="utf-8")
    source = read_source()

    assert "virtual bool UpsertNotification(" in display_header
    assert "virtual bool RemoveNotification(" in display_header
    assert 'strcmp(id, "ota_update") == 0' in source
    assert "XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE" in source
    assert "UpsertNotificationEventLocked(event);" in source
```

- [ ] **Step 2: Run the path test and verify it fails**

Run:

```powershell
python -m pytest tests/xiaoxin_notification_visual_path_test.py -q
```

Expected: failure because the bridge does not exist.

- [ ] **Step 3: Add the virtual bridge to Display**

In `main/display/display.h`, add after `ShowNotification()`:

```cpp
    virtual bool UpsertNotification(
        const char* id,
        const char* title,
        const char* body,
        const char* tag,
        uint32_t priority,
        uint32_t ttl_ms
    );
    virtual bool RemoveNotification(const char* id);
```

In `main/display/display.cc`, add:

```cpp
bool Display::UpsertNotification(
    const char* id,
    const char* title,
    const char* body,
    const char* tag,
    uint32_t priority,
    uint32_t ttl_ms
) {
    ESP_LOGW(
        TAG,
        "UpsertNotification ignored: id=%s title=%s body=%s tag=%s priority=%lu ttl=%lu",
        id != nullptr ? id : "",
        title != nullptr ? title : "",
        body != nullptr ? body : "",
        tag != nullptr ? tag : "",
        (unsigned long)priority,
        (unsigned long)ttl_ms
    );
    return false;
}

bool Display::RemoveNotification(const char* id) {
    ESP_LOGW(TAG, "RemoveNotification ignored: id=%s", id != nullptr ? id : "");
    return false;
}
```

- [ ] **Step 4: Override the bridge in the Xiaoxin display class**

In `PaopaoPetDisplay`, add:

```cpp
    bool UpsertNotification(
        const char* id,
        const char* title,
        const char* body,
        const char* tag,
        uint32_t priority,
        uint32_t ttl_ms
    ) override {
        DisplayLockGuard lock(this);
        if (id == nullptr || strcmp(id, "ota_update") != 0) {
            return false;
        }
        const xiaoxin_notification_event_t event = {
            .type = XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE,
            .title = title,
            .body = body,
            .tag = tag,
            .priority = priority,
            .ttl_ms = ttl_ms,
        };
        UpsertNotificationEventLocked(event);
        return true;
    }

    bool RemoveNotification(const char* id) override {
        DisplayLockGuard lock(this);
        if (id == nullptr || strcmp(id, "ota_update") != 0) {
            return false;
        }
        RemoveNotificationEventLocked(XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE);
        return true;
    }
```

- [ ] **Step 5: Run the path test and verify it passes**

Run:

```powershell
python -m pytest tests/xiaoxin_notification_visual_path_test.py -q
```

Expected: all tests in the file pass.

- [ ] **Step 6: Commit**

```powershell
git add main\display\display.h main\display\display.cc main\boards\waveshare\esp32-s3-touch-lcd-1.46\esp32-s3-touch-lcd-1.46.cc tests\xiaoxin_notification_visual_path_test.py
git commit -m "feat: add display notification bridge"
```

---

### Task 6: Connect OTA States to Notifications

**Files:**
- Modify: `main/application.cc`
- Test: `tests/ota_url_config_test.py`

**Interfaces:**
- Consumes: `Display::UpsertNotification()` and `Display::RemoveNotification()`
- Produces OTA calls with id `"ota_update"`

- [ ] **Step 1: Write failing OTA path assertions**

Add to `tests/ota_url_config_test.py`:

```python
def test_application_reports_ota_states_to_display_notifications(repo_root):
    source = (repo_root / "main" / "application.cc").read_text(encoding="utf-8")

    assert 'display->UpsertNotification("ota_update", "OTA 更新", "发现新版本", "系统", 4, 0);' in source
    assert 'display->UpsertNotification("ota_update", "OTA 更新", "正在下载并安装更新", "系统", 4, 0);' in source
    assert 'display->UpsertNotification("ota_update", "OTA 更新", "升级失败，请稍后重试", "系统", 4, 0);' in source
    assert 'display->RemoveNotification("ota_update");' in source
```

- [ ] **Step 2: Run the OTA test and verify it fails**

Run:

```powershell
python -m pytest tests/ota_url_config_test.py -q
```

Expected: failure because OTA notification bridge calls do not exist.

- [ ] **Step 3: Add new-version notification**

In `Application::CheckNewVersion()`, inside:

```cpp
        if (ota_->HasNewVersion()) {
```

add before `UpgradeFirmware(...)`:

```cpp
            display->UpsertNotification("ota_update", "OTA 更新", "发现新版本", "系统", 4, 0);
```

- [ ] **Step 4: Add upgrade-start notification**

In `Application::UpgradeFirmware()`, before the existing `Alert(Lang::Strings::OTA_UPGRADE, ...)`, add:

```cpp
    display->UpsertNotification("ota_update", "OTA 更新", "正在下载并安装更新", "系统", 4, 0);
```

- [ ] **Step 5: Add failure and success cleanup**

In the `if (!upgrade_success)` branch, before `Alert(...)`, add:

```cpp
        display->UpsertNotification("ota_update", "OTA 更新", "升级失败，请稍后重试", "系统", 4, 0);
```

In the success branch before `Reboot();`, add:

```cpp
        display->RemoveNotification("ota_update");
```

- [ ] **Step 6: Run the OTA test and verify it passes**

Run:

```powershell
python -m pytest tests/ota_url_config_test.py -q
```

Expected: all tests in the file pass.

- [ ] **Step 7: Commit**

```powershell
git add main\application.cc tests\ota_url_config_test.py
git commit -m "feat: notify xiaoxin ota status"
```

---

### Task 7: Final Verification and Plan Hygiene

**Files:**
- Verify: `docs/visualization/xiaoxin-feature-map.yaml`
- Verify: `tests/xiaoxin_docs_visualization_test.py`
- Verify: all files touched in Tasks 1-6

**Interfaces:**
- Consumes: all previous task outputs
- Produces: focused evidence that notification heads-up, TTL, OTA bridge, and visualization docs are internally consistent

- [ ] **Step 1: Confirm visualization docs already link this plan**

Do not create new YAML/test diffs for the plan link. The planning branch already added this plan path to `docs/visualization/xiaoxin-feature-map.yaml` and `tests/xiaoxin_docs_visualization_test.py`.

Run:

```powershell
rg -n "2026-06-24-xiaoxin-notification-heads-up.md" docs\visualization\xiaoxin-feature-map.yaml tests\xiaoxin_docs_visualization_test.py
```

Expected: both files contain `../superpowers/plans/2026-06-24-xiaoxin-notification-heads-up.md`.

- [ ] **Step 2: Run visualization verification**

Run:

```powershell
python -m pytest tests/xiaoxin_docs_visualization_test.py -q
```

Expected: all tests in the file pass.

- [ ] **Step 3: Run focused implementation verification**

Run:

```powershell
python -m pytest tests\xiaoxin_docs_visualization_test.py tests\xiaoxin_notification_visual_path_test.py tests\ota_url_config_test.py -q
gcc tests\xiaoxin_card_pager_test.c main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_card_pager.c -I main\boards\waveshare\esp32-s3-touch-lcd-1.46 -o build\xiaoxin_card_pager_test.exe; if ($LASTEXITCODE -eq 0) { .\build\xiaoxin_card_pager_test.exe }
gcc tests\xiaoxin_notification_heads_up_test.c main\boards\waveshare\esp32-s3-touch-lcd-1.46\xiaoxin_notification_heads_up.c -I main\boards\waveshare\esp32-s3-touch-lcd-1.46 -o build\xiaoxin_notification_heads_up_test.exe; if ($LASTEXITCODE -eq 0) { .\build\xiaoxin_notification_heads_up_test.exe }
git diff --check
```

Expected: pytest passes, both C tests print success messages, and `git diff --check` exits `0`.

- [ ] **Step 4: Attempt firmware build only if ESP-IDF is available**

Run:

```powershell
idf.py build
```

Expected when ESP-IDF is configured: build exits `0`.

Expected in a plain PowerShell environment: `idf.py` may not be recognized. If that happens, report the environment limitation and do not claim firmware build success.

- [ ] **Step 5: Commit final verification/docs state if any tracked files changed**

```powershell
git status --short
git add docs\visualization\xiaoxin-feature-map.yaml docs\visualization\README.zh-CN.md tests\xiaoxin_docs_visualization_test.py docs\superpowers\plans\2026-06-24-xiaoxin-notification-heads-up.md
git commit -m "docs: finalize xiaoxin notification heads-up plan"
```

---

## Self-Review

- Spec coverage: Tasks 2-4 cover the unified visual top banner for every notification, with frosted-glass styling (gradient panel, rim light, noise texture, capsule tag); Task 1 covers TTL cleanup; Tasks 5-6 cover OTA status injection; Task 7 verifies the already-linked YAML visualization and final focused test set.
- Non-goals preserved: No task adds sound, vibration, speech playback, or notification card time display.
- Type consistency: `xiaoxin_notification_heads_up_t`, `xiaoxin_notification_heads_up_snapshot_t`, `UpsertNotification`, `RemoveNotification`, `xiaoxin_card_pager_notification_upsert_event_at`, and `xiaoxin_card_pager_notification_expire` are defined before later tasks consume them.
- Risk note: `esp32-s3-touch-lcd-1.46.cc` remains large, but the queue and TTL rules live in small C modules; LVGL work is limited to object creation, rendering, timer dispatch, and 4-layer glass compositing. The noise texture is ~14.5 KB compiled. If `lv_obj_set_style_border_side()` is unavailable, the rim light fallback adds a negligible extra border line.
