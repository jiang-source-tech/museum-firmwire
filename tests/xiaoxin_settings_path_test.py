from pathlib import Path
import re


BOARD_SOURCE = Path("main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc")
CMAKE_SOURCE = Path("main/CMakeLists.txt")


def read_source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


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
                return source[brace + 1:index]
    raise AssertionError(f"function body not found: {signature}")


def strip_cpp_comments(source: str) -> str:
    return re.sub(r"//.*?$|/\*.*?\*/", "", source, flags=re.MULTILINE | re.DOTALL)


def collapse_whitespace(source: str) -> str:
    return re.sub(r"\s+", " ", source)


def section_between(source: str, start_marker: str, end_marker: str) -> str:
    start = source.index(start_marker)
    end = source.index(end_marker, start)
    return source[start:end]


def test_settings_model_is_included_and_compiled():
    board = read_source(BOARD_SOURCE)
    cmake = read_source(CMAKE_SOURCE)

    assert '#include "xiaoxin_settings_model.h"' in board
    assert "xiaoxin_settings_model.c" in cmake


def test_boot_single_click_closes_settings_before_chat_or_wifi_config():
    source = read_source(BOARD_SOURCE)
    boot_section = section_between(source, "// Boot Button", "// Power Button")

    settings_check = boot_section.index("IsSettingsOpen()")
    close_call = boot_section.index("CloseSettingsOverlay()")
    start_check = boot_section.index("kDeviceStateStarting")
    toggle_call = boot_section.index("ToggleChatState()")

    assert settings_check < close_call < start_check < toggle_call
    assert "return;" in boot_section[close_call:start_check]


def test_boot_long_press_opens_settings_from_any_runtime_state():
    source = read_source(BOARD_SOURCE)
    boot_section = section_between(source, "// Boot Button", "// Power Button")

    assert "HandleBootLongPress()" in boot_section
    assert "OpenSettingsOverlayFromBootButton()" in source
    helper = function_body(source, "void OpenSettingsOverlayFromBootButton()")
    assert "GetDeviceState()" not in helper
    assert "XIAOXIN_SETTINGS_RUNTIME_IDLE" not in helper
    assert "xiaoxin_settings_can_open" not in helper
    assert "OpenSettingsOverlay()" in helper
    assert "BOOT settings blocked" not in helper
    assert "Settings only in idle" not in helper


def test_boot_long_press_has_observable_feedback_and_hold_fallback():
    source = read_source(BOARD_SOURCE)
    boot_section = strip_cpp_comments(section_between(source, "// Boot Button", "// Power Button"))
    helper = strip_cpp_comments(function_body(source, "void OpenSettingsOverlayFromBootButton()"))

    assert "BUTTON_LONG_PRESS_START" in boot_section
    assert "BUTTON_LONG_PRESS_HOLD" in boot_section
    assert "BUTTON_PRESS_DOWN" in boot_section
    assert "BUTTON_PRESS_UP" in boot_section
    assert 'ESP_LOGI(TAG, "BOOT press down")' in boot_section
    assert 'ESP_LOGI(TAG, "BOOT press up")' in boot_section
    assert "boot_long_press_handled_" in source
    assert "void HandleBootLongPress()" in source
    assert 'ESP_LOGI(TAG, "BOOT long press' in helper
    assert 'ESP_LOGW(TAG, "BOOT settings blocked' not in helper
    assert 'ShowNotification("Settings only in idle"' not in helper
    assert "OpenSettingsOverlay()" in helper


