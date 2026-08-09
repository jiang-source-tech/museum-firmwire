from pathlib import Path


BOARD_SOURCE = Path(
    "main/boards/waveshare/esp32-s3-touch-lcd-1.46/"
    "esp32-s3-touch-lcd-1.46.cc"
)
APPLICATION_SOURCE = Path("main/application.cc")
BOARD_HEADER = Path("main/boards/common/board.h")


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


def assert_ordered(source: str, *needles: str):
    cursor = 0
    for needle in needles:
        index = source.find(needle, cursor)
        assert index != -1, f"missing ordered fragment: {needle}"
        cursor = index + len(needle)


def test_board_wires_adc_samples_into_battery_state_machine():
    source = read_source()
    constructor = function_body(source, "CustomBoard()")
    monitor = function_body(source, "void RefreshBatteryStateFromTimer()")
    reader = function_body(source, "bool ReadBatteryVoltageMv(int* voltage_mv)")

    assert '#include "xiaoxin_battery_state.h"' in source
    assert "xiaoxin_battery_context_t battery_context_" in source
    assert "xiaoxin_battery_snapshot_t battery_snapshot_" in source
    assert "esp_timer_handle_t battery_monitor_timer_" in source
    assert "battery_adc_available_" in reader
    assert "adc_oneshot_read(" in reader
    assert "adc_cali_raw_to_voltage(" in reader
    assert "pin_voltage_mv * k_battery_voltage_divider" in reader
    assert "xiaoxin_battery_state_update(" in monitor
    assert "XIAOXIN_BATTERY_LOAD_IDLE" in monitor
    assert "HandleBatterySnapshot(battery_snapshot_);" in monitor
    assert "StartBatteryMonitor();" in constructor
    assert_ordered(constructor, "InitializeSpd2010Display();", "StartBatteryMonitor();")


def test_low_edge_shows_one_time_warning_without_stopping_core_tasks():
    source = read_source()
    handler = function_body(source, "void HandleBatterySnapshot(const xiaoxin_battery_snapshot_t& snapshot)")

    assert "snapshot.low_edge" in handler
    assert "snapshot.state == XIAOXIN_BATTERY_STATE_LOW" not in handler
    assert "display->ShowLowBatteryNotification();" in handler
    assert 'ShowNotification("电量低，请尽快充电"' not in handler
    assert "InitializeMotion()" not in handler
    assert "StartNetwork()" not in handler
    assert "SetEnabled(false)" not in handler


def test_critical_edge_starts_cancelable_shutdown_without_restart():
    source = read_source()
    handler = function_body(source, "void HandleBatterySnapshot(const xiaoxin_battery_snapshot_t& snapshot)")
    begin = function_body(source, "void BeginLowBatteryShutdown(bool startup_stage)")
    finish = function_body(source, "void FinishLowBatteryShutdown()")
    cancel = function_body(source, "void CancelLowBatteryShutdownIfRecovered(const xiaoxin_battery_snapshot_t& snapshot)")

    assert "CancelLowBatteryShutdownIfRecovered(snapshot);" in handler
    assert "snapshot.critical_edge" in handler
    assert "snapshot.state == XIAOXIN_BATTERY_STATE_CRITICAL" not in handler
    assert "BeginLowBatteryShutdown(false);" in handler
    assert 'ShowNotification("电量不足，即将关机"' in begin
    assert "esp_timer_start_once(low_battery_shutdown_timer_" in begin
    assert "snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL" in cancel
    assert "snapshot.recovered_edge" in cancel
    assert "low_battery_shutdown_pending_ = false;" in cancel
    final_check = finish[finish.index("if (!low_battery_shutdown_startup_stage_)") :]
    assert_ordered(
        final_check,
        "ReadBatteryVoltageMv(&voltage_mv)",
        "xiaoxin_battery_state_update(",
        "CancelLowBatteryShutdownIfRecovered(battery_snapshot_);",
        "if (!low_battery_shutdown_pending_) {",
        "return;",
        "low_battery_shutdown_pending_ = false;",
        "xiaoxin_power_control_request_shutdown(&power_control_);",
    )
    assert "RuntimeHealthRecordLowBatteryShutdown(" in finish
    assert "RuntimeHealthForceCheckpoint();" in finish
    assert "xiaoxin_power_control_request_shutdown(&power_control_);" in finish
    assert "gpio_set_level(PWR_Control_PIN, xiaoxin_power_control_power_hold(&power_control_));" in finish
    assert "esp_restart()" not in begin
    assert "esp_restart()" not in finish


def test_startup_stage_shutdown_is_not_canceled_by_recovery_path():
    source = read_source()
    cancel = function_body(source, "void CancelLowBatteryShutdownIfRecovered(const xiaoxin_battery_snapshot_t& snapshot)")

    assert_ordered(
        cancel,
        "if (!low_battery_shutdown_pending_) {",
        "if (low_battery_shutdown_startup_stage_) {",
        "return;",
        "const bool external_power = snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL;",
    )
    assert "low_battery_shutdown_pending_ = false;" in cancel
    assert "low_battery_shutdown_startup_stage_ = false;" in cancel


def test_get_battery_level_reports_only_reliable_snapshot_percent():
    source = read_source()
    body = function_body(source, "virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override")

    assert "battery_has_snapshot_" in body
    assert "battery_snapshot_.percent_reliable" in body
    assert "level = battery_snapshot_.display_percent;" in body
    assert "charging = battery_snapshot_.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL;" in body
    assert "discharging = battery_snapshot_.power_source == XIAOXIN_BATTERY_POWER_BATTERY;" in body
    assert "return true;" in body


def test_startup_protection_uses_runtime_health_before_full_app_work():
    source = read_source()
    application = read_source(APPLICATION_SOURCE)
    board_header = read_source(BOARD_HEADER)
    constructor = function_body(source, "CustomBoard()")
    start_network = function_body(source, "void StartNetwork() override")
    initialize = function_body(application, "void Application::Initialize()")

    assert "startup_low_battery_protection_" in source
    assert "virtual bool ShouldSkipApplicationStartup() { return false; }" in board_header
    assert "bool ShouldSkipApplicationStartup() override" in source
    assert "RuntimeHealthProtectionRecommended()" in constructor
    assert "XIAOXIN_BATTERY_STATE_CRITICAL" not in constructor
    assert_ordered(
        constructor,
        "RuntimeHealthStart(on_battery_);",
        "startup_low_battery_protection_ = on_battery_ && RuntimeHealthProtectionRecommended();",
        "InitializeSpd2010Display();",
        "HandleStartupLowBatteryProtection();",
    )
    assert "if (startup_low_battery_protection_)" in start_network
    assert "return;" in start_network
    assert_ordered(
        initialize,
        "auto& board = Board::GetInstance();",
        "if (board.ShouldSkipApplicationStartup())",
        "return;",
        "BootDiagnosticsMark(\"app_initialize_start\");",
        "SetDeviceState(kDeviceStateStarting);",
        "display->SetupUI();",
        "auto codec = board.GetAudioCodec();",
        "board.StartNetwork();",
    )


def test_battery_console_command_prints_last_sample_and_state():
    source = read_source()
    body = function_body(source, "void InitializeDebugConsole()")

    assert 'const esp_console_cmd_t battery_cmd = {' in body
    assert '.command = "battery"' in body
    assert "battery_snapshot_.state" in body
    assert "battery_snapshot_.power_source" in body
    assert "last_battery_voltage_mv_" in body
    assert "esp_console_cmd_register(&battery_cmd)" in body
