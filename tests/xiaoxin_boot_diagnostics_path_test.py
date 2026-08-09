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


def test_board_boot_records_each_high_current_stage_and_deferred_imu():
    source = read_source(BOARD_SOURCE)
    constructor = function_body(source, "CustomBoard()")
    deferred = function_body(source, "void ScheduleDeferredMotionInit()")

    for stage in [
        "board_i2c_start",
        "board_io_expander_start",
        "board_qspi_start",
        "board_display_start",
        "board_backlight_ready",
        "board_touch_start",
        "board_buttons_start",
        "board_power_save_timer_start",
        "board_imu_deferred",
        "board_imu_start",
        "board_constructor_done",
    ]:
        assert f'BootDiagnosticsMark("{stage}")' in constructor or f'BootDiagnosticsMark("{stage}")' in deferred

    assert 'BootDiagnosticsMark("board_imu_deferred_start")' in deferred
    assert deferred.index('BootDiagnosticsMark("board_imu_deferred_start")') < deferred.index(
        "self->InitializeMotion();"
    )


def test_application_boot_records_ui_audio_network_and_protocol_stages():
    source = read_source(APPLICATION_SOURCE)
    initialize = function_body(source, "void Application::Initialize()")
    activation = function_body(source, "void Application::ActivationTask()")
    protocol = function_body(source, "void Application::InitializeProtocol()")

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


def test_battery_boot_splash_is_black_high_brightness_and_held_until_ready():
    source = read_source(BOARD_SOURCE)
    show_splash = function_body(source, "void ShowBootSplashLocked()")
    hide_from_timer = function_body(source, "void HideBootSplashFromTimer()")
    complete_splash = function_body(source, "void CompleteBootSplash()")
    constructor = function_body(source, "CustomBoard()")
    start_network = function_body(source, "void StartNetwork() override")

    assert "k_boot_splash_brightness_percent = 100" in source
    assert "k_boot_splash_ready_hold_timeout_ms" in source
    assert "boot_splash_wait_for_ready_ = s_boot_on_battery;" in show_splash
    assert "assets_images_boot_gif_end - assets_images_boot_gif_start" in show_splash
    assert "boot_splash_source_dsc_.data = assets_images_boot_gif_start;" in show_splash
    assert "assets_images_waving_gif_start" not in show_splash
    assert "lv_color_hex(0x000000)" in show_splash
    assert "std::make_unique<LvglGif>(&boot_splash_source_dsc_, true, 0x000000, true)" in show_splash
    assert "if (boot_splash_wait_for_ready_)" in hide_from_timer
    assert "return;" not in block_after(hide_from_timer, "if (boot_splash_wait_for_ready_)")
    assert "boot_splash_wait_for_ready_ = false;" in hide_from_timer
    assert hide_from_timer.index("boot_splash_wait_for_ready_ = false;") < hide_from_timer.index(
        "HideBootSplashLocked();"
    )
    assert "boot_splash_wait_for_ready_ = false;" in complete_splash
    assert "HideBootSplashLocked();" in complete_splash
    assert "Board::GetInstance().GetBacklight()" in complete_splash
    assert "backlight->RestoreBrightness();" in complete_splash
    assert "GetBacklight()->SetBrightness(k_boot_splash_brightness_percent, false);" in constructor
    assert "SetBrightness(10, false)" not in start_network
    assert "RestoreBrightness();" not in start_network


def test_complete_boot_splash_only_restores_brightness_when_splash_was_visible():
    source = read_source(BOARD_SOURCE)
    complete_splash = function_body(source, "void CompleteBootSplash()")

    assert "const bool restore_boot_brightness = boot_splash_visible_;" in complete_splash
    assert complete_splash.index("const bool restore_boot_brightness = boot_splash_visible_;") < complete_splash.index(
        "HideBootSplashLocked();"
    )
    assert "if (restore_boot_brightness)" in complete_splash
    assert complete_splash.index("if (restore_boot_brightness)") < complete_splash.index(
        "backlight->RestoreBrightness();"
    )
    assert "if (backlight != nullptr)" in complete_splash