def test_boot_gpio_has_pullup_and_polling_long_press_fallback():
    source = read_source(BOARD_SOURCE)
    custom_init = strip_cpp_comments(function_body(source, "void InitializeButtonsCustom()"))
    boot_section = strip_cpp_comments(section_between(source, "// Boot Button", "// Power Button"))
    fallback = strip_cpp_comments(function_body(source, "void PollBootButtonFallback()"))

    assert "gpio_set_pull_mode(BOOT_BUTTON_GPIO, GPIO_PULLUP_ONLY)" in custom_init
    assert "esp_timer_handle_t boot_poll_timer_" in source
    assert "int64_t boot_press_started_us_" in source
    assert "bool boot_poll_pressed_" in source
    assert "InitializeBootButtonPollingFallback()" in source
    assert "esp_timer_create" in source
    assert "esp_timer_start_periodic" in source
    assert "PollBootButtonFallback()" in source
    assert "gpio_get_level(BOOT_BUTTON_GPIO) == 0" in fallback
    assert 'ESP_LOGI(TAG, "BOOT poll press down")' in fallback
    assert 'ESP_LOGI(TAG, "BOOT poll long press fallback")' in fallback
    assert "HandleBootLongPress()" in fallback
    assert "boot_long_press_handled_ = false" in boot_section
    assert "boot_long_press_handled_ = false" in fallback


def test_settings_overlay_state_is_public_to_board_button_layer():
    source = read_source(BOARD_SOURCE)

    assert "bool IsSettingsOpen()" in source
    assert "void OpenSettingsOverlay()" in source
    assert "void CloseSettingsOverlay()" in source


def test_settings_overlay_labels_are_valid_utf8_chinese():
    source = read_source(BOARD_SOURCE)

    assert 'lv_label_set_text(settings_title_label_, "设置")' in source
    assert 'lv_label_set_text(settings_back_label_, "退出设置")' in source
    assert '"小芯 D151\\n固件 %s\\n本次 %s\\n上次 %s\\n重启 %s\\n欠压 %lu次"' in source
    assert "\\n构建 %s %s" not in source
    assert 'lv_label_set_text(settings_title_label_, "关于")' in source
    assert 'lv_label_set_text(settings_title_label_, "亮度")' in source
    assert 'lv_label_set_text(settings_hint_label_, "重新配网")' in source


def test_settings_back_control_is_independent_from_normal_rows():
    source = read_source(BOARD_SOURCE)

    assert "lv_obj_t* settings_back_row_" in source
    assert "lv_obj_t* settings_back_label_" in source
    assert "lv_obj_set_size(settings_back_row_, k_settings_back_row_w, k_settings_back_row_h)" in source
    assert "lv_obj_set_style_border_width(settings_back_row_, 1, 0)" in source
    assert "lv_obj_set_style_bg_opa(settings_back_row_, static_cast<lv_opa_t>(72), 0)" in source
    assert 'lv_label_set_text(row.value, "›")' in source
    assert 'lv_label_set_text(row.value, "退出设置")' not in source


def test_settings_small_buttons_are_visually_large_enough_for_round_touch_screen():
    source = read_source(BOARD_SOURCE)

    assert "static constexpr int16_t k_settings_back_row_w = 156;" in source
    assert "static constexpr int16_t k_settings_back_row_h = 36;" in source
    assert "static constexpr int16_t k_settings_back_row_y = 240;" in source
    assert "static constexpr int16_t k_settings_brightness_back_button_w = 132;" in source
    assert "static constexpr int16_t k_settings_brightness_back_button_h = 38;" in source
    assert "static constexpr int16_t k_settings_brightness_back_button_y = 180;" in source


def test_settings_back_control_sits_below_last_visible_row():
    source = read_source(BOARD_SOURCE)

    constants = {
        name: int(value)
        for name, value in re.findall(r"static constexpr int16_t (k_settings_[a-z_]+) = (\d+);", source)
    }

    last_row_bottom = (
        constants["k_settings_row_y"]
        + 3 * constants["k_settings_row_pitch"]
        + constants["k_settings_row_h"]
    )
    back_row_top = constants["k_settings_back_row_y"]

    assert back_row_top >= last_row_bottom + 6


