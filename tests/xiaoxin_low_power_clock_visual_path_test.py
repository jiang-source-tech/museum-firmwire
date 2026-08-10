from pathlib import Path

SOURCE = Path("main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc")
CMAKE_SOURCE = Path("main/CMakeLists.txt")


def read_source():
    return SOURCE.read_text(encoding="utf-8")


def compact(text):
    return "".join(text.split())


def test_low_power_clock_model_is_compiled_into_main_component():
    cmake = CMAKE_SOURCE.read_text(encoding="utf-8")

    assert "xiaoxin_low_power_clock_model.c" in cmake


def test_low_power_clock_objects_exist_and_are_hidden_by_default():
    source = read_source()
    assert "lv_obj_t* low_power_clock_layer_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_outer_arc_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_inner_arc_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_time_glow_label_ = nullptr;" in source
    assert "low_power_clock_time_shadow_label_" not in source
    assert "lv_obj_t* low_power_clock_time_label_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_date_label_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_sync_dot_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_sync_label_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_hint_label_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_second_label_ = nullptr;" in source
    assert "lv_obj_add_flag(low_power_clock_layer_, LV_OBJ_FLAG_HIDDEN);" in source


def test_low_power_clock_uses_wave_amoled_time_and_arc_layout():
    source = read_source()
    assert "LV_FONT_DECLARE(font_puhui_basic_30_4);" in source
    assert "lv_obj_t* low_power_clock_top_dial_ = nullptr;" in source
    assert "lv_arc_create(low_power_clock_layer_)" in source
    assert "lv_obj_add_flag(low_power_clock_outer_arc_, LV_OBJ_FLAG_HIDDEN);" in source
    assert "lv_obj_add_flag(low_power_clock_inner_arc_, LV_OBJ_FLAG_HIDDEN);" in source
    assert "low_power_clock_time_glow_label_ = lv_label_create(low_power_clock_layer_);" in source
    assert "low_power_clock_time_shadow_label_" not in source
    assert "const lv_font_t* clock_font = &font_puhui_basic_30_4;" in source
    assert "ConfigureLowPowerTimeLabel(low_power_clock_time_glow_label_, clock_font, 0x75DFFF, LowPowerClockOpaPercent(24), 1, 560, 150, 28);" in source
    assert "ConfigureLowPowerTimeLabel(low_power_clock_time_label_, clock_font, 0xF6FAFF, LV_OPA_COVER, 1, 532, 142, 24);" in source
    assert "static constexpr int16_t k_low_power_time_y_adjust = 20;" in source
    compact_source = compact(source)
    assert compact("RefreshLowPowerTimeLabelLocked(low_power_clock_time_glow_label_, low_power_clock_snapshot_.time_text, LV_ALIGN_TOP_LEFT, LowPowerRefX(196), LowPowerRefY(150) + k_low_power_time_y_adjust);") in compact_source
    assert compact("RefreshLowPowerTimeLabelLocked(low_power_clock_time_label_, low_power_clock_snapshot_.time_text, LV_ALIGN_TOP_LEFT, LowPowerRefX(196), LowPowerRefY(150) + k_low_power_time_y_adjust);") in compact_source
    assert "lv_obj_align(low_power_clock_time_label_, LV_ALIGN_CENTER, 0, -10);" not in source
    assert "lv_obj_set_style_transform_pivot_x(label, lv_obj_get_width(label) / 2, 0);" in source
    assert "lv_obj_set_style_transform_pivot_y(label, lv_obj_get_height(label) / 2, 0);" in source
    assert "lv_obj_add_flag(low_power_clock_hint_label_, LV_OBJ_FLAG_HIDDEN);" in source


