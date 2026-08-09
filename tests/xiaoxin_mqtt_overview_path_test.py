from pathlib import Path


DOORBELL_MQTT = Path("main/doorbell_mqtt.cc")
DOORBELL_MQTT_HEADER = Path("main/doorbell_mqtt.h")
APPLICATION = Path("main/application.cc")
APPLICATION_HEADER = Path("main/application.h")


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


def test_mqtt_data_preserves_exact_topic_and_payload_bytes():
    body = function_body(read_source(DOORBELL_MQTT), "void DoorbellMqtt::MqttEventHandler")

    assert "AppendDoorbellMqttFragment" in body
    assert "event->topic" in body
    assert "event->topic_len" in body
    assert "event->data" in body
    assert "event->data_len" in body
    assert "event->total_data_len" in body
    assert "event->current_data_offset" in body
    assert "DoorbellMqttFragmentResult::kComplete" in body
    assert "self->OnMessage(topic, payload);" in body
    assert body.count("self->OnMessage(topic, payload);") == 1
    for line in body.splitlines():
        if "ESP_LOG" in line:
            assert "topic.c_str" not in line
            assert "payload.c_str" not in line


def test_mqtt_reassembly_state_is_owned_and_cleared_across_sessions():
    source = read_source(DOORBELL_MQTT)
    header = read_source(DOORBELL_MQTT_HEADER)
    destructor = function_body(source, "DoorbellMqtt::~DoorbellMqtt")
    handler = function_body(source, "void DoorbellMqtt::MqttEventHandler")

    assert '#include "doorbell_mqtt_reassembly.h"' in header
    assert "DoorbellMqttReceiveState receive_state_;" in header
    assert "receive_state_.Clear();" in destructor
    disconnected = handler[handler.index("case MQTT_EVENT_DISCONNECTED") :]
    assert "self->receive_state_.Clear();" in disconnected


def test_mqtt_messages_route_only_by_exact_configured_topic():
    source = read_source(DOORBELL_MQTT)
    header = read_source(DOORBELL_MQTT_HEADER)
    body = function_body(source, "void DoorbellMqtt::OnMessage")

    assert "void OnMessage(const std::string& topic, const std::string& payload);" in header
    classifier = body.index("ClassifyDoorbellMqttTopic(config_, topic)")
    notification = body.index("DoorbellMqttTopic::kNotification")
    notification_handler = body.index("OnNotificationMessage(payload);")
    overview_exact = body.index("DoorbellMqttTopic::kOverview")
    overview_handler = body.index("OnOverviewMessage(payload);")
    assert classifier < notification < notification_handler
    assert classifier < overview_exact < overview_handler
    assert "return;" in body[overview_handler:]
    assert "cJSON_" not in body


def test_notification_json_is_length_aware_and_rejects_embedded_nul():
    body = function_body(read_source(DOORBELL_MQTT), "void DoorbellMqtt::OnNotificationMessage")

    assert "payload.find('\\0') != std::string::npos" in body
    assert "cJSON_ParseWithLength(payload.data(), payload.size())" in body
    assert "cJSON_Parse(payload.c_str())" not in body


def test_overview_route_has_explicit_task_four_seam_and_empty_topic_cannot_match():
    source = read_source(DOORBELL_MQTT)
    header = read_source(DOORBELL_MQTT_HEADER)
    body = function_body(source, "void DoorbellMqtt::OnMessage")
    overview = function_body(source, "void DoorbellMqtt::OnOverviewMessage")

    assert "void OnOverviewMessage(const std::string& payload);" in header
    assert "void OnNotificationMessage(const std::string& payload);" in header
    assert "DoorbellMqttTopic::kOverview" in body
    assert "OnOverviewMessage(payload);" in body
    assert "Application::GetInstance().HandleXiaoxinOverviewMqttMessage(" in overview
    assert "payload, device_id_" in overview
    assert "std::string device_id_;" in header


def test_mqtt_overview_validates_metadata_and_never_opens_voice_channel():
    source = read_source(APPLICATION)
    header = read_source(APPLICATION_HEADER)
    body = function_body(source, "void Application::HandleXiaoxinOverviewMqttMessage")

    assert "void HandleXiaoxinOverviewMqttMessage(" in header
    assert "payload.empty() || payload.size() > 2048" in body
    assert "payload.find('\\0') != std::string::npos" in body
    assert 'payload.find("\\\\u0000") != std::string::npos' in body
    assert "cJSON_ParseWithLengthOpts(" in body
    assert "parse_end" in body
    assert "parse_end != payload_end" in body
    assert "ReadXiaoxinOverviewPayloadContract(root)" in body
    assert "ValidateXiaoxinOverviewPayloadContract(" in body
    assert "expected_device" in body
    assert "overview_authority_.last_overview_revision_" in body
    assert "HandleXiaoxinOverviewUpdate(root, XiaoxinOverviewSource::kMqtt, revision);" in body
    assert body.index("HandleXiaoxinOverviewUpdate(root, XiaoxinOverviewSource::kMqtt, revision);") < body.index("overview_authority_.CommitMqttRevision(revision);")
    for forbidden in (
        "OpenAudioChannel",
        "WakeForNotification",
        "Play",
        "SendAudio",
        "UpsertNotification",
        "ShowNotification",
    ):
        assert forbidden not in body


def test_overview_cjson_predicates_are_converted_before_bool_aggregate_initialization():
    source = read_source(APPLICATION)
    json_bool = function_body(source, "static XiaoxinContractBool JsonContractBool")

    assert "cJSON_IsBool(item) != 0" in json_bool
    assert "cJSON_IsTrue(item) != 0" in json_bool


