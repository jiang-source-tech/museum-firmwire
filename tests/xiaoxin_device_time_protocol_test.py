from pathlib import Path


WEBSOCKET_PROTOCOL = Path("main/protocols/websocket_protocol.cc")


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


def test_websocket_hello_includes_device_time_snapshot():
    source = read_source(WEBSOCKET_PROTOCOL)
    body = function_body(source, "std::string WebsocketProtocol::GetHelloMessage()")

    assert '#include "time_sync_status.h"' in source
    assert "#include <sys/time.h>" in source
    assert 'cJSON_AddItemToObject(root, "device_time", BuildDeviceTimeJson());' in body


def test_device_time_snapshot_reports_sntp_status_and_wall_time_only_when_synced():
    source = read_source(WEBSOCKET_PROTOCOL)
    body = function_body(source, "static cJSON* BuildDeviceTimeJson()")

    assert "GetTimeSyncStatus()" in body
    assert "TimeSyncStatus::Synced" in body
    assert "gettimeofday(&now, nullptr)" in body
    assert 'cJSON_AddNumberToObject(device_time, "wall_time_ms"' in body
    assert 'cJSON_AddNullToObject(device_time, "wall_time_ms")' in body
    assert 'cJSON_AddStringToObject(device_time, "sync_status"' in body
    assert 'cJSON_AddStringToObject(device_time, "timezone", "Asia/Shanghai")' in body
    assert 'cJSON_AddStringToObject(device_time, "source", "sntp")' in body