def test_low_power_clock_uses_reference_sprite_coordinate_mapping():
    source = read_source()

    assert "static constexpr int16_t k_low_power_ref_lcd_w = 466;" in source
    assert "static constexpr int16_t k_low_power_ref_lcd_h = 466;" in source
    assert "static constexpr int16_t k_low_power_ref_sprite_x = 40;" in source
    assert "static constexpr int16_t k_low_power_ref_sprite_y = 120;" in source
    assert "static constexpr int16_t LowPowerRefX(int16_t sprite_x)" in source
    assert "static constexpr int16_t LowPowerRefY(int16_t sprite_y)" in source
    assert "static constexpr int16_t LowPowerRefLen(int16_t value)" in source
    assert "((k_low_power_ref_sprite_x + sprite_x) * DISPLAY_WIDTH + k_low_power_ref_lcd_w / 2) / k_low_power_ref_lcd_w" in source
    assert "((k_low_power_ref_sprite_y + sprite_y) * DISPLAY_HEIGHT + k_low_power_ref_lcd_h / 2) / k_low_power_ref_lcd_h" in source

    compact_source = compact(source)
    for snippet in [
        "const int16_t x = LowPowerRefX((int16_t)(190 + i * 6U));",
        "const int16_t y = LowPowerRefY((int16_t)(90 - j * 4U));",
        "lv_obj_align(cell, LV_ALIGN_TOP_LEFT, LowPowerRefX(200 + i * 30), LowPowerRefY(2));",
        "lv_obj_align(low_power_clock_left_red_dash_, LV_ALIGN_TOP_LEFT, LowPowerRefX(54), LowPowerRefY(120));",
        "lv_obj_align(low_power_clock_left_gray_panel_, LV_ALIGN_TOP_LEFT, LowPowerRefX(0), LowPowerRefY(145));",
        "lv_obj_align(low_power_clock_center_rule_, LV_ALIGN_TOP_LEFT, LowPowerRefX(180), LowPowerRefY(136));",
        "lv_obj_align(low_power_clock_center_stem_, LV_ALIGN_TOP_LEFT, LowPowerRefX(186), LowPowerRefY(130));",
        "lv_obj_align(low_power_clock_blue_top_block_, LV_ALIGN_TOP_LEFT, LowPowerRefX(136), LowPowerRefY(0));",
        "lv_obj_align(low_power_clock_blue_bottom_block_, LV_ALIGN_TOP_LEFT, LowPowerRefX(136), LowPowerRefY(224));",
        "lv_obj_align(low_power_clock_top_dial_, LV_ALIGN_TOP_LEFT, LowPowerRefX(372 - 28), LowPowerRefY(76 - 28));",
        "lv_obj_align(low_power_clock_title_label_, LV_ALIGN_TOP_LEFT, LowPowerRefX(186), LowPowerRefY(104));",
        "lv_obj_align(low_power_clock_second_label_, LV_ALIGN_TOP_LEFT, LowPowerRefX(24), LowPowerRefY(124));",
    ]:
        assert compact(snippet) in compact_source


def test_low_power_clock_uses_wave_amoled_visual_language():
    source = read_source()

    assert "lv_obj_t* low_power_clock_outer_arc_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_inner_arc_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_top_dial_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_date_label_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_sync_dot_ = nullptr;" in source
    assert "lv_obj_t* low_power_clock_sync_label_ = nullptr;" in source
    assert "lv_obj_add_flag(low_power_clock_outer_arc_, LV_OBJ_FLAG_HIDDEN);" in source
    assert "lv_arc_set_angles(low_power_clock_outer_arc_, marker" not in source
    assert "lv_obj_add_flag(low_power_clock_inner_arc_, LV_OBJ_FLAG_HIDDEN);" in source
    assert "lv_obj_add_flag(low_power_clock_date_label_, LV_OBJ_FLAG_HIDDEN);" in source
    assert "lv_obj_add_flag(low_power_clock_sync_label_, LV_OBJ_FLAG_HIDDEN);" in source


def test_low_power_clock_hint_keeps_smaller_theme_font():
    source = read_source()
    init_section = source[
        source.index("void InitializeLowPowerClockLayerLocked()"):
        source.index("void PollTouch", source.index("void InitializeLowPowerClockLayerLocked()"))
    ]

    assert "const lv_font_t* hint_font = lvgl_theme != nullptr && lvgl_theme->text_font() != nullptr" in init_section
    assert "lv_obj_set_style_text_font(low_power_clock_hint_label_, hint_font, 0);" in init_section
    assert "lv_obj_set_style_text_font(low_power_clock_hint_label_, clock_font, 0);" not in init_section


