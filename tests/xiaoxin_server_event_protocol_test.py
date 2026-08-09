from pathlib import Path


APPLICATION = Path("main/application.cc")
APPLICATION_HEADER = Path("main/application.h")
PROTOCOL = Path("main/protocols/protocol.cc")
PROTOCOL_HEADER = Path("main/protocols/protocol.h")


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


def test_send_xiaoxin_ack_serializes_required_fields_and_device_time():
    body = function_body(
        read_source(PROTOCOL),
        "void Protocol::SendXiaoxinAck(const std::string& delivery_id,",
    )
    header = read_source(PROTOCOL_HEADER)

    assert "virtual void SendXiaoxinAck(const std::string& delivery_id," in header
    assert "#include <esp_timer.h>" in read_source(PROTOCOL)
    assert 'cJSON_AddStringToObject(root, "type", "xiaoxin_ack");' in body
    assert 'cJSON_AddStringToObject(root, "delivery_id", delivery_id.c_str());' in body
    assert 'cJSON_AddStringToObject(root, "state", state.c_str());' in body
    assert "cJSON_AddNullToObject(root, \"reason\");" in body
    assert 'cJSON_AddStringToObject(root, "reason", reason.c_str());' in body
    assert 'cJSON_AddNumberToObject(root, "device_time", esp_timer_get_time() / 1000);' in body
    assert "SendText(text);" in body


def test_xiaoxin_event_handler_validates_fields_and_sends_failure_ack():
    body = function_body(read_source(APPLICATION), "void Application::HandleXiaoxinEvent")
    header = read_source(APPLICATION_HEADER)

    assert "void HandleXiaoxinEvent(const cJSON* root);" in header
    assert 'JsonStringOrEmpty(root, "delivery_id")' in body
    assert "RequiredXiaoxinFieldMissing(root)" in body
    assert 'protocol_->SendXiaoxinAck(delivery_id, "failed", "invalid_payload");' in body
    assert 'protocol_->SendXiaoxinAck(delivery_id, "device_received");' in body
    assert "IsValidXiaoxinDeliveryId(delivery_id)" in body


def test_xiaoxin_event_delivery_id_limit_matches_pager_storage_without_truncation():
    validation = read_source(Path("main/xiaoxin_event_validation.h"))
    pager = read_source(Path(
        "main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_card_pager.h"
    ))
    body = function_body(read_source(APPLICATION), "void Application::HandleXiaoxinEvent")

    assert "kXiaoxinNotificationIdStorageSize = 96" in validation
    assert 'kXiaoxinEventNotificationPrefix[] = "xiaoxin_event:"' in validation
    assert "kXiaoxinDeliveryIdMaxLength" in validation
    assert "#define XIAOXIN_CARD_NOTIFICATION_ID_MAX 96" in pager
    validation_index = body.index("IsValidXiaoxinDeliveryId(delivery_id)")
    schedule_index = body.index("Schedule([")
    assert validation_index < schedule_index
    invalid_branch = body[validation_index:schedule_index]
    assert 'SendXiaoxinAck(delivery_id, "failed", "invalid_payload")' in invalid_branch


def test_successful_xiaoxin_event_schedules_display_update_before_ack():
    body = function_body(read_source(APPLICATION), "void Application::HandleXiaoxinEvent")
    before_schedule = body[: body.index("Schedule([")]
    schedule_block = body[body.index("Schedule([") : body.index("if (protocol_ != nullptr)", body.index("Schedule(["))]

    for line in [
        'std::string event = JsonStringOrEmpty(root, "event");',
        'std::string title = JsonStringOrEmpty(root, "title");',
        'std::string body = JsonStringOrEmpty(root, "body");',
        'std::string tag = JsonStringOrEmpty(root, "tag");',
        'std::string notification_id = std::string("xiaoxin_event:") + delivery_id;',
    ]:
        assert line in body

    assert "display->UpsertNotification" in schedule_block
    assert "event.c_str()" in schedule_block
    assert "display->ShowNotification" in schedule_block
    assert 'protocol_->SendXiaoxinAck(delivery_id, "device_received");' not in schedule_block
    assert "Schedule([" not in before_schedule


def test_application_routes_xiaoxin_event_before_unknown_message_fallback():
    body = function_body(read_source(APPLICATION), "void Application::InitializeProtocol")

    assert 'strcmp(type->valuestring, "xiaoxin_event") == 0' in body
    assert "HandleXiaoxinEvent(root);" in body
    assert body.index('"xiaoxin_event"') < body.index("Unknown message type")
