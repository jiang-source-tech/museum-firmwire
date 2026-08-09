from pathlib import Path


APPLICATION = Path("main/application.cc")
OTA = Path("main/ota.cc")
WEBSOCKET_PROTOCOL = Path("main/protocols/websocket_protocol.cc")
DISPLAY_HEADER = Path("main/display/display.h")
WAVESHARE_LCD = Path(
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


def preprocessor_block(source: str, marker: str, end_marker: str = "#else") -> str:
    start = source.index(marker)
    end = source.index(end_marker, start)
    return source[start:end]


def test_websocket_text_frames_are_parsed_with_explicit_length():
    source = read_source(WEBSOCKET_PROTOCOL)

    assert "cJSON_ParseWithLength(data, len)" in source
    assert "cJSON_Parse(data)" not in source
    assert "std::string(data, len).c_str()" in source


def test_wake_word_path_keeps_sending_captured_wake_audio_before_detect_text():
    body = function_body(read_source(APPLICATION), "void Application::ContinueWakeWordInvoke")
    send_wake_word_block = preprocessor_block(body, "#if CONFIG_SEND_WAKE_WORD_DATA")

    assert "while (auto packet = audio_service_.PopWakeWordPacket())" in send_wake_word_block
    assert "protocol_->SendAudio(std::move(packet));" in send_wake_word_block
    assert 'protocol_->SendWakeWordDetected(wake_word);' in send_wake_word_block
    assert send_wake_word_block.index("PopWakeWordPacket") < send_wake_word_block.index("SendWakeWordDetected")


def test_local_xiaozhi_ota_override_is_treated_as_legacy():
    source = read_source(OTA)
    body = function_body(source, "std::string Ota::GetCheckVersionUrl()")

    assert "http://121.43.33.0:8003/xiaozhi/ota/" in source
    assert 'url.find("/xiaozhi/ota")' in source
    assert "IsLegacyOtaUrl(url)" in body
    assert 'settings.EraseKey("ota_url")' in body


def test_xiaoxin_overview_update_messages_are_routed_to_display():
    application = read_source(APPLICATION)
    display_header = read_source(DISPLAY_HEADER)
    lcd_source = read_source(WAVESHARE_LCD)

    protocol_body = function_body(application, "void Application::InitializeProtocol")
    handler_body = function_body(application, "void Application::HandleXiaoxinOverviewUpdate")
    overview_state_body = function_body(lcd_source, "xiaoxin_overview_state_t BuildOverviewState()")

    assert '"xiaoxin_overview_update"' in protocol_body
    assert "HandleXiaoxinOverviewUpdate(" in protocol_body
    assert "XiaoxinOverviewSource::kWebSocket" in protocol_body
    assert "display->UpdateOverviewData(" in handler_body
    assert "virtual void UpdateOverviewData(" in display_header
    assert "void UpdateOverviewData(" in lcd_source
    assert "overview_weather_summary_" in overview_state_body
    assert "overview_course_title_" in overview_state_body
    assert "overview_todo_count_" in overview_state_body


def test_xiaoxin_overview_update_notifications_are_routed_to_notification_center():
    application = read_source(APPLICATION)
    handler_body = function_body(application, "void Application::HandleXiaoxinOverviewUpdate")

    assert 'cJSON_GetObjectItem(root, "notifications")' in handler_body
    assert "cJSON_IsArray(notifications)" in handler_body
    assert 'JsonStringOrEmpty(notification, "event")' in handler_body
    assert 'JsonStringOrEmpty(notification, "id")' in handler_body
    assert 'std::string("xiaoxin_event:") + id' in handler_body
    assert "event.c_str()" in handler_body
    assert "display->UpsertNotification(" in handler_body
    assert "display->ShowNotification(" in handler_body


def test_mqtt_overview_entry_is_revision_gated_and_display_only():
    application = read_source(APPLICATION)
    mqtt_body = function_body(application, "void Application::HandleXiaoxinOverviewMqttMessage")

    assert "overview_authority_" in application
    assert "ValidateXiaoxinOverviewPayloadContract(" in mqtt_body
    assert "HandleXiaoxinOverviewUpdate(root, XiaoxinOverviewSource::kMqtt, revision);" in mqtt_body
    assert "protocol_->" not in mqtt_body
    assert "audio_service_" not in mqtt_body
    assert "MAIN_EVENT_NOTIFICATION_WAKE" not in mqtt_body
