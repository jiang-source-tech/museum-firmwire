#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_card_pager.h"

static void home_down_swipe_enters_notifications(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    xiaoxin_card_pager_press(&pager, 206, 60);
    xiaoxin_card_pager_drag(&pager, 206, 154);

    assert(xiaoxin_card_pager_current_page(&pager) == XIAOXIN_CARD_PAGE_HOME);
    assert(xiaoxin_card_pager_target_page(&pager) == XIAOXIN_CARD_PAGE_NOTIFICATIONS);
    assert(xiaoxin_card_pager_offset_y(&pager) == 94);

    xiaoxin_card_pager_release(&pager);

    assert(xiaoxin_card_pager_current_page(&pager) == XIAOXIN_CARD_PAGE_NOTIFICATIONS);
    assert(xiaoxin_card_pager_animation(&pager) == XIAOXIN_CARD_ANIMATION_SNAP);
}

static void home_up_swipe_enters_overview(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    xiaoxin_card_pager_press(&pager, 206, 352);
    xiaoxin_card_pager_drag(&pager, 206, 258);
    xiaoxin_card_pager_release(&pager);

    assert(xiaoxin_card_pager_current_page(&pager) == XIAOXIN_CARD_PAGE_OVERVIEW);
    assert(xiaoxin_card_pager_animation(&pager) == XIAOXIN_CARD_ANIMATION_SNAP);
}

static void reverse_swipes_return_home(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    xiaoxin_card_pager_press(&pager, 206, 60);
    xiaoxin_card_pager_drag(&pager, 206, 154);
    xiaoxin_card_pager_release(&pager);
    assert(xiaoxin_card_pager_current_page(&pager) == XIAOXIN_CARD_PAGE_NOTIFICATIONS);

    xiaoxin_card_pager_press(&pager, 206, 154);
    xiaoxin_card_pager_drag(&pager, 206, 60);
    xiaoxin_card_pager_release(&pager);
    assert(xiaoxin_card_pager_current_page(&pager) == XIAOXIN_CARD_PAGE_HOME);

    xiaoxin_card_pager_press(&pager, 206, 352);
    xiaoxin_card_pager_drag(&pager, 206, 258);
    xiaoxin_card_pager_release(&pager);
    assert(xiaoxin_card_pager_current_page(&pager) == XIAOXIN_CARD_PAGE_OVERVIEW);

    xiaoxin_card_pager_press(&pager, 206, 258);
    xiaoxin_card_pager_drag(&pager, 206, 352);
    xiaoxin_card_pager_release(&pager);
    assert(xiaoxin_card_pager_current_page(&pager) == XIAOXIN_CARD_PAGE_HOME);
}

static void short_reverse_swipes_return_home_from_open_pages(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    xiaoxin_card_pager_press(&pager, 206, 60);
    xiaoxin_card_pager_drag(&pager, 206, 154);
    xiaoxin_card_pager_release(&pager);
    assert(xiaoxin_card_pager_current_page(&pager) == XIAOXIN_CARD_PAGE_NOTIFICATIONS);

    xiaoxin_card_pager_press(&pager, 206, 154);
    xiaoxin_card_pager_drag(&pager, 206, 120);
    xiaoxin_card_pager_release(&pager);
    assert(xiaoxin_card_pager_current_page(&pager) == XIAOXIN_CARD_PAGE_HOME);
    assert(xiaoxin_card_pager_animation(&pager) == XIAOXIN_CARD_ANIMATION_SNAP);

    xiaoxin_card_pager_press(&pager, 206, 352);
    xiaoxin_card_pager_drag(&pager, 206, 258);
    xiaoxin_card_pager_release(&pager);
    assert(xiaoxin_card_pager_current_page(&pager) == XIAOXIN_CARD_PAGE_OVERVIEW);

    xiaoxin_card_pager_press(&pager, 206, 258);
    xiaoxin_card_pager_drag(&pager, 206, 292);
    xiaoxin_card_pager_release(&pager);
    assert(xiaoxin_card_pager_current_page(&pager) == XIAOXIN_CARD_PAGE_HOME);
    assert(xiaoxin_card_pager_animation(&pager) == XIAOXIN_CARD_ANIMATION_SNAP);
}

