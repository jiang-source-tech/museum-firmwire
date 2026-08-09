# Xiaoxin Serial Notify Test Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a VSCode/ESP-IDF monitor command that creates a test notification and opens the notification page on the Xiaoxin 1.46 board.

**Architecture:** Register an ESP-IDF UART console REPL from the Xiaoxin board constructor and add one debug command, `notify_test`. The command reuses the existing display notification path and the existing card pager page switch helper.

**Tech Stack:** ESP-IDF console REPL, C++ board implementation, pytest static source checks.

## Global Constraints

- Keep the command scoped to `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`.
- Do not introduce new runtime dependencies; `console` is already linked by `main/CMakeLists.txt`.
- The command must create a notification through `UpsertNotification` and open `XIAOXIN_CARD_PAGE_NOTIFICATIONS`.
- Keep the command name stable as `notify_test`.

---

### Task 1: Xiaoxin Serial Debug Command

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Test: `tests/xiaoxin_serial_debug_command_test.py`

**Interfaces:**
- Consumes: `PaopaoPetDisplay::UpsertNotification(...)`, `PaopaoPetDisplay::SetCardPagerPage(xiaoxin_card_page_t)`, and ESP-IDF `esp_console_cmd_register`.
- Produces: a UART REPL command named `notify_test`.

- [ ] **Step 1: Write the failing test**

```python
def test_notify_test_command_registers_uart_repl_and_opens_notification_page():
    source = read_source()
    body = function_body(source, "void InitializeDebugConsole()")

    assert '#include <esp_console.h>' in source
    assert '.command = "notify_test"' in body
    assert "esp_console_cmd_register(&notify_test_cmd)" in body
    assert "esp_console_new_repl_uart(&hw_config, &repl_config, &debug_console_repl_)" in body
    assert "esp_console_start_repl(debug_console_repl_)" in body
    assert "UpsertNotification(" in body
    assert "SetCardPagerPage(XIAOXIN_CARD_PAGE_NOTIFICATIONS)" in body
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pytest tests/xiaoxin_serial_debug_command_test.py -q`

Expected: FAIL because `InitializeDebugConsole` is not present.

- [ ] **Step 3: Write minimal implementation**

Add `#include <esp_console.h>` and `esp_console/esp_console.h` companion includes if required by the local ESP-IDF headers. Add `esp_console_repl_t* debug_console_repl_ = nullptr;` to `CustomBoard`. Implement `InitializeDebugConsole()` to register `notify_test`, start a UART REPL, and call it from the constructor after display setup.

- [ ] **Step 4: Run test to verify it passes**

Run: `pytest tests/xiaoxin_serial_debug_command_test.py -q`

Expected: PASS.

- [ ] **Step 5: Verify broader notification static tests**

Run: `pytest tests/xiaoxin_serial_debug_command_test.py tests/xiaoxin_notification_visual_path_test.py -q`

Expected: PASS.
