#include "xiaoxin_card_pager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int16_t k_min_vertical_drag_px = 6;
static const int16_t k_return_home_threshold_percent = 8;

typedef struct {
  xiaoxin_notification_event_type_t type;
  const char* title;
  const char* body;
  const char* tag;
  uint32_t priority;
  uint32_t ttl_ms;
} notification_event_defaults_t;

static const notification_event_defaults_t k_notification_defaults[] = {
  {XIAOXIN_NOTIFICATION_EVENT_REMINDER, "上课提醒", "有一节课快开始了", "课程", 1, 0},
  {XIAOXIN_NOTIFICATION_EVENT_TODO_REMINDER, "待办提醒", "有一项待办需要处理", "待办", 2, 0},
  {XIAOXIN_NOTIFICATION_EVENT_LOW_BATTERY, "低电量", "电量偏低，请尽快充电", "电量", 2, 0},
  {XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED, "WiFi 断开", "WiFi 已断开，请检查网络", "网络", 3, 0},
  {XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE, "OTA 更新", "发现新固件", "系统", 4, 0},
  {XIAOXIN_NOTIFICATION_EVENT_VOICE_RECOGNITION_FAILED, "语音识别失败", "没听清，请再说一次", "语音", 5, 8000},
};

static const notification_event_defaults_t* notification_defaults_for(
  xiaoxin_notification_event_type_t type
) {
  for (uint8_t i = 0; i < (uint8_t)(sizeof(k_notification_defaults) / sizeof(k_notification_defaults[0])); ++i) {
    if (k_notification_defaults[i].type == type) {
      return &k_notification_defaults[i];
    }
  }
  return NULL;
}

static void copy_text(char* dest, size_t dest_size, const char* text) {
  if (dest == NULL || dest_size == 0) {
    return;
  }
  if (text == NULL) {
    dest[0] = '\0';
    return;
  }
  snprintf(dest, dest_size, "%s", text);
}

static bool notification_body_contains_percent(const char* body) {
  if (body == NULL) {
    return false;
  }

  return strchr(body, '%') != NULL || strstr(body, "％") != NULL;
}

static void notification_rebind_slot(xiaoxin_card_pager_t* pager, uint8_t slot) {
  if (pager == NULL || slot >= XIAOXIN_CARD_NOTIFICATION_MAX) {
    return;
  }
  pager->notification_items[slot].title = pager->notification_title_storage[slot];
  pager->notification_items[slot].body = pager->notification_body_storage[slot];
  pager->notification_items[slot].detail = NULL;
  pager->notification_items[slot].tag = pager->notification_tag_storage[slot];
}

static void notification_clear_slot(xiaoxin_card_pager_t* pager, uint8_t slot) {
  if (pager == NULL || slot >= XIAOXIN_CARD_NOTIFICATION_MAX) {
    return;
  }
  pager->notification_types[slot] = XIAOXIN_NOTIFICATION_EVENT_LOW_BATTERY;
  pager->notification_id_storage[slot][0] = '\0';
  pager->notification_expires_at_ms[slot] = 0;
  pager->notification_title_storage[slot][0] = '\0';
  pager->notification_body_storage[slot][0] = '\0';
  pager->notification_tag_storage[slot][0] = '\0';
  pager->notification_items[slot].priority = 0;
  pager->notification_items[slot].ttl_ms = 0;
  notification_rebind_slot(pager, slot);
}

static int8_t notification_index_for_type(
  const xiaoxin_card_pager_t* pager,
  xiaoxin_notification_event_type_t type
) {
  if (pager == NULL) {
    return -1;
  }
  for (uint8_t i = 0; i < pager->notification_count; ++i) {
    if (pager->notification_types[i] == type) {
      return (int8_t)i;
    }
  }
  return -1;
}

static int8_t notification_index_for_id(
  const xiaoxin_card_pager_t* pager,
  const char* id
) {
  if (pager == NULL || id == NULL || id[0] == '\0') {
    return -1;
  }
  for (uint8_t i = 0; i < pager->notification_count; ++i) {
    if (strcmp(pager->notification_id_storage[i], id) == 0) {
      return (int8_t)i;
    }
  }
  return -1;
}

