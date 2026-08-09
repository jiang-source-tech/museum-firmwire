from pathlib import Path


PROTOCOL_HEADER = Path("main/protocols/protocol.h")
PROTOCOL_SOURCE = Path("main/protocols/protocol.cc")
DEVICE_STATUS_SOURCE = Path("main/device_status.cc")
WEBSOCKET_PROTOCOL = Path("main/protocols/websocket_protocol.cc")
MQTT_PROTOCOL = Path("main/protocols/mqtt_protocol.cc")
BOARD_HEADER = Path("main/boards/common/board.h")
WAVESHARE_BOARD = Path(
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


def test_protocol_builds_real_device_status_from_app_and_board():
    header = read_source(PROTOCOL_HEADER)
    source = read_source(PROTOCOL_SOURCE)
    protocol_body = function_body(
        source, "cJSON* Protocol::BuildDeviceStatusJson() const"
    )
    body = function_body(
        read_source(DEVICE_STATUS_SOURCE), "cJSON* BuildDeviceStatusJson()"
    )

    assert "cJSON* BuildDeviceStatusJson() const;" in header
    assert "xiaoxin::BuildDeviceStatusJson()" in protocol_body
    assert "esp_app_get_description()" in body
    assert 'cJSON_AddStringToObject(status, "firmware_version"' in body
    assert "Board::GetInstance().GetBatteryLevel" in body
    assert 'cJSON_AddNumberToObject(status, "battery_percent"' in body


def test_protocol_reports_the_same_four_bar_level_as_the_hardware_ui():
    board_header = read_source(BOARD_HEADER)
    board_source = read_source(WAVESHARE_BOARD)
    status_body = function_body(
        read_source(DEVICE_STATUS_SOURCE),
        "cJSON* BuildDeviceStatusJson()",
    )

    assert "virtual bool GetBatteryDisplayLevel(int& level);" in board_header
    assert "GetBatteryDisplayLevel(int& level) override" in board_source
    assert "level = battery_snapshot_.display_level;" in board_source
    assert "Board::GetInstance().GetBatteryDisplayLevel" in status_body
    assert 'cJSON_AddNumberToObject(status, "battery_level"' in status_body


def test_websocket_and_mqtt_hello_include_device_status():
    websocket_body = function_body(
        read_source(WEBSOCKET_PROTOCOL),
        "std::string WebsocketProtocol::GetHelloMessage()",
    )
    mqtt_body = function_body(
        read_source(MQTT_PROTOCOL),
        "std::string MqttProtocol::GetHelloMessage()",
    )

    expected = 'cJSON_AddItemToObject(root, "device_status", BuildDeviceStatusJson());'
    assert expected in websocket_body
    assert expected in mqtt_body
