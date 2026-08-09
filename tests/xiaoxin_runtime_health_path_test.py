from pathlib import Path


CMAKE_SOURCE = Path("main/CMakeLists.txt")
RUNTIME_HEALTH_HEADER = Path("main/boards/common/runtime_health.h")
RUNTIME_HEALTH_SOURCE = Path("main/boards/common/runtime_health.cc")
APPLICATION_SOURCE = Path("main/application.cc")
WAVESHARE_146_SOURCE = Path(
    "main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc"
)


def read_source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def assert_ordered(source: str, *needles: str):
    cursor = 0
    for needle in needles:
        index = source.find(needle, cursor)
        assert index != -1, f"missing ordered source fragment: {needle}"
        cursor = index + len(needle)


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


def test_runtime_health_service_is_compiled_and_uses_esp_nvs():
    cmake = read_source(CMAKE_SOURCE)
    header = read_source(RUNTIME_HEALTH_HEADER)
    source = read_source(RUNTIME_HEALTH_SOURCE)

    assert '"boards/common/runtime_health.cc"' in cmake
    assert '"boards/common/runtime_health_model.c"' in cmake
    assert 'static constexpr const char* k_namespace = "runtime_health";' in source
    assert "esp_reset_reason()" in source
    assert "nvs_get_u32" in source
    assert "nvs_set_u32" in source
    assert "RuntimeHealthStart(bool on_battery)" in header
    assert "RuntimeHealthMaybeCheckpoint(void)" in header
    assert "RuntimeHealthReadSnapshot" in header
    assert "RuntimeHealthRecordLowBatteryShutdown" in header
    assert "k_low_battery_shutdown_count_key" in source
    assert "k_last_low_battery_voltage_key" in source
    assert "k_last_low_battery_stage_key" in source


def test_runtime_health_protection_signal_gates_xiaoxin_startup_only():
    header = read_source(RUNTIME_HEALTH_HEADER)
    source = read_source(RUNTIME_HEALTH_SOURCE)
    application = read_source(APPLICATION_SOURCE)
    board = read_source(WAVESHARE_146_SOURCE)

    assert "RuntimeHealthProtectionRecommended(void)" in header
    body = function_body(source, "bool RuntimeHealthProtectionRecommended(void)")
    assert "xiaoxin_runtime_health_protection_recommended(&s_record)" in body
    assert "RuntimeHealthProtectionRecommended" not in application
    assert "startup_low_battery_protection_ = on_battery_ && RuntimeHealthProtectionRecommended();" in board
    constructor = function_body(board, "CustomBoard()")
    assert_ordered(
        constructor,
        "RuntimeHealthStart(on_battery_);",
        "startup_low_battery_protection_ = on_battery_ && RuntimeHealthProtectionRecommended();",
        "InitializeSpd2010Display();",
        "HandleStartupLowBatteryProtection();",
    )


def test_runtime_health_is_wired_into_startup_tick_reboot_and_poweroff():
    application = read_source(APPLICATION_SOURCE)
    board = read_source(WAVESHARE_146_SOURCE)

    assert '#include "runtime_health.h"' in application
    assert '#include "runtime_health.h"' in board

    assert_ordered(
        board,
        "DetectPowerSourceEarly();",
        "RuntimeHealthStart(on_battery_);",
        "BootDiagnosticsStart(on_battery_);",
    )
    assert_ordered(
        application,
        "if (bits & MAIN_EVENT_CLOCK_TICK)",
        "clock_ticks_++;",
        "RuntimeHealthMaybeCheckpoint();",
    )
    assert_ordered(
        application,
        "void Application::Reboot()",
        "RuntimeHealthForceCheckpoint();",
        "esp_restart();",
    )
    assert_ordered(
        board,
        "void RequestPowerOff()",
        "RuntimeHealthForceCheckpoint();",
        "gpio_set_level(PWR_Control_PIN, xiaoxin_power_control_power_hold(&power_control_));",
    )


def test_runtime_health_serial_command_prints_operator_summary():
    source = read_source(WAVESHARE_146_SOURCE)
    body = function_body(source, "void InitializeDebugConsole()")

    assert 'const esp_console_cmd_t runtime_health_cmd = {' in body
    assert '.command = "runtime_health"' in body
    assert "RuntimeHealthReadSnapshot(&snapshot)" in body
    assert "xiaoxin_runtime_health_format_duration" in body
    assert "current_duration" in body
    assert "last_duration" in body
    assert "max_duration" in body
    assert "xiaoxin_runtime_health_reset_label(snapshot.last_reset_kind)" in body
    assert (
        '"runtime: current=%s last=%s max=%s reset=%s brownout=%lu short_streak=%lu battery=%d '
        'low_shutdowns=%lu low_mv=%lu low_stage=%s\\n"'
        in body
    )
    assert "snapshot.current_on_battery ? 1 : 0" in body
    assert "snapshot.low_battery_shutdown_count" in body
    assert "snapshot.last_low_battery_shutdown_voltage_mv" in body
    assert 'snapshot.last_low_battery_shutdown_startup_stage ? "startup" : "runtime"' in body
    assert "esp_console_cmd_register(&runtime_health_cmd)" in body


def test_runtime_health_records_low_battery_shutdown_diagnostics():
    header = read_source(RUNTIME_HEALTH_HEADER)
    source = read_source(RUNTIME_HEALTH_SOURCE)

    assert "void RuntimeHealthRecordLowBatteryShutdown(int voltage_mv, bool startup_stage);" in header
    body = function_body(source, "void RuntimeHealthRecordLowBatteryShutdown(int voltage_mv, bool startup_stage)")
    assert "if (!s_started) {" in body
    assert "LoadRecord();" in body
    assert "xiaoxin_runtime_health_record_low_battery_shutdown(" in body
    assert "voltage_mv > 0 ? (uint32_t)voltage_mv : 0" in body
    assert "PersistRecord();" in body


def test_system_info_json_exposes_runtime_health_before_board():
    source = read_source(Path("main/boards/common/board.cc"))
    body = function_body(source, "std::string Board::GetSystemInfoJson()")

    assert '#include "runtime_health.h"' in source
    assert "xiaoxin_runtime_health_snapshot_t snapshot = {};" in body
    assert "RuntimeHealthReadSnapshot(&snapshot)" in body
    assert_ordered(
        body,
        'json += R"("ota":{)"',
        'json += R"("runtime_health":{)"',
        'json += R"("current_runtime_sec":)" + std::to_string(snapshot.current_runtime_sec)',
        'json += R"("last_runtime_sec":)" + std::to_string(snapshot.last_runtime_sec)',
        'json += R"("max_runtime_sec":)" + std::to_string(snapshot.max_runtime_sec)',
        'json += R"("last_reset":")" + std::string(xiaoxin_runtime_health_reset_label(snapshot.last_reset_kind))',
        'json += R"("brownout_count":)" + std::to_string(snapshot.brownout_count)',
        'json += R"("short_run_streak":)" + std::to_string(snapshot.short_run_streak)',
        'json += R"("board":)" + GetBoardJson();',
    )