def test_application_shows_waving_greeting_between_boot_splash_and_wifi_start():
    display_header = read_source(Path("main/display/display.h"))
    application = read_source(APPLICATION_SOURCE)
    board_source = read_source(BOARD_SOURCE)
    initialize = function_body(application, "void Application::Initialize()")
    greeting = function_body(board_source, "virtual uint32_t ShowBootGreeting() override")

    assert "virtual uint32_t ShowBootGreeting()" in display_header
    assert "const uint32_t boot_greeting_ms = display->ShowBootGreeting();" in initialize
    assert "vTaskDelay(pdMS_TO_TICKS(boot_greeting_ms));" in initialize
    assert initialize.index("display->CompleteBootSplash();") < initialize.index(
        "display->ShowBootGreeting();"
    )
    assert initialize.index("display->ShowBootGreeting();") < initialize.index(
        'BootDiagnosticsMark("app_network_start")'
    )
    assert initialize.index("vTaskDelay(pdMS_TO_TICKS(boot_greeting_ms));") < initialize.index(
        'display->SetStatus("Boot: Wi-Fi");'
    )
    assert "assets_images_waving_gif_start" in greeting
    assert "PlayGifBinaryLocked" in greeting
    assert "k_boot_greeting_duration_ms" in greeting


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


def test_xiaoxin_boot_splash_reveals_after_normal_ui_first_frame_is_ready():
    source = read_source(BOARD_SOURCE)
    setup_ui = function_body(source, "virtual void SetupUI() override")
    reveal = function_body(source, "void ScheduleBootSplashRevealAfterUiReadyLocked()")

    assert "k_boot_splash_ui_ready_reveal_delay_ms" in source
    assert "k_boot_splash_ui_ready_reveal_delay_ms = 5000" in source
    assert "ScheduleBootSplashRevealAfterUiReadyLocked();" in setup_ui
    assert setup_ui.index("PlayGifState(current_state_);") < setup_ui.index("ShowBootSplashLocked();")
    assert setup_ui.index("ShowBootSplashLocked();") < setup_ui.index(
        "ScheduleBootSplashRevealAfterUiReadyLocked();"
    )
    assert "if (boot_splash_wait_for_ready_)" in reveal
    wait_branch = block_after(reveal, "if (boot_splash_wait_for_ready_)", length=160)
    assert "return;" in wait_branch
    assert wait_branch.index("return;") < reveal.index("boot_splash_wait_for_ready_ = false;")
    assert "boot_splash_wait_for_ready_ = false;" in reveal
    assert "esp_timer_stop(boot_splash_timer_);" in reveal
    assert "esp_timer_start_once(boot_splash_timer_, k_boot_splash_ui_ready_reveal_delay_ms * 1000ULL)" in reveal
    assert "HideBootSplashLocked();" in reveal


def test_xiaoxin_boot_splash_stays_above_system_bars_while_visible():
    source = read_source(BOARD_SOURCE)
    raise_boot = function_body(source, "void RaiseBootSplashLayerLocked()")
    raise_overlays = function_body(source, "void RaiseOverlayObjects()")

    assert "boot_splash_visible_" in raise_boot
    assert "boot_splash_layer_ != nullptr" in raise_boot
    assert "lv_obj_move_foreground(boot_splash_layer_);" in raise_boot
    assert "RaiseBootSplashLayerLocked();" in raise_overlays
    assert raise_overlays.count("RaiseBootSplashLayerLocked();") >= 2
    assert raise_overlays.index("lv_obj_move_foreground(bottom_bar_);") < raise_overlays.rindex(
        "RaiseBootSplashLayerLocked();"
    )
    card_branch = block_after(raise_overlays, "if (IsCardLayerVisible())", length=420)
    assert "RaiseBootSplashLayerLocked();" in card_branch
    assert card_branch.index("RaiseBootSplashLayerLocked();") < card_branch.index("return;")