def test_settings_back_control_hit_area_does_not_overlap_fourth_row():
    source = read_source(BOARD_SOURCE)

    constants = {
        name: int(value)
        for name, value in re.findall(r"static constexpr int16_t (k_settings_[a-z_]+) = (\d+);", source)
    }

    fourth_row_bottom = (
        constants["k_settings_row_y"]
        + 3 * constants["k_settings_row_pitch"]
        + constants["k_settings_row_h"]
    )
    back_hit_top = constants["k_settings_back_row_y"] - constants["k_settings_button_hit_slop_y"]
    back_visual_bottom = constants["k_settings_back_row_y"] + constants["k_settings_back_row_h"]

    assert constants["k_settings_panel_h"] >= back_visual_bottom + 12
    assert back_hit_top >= fourth_row_bottom + 10


def test_settings_back_control_touch_closes_settings_overlay():
    source = read_source(BOARD_SOURCE)
    overlay_body = function_body(source, "void EnsureSettingsOverlayLocked()")

    assert "settings_back_row_ = lv_obj_create(settings_panel_)" in overlay_body
    assert "lv_obj_set_size(settings_back_row_, k_settings_back_row_w, k_settings_back_row_h)" in overlay_body
    assert 'lv_label_set_text(settings_back_label_, "退出设置")' in overlay_body


def test_settings_touch_manually_closes_settings_back_control():
    source = read_source(BOARD_SOURCE)
    body = function_body(source, "void HandleSettingsTouch(uint16_t x, uint16_t y, bool pressed)")
    body_flat = collapse_whitespace(body)

    assert "settings_view_ == SettingsView::List" in body
    assert "PointInObjWithSlop( settings_back_row_, x, y," in body_flat
    assert "CloseSettingsOverlayLocked();" in body


def test_settings_small_buttons_have_expanded_touch_hit_area():
    source = read_source(BOARD_SOURCE)
    body = function_body(source, "void HandleSettingsTouch(uint16_t x, uint16_t y, bool pressed)")
    body_flat = collapse_whitespace(body)

    assert "k_settings_button_hit_slop_x" in source
    assert "k_settings_button_hit_slop_y" in source
    assert "PointInObjWithSlop" in source
    assert "PointInObjWithSlop( settings_back_row_, x, y, k_settings_button_hit_slop_x, k_settings_button_hit_slop_y )" in body_flat
    assert "PointInObjWithSlop( settings_brightness_back_button_, x, y, k_settings_button_hit_slop_x, k_settings_button_hit_slop_y )" in body_flat


def test_settings_button_touch_is_detected_after_first_pressed_frame():
    source = read_source(BOARD_SOURCE)
    body = strip_cpp_comments(function_body(source, "void HandleSettingsTouch(uint16_t x, uint16_t y, bool pressed)"))
    body_flat = collapse_whitespace(body)

    assert "settings_touch_action_consumed_" in source
    assert "settings_touch_action_consumed_ = false" in body
    back_check = body.index("PointInObjWithSlop")
    assert "!touch_pressed_" not in body[:back_check]
    assert "if (!settings_touch_action_consumed_)" in body_flat


def test_settings_touch_suppresses_card_pager_gestures():
    body = function_body(read_source(BOARD_SOURCE), "void PollTouch(uint32_t now_ms)")

    assert "if (settings_open_)" in body
    assert "HandleSettingsTouch" in body
    assert body.index("touch_->ReadPoint") < body.index("if (settings_open_)")
    assert body.index("if (settings_open_)") < body.index("xiaoxin_card_pager_press")


def test_settings_overlay_registers_lvgl_touch_input_device():
    source = read_source(BOARD_SOURCE)

    assert "SettingsTouchInputReadCallback" not in source
    assert "lv_indev_create()" not in source
    assert "EnsureSettingsTouchIndevLocked();" not in function_body(source, "void AttachTouch(TouchReader* touch)")


