from pathlib import Path


APPLICATION = Path("main/application.cc")
APPLICATION_HEADER = Path("main/application.h")
DOORBELL_MQTT = Path("main/doorbell_mqtt.cc")
DOORBELL_MQTT_HEADER = Path("main/doorbell_mqtt.h")


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


def test_doorbell_mqtt_uses_ota_topics_and_no_public_default_broker():
    source = read_source(DOORBELL_MQTT)
    header = read_source(DOORBELL_MQTT_HEADER)

    assert "kDefaultBrokerHost" not in source
    assert "kDefaultBrokerPort" not in source
    assert "121.43.33.0" not in source
    assert '"device/' not in source
    assert "device/{id}" not in header
    assert "config_.status_topic" in source
    assert "config_.notification_topic" in source
    assert "config_.overview_topic" in source


def test_doorbell_mqtt_start_accepts_only_config_and_device_id():
    source = read_source(DOORBELL_MQTT)
    header = read_source(DOORBELL_MQTT_HEADER)

    signature = "void Start(const DoorbellMqttConfig& config, const std::string& device_id);"
    assert signature in header
    assert "void DoorbellMqtt::Start(const DoorbellMqttConfig& config," in source
    assert "std::string credential_" not in header
    assert "const std::string& credential" not in header
    assert "broker_host_" not in header
    assert "status_topic_" not in header
    assert "notification_topic_" not in header


def test_doorbell_mqtt_builds_client_and_lwt_from_exact_config_values():
    source = read_source(DOORBELL_MQTT)
    body = function_body(source, "void DoorbellMqtt::Start")

    assert "IsDoorbellMqttConfigValidForDevice(config, device_id)" in body
    assert "config_ = config;" in body
    assert "std::string broker_uri;" in body
    assert "if (!NormalizeDoorbellMqttEndpoint(config.endpoint, broker_uri))" in body
    assert "cfg.broker.address.uri = broker_uri.c_str();" in body
    assert "cfg.broker.address.uri = config_.endpoint.c_str();" not in body
    assert "cfg.credentials.client_id = config_.client_id.c_str();" in body
    assert "cfg.credentials.username = config_.username.c_str();" in body
    assert "cfg.credentials.authentication.password = config_.password.c_str();" in body
    assert "cfg.session.last_will.topic = config_.status_topic.c_str();" in body
    assert 'cfg.session.last_will.msg = "offline";' in body
    assert "cfg.session.keepalive = config_.keepalive_seconds;" in body
    assert "cfg.session.last_will.qos = config_.qos;" in body
    assert "cfg.session.last_will.retain = 1;" in body


def test_doorbell_mqtt_normalizes_server_endpoint_and_rejects_unsupported_forms():
    source = read_source(Path("main/doorbell_mqtt_contract.cc"))
    body = function_body(source, "bool NormalizeDoorbellMqttEndpoint")

    assert 'constexpr const char* kMqttScheme = "mqtt://";' in source
    assert "endpoint.rfind(kMqttScheme, 0) == 0" in body
    assert "endpoint.find(\"://\") != std::string::npos" in body
    assert "authority.find(':') != authority.rfind(':')" in body
    assert "host.empty() || port_text.empty()" in body
    assert "port > 65535" in body
    assert "port_separator == std::string::npos" in body
    assert "std::to_string(kDefaultMqttPort)" in body
    assert 'uri = std::string(kMqttScheme) + host + ":" + port_text;' in body


def test_doorbell_mqtt_start_failure_destroys_client_and_allows_retry():
    body = function_body(read_source(DOORBELL_MQTT), "void DoorbellMqtt::Start")
    failure = body[body.index("if (err != ESP_OK)") :]

    destroy = failure.index("esp_mqtt_client_destroy(client_);")
    clear_client = failure.index("client_ = nullptr;")
    reset_started = failure.index("started_.store(false);")
    assert destroy < clear_client < reset_started


def test_doorbell_connect_uses_configured_topics_and_preserves_retain_contract():
    body = function_body(read_source(DOORBELL_MQTT), "void DoorbellMqtt::OnConnected")
    compact = " ".join(body.split())

    assert 'esp_mqtt_client_publish( client_, config_.status_topic.c_str(), "online", 6, config_.qos, 1)' in compact
    assert "esp_mqtt_client_subscribe( client_, config_.notification_topic.c_str(), config_.qos)" in compact
    assert "if (!config_.overview_topic.empty())" in body
    assert "esp_mqtt_client_subscribe( client_, config_.overview_topic.c_str(), config_.qos)" in compact


def test_doorbell_mqtt_reports_telemetry_without_opening_audio_channel():
    source = read_source(DOORBELL_MQTT)
    header = read_source(DOORBELL_MQTT_HEADER)
    connected = function_body(source, "void DoorbellMqtt::OnConnected")
    publish = function_body(source, "void DoorbellMqtt::PublishTelemetry")
    timer = function_body(source, "void DoorbellMqtt::TelemetryTimerCallback")

    assert "void PublishTelemetry(bool force);" in header
    assert "PublishTelemetry(true);" in connected
    assert "esp_timer_start_periodic" in connected
    assert "PublishTelemetry(false);" in timer
    assert "DoorbellTelemetryTopic(config_)" in publish
    assert "xiaoxin::BuildDeviceStatusJson()" in publish
    assert 'cJSON_AddStringToObject(status, "type", "device_status")' in publish
    assert 'cJSON_AddItemToObject(root, "device_status"' not in publish
    assert "kTelemetryHeartbeatIntervalUs" in publish
    assert "battery_changed" in publish
    assert "config_.qos, 0" in " ".join(publish.split())
    assert "OpenAudioChannel" not in publish


def test_application_starts_doorbell_only_from_usable_ota_or_persisted_config():
    body = function_body(read_source(APPLICATION), "void Application::HandleActivationDoneEvent")

    assert "const auto& config = ota_->GetDoorbellMqttConfig();" in body
    assert "IsDoorbellMqttConfigValidForDevice(config, device_id)" in body
    assert "g_doorbell_mqtt.Start(config, device_id);" in body
    assert body.index("g_doorbell_mqtt.Start") < body.index("ota_.reset()")


def test_doorbell_mqtt_does_not_log_endpoint_or_password():
    source = read_source(DOORBELL_MQTT)

    for line in source.splitlines():
        if "ESP_LOG" in line:
            assert "password" not in line.lower()
            assert "endpoint" not in line.lower()


def test_doorbell_wake_uses_notification_wake_entry_not_chat_toggle():
    body = function_body(read_source(DOORBELL_MQTT), "void DoorbellMqtt::OnNotificationMessage")

    assert "app.WakeForNotification();" in body
    assert "app.ToggleChatState();" not in body


def test_notification_wake_opens_websocket_without_starting_listening():
    source = read_source(APPLICATION)
    header = read_source(APPLICATION_HEADER)
    run_body = function_body(source, "void Application::Run()")
    handler_body = function_body(source, "void Application::HandleNotificationWakeEvent()")
    continue_body = function_body(
        source,
        "void Application::ContinueOpenNotificationChannel(",
    )

    assert "void WakeForNotification();" in header
    assert "MAIN_EVENT_NOTIFICATION_WAKE" in header
    assert "if (bits & MAIN_EVENT_NOTIFICATION_WAKE)" in run_body
    assert "ContinueOpenNotificationChannel" in handler_body
    assert "protocol_->OpenAudioChannel()" in continue_body
    assert "SetDeviceState(kDeviceStateIdle);" in continue_body
    assert "SetListeningMode" not in continue_body
    assert "SendStartListening" not in continue_body