static void short_vertical_drag_rebounds_to_current_page(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    xiaoxin_card_pager_press(&pager, 206, 100);
    xiaoxin_card_pager_drag(&pager, 206, 142);
    xiaoxin_card_pager_release(&pager);

    assert(xiaoxin_card_pager_current_page(&pager) == XIAOXIN_CARD_PAGE_HOME);
    assert(xiaoxin_card_pager_animation(&pager) == XIAOXIN_CARD_ANIMATION_REBOUND);
    assert(xiaoxin_card_pager_offset_y(&pager) == 0);
}

static void horizontal_drag_is_not_a_card_page_drag(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    xiaoxin_card_pager_press(&pager, 120, 206);
    xiaoxin_card_pager_drag(&pager, 190, 218);

    assert(!xiaoxin_card_pager_is_dragging(&pager));
    assert(xiaoxin_card_pager_target_page(&pager) == XIAOXIN_CARD_PAGE_HOME);
}

static void long_drag_can_follow_across_the_screen(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    xiaoxin_card_pager_press(&pager, 206, 0);
    xiaoxin_card_pager_drag(&pager, 206, 360);

    assert(xiaoxin_card_pager_is_dragging(&pager));
    assert(xiaoxin_card_pager_offset_y(&pager) == 103);

    xiaoxin_card_pager_drag(&pager, 206, 460);
    assert(xiaoxin_card_pager_offset_y(&pager) == 103);
}

static void drag_threshold_controls_snap_vs_rebound(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    assert(pager.threshold_px == 82);
    assert(pager.max_drag_px == 103);

    xiaoxin_card_pager_press(&pager, 206, 100);
    xiaoxin_card_pager_drag(&pager, 206, 181);
    xiaoxin_card_pager_release(&pager);
    assert(xiaoxin_card_pager_current_page(&pager) == XIAOXIN_CARD_PAGE_HOME);
    assert(xiaoxin_card_pager_animation(&pager) == XIAOXIN_CARD_ANIMATION_REBOUND);

    xiaoxin_card_pager_press(&pager, 206, 100);
    xiaoxin_card_pager_drag(&pager, 206, 182);
    xiaoxin_card_pager_release(&pager);
    assert(xiaoxin_card_pager_current_page(&pager) == XIAOXIN_CARD_PAGE_NOTIFICATIONS);
    assert(xiaoxin_card_pager_animation(&pager) == XIAOXIN_CARD_ANIMATION_SNAP);
}

static void visual_page_stays_stable_during_continuous_drag(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    xiaoxin_card_pager_press(&pager, 206, 10);
    xiaoxin_card_pager_drag(&pager, 206, 80);

    assert(xiaoxin_card_pager_visual_page(&pager) == XIAOXIN_CARD_PAGE_NOTIFICATIONS);

    xiaoxin_card_pager_drag(&pager, 206, 140);
    assert(xiaoxin_card_pager_visual_page(&pager) == XIAOXIN_CARD_PAGE_NOTIFICATIONS);
}

static void overview_items_are_owned_by_overview_model(void) {
    const xiaoxin_card_item_t* items = NULL;
    uint8_t count = 0;

    xiaoxin_card_pager_items(XIAOXIN_CARD_PAGE_NOTIFICATIONS, &items, &count);
    assert(count == 0);
    assert(items == NULL);

    xiaoxin_card_pager_items(XIAOXIN_CARD_PAGE_OVERVIEW, &items, &count);
    assert(count == 0);
    assert(items == NULL);
}

static void notification_center_starts_empty_until_events_arrive(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    assert(xiaoxin_card_pager_notification_empty(&pager));
    assert(xiaoxin_card_pager_notification_count(&pager) == 0);
    assert(xiaoxin_card_pager_current_notification(&pager) == NULL);
}