def test_low_power_clock_state_includes_date_and_sync_status_without_battery_monitoring():
    source = read_source()
    build_state_section = source[
        source.index("xiaoxin_low_power_clock_state_t BuildLowPowerClockState("):
        source.index("void RefreshLowPowerClockScreenLocked(bool force)")
    ]

    assert "battery_snapshot_" not in build_state_section
    assert "GetTimeSyncStatus()" in build_state_section
    assert "state.month = timeinfo.tm_mon + 1;" in build_state_section
    assert "state.day = timeinfo.tm_mday;" in build_state_section
    assert "state.weekday = timeinfo.tm_wday;" in build_state_section
    assert "state.second = timeinfo.tm_sec;" in build_state_section
    assert "state.battery_known" not in build_state_section
    assert "state.battery_percent" not in build_state_section


def test_low_power_clock_refresh_updates_orbit_secondary_labels():
    source = read_source()
    refresh_section = source[
        source.index("void RefreshLowPowerClockScreenLocked(bool force)"):
        source.index("void RefreshLowPowerClockAnimationLocked()")
    ]

    assert "lv_label_set_text(low_power_clock_date_label_, low_power_clock_snapshot_.date_text);" in refresh_section
    assert "low_power_clock_battery_label_" not in refresh_section
    assert "low_power_clock_snapshot_.battery_text" not in refresh_section
    assert "lv_label_set_text(low_power_clock_sync_label_, low_power_clock_snapshot_.sync_text);" in refresh_section
    assert "lv_obj_set_style_bg_color(low_power_clock_sync_dot_, lv_color_hex(low_power_clock_snapshot_.sync_color_hex), 0);" in refresh_section
    assert "UpdateLowPowerSecondGaugeLocked(low_power_clock_snapshot_.second_text, state.second);" in refresh_section
    assert "low_power_clock_last_second_" in refresh_section
    assert "xiaoxin_low_power_clock_should_refresh(low_power_clock_last_minute_, current_minute, low_power_clock_last_second_, current_second)" in refresh_section


def test_low_power_clock_no_longer_uses_small_icon_left_layout():
    source = read_source()
    assert "lv_obj_t* low_power_clock_icon_label_ = nullptr;" not in source
    assert "lv_obj_align_to(low_power_clock_time_label_, low_power_clock_icon_label_, LV_ALIGN_OUT_RIGHT_MID" not in source
    assert "lv_label_set_text(low_power_clock_icon_label_" not in source


def test_low_power_clock_enters_with_dim_backlight_and_exits_with_restore():
    source = read_source()
    assert "ShowLowPowerClockScreen()" in source
    assert "HideLowPowerClockScreen()" in source
    assert "StartLowPowerClockRefreshTimer();" in source
    assert "StopLowPowerClockRefreshTimer();" in source
    assert "backlight->SetBrightness(low_power_clock_snapshot_.brightness_percent, false);" in source
    assert "backlight->RestoreBrightness();" in source


def test_power_button_still_wakes_power_save_timer():
    source = read_source()
    power_section = source[source.index("// Power Button"):]
    assert "self->WakePowerSaveTimer();" in power_section


def test_power_save_timer_callbacks_show_and_hide_clock_screen():
    source = read_source()
    assert "display->ShowLowPowerClockScreen();" in source
    assert "display->HideLowPowerClockScreen();" in source
    assert "GetDisplay()->SetPowerSaveMode(true);" not in source
    assert "GetDisplay()->SetPowerSaveMode(false);" not in source


def test_power_save_mode_remains_task3_timer_boundary():
    source = read_source()
    power_save_section = source[
        source.index("virtual void SetPowerSaveMode(bool on) override {"):
        source.index("void AttachTouch", source.index("virtual void SetPowerSaveMode(bool on) override {"))
    ]
    assert "LvglDisplay::SetPowerSaveMode(on);" in power_save_section
    assert "ShowLowPowerClockScreen();" not in power_save_section
    assert "HideLowPowerClockScreen();" not in power_save_section


