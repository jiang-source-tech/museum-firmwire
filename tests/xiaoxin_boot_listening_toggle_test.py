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