def test_settings_rows_use_lvgl_click_events_to_open_items():
    source = read_source(BOARD_SOURCE)
    overlay_body = function_body(source, "void EnsureSettingsOverlayLocked()")
    callback_body = function_body(source, "static void SettingsRowClicked(lv_event_t* e)")

    assert "lv_obj_add_event_cb(row.container, SettingsRowClicked, LV_EVENT_CLICKED, this)" in overlay_body
    assert "lv_event_get_target(e)" in callback_body
    assert "OpenSettingsItemLocked(self->settings_rows_[i].item)" in callback_body


def test_brightness_setting_uses_backlight_api_not_direct_settings_write():
    source = read_source(BOARD_SOURCE)
    body = function_body(source, "void ApplySettingsBrightness(uint8_t brightness, bool permanent)")

    assert "xiaoxin_settings_clamp_percent" in body
    assert "Board::GetInstance().GetBacklight()" in body
    assert "SetBrightness(clamped, permanent)" in body
    assert 'Settings("display"' not in body


def test_brightness_page_exposes_dynamic_slider_not_three_presets():
    source = read_source(BOARD_SOURCE)
    body = strip_cpp_comments(
        function_body(source, "void PaopaoPetDisplay::RenderSettingsBrightnessPage()")
    )

    assert "settings_brightness_value_label_" in source
    assert "settings_brightness_track_" in source
    assert "settings_brightness_fill_" in source
    assert "settings_brightness_thumb_" in source
    assert "settings_brightness_low_label_" in source
    assert "settings_brightness_high_label_" in source
    assert "settings_brightness_back_button_" in source
    assert 'lv_label_set_text(settings_brightness_low_label_, "低")' in source
    assert 'lv_label_set_text(settings_brightness_high_label_, "高")' in source
    assert 'lv_label_set_text(settings_brightness_back_button_label_, "返回")' in source
    assert "ShowSettingsBrightnessSliderLocked" in body
    assert "UpdateSettingsBrightnessSliderLocked" in body
    assert "ApplySettingsBrightness(30)" not in body
    assert "ApplySettingsBrightness(70)" not in body
    assert "ApplySettingsBrightness(100)" not in body
    assert '"30  70  100"' not in body
    assert '"拖动调节 · BOOT 返回"' not in body


def test_brightness_slider_drag_previews_and_release_persists():
    source = read_source(BOARD_SOURCE)
    slider_body = strip_cpp_comments(function_body(source, "void HandleSettingsBrightnessSliderEvent(lv_event_t* e)"))
    apply_body = strip_cpp_comments(function_body(source, "void ApplySettingsBrightness(uint8_t brightness, bool permanent)"))

    assert "lv_indev_get_point" in slider_body
    assert "xiaoxin_settings_brightness_from_x" in slider_body
    assert "ApplySettingsBrightness(settings_brightness_value_, false)" in slider_body
    assert "ApplySettingsBrightness(settings_brightness_value_, true)" in slider_body
    assert "SetBrightness(clamped, permanent)" in apply_body


def test_brightness_manual_drag_captures_until_release():
    source = read_source(BOARD_SOURCE)
    touch_body = collapse_whitespace(
        strip_cpp_comments(function_body(source, "void HandleSettingsTouch(uint16_t x, uint16_t y, bool pressed)"))
    )

    assert "settings_brightness_dragging_ || SettingsBrightnessSliderContains(x, y)" in touch_body
    assert "settings_brightness_dragging_ = false; ApplySettingsBrightness(settings_brightness_value_, true)" in touch_body


def test_brightness_preview_is_deduplicated_to_avoid_drag_log_flood():
    source = read_source(BOARD_SOURCE)
    apply_body = strip_cpp_comments(function_body(source, "void ApplySettingsBrightness(uint8_t brightness, bool permanent)"))

    assert "settings_brightness_applied_value_" in source
    assert "settings_brightness_applied_permanent_" in source
    assert "settings_brightness_applied_value_ == clamped" in apply_body
    assert "settings_brightness_applied_permanent_ == permanent" in apply_body
    assert "return;" in apply_body[:apply_body.index("SetBrightness(clamped, permanent)")]


