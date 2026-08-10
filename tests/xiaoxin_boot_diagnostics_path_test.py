from pathlib import Path


BOARD_SOURCE = Path(
    "main/boards/waveshare/esp32-s3-touch-lcd-1.46/"
    "esp32-s3-touch-lcd-1.46.cc"
)
APPLICATION_SOURCE = Path("main/application.cc")
WIFI_BOARD_SOURCE = Path("main/boards/common/wifi_board.cc")
CMAKE_SOURCE = Path("main/CMakeLists.txt")
DIAGNOSTICS_HEADER = Path("main/boards/common/boot_diagnostics.h")
DIAGNOSTICS_SOURCE = Path("main/boards/common/boot_diagnostics.cc")


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


def test_boot_diagnostics_module_is_compiled_and_uses_nvs():
    cmake = read_source(CMAKE_SOURCE)
    header = read_source(DIAGNOSTICS_HEADER)
    source = read_source(DIAGNOSTICS_SOURCE)

    assert '"boards/common/boot_diagnostics.cc"' in cmake
    assert "void BootDiagnosticsStart(bool on_battery);" in header
    assert "void BootDiagnosticsMark(const char* stage);" in header
    assert "void BootDiagnosticsMarkError(const char* stage, int error_code);" in header
    assert "bool BootDiagnosticsReadPrevious(char* buffer, size_t buffer_size, bool* on_battery);" in header
    assert "nvs_open" in source
    assert "nvs_set_str" in source
    assert "nvs_get_str" in source
    assert "nvs_commit" in source
    assert "esp_timer_get_time()" in source


def test_battery_boot_starts_persistent_diagnostics_after_power_source_detection():
    source = read_source(BOARD_SOURCE)
    constructor = function_body(source, "CustomBoard()")

    assert '#include "boot_diagnostics.h"' in source
    assert "BootDiagnosticsStart(on_battery_);" in constructor
    assert "BootDiagnosticsMark(\"board_power_source_detected\");" in constructor
    assert "BootDiagnosticsFlush();" in constructor
    assert constructor.index("DetectPowerSourceEarly();") < constructor.index("BootDiagnosticsStart(on_battery_);")
    assert constructor.index("BootDiagnosticsStart(on_battery_);") < constructor.index("InitializeI2c();")
    assert constructor.index("InitializeDebugConsole();") < constructor.rindex("BootDiagnosticsFlush();")


def test_boot_diagnostics_start_is_memory_only_until_explicit_flush():
    source = read_source(DIAGNOSTICS_SOURCE)
    start_body = function_body(source, "void BootDiagnosticsStart(bool on_battery)")
    flush_body = function_body(source, "void BootDiagnosticsFlush()")

    assert "void BootDiagnosticsFlush(void);" in read_source(DIAGNOSTICS_HEADER)
    assert "nvs_open" not in start_body
    assert "PersistPreviousTrace(" not in start_body
    assert "PersistCurrentTrace();" in flush_body
    assert "PersistPreviousTrace(" in flush_body


def test_board_boot_records_each_high_current_stage():
    source = read_source(BOARD_SOURCE)
    constructor = function_body(source, "CustomBoard()")

    for stage in [
        "board_i2c_start",
        "board_io_expander_start",
        "board_qspi_start",
        "board_display_start",
        "board_backlight_ready",
        "board_touch_start",
        "board_buttons_start",
        "board_power_save_timer_start",
        "board_constructor_done",
    ]:
        assert f'BootDiagnosticsMark("{stage}")' in constructor


def test_application_boot_records_ui_audio_network_and_protocol_stages():
    source = read_source(APPLICATION_SOURCE)
    initialize = function_body(source, "void Application::Initialize()")
    activation = function_body(source, "void Application::ActivationTask()")
    protocol = function_body(source, "bool Application::InitializeProtocol()")

    assert '#include "boot_diagnostics.h"' in source
    assert 'BootDiagnosticsMark("app_initialize_start")' in initialize
    assert 'BootDiagnosticsMark("app_ui_ready")' in initialize
    assert 'BootDiagnosticsMark("app_audio_start")' in initialize
    assert 'BootDiagnosticsMark("app_network_start")' in initialize
    assert 'ESP_LOGI(TAG, "Boot: Audio");' in initialize
    assert 'display->SetStatus("Boot: Audio");' not in initialize
    assert 'display->SetStatus("Boot: Wi-Fi");' in initialize
    assert 'display->CompleteBootSplash();' in initialize
    assert initialize.index('BootDiagnosticsMark("app_audio_start")') < initialize.index(
        'ESP_LOGI(TAG, "Boot: Audio");'
    )
    assert initialize.index('ESP_LOGI(TAG, "Boot: Audio");') < initialize.index(
        "auto codec = board.GetAudioCodec();"
    )
    assert initialize.index("auto codec = board.GetAudioCodec();") < initialize.index(
        "audio_service_.Initialize(codec);"
    )
    assert initialize.index("audio_service_.Initialize(codec);") < initialize.index(
        "audio_service_.Start();"
    )
    assert initialize.index("audio_service_.Start();") < initialize.index(
        "display->CompleteBootSplash();"
    )
    assert initialize.index("display->CompleteBootSplash();") < initialize.index(
        'BootDiagnosticsMark("app_network_start")'
    )
    assert initialize.index('BootDiagnosticsMark("app_network_start")') < initialize.index(
        'display->SetStatus("Boot: Wi-Fi");'
    )
    assert 'BootDiagnosticsMark("activation_task_start")' in activation
    assert 'BootDiagnosticsMark("protocol_initialize_start")' in protocol
    assert 'BootDiagnosticsMark("protocol_start_done")' in protocol


