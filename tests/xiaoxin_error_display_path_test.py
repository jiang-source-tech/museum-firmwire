from pathlib import Path


APPLICATION_HEADER = Path("main/application.h")
APPLICATION_SOURCE = Path("main/application.cc")


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


def block_after(source: str, marker: str, length: int = 420) -> str:
    start = source.index(marker)
    return source[start : start + length]


def test_error_event_marks_bottom_error_message_visible_before_idle_refresh():
    header = read_source(APPLICATION_HEADER)
    source = read_source(APPLICATION_SOURCE)
    run_body = function_body(source, "void Application::Run()")
    error_block = block_after(run_body, "if (bits & MAIN_EVENT_ERROR)", length=260)

    assert "bool error_message_visible_ = false;" in header
    assert "error_message_visible_ = true;" in error_block
    assert error_block.index("error_message_visible_ = true;") < error_block.index(
        "SetDeviceState(kDeviceStateIdle);"
    )
    assert error_block.index("SetDeviceState(kDeviceStateIdle);") < error_block.index(
        "Alert(Lang::Strings::ERROR"
    )


def test_error_streaming_is_owned_by_bottom_subtitle_display():
    header = read_source(APPLICATION_HEADER)
    source = read_source(APPLICATION_SOURCE)

    assert "ShowErrorMessageStream" not in header
    assert "RunErrorMessageStreamTask" not in header
    assert "error_message_stream_generation_" not in header
    assert "ShowErrorMessageStream" not in source


def test_idle_state_keeps_visible_bottom_error_message():
    source = read_source(APPLICATION_SOURCE)
    body = function_body(source, "void Application::HandleStateChangedEvent()")
    idle_block = block_after(body, "case kDeviceStateIdle:", length=360)

    assert "if (!error_message_visible_)" in idle_block
    assert "display->ClearChatMessages();" in idle_block
    assert idle_block.index("if (!error_message_visible_)") < idle_block.index(
        "display->ClearChatMessages();"
    )
    assert "display->SetChatMessage(\"system\", \"\");" not in idle_block


def test_audio_channel_closed_defers_chat_cleanup_to_guarded_idle_state():
    source = read_source(APPLICATION_SOURCE)
    init_body = function_body(source, "void Application::InitializeProtocol()")
    close_start = init_body.index("protocol_->OnAudioChannelClosed")
    close_end = init_body.index("protocol_->OnIncomingJson", close_start)
    close_block = init_body[close_start:close_end]

    assert "display->SetChatMessage(\"system\", \"\");" not in close_block
    idle_body = function_body(source, "void Application::HandleStateChangedEvent()")
    idle_block = block_after(idle_body, "case kDeviceStateIdle:", length=360)
    assert "if (!error_message_visible_)" in idle_block
    assert "display->ClearChatMessages();" in idle_block


def test_new_conversation_attempt_clears_previous_bottom_error_message():
    source = read_source(APPLICATION_SOURCE)
    body = function_body(source, "void Application::HandleStateChangedEvent()")
    connecting_block = block_after(body, "case kDeviceStateConnecting:", length=320)

    assert "error_message_visible_ = false;" in connecting_block
    assert connecting_block.index("error_message_visible_ = false;") < connecting_block.index(
        "display->SetChatMessage(\"system\", \"\");"
    )
