from pathlib import Path


CONFIG = Path("main/doorbell_config.cc")
CONFIG_HEADER = Path("main/doorbell_config.h")
DOORBELL = Path("main/doorbell_mqtt.cc")
DOORBELL_HEADER = Path("main/doorbell_mqtt.h")
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


def test_doorbell_version_is_finite_exact_integer_one_before_enabled_is_used():
    source = read_source(Path("main/doorbell_config_contract.cc"))
    parse = function_body(source, "DoorbellMqttConfig ParseDoorbellMqttConfigContract")

    assert "IsExactNumberInRange(input.version.value, 1.0, 1.0)" in parse
    assert "config.enabled = input.enabled.value" in parse


def test_config_persists_validated_keepalive_and_qos_with_legacy_defaults():
    header = read_source(CONFIG_HEADER)
    source = read_source(CONFIG)
    usable = function_body(source, "bool DoorbellMqttConfig::IsUsable() const")
    parse = function_body(source, "DoorbellMqttConfig ParseDoorbellMqttConfig")
    load = function_body(source, "DoorbellMqttConfig LoadDoorbellMqttConfig")

    assert "kDefaultKeepaliveSeconds = 240" in header
    assert "kMinKeepaliveSeconds" in header
    assert "kMaxKeepaliveSeconds" in header
    assert "int keepalive_seconds = kDefaultKeepaliveSeconds;" in header
    assert "int qos = 1;" in header
    assert "keepalive_seconds >= kMinKeepaliveSeconds" in usable
    assert "keepalive_seconds <= kMaxKeepaliveSeconds" in usable
    assert "qos == 1" in usable
    assert 'input.keepalive_seconds = JsonNumber(root, "keepalive_seconds")' in parse
    assert 'input.qos = JsonNumber(root, "qos")' in parse
    assert 'settings.SetInt("keepalive", config.keepalive_seconds);' in source
    assert 'settings.SetInt("qos", config.qos);' in source
    assert "kDefaultKeepaliveSeconds" in load
    assert '"keepalive", DoorbellMqttConfig::kDefaultKeepaliveSeconds' in load
    assert 'settings.GetInt("qos", 1)' in load


def test_mqtt_endpoint_accepts_host_only_with_default_port_and_rejects_ipv6():
    source = read_source(Path("main/doorbell_mqtt_contract.cc"))
    normalize = function_body(source, "bool NormalizeDoorbellMqttEndpoint")

    assert "kDefaultMqttPort = 1883" in source
    assert "port_separator == std::string::npos" in normalize
    assert "std::to_string(kDefaultMqttPort)" in normalize
    assert "authority.find('[')" in normalize
    assert "authority.find(']')" in normalize
    assert 'uri = std::string(kMqttScheme) + host + ":" + port_text;' in normalize


def test_doorbell_uses_configured_keepalive_qos_and_confirms_suback():
    source = read_source(DOORBELL)
    header = read_source(DOORBELL_HEADER)
    start = function_body(source, "void DoorbellMqtt::Start")
    connected = function_body(source, "void DoorbellMqtt::OnConnected")
    handler = function_body(source, "void DoorbellMqtt::MqttEventHandler")
    subscribed = function_body(source, "void DoorbellMqtt::OnSubscribed")

    assert "cfg.session.keepalive = config_.keepalive_seconds;" in start
    assert "cfg.session.last_will.qos = config_.qos;" in start
    assert "config_.qos" in connected
    assert "notification_subscribe_mid_" in header
    assert "overview_subscribe_mid_" in header
    assert "MQTT_EVENT_SUBSCRIBED" in handler
    assert "self->OnSubscribed(event);" in handler
    assert "event->msg_id == notification_subscribe_mid_" in subscribed
    assert "event->msg_id == overview_subscribe_mid_" in subscribed
    assert "EvaluateDoorbellMqttSuback" in subscribed
    assert "subscription request queued" in connected
    assert "configured topics subscribed" not in connected


def test_mqtt_overview_authority_blocks_websocket_without_blocking_mqtt():
    source = read_source(APPLICATION)
    header = read_source(APPLICATION_HEADER)
    apply = function_body(source, "void Application::HandleXiaoxinOverviewUpdate")
    mqtt = function_body(source, "void Application::HandleXiaoxinOverviewMqttMessage")
    protocol = function_body(source, "void Application::InitializeProtocol")
    activation = function_body(source, "void Application::ActivationTask")

    assert '#include "xiaoxin_overview_authority_state.h"' in header
    assert "XiaoxinOverviewAuthorityState overview_authority_;" in header
    assert "XiaoxinOverviewSource source" in source
    assert "overview_authority_.Allows(source)" in apply
    assert "XiaoxinOverviewSource::kWebSocket" in protocol
    assert "XiaoxinOverviewSource::kMqtt" in mqtt
    assert "ValidateXiaoxinOverviewPayloadContract(" in mqtt
    assert "overview_authority_.CommitMqttRevision(revision)" in mqtt
    assert "overview_authority_.Configure(" in activation
    assert "doorbell_config.overview_topic.empty()" in activation
    assert activation.index("overview_authority_.Configure(") < activation.index(
        "InitializeProtocol();"
    )


def test_overview_logs_do_not_emit_weather_course_or_todo_content():
    body = function_body(read_source(APPLICATION), "void Application::HandleXiaoxinOverviewUpdate")

    assert "xiaoxin_overview_update received:" not in body
    for line in body.splitlines():
        if "ESP_LOG" in line:
            assert "weather_summary" not in line
            assert "course_title" not in line
            assert "course_detail" not in line
            assert "todo_detail" not in line