static void notification_event_injection_adds_real_notifications(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t low_battery = {
        .type = XIAOXIN_NOTIFICATION_EVENT_LOW_BATTERY,
        .body = "剩余 18%",
    };
    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &low_battery));

    assert(!xiaoxin_card_pager_notification_empty(&pager));
    assert(xiaoxin_card_pager_notification_count(&pager) == 1);
    const xiaoxin_card_item_t* current = xiaoxin_card_pager_current_notification(&pager);
    assert(current != NULL);
    assert(strcmp(current->title, "低电量") == 0);
    assert(strcmp(current->body, "电量偏低，请尽快充电") == 0);
    assert(strcmp(current->tag, "电量") == 0);
    assert(current->priority == 2);
}

static void notification_low_battery_copy_respects_safe_critical_body(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t critical_low_battery = {
        .type = XIAOXIN_NOTIFICATION_EVENT_LOW_BATTERY,
        .body = "电量很低，请尽快充电",
    };
    const xiaoxin_notification_event_t percent_low_battery = {
        .type = XIAOXIN_NOTIFICATION_EVENT_LOW_BATTERY,
        .body = "剩余 18%",
    };

    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &critical_low_battery));
    const xiaoxin_card_item_t* current = xiaoxin_card_pager_current_notification(&pager);
    assert(current != NULL);
    assert(strcmp(current->body, "电量很低，请尽快充电") == 0);

    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &percent_low_battery));
    current = xiaoxin_card_pager_current_notification(&pager);
    assert(current != NULL);
    assert(strcmp(current->body, "电量偏低，请尽快充电") == 0);
}

static void notification_event_upsert_replaces_existing_source(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t first = {
        .type = XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED,
        .body = "正在重新连接",
    };
    const xiaoxin_notification_event_t second = {
        .type = XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED,
        .body = "已断开 30 秒",
    };

    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &first));
    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &second));

    assert(xiaoxin_card_pager_notification_count(&pager) == 1);
    const xiaoxin_card_item_t* current = xiaoxin_card_pager_current_notification(&pager);
    assert(current != NULL);
    assert(strcmp(current->title, "WiFi 断开") == 0);
    assert(strcmp(current->body, "已断开 30 秒") == 0);
}

static void chat_reply_events_are_ignored_by_notification_center(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t chat = {
        .type = XIAOXIN_NOTIFICATION_EVENT_CHAT_REPLY,
        .body = "小芯回复了你",
    };

    assert(!xiaoxin_card_pager_notification_upsert_event(&pager, &chat));
    assert(xiaoxin_card_pager_notification_empty(&pager));
}

static void course_reminder_helper_injects_class_notification_once_in_window(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const int64_t start_ms = 1000000;
    const xiaoxin_course_reminder_t reminder = {
        .course_id = "math-001",
        .course_name = "高等数学",
        .classroom = "3教 204",
        .starts_at_unix_ms = start_ms,
        .remind_before_min = 15,
    };

    assert(!xiaoxin_card_pager_notification_upsert_course_reminder(&pager, &reminder, start_ms - 16 * 60 * 1000));
    assert(xiaoxin_card_pager_notification_empty(&pager));

    assert(xiaoxin_card_pager_notification_upsert_course_reminder(&pager, &reminder, start_ms - 15 * 60 * 1000));
    assert(xiaoxin_card_pager_notification_upsert_course_reminder(&pager, &reminder, start_ms - 14 * 60 * 1000));

    assert(xiaoxin_card_pager_notification_count(&pager) == 1);
    const xiaoxin_card_item_t* current = xiaoxin_card_pager_current_notification(&pager);
    assert(current != NULL);
    assert(strcmp(current->title, "上课提醒") == 0);
    assert(strcmp(current->body, "14 分钟后 高等数学 @ 3教 204") == 0);
    assert(strcmp(current->tag, "课程") == 0);
    assert(current->priority == 1);
}

