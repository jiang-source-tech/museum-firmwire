#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XIAOXIN_CARD_NOTIFICATION_MAX 6
#define XIAOXIN_CARD_NOTIFICATION_TITLE_MAX 32
#define XIAOXIN_CARD_NOTIFICATION_BODY_MAX 96
#define XIAOXIN_CARD_NOTIFICATION_TAG_MAX 16
#define XIAOXIN_CARD_NOTIFICATION_ID_MAX 96

typedef enum {
  XIAOXIN_CARD_PAGE_HOME = 0,
  XIAOXIN_CARD_PAGE_NOTIFICATIONS,
  XIAOXIN_CARD_PAGE_OVERVIEW,
} xiaoxin_card_page_t;

typedef enum {
  XIAOXIN_CARD_ANIMATION_NONE = 0,
  XIAOXIN_CARD_ANIMATION_SNAP,
  XIAOXIN_CARD_ANIMATION_REBOUND,
} xiaoxin_card_animation_t;

typedef struct {
  const char* title;
  const char* body;
  const char* detail;
  const char* tag;
  uint32_t priority;
  uint32_t ttl_ms;
} xiaoxin_card_item_t;

typedef enum {
  XIAOXIN_NOTIFICATION_EVENT_LOW_BATTERY = 0,
  XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED,
  XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE,
  XIAOXIN_NOTIFICATION_EVENT_VOICE_RECOGNITION_FAILED,
  /* Compatibility value: chat replies are no longer rendered as notification cards. */
  XIAOXIN_NOTIFICATION_EVENT_CHAT_REPLY,
  XIAOXIN_NOTIFICATION_EVENT_REMINDER,
  XIAOXIN_NOTIFICATION_EVENT_TODO_REMINDER,
} xiaoxin_notification_event_type_t;

typedef struct {
  const char* course_id;
  const char* course_name;
  const char* classroom;
  int64_t starts_at_unix_ms;
  uint16_t remind_before_min;
} xiaoxin_course_reminder_t;

typedef struct {
  const char* id;
  xiaoxin_notification_event_type_t type;
  const char* title;
  const char* body;
  const char* tag;
  uint32_t priority;
  uint32_t ttl_ms;
} xiaoxin_notification_event_t;

typedef struct {
  xiaoxin_card_page_t current_page;
  xiaoxin_card_page_t target_page;
  xiaoxin_card_animation_t animation;
  int16_t screen_height;
  int16_t threshold_px;
  int16_t max_drag_px;
  int16_t start_x;
  int16_t start_y;
  int16_t last_x;
  int16_t last_y;
  int16_t offset_y;
  uint8_t notification_index;
  uint8_t notification_count;
  uint8_t notification_dismissed_mask;
  xiaoxin_notification_event_type_t notification_types[XIAOXIN_CARD_NOTIFICATION_MAX];
  char notification_id_storage[XIAOXIN_CARD_NOTIFICATION_MAX][XIAOXIN_CARD_NOTIFICATION_ID_MAX];
  int64_t notification_expires_at_ms[XIAOXIN_CARD_NOTIFICATION_MAX];
  xiaoxin_card_item_t notification_items[XIAOXIN_CARD_NOTIFICATION_MAX];
  char notification_title_storage[XIAOXIN_CARD_NOTIFICATION_MAX][XIAOXIN_CARD_NOTIFICATION_TITLE_MAX];
  char notification_body_storage[XIAOXIN_CARD_NOTIFICATION_MAX][XIAOXIN_CARD_NOTIFICATION_BODY_MAX];
  char notification_tag_storage[XIAOXIN_CARD_NOTIFICATION_MAX][XIAOXIN_CARD_NOTIFICATION_TAG_MAX];
  bool pressed;
  bool dragging;
} xiaoxin_card_pager_t;

void xiaoxin_card_pager_init(xiaoxin_card_pager_t* pager, int16_t screen_height);
void xiaoxin_card_pager_press(xiaoxin_card_pager_t* pager, int16_t x, int16_t y);
void xiaoxin_card_pager_drag(xiaoxin_card_pager_t* pager, int16_t x, int16_t y);
void xiaoxin_card_pager_release(xiaoxin_card_pager_t* pager);

xiaoxin_card_page_t xiaoxin_card_pager_current_page(const xiaoxin_card_pager_t* pager);
xiaoxin_card_page_t xiaoxin_card_pager_target_page(const xiaoxin_card_pager_t* pager);
xiaoxin_card_animation_t xiaoxin_card_pager_animation(const xiaoxin_card_pager_t* pager);
int16_t xiaoxin_card_pager_offset_y(const xiaoxin_card_pager_t* pager);
bool xiaoxin_card_pager_is_dragging(const xiaoxin_card_pager_t* pager);
bool xiaoxin_card_pager_allows_pet_interaction(const xiaoxin_card_pager_t* pager);
xiaoxin_card_page_t xiaoxin_card_pager_visual_page(const xiaoxin_card_pager_t* pager);
uint8_t xiaoxin_card_pager_notification_index(const xiaoxin_card_pager_t* pager);
uint8_t xiaoxin_card_pager_notification_count(const xiaoxin_card_pager_t* pager);
const xiaoxin_card_item_t* xiaoxin_card_pager_notification_at(
  const xiaoxin_card_pager_t* pager,
  uint8_t visible_index
);
bool xiaoxin_card_pager_notification_upsert_event(
  xiaoxin_card_pager_t* pager,
  const xiaoxin_notification_event_t* event
);
bool xiaoxin_card_pager_notification_upsert_event_at(
  xiaoxin_card_pager_t* pager,
  const xiaoxin_notification_event_t* event,
  int64_t now_ms
);
uint8_t xiaoxin_card_pager_notification_expire(
  xiaoxin_card_pager_t* pager,
  int64_t now_ms
);
bool xiaoxin_card_pager_notification_has_pending_expiry(
  const xiaoxin_card_pager_t* pager
);
const xiaoxin_card_item_t* xiaoxin_card_pager_notification_find_by_type(
  const xiaoxin_card_pager_t* pager,
  xiaoxin_notification_event_type_t type
);
const xiaoxin_card_item_t* xiaoxin_card_pager_notification_find_by_id(
  const xiaoxin_card_pager_t* pager,
  const char* id
);
bool xiaoxin_card_pager_notification_upsert_course_reminder(
  xiaoxin_card_pager_t* pager,
  const xiaoxin_course_reminder_t* reminder,
  int64_t now_unix_ms
);
bool xiaoxin_card_pager_notification_remove_event(
  xiaoxin_card_pager_t* pager,
  xiaoxin_notification_event_type_t type
);
bool xiaoxin_card_pager_notification_dismiss(
  xiaoxin_card_pager_t* pager,
  uint8_t visible_index
);
void xiaoxin_card_pager_notification_clear_all(xiaoxin_card_pager_t* pager);
bool xiaoxin_card_pager_notification_empty(const xiaoxin_card_pager_t* pager);
bool xiaoxin_card_pager_notification_next(xiaoxin_card_pager_t* pager);
bool xiaoxin_card_pager_notification_prev(xiaoxin_card_pager_t* pager);
const xiaoxin_card_item_t* xiaoxin_card_pager_current_notification(const xiaoxin_card_pager_t* pager);

const char* xiaoxin_card_page_name(xiaoxin_card_page_t page);
void xiaoxin_card_pager_items(
  xiaoxin_card_page_t page,
  const xiaoxin_card_item_t** items,
  uint8_t* count
);

#ifdef __cplusplus
}
#endif
