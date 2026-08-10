from pathlib import Path


LCD_HEADER = Path("main/display/lcd_display.h")
LCD_SOURCE = Path("main/display/lcd_display.cc")
APPLICATION_SOURCE = Path("main/application.cc")
BOARD_DISPLAY_SOURCE = Path(
    "main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc"
)


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
                return source[brace + 1 : index]
    raise AssertionError(f"function body not found: {signature}")


def last_function_body(source: str, signature: str) -> str:
    start = source.rindex(signature)
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


def test_single_line_bottom_subtitle_scrolls_once_and_stops_on_the_tail():
    source = read_source(LCD_SOURCE)
    setup_ui = last_function_body(source, "void LcdDisplay::SetupUI")
    multiline_start = setup_ui.index("#if CONFIG_USE_MULTILINE_CHAT_MESSAGE")
    single_line = setup_ui[
        setup_ui.index("#else", multiline_start) : setup_ui.index(
            "#endif", multiline_start
        )
    ]

    assert (
        "lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);"
        in single_line
    )
    assert "lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_SCROLLABLE);" in single_line
    assert (
        "lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_CLIP);"
        in single_line
    )
    assert (
        "lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_LEFT, 0);"
        in single_line
    )
    assert "LV_LABEL_LONG_SCROLL_CIRCULAR" not in single_line
    assert "LV_ANIM_REPEAT_INFINITE" not in single_line
    assert (
        "lv_obj_set_width(chat_message_label_, LV_SIZE_CONTENT);"
        in single_line
    )


def test_set_and_update_chat_message_use_one_shot_native_lvgl_animation():
    source = read_source(LCD_SOURCE)
    header = read_source(LCD_HEADER)
    set_message = last_function_body(
        source,
        "void LcdDisplay::SetChatMessage(const char* role, const char* content)",
    )
    update_message = last_function_body(
        source,
        "void LcdDisplay::UpdateChatMessage(const char* role, const char* content)",
    )

    assert "SetChatMessageInternal(role, content, false);" in set_message
    assert "SetChatMessageInternal(role, content, true);" in update_message
    assert "lv_anim_set_repeat_count(&animation, 1);" in source
    assert "LV_ANIM_REPEAT_INFINITE" not in source
    assert "chat_message_marquee_timer_" not in header
    assert "ChatMessageMarqueeTimerCallback" not in header
    assert "esp_timer_start_periodic" not in source


def test_subtitle_update_preserves_current_scroll_position():
    source = read_source(LCD_SOURCE)
    update_internal = last_function_body(
        source, "void LcdDisplay::SetChatMessageInternal"
    )

    assert "preserve_scroll_position" in update_internal
    assert "lv_obj_get_x(chat_message_label_)" in update_internal
    assert "std::clamp<int32_t>(previous_x, end_x, 0)" in " ".join(
        update_internal.split()
    )


def test_empty_and_cleared_subtitles_stop_by_clearing_text_and_hiding_bar():
    source = read_source(LCD_SOURCE)
    set_message = last_function_body(source, "void LcdDisplay::SetChatMessageInternal")
    clear_messages = last_function_body(source, "void LcdDisplay::ClearChatMessages")

    assert (
        'lv_label_set_text(chat_message_label_, content == nullptr ? "" : content);'
        in set_message
    )
    assert "lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);" in set_message
    assert "lv_label_set_text(chat_message_label_, \"\");" in clear_messages
    assert "lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);" in clear_messages
    assert "StopChatMessageMarquee" not in set_message + clear_messages


def test_multiline_subtitles_remain_wrapped_and_share_the_same_text_entry():
    source = read_source(LCD_SOURCE)
    setup_ui = last_function_body(source, "void LcdDisplay::SetupUI")
    set_message = last_function_body(source, "void LcdDisplay::SetChatMessageInternal")
    multiline_start = setup_ui.index("#if CONFIG_USE_MULTILINE_CHAT_MESSAGE")
    multiline_setup = setup_ui[
        multiline_start : setup_ui.index("#else", multiline_start)
    ]
    multiline_route_start = set_message.index(
        "#if CONFIG_USE_MULTILINE_CHAT_MESSAGE"
    )
    multiline_route = set_message[
        multiline_route_start : set_message.index("#endif", multiline_route_start)
    ]

    assert (
        "lv_obj_set_width(chat_message_label_, LV_HOR_RES - lvgl_theme->spacing(8));"
        in multiline_setup
    )
    assert (
        "lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);"
        in multiline_setup
    )
    assert "lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);" in multiline_route
    assert "StopChatMessageMarquee" not in multiline_route


def test_all_subtitle_sources_keep_using_the_unified_display_entry():
    application = read_source(APPLICATION_SOURCE)
    board_display = read_source(BOARD_DISPLAY_SOURCE)
    show_activation = function_body(application, "void Application::ShowActivationCode")
    alert = function_body(application, "void Application::Alert")
    board_set_message = function_body(
        board_display,
        "virtual void SetChatMessage(const char* role, const char* content) override",
    )

    assert "Alert(Lang::Strings::ACTIVATION, message.c_str()" in show_activation
    assert 'display->SetChatMessage("system", message);' in alert
    assert "LcdDisplay::SetChatMessage(role, content);" in board_set_message


def test_sentence_update_only_refreshes_subtitle_text():
    application = read_source(APPLICATION_SOURCE)
    incoming_json = function_body(
        application,
        "protocol_->OnIncomingJson([this, display](const cJSON* root)",
    )
    update_branch = function_body(
        incoming_json,
        'strcmp(state->valuestring, "sentence_update") == 0',
    )

    assert 'display->UpdateChatMessage("assistant", message.c_str());' in update_branch
    assert "tts_playback_session_.sentence_id() != sentence_id" in update_branch
    assert "SetDeviceState" not in update_branch
    assert "HandleReliableTtsStart" not in update_branch
    assert "HandleReliableTtsStop" not in update_branch