static void notification_events_are_sorted_by_priority(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t low_battery = {
        .type = XIAOXIN_NOTIFICATION_EVENT_LOW_BATTERY,
    };
    const xiaoxin_notification_event_t wifi = {
        .type = XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED,
    };
    const xiaoxin_notification_event_t voice = {
        .type = XIAOXIN_NOTIFICATION_EVENT_VOICE_RECOGNITION_FAILED,
        .body = "没听清，请再说一次",
    };
    const xiaoxin_notification_event_t ota = {
        .type = XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE,
        .body = "发现新固件",
    };
    const xiaoxin_notification_event_t reminder = {
        .type = XIAOXIN_NOTIFICATION_EVENT_REMINDER,
        .body = "15 分钟后 高等数学 @ 3教 204",
    };

    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &voice));
    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &low_battery));
    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &wifi));
    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &voice));
    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &ota));
    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &reminder));

    assert(xiaoxin_card_pager_notification_count(&pager) == 5);
    assert(strcmp(xiaoxin_card_pager_notification_at(&pager, 0)->title, "上课提醒") == 0);
    assert(strcmp(xiaoxin_card_pager_notification_at(&pager, 1)->title, "低电量") == 0);
    assert(strcmp(xiaoxin_card_pager_notification_at(&pager, 2)->title, "WiFi 断开") == 0);
    assert(strcmp(xiaoxin_card_pager_notification_at(&pager, 3)->title, "OTA 更新") == 0);
    assert(strcmp(xiaoxin_card_pager_notification_at(&pager, 4)->title, "语音识别失败") == 0);
}

static void notification_event_remove_resolves_status_notifications(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t wifi = {
        .type = XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED,
        .body = "正在重新连接",
    };

    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &wifi));
    assert(xiaoxin_card_pager_notification_count(&pager) == 1);
    assert(xiaoxin_card_pager_notification_remove_event(&pager, XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED));
    assert(xiaoxin_card_pager_notification_empty(&pager));
    assert(!xiaoxin_card_pager_notification_remove_event(&pager, XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED));
}

static void notification_pagination_tracks_current_item(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t events[] = {
        {.type = XIAOXIN_NOTIFICATION_EVENT_LOW_BATTERY, .body = "剩余 18%"},
        {.type = XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED, .body = "正在重新连接"},
        {.type = XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE, .body = "发现新固件"},
        {.type = XIAOXIN_NOTIFICATION_EVENT_REMINDER, .body = "15 分钟后 高等数学 @ 3教 204"},
    };
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i) {
        assert(xiaoxin_card_pager_notification_upsert_event(&pager, &events[i]));
    }

    assert(xiaoxin_card_pager_notification_index(&pager) == 0);
    assert(xiaoxin_card_pager_notification_count(&pager) == 4);
    const xiaoxin_card_item_t* first = xiaoxin_card_pager_current_notification(&pager);
    assert(first != NULL);
    assert(first->priority == 1);

    assert(!xiaoxin_card_pager_notification_prev(&pager));
    assert(xiaoxin_card_pager_notification_index(&pager) == 0);

    assert(xiaoxin_card_pager_notification_next(&pager));
    assert(xiaoxin_card_pager_notification_index(&pager) == 1);
    const xiaoxin_card_item_t* second = xiaoxin_card_pager_current_notification(&pager);
    assert(second != NULL);
    assert(second->priority == 2);

    assert(xiaoxin_card_pager_notification_next(&pager));
    assert(xiaoxin_card_pager_notification_next(&pager));
    assert(xiaoxin_card_pager_notification_index(&pager) == 3);
    assert(!xiaoxin_card_pager_notification_next(&pager));
    assert(xiaoxin_card_pager_notification_index(&pager) == 3);
}

