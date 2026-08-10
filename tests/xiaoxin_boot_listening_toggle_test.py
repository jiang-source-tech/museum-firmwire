from pathlib import Path


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
                return source[brace + 1:index]
    raise AssertionError(f"function body not found: {signature}")


def branch_between(source: str, start_marker: str, end_marker: str) -> str:
    start = source.index(start_marker)
    end = source.index(end_marker, start)
    return source[start:end]


def test_boot_toggle_from_listening_returns_to_idle_without_channel_close_callback():
    body = function_body(read_source(APPLICATION_SOURCE), "void Application::HandleToggleChatEvent()")
    listening_branch = branch_between(
        body,
        "state == kDeviceStateListening",
        "}",
    )

    assert "CloseAudioChannel()" in listening_branch
    assert "SetDeviceState(kDeviceStateIdle)" in listening_branch


def test_listening_state_clears_stale_chat_subtitles_and_museum_state_stays_off_chat_bar():
    source = read_source(APPLICATION_SOURCE)
    state_changed = function_body(source, "void Application::HandleStateChangedEvent()")
    listening_branch = branch_between(
        state_changed,
        "case kDeviceStateListening:",
        "case kDeviceStateThinking:",
    )
    incoming_json = function_body(
        source,
        "protocol_->OnIncomingJson([this, display](const cJSON* root)",
    )
    museum_branch = branch_between(
        incoming_json,
        'strcmp(type->valuestring, "museum_state") == 0',
        'strcmp(type->valuestring, "mcp") == 0',
    )

    assert "display->ClearChatMessages();" in listening_branch
    assert listening_branch.index("display->ClearChatMessages();") < listening_branch.index(
        "display->SetStatus(Lang::Strings::LISTENING);"
    )
    assert "BuildMuseumStateDisplayText" in museum_branch
    assert "display->SetMuseumState(message.c_str());" in museum_branch
    assert "SetChatMessage" not in museum_branch

