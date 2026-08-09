from pathlib import Path


SOURCE = Path(
    "main/boards/waveshare/esp32-s3-touch-lcd-1.46/"
    "esp32-s3-touch-lcd-1.46.cc"
)
SDKCONFIG = Path("sdkconfig")
DOCS = Path("docs/xiaoxin-serial-debug-commands.zh-CN.md")


def read_source() -> str:
    return SOURCE.read_text(encoding="utf-8")


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


def test_notify_test_command_registers_usb_repl_and_opens_notification_page():
    source = read_source()
    body = function_body(source, "void InitializeDebugConsole()")
    debug_page_body = function_body(source, "void OpenNotificationPageForDebug()")

    assert "#include <esp_console.h>" in source
    assert "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y" in SDKCONFIG.read_text(encoding="utf-8")
    assert '.command = "notify_test"' in body
    assert "esp_console_cmd_register(&notify_test_cmd)" in body
    assert "#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG" in body
    assert "ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT()" in body
    assert "esp_console_new_repl_usb_serial_jtag(" in body
    assert "esp_console_new_repl_uart(" in body
    assert "esp_console_start_repl(debug_console_repl_)" in body
    assert "ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag" not in body
    assert "ESP_ERROR_CHECK(esp_console_new_repl_uart" not in body
    assert "ESP_ERROR_CHECK(esp_console_start_repl" not in body
    assert "Debug console REPL unavailable" in body
    assert "Debug console start failed" in body
    assert "UpsertNotification(" in body
    assert "debug_notify_test_seeded_" not in source
    assert "notification already shown" not in source
    assert "OpenNotificationPageForDebug()" in body
    assert "SetCardPagerPage(XIAOXIN_CARD_PAGE_NOTIFICATIONS)" in debug_page_body


def test_serial_debug_command_docs_cover_registered_xiaoxin_commands():
    source = read_source()
    body = function_body(source, "void InitializeDebugConsole()")
    docs = DOCS.read_text(encoding="utf-8")

    commands = ["notify_test", "boot_diag", "battery", "runtime_health"]
    for command in commands:
        assert f'.command = "{command}"' in body
        assert f"| `{command}` |" in docs
        assert f"## `{command}`" in docs

    assert "USB 接入后的辅助诊断" in docs
    assert "low_shutdowns=<count>" in docs
    assert "tests/xiaoxin_serial_debug_command_test.py" in docs