def test_brightness_page_back_button_returns_to_settings_list():
    source = read_source(BOARD_SOURCE)
    overlay_body = strip_cpp_comments(function_body(source, "void EnsureSettingsOverlayLocked()"))
    touch_body = strip_cpp_comments(function_body(source, "void HandleSettingsTouch(uint16_t x, uint16_t y, bool pressed)"))

    assert "settings_brightness_back_button_ = lv_obj_create(settings_panel_)" in overlay_body
    assert 'lv_label_set_text(settings_brightness_back_button_label_, "返回")' in overlay_body
    assert "settings_view_ == SettingsView::Brightness" in touch_body
    assert "PointInObjWithSlop" in touch_body
    assert "settings_brightness_back_button_" in touch_body
    assert "RenderSettingsListLocked()" in touch_body


def test_wifi_reconfiguration_reuses_existing_entrypoint():
    source = read_source(BOARD_SOURCE)
    body = function_body(source, "void RequestSettingsWifiConfig()")

    assert "CloseSettingsOverlay()" in body
    assert "EnterWifiConfigMode()" in body


def test_wifi_page_defers_board_reconfiguration_until_after_display_lock():
    source = read_source(BOARD_SOURCE)
    body = strip_cpp_comments(
        function_body(source, "void PaopaoPetDisplay::RenderSettingsWifiPage()")
    )
    loop_body = strip_cpp_comments(function_body(source, "void RunRenderLoop()"))
    lock_start = loop_body.index("DisplayLockGuard lock(this)")
    lock_end = loop_body.index("}", lock_start)
    helper_call = loop_body.index("RequestSettingsWifiConfigFromSettingsPage()")

    assert "settings_wifi_config_requested_ = true" in body
    assert "CustomBoard::Instance()->RequestSettingsWifiConfig()" not in body
    assert lock_start < loop_body.index("ConsumeSettingsWifiConfigRequestLocked()") < lock_end
    assert lock_end < helper_call
    assert "void CloseSettingsOverlayLocked()" in source
    assert "static void RequestSettingsWifiConfigFromSettingsPage()" in source


def test_target_settings_caps_do_not_enable_audio_or_vibration_initially():
    body = function_body(
        read_source(BOARD_SOURCE),
        "xiaoxin_settings_caps_t PaopaoPetDisplay::SettingsCaps() const",
    )

    assert ".has_audio_output = false" in body
    assert ".has_vibration = false" in body


def test_power_save_setting_is_only_enabled_when_timer_is_initialized():
    source = read_source(BOARD_SOURCE)
    caps_body = strip_cpp_comments(
        function_body(source, "xiaoxin_settings_caps_t PaopaoPetDisplay::SettingsCaps() const")
    )
    timer_body = strip_cpp_comments(function_body(source, "void InitializePowerSaveTimer()"))

    assert "PowerSaveTimer* power_save_timer_" in source
    assert "InitializePowerSaveTimer()" in source
    assert ".has_power_save_scheduler = power_save_timer_ != nullptr" in caps_body
    assert "new PowerSaveTimer(-1, 60, -1)" in source
    assert "OnShutdownRequest" not in timer_body
    assert "ShowLowPowerClockScreen()" in source
    assert "HideLowPowerClockScreen()" in source
    assert "SetPowerSaveMode(true)" not in source
    assert "SetPowerSaveMode(false)" not in source


