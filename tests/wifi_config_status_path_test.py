from pathlib import Path


WIFI_BOARD_SOURCE = Path("main/boards/common/wifi_board.cc")
APPLICATION_SOURCE = Path("main/application.cc")
SDKCONFIG_SOURCE = Path("sdkconfig")
SDKCONFIG_DEFAULTS_SOURCE = Path("sdkconfig.defaults")


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


def block_after(source: str, marker: str, length: int = 240) -> str:
    start = source.index(marker)
    return source[start : start + length]


def braced_block_after(source: str, marker: str) -> str:
    start = source.index(marker)
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
    raise AssertionError(f"block not found: {marker}")


def test_wifi_config_mode_network_icon_does_not_claim_connected():
    body = function_body(read_source(WIFI_BOARD_SOURCE), "const char* WifiBoard::GetNetworkStateIcon()")
    config_mode_block = braced_block_after(body, "if (wifi.IsConfigMode())")

    assert "FONT_AWESOME_WIFI_SLASH" in config_mode_block
    assert "return FONT_AWESOME_WIFI;" not in config_mode_block


def test_wifi_config_state_refreshes_status_bar_immediately():
    body = function_body(read_source(APPLICATION_SOURCE), "void Application::HandleStateChangedEvent()")
    wifi_config_block = block_after(body, "case kDeviceStateWifiConfiguring:")

    assert "display->SetStatus(Lang::Strings::WIFI_CONFIG_MODE);" in wifi_config_block
    assert "display->UpdateStatusBar(true);" in wifi_config_block


def test_wifi_config_state_can_enter_display_standby_when_idle():
    body = function_body(read_source(APPLICATION_SOURCE), "bool Application::CanEnterSleepMode()")

    assert "const DeviceState state = GetDeviceState();" in body
    assert "state != kDeviceStateIdle && state != kDeviceStateWifiConfiguring" in body
    assert "protocol_ && protocol_->IsAudioChannelOpened()" in body
    assert "!audio_service_.IsIdle()" in body


def test_wifi_scan_and_connect_update_top_status_instead_of_leaving_initializing():
    body = function_body(read_source(APPLICATION_SOURCE), "void Application::Initialize()")
    scanning_block = block_after(body, "case NetworkEvent::Scanning:", length=260)
    connecting_block = block_after(body, "case NetworkEvent::Connecting:", length=520)

    assert "display->SetStatus(Lang::Strings::SCANNING_WIFI);" in scanning_block
    assert "display->SetStatus(Lang::Strings::CONNECTING);" in connecting_block


def test_application_leaves_initializing_status_before_starting_network():
    body = function_body(read_source(APPLICATION_SOURCE), "void Application::Initialize()")
    before_start_network = body[: body.index("board.StartNetwork();")]

    assert "display->SetStatus(Lang::Strings::SCANNING_WIFI);" in before_start_network
    assert "display->UpdateStatusBar(true);" in before_start_network


def test_wifi_connection_starts_time_synchronization():
    source = read_source(WIFI_BOARD_SOURCE)
    body = function_body(source, "void WifiBoard::OnNetworkEvent(NetworkEvent event, const std::string& data)")
    connected_block = block_after(body, "case NetworkEvent::Connected:", length=420)
    sync_body = function_body(source, "static void StartTimeSynchronization()")

    assert "#include <esp_sntp.h>" in source
    assert "StartTimeSynchronization();" in connected_block
    assert "static constexpr const char* NTP_SERVERS[]" in source
    assert '"ntp.aliyun.com"' in source
    assert '"ntp.tencent.com"' in source
    assert '"ntp.ntsc.ac.cn"' in source
    assert '"cn.pool.ntp.org"' not in source
    assert '"pool.ntp.org"' not in source
    assert "k_ntp_server_count" in source
    assert 'static constexpr char DEFAULT_TIMEZONE[] = "CST-8";' in source
    assert '#include <stdlib.h>' in source
    assert '#include <sys/time.h>' in source
    assert '#include <time.h>' in source
    assert 'setenv("TZ", DEFAULT_TIMEZONE, 1);' in source
    assert "tzset();" in source
    assert "esp_sntp_set_time_sync_notification_cb(OnSntpTimeSync);" in source
    assert "for (size_t i = 0; i < k_ntp_server_count; ++i)" in source
    assert "esp_sntp_setservername(i, NTP_SERVERS[i]);" in source
    assert "esp_sntp_init();" in source
    assert "esp_sntp_enabled()" in sync_body
    assert "esp_sntp_get_sync_status()" in sync_body
    assert "esp_sntp_restart()" in sync_body
    assert "MarkTimeSyncStarted();" in braced_block_after(sync_body, "if (sntp_started || esp_sntp_enabled())")