static int64_t notification_expiry_from_ttl(uint32_t ttl_ms, int64_t now_ms) {
  if (ttl_ms == 0) {
    return 0;
  }
  return now_ms + (int64_t)ttl_ms;
}

static void clamp_notification_index(xiaoxin_card_pager_t* pager) {
  if (pager == NULL) {
    return;
  }
  if (pager->notification_count == 0) {
    pager->notification_index = 0;
  } else if (pager->notification_index >= pager->notification_count) {
    pager->notification_index = (uint8_t)(pager->notification_count - 1);
  }
}

static void notification_swap_slots(xiaoxin_card_pager_t* pager, uint8_t a, uint8_t b) {
  if (pager == NULL || a == b || a >= XIAOXIN_CARD_NOTIFICATION_MAX || b >= XIAOXIN_CARD_NOTIFICATION_MAX) {
    return;
  }

  const xiaoxin_notification_event_type_t type_tmp = pager->notification_types[a];
  pager->notification_types[a] = pager->notification_types[b];
  pager->notification_types[b] = type_tmp;

  char id_tmp[XIAOXIN_CARD_NOTIFICATION_ID_MAX];
  memcpy(id_tmp, pager->notification_id_storage[a], sizeof(id_tmp));
  memcpy(pager->notification_id_storage[a], pager->notification_id_storage[b], sizeof(id_tmp));
  memcpy(pager->notification_id_storage[b], id_tmp, sizeof(id_tmp));

  const int64_t expires_tmp = pager->notification_expires_at_ms[a];
  pager->notification_expires_at_ms[a] = pager->notification_expires_at_ms[b];
  pager->notification_expires_at_ms[b] = expires_tmp;

  const xiaoxin_card_item_t item_tmp = pager->notification_items[a];
  pager->notification_items[a] = pager->notification_items[b];
  pager->notification_items[b] = item_tmp;

  char title_tmp[XIAOXIN_CARD_NOTIFICATION_TITLE_MAX];
  char body_tmp[XIAOXIN_CARD_NOTIFICATION_BODY_MAX];
  char tag_tmp[XIAOXIN_CARD_NOTIFICATION_TAG_MAX];
  memcpy(title_tmp, pager->notification_title_storage[a], sizeof(title_tmp));
  memcpy(body_tmp, pager->notification_body_storage[a], sizeof(body_tmp));
  memcpy(tag_tmp, pager->notification_tag_storage[a], sizeof(tag_tmp));
  memcpy(pager->notification_title_storage[a], pager->notification_title_storage[b], sizeof(title_tmp));
  memcpy(pager->notification_body_storage[a], pager->notification_body_storage[b], sizeof(body_tmp));
  memcpy(pager->notification_tag_storage[a], pager->notification_tag_storage[b], sizeof(tag_tmp));
  memcpy(pager->notification_title_storage[b], title_tmp, sizeof(title_tmp));
  memcpy(pager->notification_body_storage[b], body_tmp, sizeof(body_tmp));
  memcpy(pager->notification_tag_storage[b], tag_tmp, sizeof(tag_tmp));

  notification_rebind_slot(pager, a);
  notification_rebind_slot(pager, b);
}

static void notification_sort_by_priority(xiaoxin_card_pager_t* pager) {
  if (pager == NULL) {
    return;
  }
  for (uint8_t i = 1; i < pager->notification_count; ++i) {
    uint8_t j = i;
    while (j > 0 &&
           pager->notification_items[j - 1].priority > pager->notification_items[j].priority) {
      notification_swap_slots(pager, (uint8_t)(j - 1), j);
      j--;
    }
  }
}

