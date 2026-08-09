from pathlib import Path


HEARTBEAT = Path("main/device_location_heartbeat.cc")
HEARTBEAT_HEADER = Path("main/device_location_heartbeat.h")
HEARTBEAT_STATE = Path("main/device_location_heartbeat_state.h")
APPLICATION = Path("main/application.cc")
APPLICATION_HEADER = Path("main/application.h")
CMAKE = Path("main/CMakeLists.txt")


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


def test_location_heartbeat_posts_identity_headers_and_empty_json_only():
    source = read_source(HEARTBEAT)

    assert 'esp_http_client_set_header(client, "Content-Type", "application/json")' in source
    assert 'esp_http_client_set_header(client, "Device-Id", config.device_id.c_str())' in source
    assert 'esp_http_client_set_header(client, "Device-Username", config.username.c_str())' in source
    assert 'esp_http_client_set_header(client, "Authorization", authorization.c_str())' in source
    assert 'esp_http_client_set_post_field(client, "{}", 2)' in source
    assert "HTTP_METHOD_POST" in source
    assert "public_ip" not in source
    assert "tenant" not in source.lower()


def test_location_heartbeat_derives_only_http_or_https_ota_origin():
    source = read_source(HEARTBEAT)
    derive = function_body(
        source, "std::string DeviceLocationHeartbeat::DeriveHeartbeatUrl"
    )

    assert 'find("://")' in derive
    assert 'scheme != "http" && scheme != "https"' in derive
    assert 'find_first_of("/?#", authority_start)' in derive
    assert '"/api/xiaoxin/device/location-heartbeat"' in derive
    assert "authority.empty()" in derive
    assert "ota_url.substr(0, origin_end)" in derive
    assert "authority.front() == '['" in derive
    assert "port_value > 65535" in derive
    assert "unbracketed IPv6" in derive


def test_invalid_url_or_missing_identity_disables_heartbeat():
    source = read_source(HEARTBEAT)
    configure = function_body(source, "void DeviceLocationHeartbeat::Configure")

    assert "heartbeat_url.empty()" in configure
    assert "device_id.empty()" in configure
    assert "username.empty()" in configure
    assert "password.empty()" in configure
    assert "configured_ = false" in configure


def test_activation_captures_ota_url_and_credentials_before_ota_reset():
    source = read_source(APPLICATION)
    body = function_body(source, "void Application::HandleActivationDoneEvent()")

    ota_url = body.index("ota_->GetCheckVersionUrl()")
    mqtt_config = body.index("ota_->GetDoorbellMqttConfig()")
    device_id = body.index("SystemInfo::GetMacAddress()")
    configure = body.index("location_heartbeat_.Configure")
    reset = body.index("ota_.reset()")
    assert ota_url < configure < reset
    assert mqtt_config < configure < reset
    assert device_id < configure < reset


def test_boot_send_and_genuine_reconnect_are_wired_to_network_events():
    application = read_source(APPLICATION)
    connected = function_body(application, "void Application::HandleNetworkConnectedEvent()")
    disconnected = function_body(
        application, "void Application::HandleNetworkDisconnectedEvent()"
    )
    heartbeat = read_source(HEARTBEAT)

    assert "location_heartbeat_.OnNetworkConnected();" in connected
    assert "location_heartbeat_.OnNetworkDisconnected();" in disconnected
    assert "boot_heartbeat_sent_" in heartbeat
    assert "reconnect_armed_" in heartbeat
    assert "network_connected_)" in heartbeat


def test_worker_is_background_single_flight_and_debounced():
    source = read_source(HEARTBEAT)
    state = read_source(HEARTBEAT_STATE)

    assert '#include "device_location_heartbeat_state.h"' in source
    assert "xTaskCreate" in source
    assert "WorkerTask" in source
    assert "request_in_flight_" in state
    assert "request_pending_" in state
    assert "network_generation_" in state
    assert "pending_generation_" in state
    assert "worker_task_" in source
    assert "WorkerContext" in source
    assert "std::shared_ptr<State>" in source


def test_disconnect_and_worker_exit_clear_stale_pending_reconnect():
    source = read_source(HEARTBEAT)
    disconnected = function_body(
        source, "void DeviceLocationHeartbeat::OnNetworkDisconnected()"
    )
    worker = function_body(source, "void DeviceLocationHeartbeat::WorkerTask")

    assert "request_gate.OnNetworkDisconnected();" in disconnected
    assert "request_gate.Complete" in worker


def test_failure_path_has_no_voice_websocket_wake_or_reboot_side_effects():
    source = read_source(HEARTBEAT)

    for forbidden in (
        "Reboot(",
        "WebSocket",
        "WakeForNotification",
        "MAIN_EVENT_NOTIFICATION_WAKE",
        "audio_service",
        "PlaySound",
    ):
        assert forbidden not in source


def test_heartbeat_uses_tls_capable_transport_without_logging_secrets():
    heartbeat = read_source(HEARTBEAT)
    for line in heartbeat.splitlines():
        if "ESP_LOG" in line:
            assert "password" not in line.lower()
            assert "authorization" not in line.lower()
            assert '"{}"' not in line

    assert '#include <esp_http_client.h>' in heartbeat
    assert "esp_crt_bundle_attach" in heartbeat
    assert "http_config.disable_auto_redirect = true" in heartbeat
    assert "esp_http_client_perform" in heartbeat
    assert "esp_http_client_cleanup" in heartbeat
    assert "CreateHttp" not in heartbeat


def test_component_is_owned_by_application_and_included_in_build():
    header = read_source(HEARTBEAT_HEADER)
    application_header = read_source(APPLICATION_HEADER)
    cmake = read_source(CMAKE)

    assert "class DeviceLocationHeartbeat" in header
    assert '#include "device_location_heartbeat.h"' in application_header
    assert "DeviceLocationHeartbeat location_heartbeat_;" in application_header
    assert '"device_location_heartbeat.cc"' in cmake