def test_low_power_clock_refresh_uses_dedicated_timer_with_display_lock():
    source = read_source()
    assert "esp_timer_handle_t low_power_clock_timer_ = nullptr;" in source
    assert "InitializeLowPowerClockRefreshTimer();" in source
    assert ".name = \"low_power_clock\"" in source
    assert "void StartLowPowerClockRefreshTimer()" in source
    assert "void StopLowPowerClockRefreshTimer()" in source
    assert "static constexpr uint64_t k_low_power_clock_timer_period_us = 50 * 1000;" in source
    assert "esp_timer_start_periodic(low_power_clock_timer_, k_low_power_clock_timer_period_us)" in source
    assert "esp_timer_stop(low_power_clock_timer_)" in source

    timer_refresh_section = source[
        source.index("void RefreshLowPowerClockScreenFromTimer()"):
        source.index("void InitializeLowPowerClockRefreshTimer()")
    ]
    assert "DisplayLockGuard lock(this);" in timer_refresh_section
    assert "if (!low_power_clock_visible_)" in timer_refresh_section
    assert "RefreshLowPowerClockAnimationLocked();" in timer_refresh_section
    assert "RefreshLowPowerClockScreenLocked(false);" in timer_refresh_section
    assert "UpdateLowPowerSecondGaugeLocked(" not in timer_refresh_section

    animation_section = source[
        source.index("void RefreshLowPowerClockAnimationLocked()"):
        source.index("void RefreshLowPowerClockScreenFromTimer()")
    ]
    assert "lv_arc_set_rotation(low_power_clock_inner_arc_, start);" in animation_section

    init_section = source[
        source.index("void InitializeLowPowerClockRefreshTimer()"):
        source.index("void StartLowPowerClockRefreshTimer()")
    ]
    assert "esp_timer_start_periodic" not in init_section


def test_low_power_clock_orbit_animation_updates_two_rings():
    source = read_source()
    animation_section = source[
        source.index("void RefreshLowPowerClockAnimationLocked()"):
        source.index("void RefreshLowPowerClockScreenFromTimer()")
    ]

    assert "lv_arc_set_rotation(low_power_clock_inner_arc_, start);" in animation_section
    assert "lv_arc_set_rotation(low_power_clock_outer_arc_" not in animation_section
    assert "lv_arc_set_rotation(low_power_clock_top_dial_, (start + 42) % 360);" in animation_section
    assert "lv_obj_set_style_opa(low_power_clock_sync_dot_, dot_opa, 0);" in animation_section


def test_backlight_transition_timer_is_not_restarted_while_active():
    source = Path("main/boards/common/backlight.cc").read_text(encoding="utf-8")
    assert "esp_timer_is_active(transition_timer_)" in source
    assert "if (!esp_timer_is_active(transition_timer_))" in source


def test_low_power_clock_refresh_is_not_in_hot_render_loop():
    source = read_source()
    render_loop_section = source[
        source.index("void RunRenderLoop()"):
        source.index("static void RenderTask", source.index("void RunRenderLoop()"))
    ]
    assert "RefreshLowPowerClockScreenLocked(false);" not in render_loop_section


def test_low_power_clock_uses_wave_amoled_objects_instead_of_snake():
    source = read_source()

    assert "static constexpr uint8_t k_low_power_wave_bar_count = 20;" in source
    assert "lv_obj_t* low_power_wave_bar_layer_ = nullptr;" in source
    assert "lv_obj_t* low_power_wave_bars_[k_low_power_wave_bar_count] = {};" not in source
    assert "uint8_t low_power_wave_bar_levels_[k_low_power_wave_bar_count] = {};" in source
    assert "uint8_t low_power_wave_bar_target_levels_[k_low_power_wave_bar_count] = {};" in source
    assert "uint32_t low_power_wave_random_state_" in source
    assert "lv_obj_t* low_power_clock_snake_bg_" not in source
    assert "LowPowerSnakeCell" not in source
    assert "StartNewLowPowerSnakeGameLocked();" not in source