static void notification_dismiss_removes_visible_item(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t events[] = {
        {.type = XIAOXIN_NOTIFICATION_EVENT_LOW_BATTERY, .body = "剩余 18%"},
        {.type = XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED, .body = "正在重新连接"},
        {.type = XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE, .body = "发现新固件"},
        {.type = XIAOXIN_NOTIFICATION_EVENT_REMINDER, .body = "15 分钟后 高等数学 @ 3教 204"},
    };
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i) {
        assert(xiaoxin_card_pager_notification_upsert_event(&pager, &events[i]));
    }

    assert(xiaoxin_card_pager_notification_count(&pager) == 4);
    const xiaoxin_card_item_t* before = xiaoxin_card_pager_notification_at(&pager, 2);
    assert(before != NULL);
    char before_title[XIAOXIN_CARD_NOTIFICATION_TITLE_MAX];
    snprintf(before_title, sizeof(before_title), "%s", before->title);

    assert(xiaoxin_card_pager_notification_dismiss(&pager, 1));
    assert(xiaoxin_card_pager_notification_count(&pager) == 3);
    const xiaoxin_card_item_t* after = xiaoxin_card_pager_notification_at(&pager, 1);
    assert(after != NULL);
    assert(strcmp(after->title, before_title) == 0);
}

static void notification_dismiss_current_item_clamps_index(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t events[] = {
        {.type = XIAOXIN_NOTIFICATION_EVENT_LOW_BATTERY, .body = "剩余 18%"},
        {.type = XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED, .body = "正在重新连接"},
        {.type = XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE, .body = "发现新固件"},
        {.type = XIAOXIN_NOTIFICATION_EVENT_REMINDER, .body = "15 分钟后 高等数学 @ 3教 204"},
    };
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i) {
        assert(xiaoxin_card_pager_notification_upsert_event(&pager, &events[i]));
    }

    assert(xiaoxin_card_pager_notification_next(&pager));
    assert(xiaoxin_card_pager_notification_next(&pager));
    assert(xiaoxin_card_pager_notification_next(&pager));
    assert(xiaoxin_card_pager_notification_index(&pager) == 3);

    assert(xiaoxin_card_pager_notification_dismiss(&pager, 3));
    assert(xiaoxin_card_pager_notification_count(&pager) == 3);
    assert(xiaoxin_card_pager_notification_index(&pager) == 2);
    assert(xiaoxin_card_pager_current_notification(&pager) != NULL);
}

static void notification_clear_all_empties_notifications(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    xiaoxin_card_pager_notification_clear_all(&pager);

    assert(xiaoxin_card_pager_notification_empty(&pager));
    assert(xiaoxin_card_pager_notification_count(&pager) == 0);
    assert(xiaoxin_card_pager_notification_index(&pager) == 0);
    assert(xiaoxin_card_pager_current_notification(&pager) == NULL);
    assert(!xiaoxin_card_pager_notification_next(&pager));
    assert(!xiaoxin_card_pager_notification_prev(&pager));
}

static void notification_dismiss_invalid_index_returns_false(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    assert(!xiaoxin_card_pager_notification_dismiss(&pager, 0));
    assert(xiaoxin_card_pager_notification_count(&pager) == 0);
}

static void non_home_pages_capture_pet_interaction(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    assert(xiaoxin_card_pager_allows_pet_interaction(&pager));

    xiaoxin_card_pager_press(&pager, 206, 60);
    xiaoxin_card_pager_drag(&pager, 206, 154);
    xiaoxin_card_pager_release(&pager);

    assert(!xiaoxin_card_pager_allows_pet_interaction(&pager));
}