static void notification_shift_left(xiaoxin_card_pager_t* pager, uint8_t index) {
  if (pager == NULL || index >= pager->notification_count) {
    return;
  }
  for (uint8_t i = index; (uint8_t)(i + 1) < pager->notification_count; ++i) {
    pager->notification_types[i] = pager->notification_types[i + 1];
    memcpy(pager->notification_id_storage[i], pager->notification_id_storage[i + 1], XIAOXIN_CARD_NOTIFICATION_ID_MAX);
    pager->notification_expires_at_ms[i] = pager->notification_expires_at_ms[i + 1];
    pager->notification_items[i] = pager->notification_items[i + 1];
    memcpy(pager->notification_title_storage[i], pager->notification_title_storage[i + 1], XIAOXIN_CARD_NOTIFICATION_TITLE_MAX);
    memcpy(pager->notification_body_storage[i], pager->notification_body_storage[i + 1], XIAOXIN_CARD_NOTIFICATION_BODY_MAX);
    memcpy(pager->notification_tag_storage[i], pager->notification_tag_storage[i + 1], XIAOXIN_CARD_NOTIFICATION_TAG_MAX);
    notification_rebind_slot(pager, i);
  }
  pager->notification_count--;
  notification_clear_slot(pager, pager->notification_count);
  clamp_notification_index(pager);
}

static int16_t abs_i16(int16_t value) {
  return (int16_t)(value < 0 ? -value : value);
}