def test_low_power_clock_ports_wave_reference_static_effects():
    source = read_source()
    init_section = source[
        source.index("void InitializeLowPowerClockLayerLocked()"):
        source.index("void PollTouch", source.index("void InitializeLowPowerClockLayerLocked()"))
    ]

    assert "InitializeLowPowerWaveReferenceBlocksLocked(hint_font);" in init_section
    assert "InitializeLowPowerSecondGaugeLocked(hint_font);" in init_section
    assert "InitializeLowPowerWaveBarsLocked();" in init_section
    assert "InitializeLowPowerWaveReferenceLabelsLocked(hint_font);" in init_section
    assert "low_power_wave_bar_layer_ = lv_obj_create(low_power_clock_layer_);" in source
    assert "lv_obj_set_size(low_power_wave_bar_layer_, DISPLAY_WIDTH, DISPLAY_HEIGHT);" in source
    assert "lv_obj_add_event_cb(low_power_wave_bar_layer_, LowPowerWaveMotionDrawEvent, LV_EVENT_DRAW_MAIN, this);" in source
    assert "low_power_clock_second_label_ = lv_label_create(low_power_clock_layer_);" in source
    assert "const int16_t x_ref = (int16_t)std::lround(118.0 * std::cos(k_low_power_ref_rad * a)) - 2;" in source
    assert "const int16_t y_ref = (int16_t)std::lround(118.0 * std::sin(k_low_power_ref_rad * a)) + 120;" in source
    assert "lv_draw_line(layer, &tick_dsc);" in source
    assert "lv_obj_align(low_power_clock_second_label_, LV_ALIGN_TOP_LEFT, LowPowerRefX(24), LowPowerRefY(124));" in source
    assert "lv_obj_center(low_power_clock_micro_label_);" in source
    assert "LowPowerRefLen(72)," in source
    assert "lv_obj_set_style_text_font(low_power_clock_micro_label_, hint_font, 0);" in source
    assert "lv_obj_align(low_power_clock_left_red_dash_, LV_ALIGN_TOP_LEFT, LowPowerRefX(54), LowPowerRefY(120));" in source
    assert "lv_obj_align(low_power_clock_left_red_dash_, LV_ALIGN_TOP_LEFT, 88, marker_y);" not in source
    assert 'lv_label_set_text(low_power_clock_micro_label_, "second");' in source
    assert 'lv_label_set_text(low_power_clock_micro_label_, "mil");' not in source
    assert '"AMOLED"' not in source
    assert '"*HRS*"' not in source
    assert "static constexpr const char* k_runtime_letters[] = {\"M\", \"U\", \"S\", \"E\", \"U\", \"M\"};" in source
    assert "lv_obj_t* cell = CreateLowPowerBlock(low_power_clock_layer_, LowPowerRefLen(26), LowPowerRefLen(26), 0x30363D, LowPowerClockOpaPercent(82));" in source
    assert "lv_obj_align(cell, LV_ALIGN_TOP_LEFT, LowPowerRefX(200 + i * 30), LowPowerRefY(2));" in source
    assert "lv_label_set_text(letter, k_runtime_letters[i]);" in source
    assert 'lv_label_set_text(low_power_clock_mode_label_, "runtime");' not in source
    assert 'lv_label_set_text(low_power_clock_title_label_, "MUSEUM TIME");' in source
    assert '"VOLOS TIME"' not in source
    assert 'lv_label_set_text(low_power_clock_probe_label_, "CAN YOU READ THIS");' in source


def test_low_power_clock_date_uses_larger_high_contrast_label_style():
    source = read_source()
    init_section = source[
        source.index("void InitializeLowPowerClockLayerLocked()"):
        source.index("low_power_clock_sync_dot_ = lv_obj_create(low_power_clock_layer_);")
    ]

    assert "const lv_font_t* date_font = &font_puhui_basic_20_4;" in init_section
    assert "lv_obj_set_style_text_opa(low_power_clock_date_label_, LV_OPA_COVER, 0);" in init_section
    assert "lv_obj_set_style_text_font(low_power_clock_date_label_, date_font, 0);" in init_section
    assert "lv_obj_set_style_text_font(low_power_clock_date_label_, hint_font, 0);" not in init_section


def test_low_power_clock_wave_animation_updates_bars_and_instrument_arcs():
    source = read_source()
    animation_section = source[
        source.index("void RefreshLowPowerClockAnimationLocked()"):
        source.index("void RefreshLowPowerClockScreenFromTimer()")
    ]

    assert "UpdateLowPowerWaveBarsLocked();" in animation_section
    assert "lv_arc_set_rotation(low_power_clock_inner_arc_, start);" in animation_section
    assert "lv_arc_set_rotation(low_power_clock_outer_arc_" not in animation_section
    assert "lv_arc_set_rotation(low_power_clock_top_dial_, (start + 42) % 360);" in animation_section
    assert "AdvanceLowPowerSnakeLocked();" not in animation_section
    assert "lv_obj_invalidate(low_power_clock_snake_bg_);" not in animation_section