static void notification_ttl_expire_removes_only_expired_items(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t voice = {
        .type = XIAOXIN_NOTIFICATION_EVENT_VOICE_RECOGNITION_FAILED,
        .body = "娌″惉娓咃紝璇峰啀璇翠竴娆�",
        .ttl_ms = 8000,
    };
    const xiaoxin_notification_event_t wifi = {
        .type = XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED,
        .body = "WiFi 宸叉柇寮€锛屾鍦ㄩ噸鏂拌繛鎺�",
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

static void notification_expiry_tracking_ignores_persistent_cards(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t wifi = {
        .type = XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED,
        .body = "WiFi disconnected",
    };
    const xiaoxin_notification_event_t voice = {
        .type = XIAOXIN_NOTIFICATION_EVENT_VOICE_RECOGNITION_FAILED,
        .body = "Please try again",
        .ttl_ms = 8000,
    };

    assert(!xiaoxin_card_pager_notification_has_pending_expiry(&pager));
    assert(xiaoxin_card_pager_notification_upsert_event_at(&pager, &wifi, 1000));
    assert(!xiaoxin_card_pager_notification_has_pending_expiry(&pager));
    assert(xiaoxin_card_pager_notification_upsert_event_at(&pager, &voice, 1000));
    assert(xiaoxin_card_pager_notification_has_pending_expiry(&pager));

    assert(xiaoxin_card_pager_notification_expire(&pager, 9000) == 1);
    assert(!xiaoxin_card_pager_notification_has_pending_expiry(&pager));
    assert(xiaoxin_card_pager_notification_count(&pager) == 1);
}

static void notification_find_by_type_survives_priority_sort(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t voice = {
        .type = XIAOXIN_NOTIFICATION_EVENT_VOICE_RECOGNITION_FAILED,
        .body = "娌″惉娓咃紝璇峰啀璇翠竴娆�",
    };
    const xiaoxin_notification_event_t reminder = {
        .type = XIAOXIN_NOTIFICATION_EVENT_REMINDER,
        .body = "15 鍒嗛挓鍚?楂樼瓑鏁板 @ 3鏁?204",
    };

    assert(xiaoxin_card_pager_notification_upsert_event_at(&pager, &voice, 2000));
    assert(xiaoxin_card_pager_notification_upsert_event_at(&pager, &reminder, 2000));

    const xiaoxin_card_item_t* found =
        xiaoxin_card_pager_notification_find_by_type(&pager, XIAOXIN_NOTIFICATION_EVENT_VOICE_RECOGNITION_FAILED);
    assert(found != NULL);
    assert(strcmp(found->title, "语音识别失败") == 0);
    assert(strcmp(found->body, "娌″惉娓咃紝璇峰啀璇翠竴娆�") == 0);
}

static void todo_and_course_reminders_keep_separate_notification_slots(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t course = {
        .type = XIAOXIN_NOTIFICATION_EVENT_REMINDER,
        .title = "Course reminder",
        .body = "Math starts in 15 minutes",
        .tag = "Course",
        .priority = 1,
    };
    const xiaoxin_notification_event_t todo = {
        .type = XIAOXIN_NOTIFICATION_EVENT_TODO_REMINDER,
        .title = "Todo reminder",
        .body = "Submit lab report by 18:00",
        .tag = "Todo",
        .priority = 2,
    };

    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &course));
    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &todo));

    assert(xiaoxin_card_pager_notification_count(&pager) == 2);
    assert(xiaoxin_card_pager_notification_find_by_type(&pager, XIAOXIN_NOTIFICATION_EVENT_REMINDER) != NULL);
    assert(xiaoxin_card_pager_notification_find_by_type(&pager, XIAOXIN_NOTIFICATION_EVENT_TODO_REMINDER) != NULL);
}

static void delivery_id_upsert_is_idempotent_without_merging_same_type(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);

    const xiaoxin_notification_event_t first = {
        .id = "xiaoxin_event:delivery-1",
        .type = XIAOXIN_NOTIFICATION_EVENT_REMINDER,
        .title = "Course reminder",
        .body = "Math starts soon",
    };
    const xiaoxin_notification_event_t first_update = {
        .id = "xiaoxin_event:delivery-1",
        .type = XIAOXIN_NOTIFICATION_EVENT_TODO_REMINDER,
        .title = "Todo reminder",
        .body = "Submit math notes now",
    };
    const xiaoxin_notification_event_t second = {
        .id = "xiaoxin_event:delivery-2",
        .type = XIAOXIN_NOTIFICATION_EVENT_REMINDER,
        .title = "Course reminder",
        .body = "Physics starts soon",
    };

    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &first));
    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &first_update));
    assert(xiaoxin_card_pager_notification_count(&pager) == 1);
    const xiaoxin_card_item_t* updated =
        xiaoxin_card_pager_notification_find_by_id(&pager, first.id);
    assert(updated != NULL);
    assert(strcmp(updated->body, "Submit math notes now") == 0);
    assert(xiaoxin_card_pager_notification_find_by_type(
        &pager, XIAOXIN_NOTIFICATION_EVENT_REMINDER) == NULL);
    assert(xiaoxin_card_pager_notification_find_by_type(
        &pager, XIAOXIN_NOTIFICATION_EVENT_TODO_REMINDER) == updated);

    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &second));
    assert(xiaoxin_card_pager_notification_count(&pager) == 2);
    assert(xiaoxin_card_pager_notification_find_by_id(&pager, first.id) != NULL);
    assert(xiaoxin_card_pager_notification_find_by_id(&pager, second.id) != NULL);
}

