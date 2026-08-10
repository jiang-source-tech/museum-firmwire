from pathlib import Path


APPLICATION = Path("main/application.cc")
OTA = Path("main/ota.cc")
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
