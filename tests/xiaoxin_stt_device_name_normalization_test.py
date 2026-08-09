from pathlib import Path


APPLICATION_SOURCE = Path("main/application.cc")


def read_source() -> str:
    return APPLICATION_SOURCE.read_text(encoding="utf-8")


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


def block_after(source: str, marker: str, length: int = 520) -> str:
    start = source.index(marker)
    return source[start : start + length]


def block_between(source: str, marker: str, next_marker: str) -> str:
    start = source.index(marker)
    end = source.index(next_marker, start)
    return source[start:end]


def test_stt_user_caption_normalizes_xiaoxin_device_name_variants():
    source = read_source()
    normalize_body = function_body(source, "static std::string NormalizeXiaoxinDeviceName")
    init_body = function_body(source, "void Application::InitializeProtocol()")
    stt_block = block_between(
        init_body,
        'strcmp(type->valuestring, "stt") == 0',
        'strcmp(type->valuestring, "llm") == 0',
    )

    assert '"小新"' in normalize_body
    assert '"晓新"' in normalize_body
    assert '"小芯"' in normalize_body
    assert "NormalizeXiaoxinDeviceName(std::string(text->valuestring))" in stt_block
    assert 'display->SetChatMessage("user", message.c_str());' in stt_block
    assert stt_block.index("NormalizeXiaoxinDeviceName") < stt_block.index(
        'display->SetChatMessage("user", message.c_str());'
    )


def test_device_name_normalization_is_not_applied_to_assistant_tts_caption():
    source = read_source()
    init_body = function_body(source, "void Application::InitializeProtocol()")
    tts_block = block_after(init_body, 'strcmp(state->valuestring, "sentence_start") == 0', length=520)

    assert "NormalizeXiaoxinDeviceName" not in tts_block


def test_wake_word_invoke_sends_wake_audio_before_text_detect():
    source = read_source()
    body = function_body(source, "void Application::ContinueWakeWordInvoke")
    send_wake_word_start = body.index("#if CONFIG_SEND_WAKE_WORD_DATA")
    send_wake_word_end = body.index("#else", send_wake_word_start)
    send_wake_word_block = body[send_wake_word_start:send_wake_word_end]

    assert "while (auto packet = audio_service_.PopWakeWordPacket())" in send_wake_word_block
    assert "protocol_->SendAudio(std::move(packet));" in send_wake_word_block
    assert 'protocol_->SendWakeWordDetected(wake_word);' in send_wake_word_block
    assert send_wake_word_block.index("PopWakeWordPacket") < send_wake_word_block.index("SendWakeWordDetected")