def test_low_power_clock_left_gauge_rolls_like_wave_reference():
    source = read_source()

    assert "static constexpr uint8_t k_low_power_left_gauge_point_count = 120;" in source
    assert "static constexpr uint8_t k_low_power_left_gauge_tick_count = 40;" in source
    assert "lv_obj_t* low_power_left_gauge_pixels_[k_low_power_left_gauge_point_count] = {};" not in source
    assert "lv_obj_t* low_power_left_gauge_ticks_[k_low_power_left_gauge_tick_count] = {};" not in source
    assert "void DrawLowPowerLeftGaugeLocked(lv_layer_t* layer, uint16_t angle)" in source

    draw_section = source[
        source.index("void DrawLowPowerLeftGaugeLocked(lv_layer_t* layer, uint16_t angle)"):
        source.index("void DrawLowPowerWaveBarsLocked", source.index("void DrawLowPowerLeftGaugeLocked(lv_layer_t* layer, uint16_t angle)"))
    ]
    assert "const uint16_t a = (uint16_t)((angle + i * 3U) % 360U);" in draw_section
    assert "std::cos(k_low_power_ref_rad * a)" in draw_section
    assert "std::sin(k_low_power_ref_rad * a)" in draw_section
    assert "lv_draw_rect(layer, &pixel_dsc, &pixel_area);" in draw_section
    assert "lv_draw_line(layer, &tick_dsc);" in draw_section

    animation_section = source[
        source.index("void RefreshLowPowerClockAnimationLocked()"):
        source.index("void RefreshLowPowerClockScreenFromTimer()")
    ]
    assert "UpdateLowPowerLeftGaugeTicksLocked(start);" not in animation_section
    assert "lv_obj_invalidate(low_power_wave_bar_layer_);" in animation_section
    assert "lv_obj_create(low_power_clock_layer_)" not in animation_section


def test_low_power_clock_wave_animation_uses_single_draw_layer_and_fast_bouncy_targets():
    source = read_source()

    assert "lv_obj_t* low_power_wave_bars_[k_low_power_wave_bar_count] = {};" not in source
    assert "uint8_t low_power_wave_bar_target_levels_[k_low_power_wave_bar_count] = {};" in source
    assert "static constexpr uint8_t k_low_power_wave_bar_step = 3;" in source
    assert "static void LowPowerWaveMotionDrawEvent(lv_event_t* e)" in source

    update_section = source[
        source.index("void UpdateLowPowerWaveBarsLocked()"):
        source.index("void DrawLowPowerLeftGaugeLocked", source.index("void UpdateLowPowerWaveBarsLocked()"))
    ]
    assert "low_power_wave_bar_target_levels_[i]" in update_section
    assert "(low_power_clock_animation_tick_ % 4U) == 0U" not in update_section
    assert "low_power_wave_bar_target_levels_[i] = target;" in update_section
    assert "if (level < target)" in update_section
    assert "std::min<uint8_t>(k_low_power_wave_bar_step, (uint8_t)(target - level))" in update_section
    assert "else if (level > target)" in update_section
    assert "std::min<uint8_t>(k_low_power_wave_bar_step, (uint8_t)(level - target))" in update_section
    assert "low_power_wave_bar_levels_[i] = level;" in update_section
    assert "lv_obj_set_size(low_power_wave_bars_[i]" not in update_section
    assert "lv_obj_set_pos(low_power_wave_bars_[i]" not in update_section


def test_low_power_clock_uses_supported_lvgl_opacity_values():
    source = read_source()

    assert "static constexpr lv_opa_t LowPowerClockOpaPercent(uint8_t percent)" in source
    for unsupported_opa in ("LV_OPA_35", "LV_OPA_55", "LV_OPA_75", "LV_OPA_85", "LV_OPA_95"):
        assert unsupported_opa not in source
