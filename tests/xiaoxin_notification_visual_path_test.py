from pathlib import Path
import re


SOURCE = Path(
    "main/boards/waveshare/esp32-s3-touch-lcd-1.46/"
    "esp32-s3-touch-lcd-1.46.cc"
)


def read_source() -> str:
    return SOURCE.read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"function body not found: {signature}")


def normalize_whitespace(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def test_notification_scroll_animation_uses_lightweight_visual_path():
    body = function_body(
        read_source(),
        "static void NotificationScrollSetY(void* obj, int32_t scroll_y)",
    )

    assert "ApplyNotificationScrollVisual((int16_t)scroll_y, false, true);" in body


def test_touch_point_logging_only_happens_on_press_transition():
    body = function_body(read_source(), "void PollTouch(uint32_t now_ms)")

    assert "now_ms - touch_last_point_log_ms_ >= 1000" not in body


def test_moving_notification_card_containers_do_not_render_shadows():
    source = read_source()

    assert "lv_obj_set_style_shadow_color(card.container" not in source
    assert "lv_obj_set_style_shadow_width(card.container" not in source
    assert "lv_obj_set_style_shadow_opa(card.container" not in source
    assert "lv_obj_set_style_shadow_offset_y(card.container" not in source


def test_card_pager_layer_has_subtle_empty_state_background():
    source = read_source()
    body = function_body(source, "void InitializeCardPagerLayer()")

    assert "static constexpr uint32_t k_card_layer_bg_color = 0xe9edf3;" in source
    assert "static constexpr lv_opa_t k_card_layer_bg_opa = static_cast<lv_opa_t>(18);" in source
    assert "lv_obj_set_style_bg_color(card_layer_, lv_color_hex(k_card_layer_bg_color), 0);" in body
    assert "lv_obj_set_style_bg_opa(card_layer_, k_card_layer_bg_opa, 0);" in body
    assert "lv_obj_set_style_bg_opa(card_layer_, LV_OPA_TRANSP, 0);" not in body


def test_overview_uses_time_labels_and_keeps_notifications_page_titleless():
    source = read_source()
    body = function_body(source, "void RenderCardPage(xiaoxin_card_page_t page, bool prepare_entry_animation = false)")

    assert "static constexpr uint32_t k_page_title_color = 0x111111;" in source
    assert "lv_obj_t* overview_time_label_ = nullptr;" in source
    assert "lv_obj_t* overview_date_label_ = nullptr;" in source
    assert "xiaoxin_overview_snapshot_t overview_snapshot_ = {};" in source
    assert "lv_obj_set_style_text_color(overview_time_label_, lv_color_hex(k_page_title_color), 0);" in source
    assert "lv_label_set_text(overview_time_label_, overview_snapshot_.time_text);" in body
    assert "lv_label_set_text(overview_date_label_, overview_snapshot_.date_text);" in body
    assert 'lv_label_set_text(card_title_label_, "\\xE9\\x80\\x9A\\xE7\\x9F\\xA5");' not in body
    assert 'lv_label_set_text(card_title_label_, "\\xE6\\x80\\xBB\\xE8\\xA7\\x88");' not in body


def test_overview_page_consumes_overview_model_snapshot():
    source = read_source()
    body = function_body(source, "void RenderCardPage(xiaoxin_card_page_t page, bool prepare_entry_animation = false)")

    assert '#include "xiaoxin_overview_model.h"' in source
    assert "xiaoxin_overview_state_t overview_state = BuildOverviewState();" in body
    assert "xiaoxin_overview_model_build(&overview_state, &overview_snapshot_);" in body
    assert "const xiaoxin_card_item_t* items = overview_snapshot_.items;" in body
    assert "const uint8_t count = overview_snapshot_.item_count;" in body
    assert "xiaoxin_card_pager_items(page, &items, &count);" not in body


def test_overview_uses_four_reusable_row_labels_without_child_objects():
    source = read_source()
    init_body = function_body(source, "void InitializeCardPagerLayer()")
    render_body = function_body(source, "void RenderCardPage(xiaoxin_card_page_t page, bool prepare_entry_animation = false)")

    assert "static constexpr uint8_t k_overview_visible_row_count = 4;" in source
    assert "lv_obj_t* overview_row_labels_[k_overview_visible_row_count] = {};" in source
    assert "OverviewRow overview_rows_" not in source
    assert "overview_separators_" not in source
    overview_bodies = init_body + render_body
    assert "row.icon_bg" not in overview_bodies
    assert "row.icon" not in overview_bodies
    assert "row.text_box" not in overview_bodies
    assert "row.title" not in overview_bodies
    assert "row.body" not in overview_bodies
    assert "row.detail" not in overview_bodies
    assert "row.arrow" not in overview_bodies
    assert "overview_row_labels_[i] = lv_label_create(card_layer_);" in init_body
    assert "lv_label_set_text(overview_row_labels_[i], row_text);" in render_body


def test_overview_time_labels_are_hidden_before_page_specific_rendering():
    body = function_body(
        read_source(),
        "void RenderCardPage(xiaoxin_card_page_t page, bool prepare_entry_animation = false)",
    )

    assert "AddFlagIfCreated(overview_time_label_, LV_OBJ_FLAG_HIDDEN);" in body
    assert "AddFlagIfCreated(overview_date_label_, LV_OBJ_FLAG_HIDDEN);" in body


def test_status_bar_refreshes_visible_overview_snapshot():
    source = read_source()
    status_body = function_body(source, "virtual void UpdateStatusBar(bool update_all = false) override")
    refresh_body = function_body(source, "void RefreshOverviewPageIfVisible()")

    assert "RefreshOverviewPageIfVisible();" in status_body
    assert "rendered_card_page_ != XIAOXIN_CARD_PAGE_OVERVIEW" in refresh_body
    assert "lv_obj_has_flag(card_layer_, LV_OBJ_FLAG_HIDDEN)" in refresh_body
    assert "RenderCardPage(XIAOXIN_CARD_PAGE_OVERVIEW, false);" in refresh_body


def test_battery_monitor_refreshes_visible_overview_snapshot():
    source = read_source()
    public_refresh_body = function_body(source, "void UpdateOverviewBatterySnapshot(const xiaoxin_battery_snapshot_t& snapshot)")
    battery_body = function_body(source, "void HandleBatterySnapshot(const xiaoxin_battery_snapshot_t& snapshot)")
    overview_state_body = function_body(source, "xiaoxin_overview_state_t BuildOverviewState()")

    assert "DisplayLockGuard lock(this);" in public_refresh_body
    assert "overview_battery_snapshot_ = snapshot;" in public_refresh_body
    assert "overview_battery_has_snapshot_ = true;" in public_refresh_body
    assert "RefreshOverviewPageIfVisible();" in public_refresh_body
    assert "display->UpdateOverviewBatterySnapshot(snapshot);" in battery_body
    assert "xiaoxin_battery_snapshot_t overview_battery_snapshot_ = {};" in source
    assert "bool overview_battery_has_snapshot_ = false;" in source
    assert "state.battery_state = overview_battery_snapshot_.state;" in overview_state_body
    assert "state.battery_power_source = overview_battery_snapshot_.power_source;" in overview_state_body
    assert "state.battery_level = overview_battery_snapshot_.display_level;" in overview_state_body
    assert "state.battery_known = overview_battery_snapshot_.percent_reliable;" in overview_state_body


def test_home_screen_does_not_create_wifi_status_overlay():
    source = read_source()
    status_body = function_body(source, "virtual void UpdateStatusBar(bool update_all = false) override")

    assert "lv_obj_t* system_overlay_ = nullptr;" not in source
    assert "system_overlay_ = lv_obj_create" not in source
    assert "network_label_ = lv_label_create(system_overlay_)" not in source
    assert "lv_label_set_text(network_label_" not in source
    assert "ApplySystemOverlayNetworkStyle();" not in status_body
    assert "SyncNetworkStatusLocked();" in status_body


def test_empty_notifications_use_prominent_panel():
    source = read_source()
    init_body = function_body(source, "void InitializeCardPagerLayer()")
    render_body = function_body(
        source,
        "void RenderNotificationCards(const xiaoxin_card_item_t* /*items*/, uint8_t count, bool prepare_entry_animation)",
    )

    assert "lv_obj_t* notification_empty_panel_ = nullptr;" in source
    assert "lv_obj_set_style_bg_color(notification_empty_panel_, lv_color_hex(k_notification_empty_panel_bg), 0);" in init_body
    assert "lv_obj_set_style_bg_opa(notification_empty_panel_, k_notification_empty_panel_opa, 0);" in init_body
    assert "lv_obj_set_style_text_color(notification_empty_label_, lv_color_hex(k_page_title_color), 0);" in init_body
    assert "RemoveFlagIfCreated(notification_empty_panel_, LV_OBJ_FLAG_HIDDEN);" in render_body
    assert "AddFlagIfCreated(notification_empty_panel_, LV_OBJ_FLAG_HIDDEN);" in render_body


def test_notification_clear_button_stays_centered_on_notifications_page():
    source = read_source()
    init_body = function_body(source, "void InitializeCardPagerLayer()")
    render_body = function_body(
        source,
        "void RenderCardPage(xiaoxin_card_page_t page, bool prepare_entry_animation = false)",
    )

    assert "lv_obj_align(notification_clear_button_, LV_ALIGN_TOP_MID, 0, k_notification_clear_button_y);" in init_body
    assert "lv_obj_align(notification_clear_button_, LV_ALIGN_TOP_RIGHT" not in render_body


def test_notification_clear_button_is_foregrounded_when_visible():
    render_body = function_body(
        read_source(),
        "void RenderNotificationCards(const xiaoxin_card_item_t* /*items*/, uint8_t count, bool prepare_entry_animation)",
    )

    assert "lv_obj_align(notification_clear_button_, LV_ALIGN_TOP_MID, 0, k_notification_clear_button_y);" in render_body
    assert "lv_obj_move_foreground(notification_clear_button_);" in render_body


def test_touch_poll_interval_does_not_exceed_display_refresh_budget():
    source = read_source()

    assert "static constexpr uint32_t k_touch_poll_ms = 16;" in source


def test_assistant_chat_message_does_not_create_notification_card():
    body = function_body(read_source(), "virtual void SetChatMessage")

    assert "AddChatReplyNotificationLocked" not in body
    assert "XIAOXIN_NOTIFICATION_EVENT_CHAT_REPLY" not in body


def test_legacy_low_battery_popup_is_suppressed_to_protect_subtitle():
    source = read_source()
    helper_body = function_body(source, "void HideLegacyLowBatteryPopupLocked()")
    status_body = function_body(source, "virtual void UpdateStatusBar(bool update_all = false) override")
    raise_body = function_body(source, "void RaiseOverlayObjects()")

    assert "lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);" in helper_body
    assert "HideLegacyLowBatteryPopupLocked();" in status_body
    assert "HideLegacyLowBatteryPopupLocked();" in raise_body
    assert "lv_obj_move_foreground(low_battery_popup_);" not in raise_body


def test_low_battery_notification_uses_status_copy_without_percent():
    source = read_source()
    status_body = function_body(source, "virtual void UpdateStatusBar(bool update_all = false) override")

    assert "battery_context_" not in status_body
    assert "battery_snapshot_" not in status_body
    assert "RefreshBatterySnapshotLocked();" not in status_body
    assert "ApplyBatteryOverlayLevel();" not in status_body
    assert "SyncLowBatteryNotificationLocked();" not in status_body


def test_low_battery_notification_uses_typed_notification_event():
    source = read_source()
    body = function_body(source, "void ShowLowBatteryNotification()")

    assert "void SyncLowBatteryNotificationLocked()" not in source
    assert "low_battery_notification_active_" not in source
    assert "last_low_battery_notification_" not in source
    assert "DisplayLockGuard lock(this);" in body
    assert "XIAOXIN_NOTIFICATION_EVENT_LOW_BATTERY" in body
    assert '.title = "低电量"' in body
    assert '.body = "电量低，请尽快充电"' in body
    assert '.tag = "电量"' in body
    assert "UpsertNotificationEventLocked(event);" in body
    assert "RaiseOverlayObjects();" in body


def test_low_battery_notification_reacts_to_state_changes_at_same_percent():
    source = read_source()

    assert "lv_obj_t* battery_overlay_" not in source
    assert "battery_overlay_ = lv_obj_create" not in source
    assert "battery_overlay_box_" not in source
    assert "battery_overlay_fill_" not in source
    assert "battery_overlay_cap_" not in source


def test_battery_overlay_uses_stable_display_level():
    source = read_source()
    status_body = function_body(source, "virtual void UpdateStatusBar(bool update_all = false) override")

    assert "void ApplyBatteryOverlayLevel()" not in source
    assert "battery_snapshot_.display_level" not in status_body
    assert "k_system_battery_w" not in source


def test_notifications_are_persisted_without_heads_up_overlay():
    source = read_source()
    cmake = Path("main/CMakeLists.txt").read_text(encoding="utf-8")
    board_dir = SOURCE.parent
    show_body = function_body(source, "virtual void ShowNotification(const char* notification, int duration_ms = 3000) override")
    upsert_body = function_body(source, "bool UpsertNotificationEventLocked(const xiaoxin_notification_event_t& event)")

    assert "UpsertNotificationEventLocked(event);" in show_body
    assert "xiaoxin_card_pager_notification_upsert_event_at" in upsert_body
    assert "&card_pager_, &event, NowMs()" in normalize_whitespace(upsert_body)
    assert "RefreshNotificationPageIfVisibleLocked();" in upsert_body
    assert "notification_heads_up" not in source
    assert "NotificationHeadsUp" not in source
    assert "xiaoxin_notification_heads_up" not in cmake
    assert not (board_dir / "xiaoxin_notification_heads_up.c").exists()
    assert not (board_dir / "xiaoxin_notification_heads_up.h").exists()
    assert not (board_dir / "xiaoxin_notification_heads_up_glass_texture.c").exists()


def test_notification_maintenance_timer_only_tracks_expiring_cards():
    source = read_source()
    pager_header = Path(
        "main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_card_pager.h"
    ).read_text(encoding="utf-8")
    sync_body = function_body(source, "void SyncNotificationMaintenanceTimerLocked()")
    upsert_body = function_body(source, "bool UpsertNotificationEventLocked(const xiaoxin_notification_event_t& event)")
    timer_body = function_body(source, "void RefreshNotificationsFromTimer()")
    remove_body = function_body(source, "void RemoveNotificationEventLocked(xiaoxin_notification_event_type_t type)")
    dismiss_body = function_body(source, "static void NotificationDismissAnimationCompleted(lv_anim_t* anim)")
    touch_body = function_body(source, "void HandleTouchRelease(uint32_t now_ms)")

    assert "xiaoxin_card_pager_notification_has_pending_expiry" in pager_header
    assert "xiaoxin_card_pager_notification_has_pending_expiry(&card_pager_)" in sync_body
    assert "StartNotificationMaintenanceTimer();" in sync_body
    assert "StopNotificationMaintenanceTimer();" in sync_body
    assert "SyncNotificationMaintenanceTimerLocked();" in upsert_body
    assert "SyncNotificationMaintenanceTimerLocked();" in timer_body
    assert "SyncNotificationMaintenanceTimerLocked();" in remove_body
    assert "self->SyncNotificationMaintenanceTimerLocked();" in dismiss_body
    clear_index = touch_body.index("xiaoxin_card_pager_notification_clear_all(&card_pager_);")
    sync_index = touch_body.index("SyncNotificationMaintenanceTimerLocked();", clear_index)
    assert clear_index < sync_index


def test_display_notification_bridge_maps_ota_to_xiaoxin_notification_center():
    display_header = Path("main/display/display.h").read_text(encoding="utf-8")
    source = read_source()
    upsert_body = function_body(source, "bool UpsertNotification(")
    remove_body = function_body(source, "bool RemoveNotification(const char* id) override")

    assert "virtual bool UpsertNotification(" in display_header
    assert "virtual bool RemoveNotification(" in display_header
    assert 'strcmp(id, "ota_update") == 0' in source
    assert "XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE" in source
    assert "UpsertNotificationEventLocked(event);" in source
    assert 'std::strncmp(id, "xiaoxin_event:", std::strlen("xiaoxin_event:")) == 0' in upsert_body
    assert "const char* event_type" in source
    assert "NotificationTypeForEvent(event_type)" in upsert_body
    assert ".id = id" in upsert_body
    assert "XIAOXIN_NOTIFICATION_EVENT_TODO_REMINDER" in source
    assert "RemoveNotificationEventLocked(XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE);" in remove_body


def test_waveshare_upsert_propagates_pager_full_failure_to_application_fallback():
    source = read_source()
    application = Path("main/application.cc").read_text(encoding="utf-8")
    upsert = function_body(source, "bool UpsertNotification(")
    helper = function_body(
        source,
        "bool UpsertNotificationEventLocked(const xiaoxin_notification_event_t& event)",
    )
    event_handler = function_body(application, "void Application::HandleXiaoxinEvent")

    assert "const bool upserted" in helper
    assert "xiaoxin_card_pager_notification_upsert_event_at" in helper
    assert "return upserted;" in helper
    assert "return UpsertNotificationEventLocked(event);" in upsert
    assert "if (!shown)" in event_handler
    assert "display->ShowNotification" in event_handler