def test_manual_wifi_reconfiguration_forgets_previous_credentials_before_config_ap():
    source = read_source(WIFI_BOARD_SOURCE)
    body = function_body(source, "void WifiBoard::EnterWifiConfigMode()")

    assert "SsidManager::GetInstance().Clear();" in source
    assert "ClearSavedWifiCredentialsForReconfiguration();" in body

    for start in [index for index in range(len(body)) if body.startswith("WifiManager::GetInstance().StopStation();", index)]:
        following = body[start : body.index("StartWifiConfigMode();", start)]
        assert "ClearSavedWifiCredentialsForReconfiguration();" in following


def test_local_default_credentials_remain_available_as_wifi_fallback():
    source = read_source(WIFI_BOARD_SOURCE)
    body = function_body(source, "void WifiBoard::TryWifiConnect()")
    fallback_body = function_body(source, "static void EnsureDefaultWifiCredentials(SsidManager& ssid_manager)")

    assert 'static constexpr char DEFAULT_WIFI_SSID[] = "Jiang";' in source
    assert 'static constexpr char DEFAULT_WIFI_PASSWORD[] = "lhj123456";' in source
    assert "RemoveDefaultWifiCredentialsIfPresent" not in source
    assert "EnsureDefaultWifiCredentials(ssid_manager);" in body
    assert "item.ssid != DEFAULT_WIFI_SSID" in fallback_body
    assert "item.password != DEFAULT_WIFI_PASSWORD" in fallback_body
    assert "ssid_manager.GetSsidList().empty()" not in fallback_body
    assert "ssid_manager.AddSsid(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASSWORD);" in fallback_body
    assert body.index("EnsureDefaultWifiCredentials(ssid_manager);") < body.index(
        "bool have_ssid = !ssid_manager.GetSsidList().empty();"
    )


def test_manual_wifi_reconfiguration_can_interrupt_activation_ota_check():
    wifi_source = read_source(WIFI_BOARD_SOURCE)
    wifi_body = function_body(wifi_source, "void WifiBoard::EnterWifiConfigMode()")
    delayed_reconfig_block = braced_block_after(
        wifi_body,
        "state == kDeviceStateSpeaking || state == kDeviceStateListening || state == kDeviceStateIdle"
    )

    assert "state == kDeviceStateActivating" in wifi_body
    assert "AbortActivationForWifiConfig();" in wifi_body
    assert "StartWifiConfigMode();" in delayed_reconfig_block

    app_header = read_source(Path("main/application.h"))
    app_source = read_source(APPLICATION_SOURCE)
    activation_body = function_body(app_source, "void Application::ActivationTask()")
    check_version_body = function_body(app_source, "void Application::CheckNewVersion()")

    assert "void AbortActivationForWifiConfig();" in app_header
    assert "activation_abort_requested_" in app_header
    assert activation_body.index("CheckNewVersion();") < activation_body.index("InitializeProtocol();")
    abort_after_version = activation_body.index(
        "if (activation_abort_requested_)", activation_body.index("CheckNewVersion();")
    )
    assert abort_after_version < activation_body.index("InitializeProtocol();")
    assert "if (activation_abort_requested_) {\n        return;\n    }\n\n    // Signal completion" in activation_body
    assert "activation_abort_requested_" in check_version_body


def test_activation_restart_is_deferred_when_previous_activation_task_is_still_exiting():
    app_header = read_source(Path("main/application.h"))
    app_source = read_source(APPLICATION_SOURCE)
    helper_body = function_body(app_source, "void Application::StartActivationTask()")
    network_connected_body = function_body(app_source, "void Application::HandleNetworkConnectedEvent()")

    assert "bool activation_restart_pending_" in app_header
    assert "StartActivationTask();" in network_connected_body
    assert "activation_restart_pending_ = true;" in helper_body
    assert "if (app->activation_restart_pending_)" in helper_body
    assert "app->StartActivationTask();" in helper_body


def test_sntp_config_allows_three_servers():
    sdkconfig = read_source(SDKCONFIG_SOURCE)
    sdkconfig_defaults = read_source(SDKCONFIG_DEFAULTS_SOURCE)

    assert "CONFIG_LWIP_SNTP_MAX_SERVERS=3" in sdkconfig
    assert "CONFIG_LWIP_SNTP_MAX_SERVERS=3" in sdkconfig_defaults


def test_wifi_time_sync_updates_shared_status():
    source = read_source(WIFI_BOARD_SOURCE)
    cmake = Path("main/CMakeLists.txt").read_text(encoding="utf-8")

    assert '#include "time_sync_status.h"' in source
    assert "MarkTimeSyncStarted();" in source
    assert "MarkTimeSyncSucceeded();" in source
    assert '"boards/common/time_sync_status.cc"' in cmake