static int16_t clamp_i16(int16_t value, int16_t min_value, int16_t max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static xiaoxin_card_page_t target_for_delta(
  xiaoxin_card_page_t current_page,
  int16_t dy
) {
  if (dy > 0) {
    return current_page == XIAOXIN_CARD_PAGE_OVERVIEW
      ? XIAOXIN_CARD_PAGE_HOME
      : XIAOXIN_CARD_PAGE_NOTIFICATIONS;
  }
  if (dy < 0) {
    return current_page == XIAOXIN_CARD_PAGE_NOTIFICATIONS
      ? XIAOXIN_CARD_PAGE_HOME
      : XIAOXIN_CARD_PAGE_OVERVIEW;
  }
  return current_page;
}

static int16_t release_threshold_px(const xiaoxin_card_pager_t* pager) {
  if (pager == NULL) {
    return 0;
  }

  if (pager->target_page == XIAOXIN_CARD_PAGE_HOME &&
      pager->current_page != XIAOXIN_CARD_PAGE_HOME) {
    return (int16_t)((pager->screen_height * k_return_home_threshold_percent) / 100);
  }

  return pager->threshold_px;
}

void xiaoxin_card_pager_init(xiaoxin_card_pager_t* pager, int16_t screen_height) {
  if (pager == NULL) {
    return;
  }

  pager->current_page = XIAOXIN_CARD_PAGE_HOME;
  pager->target_page = XIAOXIN_CARD_PAGE_HOME;
  pager->animation = XIAOXIN_CARD_ANIMATION_NONE;
  pager->screen_height = screen_height;
  pager->threshold_px = (int16_t)((screen_height * 20) / 100);
  pager->max_drag_px = (int16_t)((screen_height * 25) / 100);
  pager->start_x = 0;
  pager->start_y = 0;
  pager->last_x = 0;
  pager->last_y = 0;
  pager->offset_y = 0;
  pager->notification_index = 0;
  pager->notification_count = 0;
  pager->notification_dismissed_mask = 0;
  for (uint8_t i = 0; i < XIAOXIN_CARD_NOTIFICATION_MAX; ++i) {
    notification_clear_slot(pager, i);
  }
  pager->pressed = false;
  pager->dragging = false;
}

void xiaoxin_card_pager_press(xiaoxin_card_pager_t* pager, int16_t x, int16_t y) {
  if (pager == NULL) {
    return;
  }

  pager->start_x = x;
  pager->start_y = y;
  pager->last_x = x;
  pager->last_y = y;
  pager->target_page = pager->current_page;
  pager->animation = XIAOXIN_CARD_ANIMATION_NONE;
  pager->offset_y = 0;
  pager->pressed = true;
  pager->dragging = false;
}

void xiaoxin_card_pager_drag(xiaoxin_card_pager_t* pager, int16_t x, int16_t y) {
  if (pager == NULL || !pager->pressed) {
    return;
  }

  pager->last_x = x;
  pager->last_y = y;

  const int16_t dx = (int16_t)(x - pager->start_x);
  const int16_t dy = (int16_t)(y - pager->start_y);
  const int16_t abs_dx = abs_i16(dx);
  const int16_t abs_dy = abs_i16(dy);
  if (abs_dy < k_min_vertical_drag_px || abs_dx > abs_dy) {
    pager->dragging = false;
    pager->target_page = pager->current_page;
    pager->offset_y = 0;
    return;
  }

  pager->dragging = true;
  pager->target_page = target_for_delta(pager->current_page, dy);
  pager->offset_y = clamp_i16(dy, (int16_t)-pager->max_drag_px, pager->max_drag_px);
}

void xiaoxin_card_pager_release(xiaoxin_card_pager_t* pager) {
  if (pager == NULL || !pager->pressed) {
    return;
  }

  if (pager->dragging && abs_i16(pager->offset_y) >= release_threshold_px(pager)) {
    pager->current_page = pager->target_page;
    pager->animation = XIAOXIN_CARD_ANIMATION_SNAP;
  } else {
    pager->target_page = pager->current_page;
    pager->animation = pager->dragging
      ? XIAOXIN_CARD_ANIMATION_REBOUND
      : XIAOXIN_CARD_ANIMATION_NONE;
  }

  pager->offset_y = 0;
  pager->pressed = false;
  pager->dragging = false;
}

xiaoxin_card_page_t xiaoxin_card_pager_current_page(const xiaoxin_card_pager_t* pager) {
  return pager != NULL ? pager->current_page : XIAOXIN_CARD_PAGE_HOME;
}

xiaoxin_card_page_t xiaoxin_card_pager_target_page(const xiaoxin_card_pager_t* pager) {
  return pager != NULL ? pager->target_page : XIAOXIN_CARD_PAGE_HOME;
}

xiaoxin_card_animation_t xiaoxin_card_pager_animation(const xiaoxin_card_pager_t* pager) {
  return pager != NULL ? pager->animation : XIAOXIN_CARD_ANIMATION_NONE;
}

int16_t xiaoxin_card_pager_offset_y(const xiaoxin_card_pager_t* pager) {
  return pager != NULL ? pager->offset_y : 0;
}

bool xiaoxin_card_pager_is_dragging(const xiaoxin_card_pager_t* pager) {
  return pager != NULL && pager->dragging;
}

bool xiaoxin_card_pager_allows_pet_interaction(const xiaoxin_card_pager_t* pager) {
  return pager == NULL || pager->current_page == XIAOXIN_CARD_PAGE_HOME;
}

xiaoxin_card_page_t xiaoxin_card_pager_visual_page(const xiaoxin_card_pager_t* pager) {
  if (pager == NULL) {
    return XIAOXIN_CARD_PAGE_HOME;
  }

  if (!pager->dragging) {
    return pager->current_page;
  }

  return pager->target_page == XIAOXIN_CARD_PAGE_HOME
    ? pager->current_page
    : pager->target_page;
}

uint8_t xiaoxin_card_pager_notification_index(const xiaoxin_card_pager_t* pager) {
  return pager != NULL ? pager->notification_index : 0;
}

uint8_t xiaoxin_card_pager_notification_count(const xiaoxin_card_pager_t* pager) {
  return pager != NULL ? pager->notification_count : 0;
}

const xiaoxin_card_item_t* xiaoxin_card_pager_notification_at(
  const xiaoxin_card_pager_t* pager,
  uint8_t visible_index
) {
  if (pager == NULL || visible_index >= pager->notification_count) {
    return NULL;
  }
  return &pager->notification_items[visible_index];
}

bool xiaoxin_card_pager_notification_upsert_event(
  xiaoxin_card_pager_t* pager,
  const xiaoxin_notification_event_t* event
) {
  return xiaoxin_card_pager_notification_upsert_event_at(pager, event, 0);
}

bool xiaoxin_card_pager_notification_upsert_event_at(
  xiaoxin_card_pager_t* pager,
  const xiaoxin_notification_event_t* event,
  int64_t now_ms
) {
  if (pager == NULL || event == NULL) {
    return false;
  }
  if (event->id != NULL &&
      strlen(event->id) >= XIAOXIN_CARD_NOTIFICATION_ID_MAX) {
    return false;
  }

  const notification_event_defaults_t* defaults = notification_defaults_for(event->type);
  if (defaults == NULL) {
    return false;
  }

  int8_t slot = event->id != NULL && event->id[0] != '\0'
    ? notification_index_for_id(pager, event->id)
    : notification_index_for_type(pager, event->type);
  if (slot < 0) {
    if (pager->notification_count >= XIAOXIN_CARD_NOTIFICATION_MAX) {
      return false;
    }
    slot = (int8_t)pager->notification_count;
    pager->notification_count++;
  }

  const uint8_t index = (uint8_t)slot;
  const char* body = defaults->body;
  if (event->body != NULL && event->body[0] != '\0') {
    if (event->type != XIAOXIN_NOTIFICATION_EVENT_LOW_BATTERY ||
        !notification_body_contains_percent(event->body)) {
      body = event->body;
    }
  }

  pager->notification_types[index] = event->type;
  copy_text(
    pager->notification_id_storage[index],
    XIAOXIN_CARD_NOTIFICATION_ID_MAX,
    event->id
  );
  copy_text(
    pager->notification_title_storage[index],
    XIAOXIN_CARD_NOTIFICATION_TITLE_MAX,
    event->title != NULL ? event->title : defaults->title
  );
  copy_text(
    pager->notification_body_storage[index],
    XIAOXIN_CARD_NOTIFICATION_BODY_MAX,
    body
  );
  copy_text(
    pager->notification_tag_storage[index],
    XIAOXIN_CARD_NOTIFICATION_TAG_MAX,
    event->tag != NULL ? event->tag : defaults->tag
  );
  pager->notification_items[index].priority = event->priority != 0 ? event->priority : defaults->priority;
  pager->notification_items[index].ttl_ms = event->ttl_ms != 0 ? event->ttl_ms : defaults->ttl_ms;
  pager->notification_expires_at_ms[index] =
    notification_expiry_from_ttl(pager->notification_items[index].ttl_ms, now_ms);
  notification_rebind_slot(pager, index);
  notification_sort_by_priority(pager);
  clamp_notification_index(pager);
  return true;
}

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

bool xiaoxin_card_pager_notification_has_pending_expiry(
  const xiaoxin_card_pager_t* pager
) {
  if (pager == NULL) {
    return false;
  }

  for (uint8_t index = 0; index < pager->notification_count; ++index) {
    if (pager->notification_expires_at_ms[index] > 0) {
      return true;
    }
  }
  return false;
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

const xiaoxin_card_item_t* xiaoxin_card_pager_notification_find_by_id(
  const xiaoxin_card_pager_t* pager,
  const char* id
) {
  const int8_t index = notification_index_for_id(pager, id);
  if (index < 0) {
    return NULL;
  }
  return &pager->notification_items[(uint8_t)index];
}

bool xiaoxin_card_pager_notification_upsert_course_reminder(
  xiaoxin_card_pager_t* pager,
  const xiaoxin_course_reminder_t* reminder,
  int64_t now_unix_ms
) {
  if (pager == NULL || reminder == NULL || reminder->course_name == NULL || reminder->course_name[0] == '\0') {
    return false;
  }

  const int64_t remind_window_ms = (int64_t)reminder->remind_before_min * 60 * 1000;
  const int64_t until_start_ms = reminder->starts_at_unix_ms - now_unix_ms;
  if (until_start_ms > remind_window_ms) {
    return false;
  }

  int64_t minutes_until_start = 0;
  if (until_start_ms > 0) {
    minutes_until_start = (until_start_ms + 59999) / (60 * 1000);
  }

  char body[XIAOXIN_CARD_NOTIFICATION_BODY_MAX];
  const char* classroom = reminder->classroom != NULL ? reminder->classroom : "";
  if (classroom[0] != '\0') {
    snprintf(
      body,
      sizeof(body),
      "%lld 分钟后 %s @ %s",
      (long long)minutes_until_start,
      reminder->course_name,
      classroom
    );
  } else {
    snprintf(
      body,
      sizeof(body),
      "%lld 分钟后 %s",
      (long long)minutes_until_start,
      reminder->course_name
    );
  }

  const xiaoxin_notification_event_t event = {
    .type = XIAOXIN_NOTIFICATION_EVENT_REMINDER,
    .title = "上课提醒",
    .body = body,
    .tag = "课程",
    .priority = 1,
    .ttl_ms = 0,
  };
  return xiaoxin_card_pager_notification_upsert_event(pager, &event);
}

bool xiaoxin_card_pager_notification_remove_event(
  xiaoxin_card_pager_t* pager,
  xiaoxin_notification_event_type_t type
) {
  const int8_t index = notification_index_for_type(pager, type);
  if (index < 0) {
    return false;
  }
  notification_shift_left(pager, (uint8_t)index);
  return true;
}

bool xiaoxin_card_pager_notification_dismiss(xiaoxin_card_pager_t* pager, uint8_t visible_index) {
  if (pager == NULL || visible_index >= pager->notification_count) {
    return false;
  }
  notification_shift_left(pager, visible_index);
  return true;
}

void xiaoxin_card_pager_notification_clear_all(xiaoxin_card_pager_t* pager) {
  if (pager == NULL) {
    return;
  }
  pager->notification_count = 0;
  pager->notification_index = 0;
  pager->notification_dismissed_mask = 0;
  for (uint8_t i = 0; i < XIAOXIN_CARD_NOTIFICATION_MAX; ++i) {
    notification_clear_slot(pager, i);
  }
}

bool xiaoxin_card_pager_notification_empty(const xiaoxin_card_pager_t* pager) {
  return xiaoxin_card_pager_notification_count(pager) == 0;
}

bool xiaoxin_card_pager_notification_next(xiaoxin_card_pager_t* pager) {
  if (pager == NULL || xiaoxin_card_pager_notification_empty(pager)) {
    return false;
  }

  if ((uint8_t)(pager->notification_index + 1) >= xiaoxin_card_pager_notification_count(pager)) {
    return false;
  }

  pager->notification_index++;
  return true;
}

bool xiaoxin_card_pager_notification_prev(xiaoxin_card_pager_t* pager) {
  if (pager == NULL || xiaoxin_card_pager_notification_empty(pager) || pager->notification_index == 0) {
    return false;
  }

  pager->notification_index--;
  return true;
}

const xiaoxin_card_item_t* xiaoxin_card_pager_current_notification(const xiaoxin_card_pager_t* pager) {
  if (pager == NULL || xiaoxin_card_pager_notification_empty(pager)) {
    return NULL;
  }

  uint8_t index = pager->notification_index;
  const uint8_t visible_count = xiaoxin_card_pager_notification_count(pager);
  if (index >= visible_count) {
    index = (uint8_t)(visible_count - 1);
  }
  return xiaoxin_card_pager_notification_at(pager, index);
}

const char* xiaoxin_card_page_name(xiaoxin_card_page_t page) {
  switch (page) {
    case XIAOXIN_CARD_PAGE_HOME:
      return "home";
    case XIAOXIN_CARD_PAGE_NOTIFICATIONS:
      return "notifications";
    case XIAOXIN_CARD_PAGE_OVERVIEW:
      return "overview";
    default:
      return "home";
  }
}

void xiaoxin_card_pager_items(
  xiaoxin_card_page_t page,
  const xiaoxin_card_item_t** items,
  uint8_t* count
) {
  if (items == NULL || count == NULL) {
    return;
  }

  (void)page;
  *items = NULL;
  *count = 0;
}
