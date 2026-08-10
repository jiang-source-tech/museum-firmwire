from pathlib import Path


BOARD_SOURCE = Path(
    "main/boards/waveshare/esp32-s3-touch-lcd-1.46/"
    "esp32-s3-touch-lcd-1.46.cc"
)


def read_source(path: Path = BOARD_SOURCE) -> str:
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


def test_notifications_use_the_home_screen_temporary_message_path():
    source = read_source()
    show_body = function_body(
        source,
        "virtual void ShowNotification(const char* notification, int duration_ms = 3000) override",
    )
    upsert_body = function_body(
        source,
        "bool UpsertNotification(",
    )

    assert "LcdDisplay::ShowNotification" in show_body
    assert "RaiseOverlayObjects();" in show_body
    assert "ShowNotification(message, duration_ms);" in upsert_body


def test_touch_polling_keeps_settings_and_power_wake_paths_available():
    source = read_source()
    poll_body = function_body(source, "void PollTouch(uint32_t now_ms)")

    assert "touch_->ReadPoint(x, y, pressed)" in poll_body
    assert "power_save_timer_wake_requested_ = true;" in poll_body
    assert "HandleSettingsTouch(" in poll_body
    assert "touch_last_x_ = x;" in poll_body