def test_mqtt_overview_requires_exact_integer_metadata_and_valid_card_fields():
    source = read_source(APPLICATION)
    metadata = function_body(source, "bool IsExactJsonIntegerInRange")
    cards = function_body(source, "bool ValidateXiaoxinOverviewCards")

    assert "cJSON_IsNumber(item)" in metadata
    assert "std::isfinite(item->valuedouble)" in metadata
    assert "std::floor(item->valuedouble) != item->valuedouble" in metadata
    assert "INT_MAX" in metadata
    assert 'cJSON_GetObjectItem(root, "weather")' in cards
    assert 'cJSON_GetObjectItem(root, "course")' in cards
    assert 'cJSON_GetObjectItem(root, "todo")' in cards
    assert "cJSON_IsObject(weather)" in cards
    assert "cJSON_IsObject(course)" in cards
    assert "cJSON_IsObject(todo)" in cards
    assert "JsonRequiredBool" in cards
    assert "JsonRequiredUtf8String" in cards
    assert 'cJSON_GetObjectItem(todo, "count")' in cards
    assert "IsExactJsonIntegerInRange" in cards
    assert "kXiaoxinOverviewTextMaxBytes" in cards
    assert "kXiaoxinOverviewBodyMaxBytes" in cards
    assert "kXiaoxinOverviewDetailMaxBytes" in cards


def test_mqtt_overview_unbound_payload_clears_with_explicit_cards_and_no_notifications():
    body = function_body(read_source(APPLICATION), "void Application::HandleXiaoxinOverviewMqttMessage")

    assert "ReadXiaoxinOverviewPayloadContract(root)" in body
    assert "ValidateXiaoxinOverviewPayloadContract(" in body
    assert "HandleXiaoxinOverviewUpdate(root, XiaoxinOverviewSource::kMqtt, revision);" in body


def test_unbound_overview_accepts_only_safe_server_placeholders_before_ui_or_revision():
    source = read_source(APPLICATION)
    validator = function_body(source, "bool ValidateUnboundXiaoxinOverviewCards")
    iso_date = function_body(source, "bool IsValidIsoDate")
    handler = function_body(source, "void Application::HandleXiaoxinOverviewMqttMessage")

    assert 'JsonStringOrEmpty(weather, "province").empty()' in validator
    assert 'JsonStringOrEmpty(weather, "city").empty()' in validator
    assert 'IsValidIsoDate(JsonStringOrEmpty(weather, "date"))' in validator
    assert 'JsonStringOrEmpty(weather, "fetched_at").empty()' in validator
    assert 'JsonStringOrEmpty(weather, "summary") == "设备未绑定"' in validator
    assert 'JsonStringOrEmpty(weather, "detail") == "绑定后显示天气"' in validator
    assert 'JsonStringOrEmpty(course, "title") == "设备未绑定"' in validator
    assert 'JsonStringOrEmpty(course, "detail") == "绑定后显示课程"' in validator
    assert 'JsonStringOrEmpty(todo, "detail") == "绑定后显示待办"' in validator
    assert "date.size() != 10" in iso_date
    assert "date[4] != '-' || date[7] != '-'" in iso_date
    assert "days_in_month" in iso_date
    assert "is_leap_year" in iso_date
    validate = handler.index("ValidateXiaoxinOverviewPayloadContract(")
    apply = handler.index("HandleXiaoxinOverviewUpdate(root, XiaoxinOverviewSource::kMqtt, revision);")
    revision = handler.index("overview_authority_.CommitMqttRevision(revision);")
    assert validate < apply < revision


def test_mqtt_overview_parser_uses_scoped_cjson_cleanup_and_valid_utf8():
    source = read_source(APPLICATION)
    body = function_body(source, "void Application::HandleXiaoxinOverviewMqttMessage")
    utf8 = function_body(source, "bool IsValidUtf8")

    assert "std::unique_ptr<cJSON, decltype(&cJSON_Delete)>" in body
    assert "cJSON_Delete" in body
    assert "IsValidUtf8" in source
    assert "continuation" in utf8


def test_optional_companion_projection_reaches_the_overview_display():
    source = read_source(APPLICATION)
    contract_reader = function_body(
        source, "static XiaoxinOverviewPayloadContract ReadXiaoxinOverviewPayloadContract"
    )
    apply = function_body(source, "void Application::HandleXiaoxinOverviewUpdate")

    assert 'cJSON_GetObjectItem(root, "companion")' in contract_reader
    for field in (
        "xiaoxin_age",
        "academic_stage",
        "growth_moment_id",
        "growth_summary",
        "expression",
    ):
        assert f'"{field}"' in contract_reader
    assert 'JsonIntOrDefault(companion, "xiaoxin_age", 0)' in apply
    assert 'JsonStringOrEmpty(companion, "growth_summary")' in apply
    assert "companion_available" in apply
    assert "display->UpdateOverviewData(" in apply


def test_unconfigured_topics_and_payload_content_are_not_logged():
    source = read_source(DOORBELL_MQTT)
    bodies = [
        function_body(source, "void DoorbellMqtt::OnMessage"),
        function_body(source, "void DoorbellMqtt::OnNotificationMessage"),
    ]

    for body in bodies:
        for line in body.splitlines():
            if "ESP_LOG" in line:
                assert "payload" not in line
                assert "topic.c_str" not in line
                assert "type->valuestring" not in line