def test_wifi_events_and_timeout_are_recorded_for_offline_battery_replay():
    source = read_source(WIFI_BOARD_SOURCE)
    event_body = function_body(source, "void WifiBoard::OnNetworkEvent(NetworkEvent event, const std::string& data)")
    timeout_body = function_body(source, "void WifiBoard::OnWifiConnectTimeout(void* arg)")

    assert '#include "boot_diagnostics.h"' in source
    for marker, stage in [
        ("case NetworkEvent::Scanning:", "wifi_scanning"),
        ("case NetworkEvent::Connecting:", "wifi_connecting"),
        ("case NetworkEvent::Connected:", "wifi_connected"),
        ("case NetworkEvent::Disconnected:", "wifi_disconnected"),
        ("case NetworkEvent::WifiConfigModeEnter:", "wifi_config_mode_enter"),
    ]:
        assert f'BootDiagnosticsMark("{stage}")' in block_after(event_body, marker)

    assert 'BootDiagnosticsMark("wifi_connect_timeout")' in timeout_body


def test_ota_check_and_activation_failures_record_error_codes():
    source = read_source(APPLICATION_SOURCE)
    check_new_version = function_body(source, "void Application::CheckNewVersion()")
    activation = block_after(check_new_version, "esp_err_t err = ota_->Activate();", length=520)

    assert 'BootDiagnosticsMark("ota_check_start")' in check_new_version
    assert 'BootDiagnosticsMark("ota_check_done")' in check_new_version
    assert 'BootDiagnosticsMarkError("ota_check_failed", err)' in check_new_version
    assert 'BootDiagnosticsMark("activation_poll_start")' in check_new_version
    assert 'BootDiagnosticsMark("activation_poll_done")' in check_new_version
    assert 'BootDiagnosticsMarkError("activation_poll_failed", err)' in activation


def test_debug_console_exposes_previous_boot_diagnostics_trace():
    source = read_source(BOARD_SOURCE)
    body = function_body(source, "void InitializeDebugConsole()")

    assert '"boot_diag"' in body
    assert "BootDiagnosticsReadPrevious" in body
    assert "BootDiagnosticsReadCurrent" in body
    assert "previous boot" in body
    assert "current boot" in body


def test_battery_boot_defers_debug_console_until_after_constructor():
    source = read_source(BOARD_SOURCE)
    constructor = function_body(source, "CustomBoard()")
    deferred = function_body(source, "void ScheduleDeferredDebugConsole()")

    assert "ScheduleDeferredDebugConsole();" in constructor
    assert "if (on_battery_)" in constructor
    assert constructor.index("ScheduleDeferredDebugConsole();") < constructor.index(
        'BootDiagnosticsMark("board_constructor_done")'
    )
    assert constructor.index("InitializeDebugConsole();") < constructor.index(
        'BootDiagnosticsMark("board_constructor_done")'
    )
    assert "xTaskCreate" in deferred
    assert "vTaskDelay(pdMS_TO_TICKS(2000));" in deferred
    assert "self->InitializeDebugConsole();" in deferred


def test_application_completes_boot_splash_when_ui_can_be_revealed():
    display_header = read_source(Path("main/display/display.h"))
    application = read_source(APPLICATION_SOURCE)
    activation_done = function_body(application, "void Application::HandleActivationDoneEvent()")
    check_version = function_body(application, "void Application::CheckNewVersion()")
    state_changed = function_body(application, "void Application::HandleStateChangedEvent()")

    assert "virtual void CompleteBootSplash()" in display_header
    assert "display->CompleteBootSplash();" in activation_done
    assert activation_done.index("SetDeviceState(kDeviceStateIdle);") < activation_done.index(
        "display->CompleteBootSplash();"
    )
    assert "display->CompleteBootSplash();" in block_after(check_version, "if (ota_->HasActivationCode())")
    assert "display->CompleteBootSplash();" in block_after(state_changed, "case kDeviceStateWifiConfiguring:")