static void notification_id_rejects_truncation_and_accepts_maximum_length(void) {
    xiaoxin_card_pager_t pager;
    xiaoxin_card_pager_init(&pager, 412);
    char maximum_id[XIAOXIN_CARD_NOTIFICATION_ID_MAX];
    char oversized_id[XIAOXIN_CARD_NOTIFICATION_ID_MAX + 1];
    memset(maximum_id, 'a', sizeof(maximum_id) - 1);
    maximum_id[sizeof(maximum_id) - 1] = '\0';
    memset(oversized_id, 'b', sizeof(oversized_id) - 1);
    oversized_id[sizeof(oversized_id) - 1] = '\0';

    const xiaoxin_notification_event_t maximum = {
        .id = maximum_id,
        .type = XIAOXIN_NOTIFICATION_EVENT_REMINDER,
        .title = "Maximum id",
        .body = "First delivery",
    };
    const xiaoxin_notification_event_t maximum_retry = {
        .id = maximum_id,
        .type = XIAOXIN_NOTIFICATION_EVENT_REMINDER,
        .title = "Maximum id",
        .body = "Retry delivery",
    };
    const xiaoxin_notification_event_t oversized = {
        .id = oversized_id,
        .type = XIAOXIN_NOTIFICATION_EVENT_REMINDER,
        .title = "Oversized id",
        .body = "Must be rejected",
    };

    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &maximum));
    assert(xiaoxin_card_pager_notification_upsert_event(&pager, &maximum_retry));
    assert(xiaoxin_card_pager_notification_count(&pager) == 1);
    const xiaoxin_card_item_t* updated =
        xiaoxin_card_pager_notification_find_by_id(&pager, maximum_id);
    assert(updated != NULL);
    assert(strcmp(updated->body, "Retry delivery") == 0);
    assert(!xiaoxin_card_pager_notification_upsert_event(&pager, &oversized));
    assert(xiaoxin_card_pager_notification_count(&pager) == 1);
}

int main(void) {
    home_down_swipe_enters_notifications();
    home_up_swipe_enters_overview();
    reverse_swipes_return_home();
    short_reverse_swipes_return_home_from_open_pages();
    short_vertical_drag_rebounds_to_current_page();
    horizontal_drag_is_not_a_card_page_drag();
    long_drag_can_follow_across_the_screen();
    drag_threshold_controls_snap_vs_rebound();
    visual_page_stays_stable_during_continuous_drag();
    overview_items_are_owned_by_overview_model();
    notification_center_starts_empty_until_events_arrive();
    notification_event_injection_adds_real_notifications();
    notification_low_battery_copy_respects_safe_critical_body();
    notification_event_upsert_replaces_existing_source();
    chat_reply_events_are_ignored_by_notification_center();
    course_reminder_helper_injects_class_notification_once_in_window();
    notification_events_are_sorted_by_priority();
    notification_event_remove_resolves_status_notifications();
    notification_pagination_tracks_current_item();
    notification_dismiss_removes_visible_item();
    notification_dismiss_current_item_clamps_index();
    notification_clear_all_empties_notifications();
    notification_dismiss_invalid_index_returns_false();
    non_home_pages_capture_pet_interaction();
    notification_ttl_expire_removes_only_expired_items();
    notification_expiry_tracking_ignores_persistent_cards();
    notification_find_by_type_survives_priority_sort();
    todo_and_course_reminders_keep_separate_notification_slots();
    delivery_id_upsert_is_idempotent_without_merging_same_type();
    notification_id_rejects_truncation_and_accepts_maximum_length();
    puts("xiaoxin_card_pager tests passed");
    return 0;
}