def test_power_save_timer_is_reset_by_local_button_and_touch_activity():
    source = read_source(BOARD_SOURCE)
    poll_body = strip_cpp_comments(function_body(source, "void PollTouch(uint32_t now_ms)"))
    loop_body = strip_cpp_comments(function_body(source, "void RunRenderLoop()"))
    lock_start = loop_body.index("DisplayLockGuard lock(this)")
    lock_end = loop_body.index("}", lock_start)
    wake_helper_call = loop_body.index("WakePowerSaveTimerFromTouch()")
    boot_section = strip_cpp_comments(section_between(source, "// Boot Button", "// Power Button"))
    power_section = strip_cpp_comments(section_between(source, "// Power Button", "public:"))

    assert "void WakePowerSaveTimer()" in source
    assert "power_save_timer_->WakeUp()" in function_body(source, "void WakePowerSaveTimer()")
    assert "WakePowerSaveTimer();" in function_body(source, "void HandleBootLongPress()")
    assert boot_section.count("self->HandleBootLongPress();") >= 2
    assert power_section.count("self->WakePowerSaveTimer();") >= 2
    assert "power_save_timer_wake_requested_ = true" in poll_body
    assert "WakePowerSaveTimerFromTouch()" not in poll_body
    assert lock_start < loop_body.index("ConsumePowerSaveTimerWakeRequestLocked()") < lock_end
    assert lock_end < wake_helper_call


def test_power_save_row_renders_status_capsule_and_click_toggles_setting():
    source = read_source(BOARD_SOURCE)
    render_body = strip_cpp_comments(
        function_body(source, "void RenderSettingsListLocked()")
    )
    toggle_body = strip_cpp_comments(
        function_body(source, "void ToggleSettingsPowerSaveLocked()")
    )
    open_body = strip_cpp_comments(
        function_body(source, "void OpenSettingsItemLocked(xiaoxin_settings_item_t item)")
    )

    assert "ApplySettingsPowerSaveRowStyleLocked(row, power_save_enabled)" in render_body
    assert "xiaoxin_settings_power_save_value_label(power_save_enabled)" in render_body
    assert "Settings settings(\"wifi\", true)" in toggle_body
    assert "settings.SetBool(\"sleep_mode\", enabled)" in toggle_body
    assert "power_save_timer->SetEnabled(enabled)" in toggle_body
    assert "case XIAOXIN_SETTINGS_ITEM_POWER_SAVE:" in open_body
    assert "ToggleSettingsPowerSaveLocked()" in open_body
    assert "RenderSettingsListLocked()" in open_body[open_body.index("case XIAOXIN_SETTINGS_ITEM_POWER_SAVE:"):]


def test_power_save_toggle_does_not_use_bottom_hint_that_overlaps_exit_button():
    source = read_source(BOARD_SOURCE)

    assert "省电已开启" not in source
    assert "省电已关闭" not in source


def test_power_save_no_longer_tints_removed_home_battery_meter():
    source = read_source(BOARD_SOURCE)

    assert "void ApplyBatteryOverlayLevel()" not in source
    assert "k_battery_meter_power_save" not in source
    assert "xiaoxin_settings_power_save_battery_color(" not in source


def test_about_page_prioritizes_xiaoxin_product_identity():
    source = read_source(BOARD_SOURCE)
    body = function_body(source, "void RenderSettingsAboutPage()")

    assert "#include <esp_app_desc.h>" in source
    assert '#include "runtime_health.h"' in source
    assert "esp_app_get_description()" in body
    assert "RuntimeHealthReadSnapshot(&snapshot)" in body
    assert "xiaoxin_runtime_health_format_duration" in body
    assert "xiaoxin_runtime_health_reset_label(snapshot.last_reset_kind)" in body
    assert "小芯 D151" in body
    assert "固件 %s" in body
    assert "本次 %s" in body
    assert "上次 %s" in body
    assert "重启 %s" in body
    assert "欠压 %lu次" in body
    assert "snapshot.brownout_count" in body
    assert "桌面助手" not in body
    assert "构建 %s %s" not in body
    assert "Waveshare ESP32-S3 Touch LCD 1.46" not in body


def test_about_page_body_is_lifted_above_round_screen_bottom():
    source = read_source(BOARD_SOURCE)
    body = function_body(source, "void RenderSettingsAboutPage()")

    assert "static constexpr int16_t k_settings_about_body_y = 92;" in source
    assert "lv_obj_align(settings_hint_label_, LV_ALIGN_TOP_MID, 0, k_settings_about_body_y)" in body
