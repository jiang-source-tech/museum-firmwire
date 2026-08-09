from pathlib import Path


APPLICATION = Path("main/application.cc")
APPLICATION_HEADER = Path("main/application.h")
KCONFIG = Path("main/Kconfig.projbuild")
SDKCONFIG = Path("sdkconfig")
SDKCONFIG_DEFAULTS = Path("sdkconfig.defaults")
SDKCONFIG_DEFAULTS_XIAOZHI = Path("sdkconfig.defaults.xiaozhi")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"function body not found: {signature}")


def read_config_value(path: Path, key: str) -> str:
    prefix = f"{key}="
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith(prefix):
            return line.removeprefix(prefix).strip().strip('"')
    raise AssertionError(f"{key} not found in {path}")


def test_online_devices_have_a_bounded_six_hour_periodic_ota_schedule() -> None:
    header = APPLICATION_HEADER.read_text(encoding="utf-8")
    source = APPLICATION.read_text(encoding="utf-8")
    kconfig = KCONFIG.read_text(encoding="utf-8")

    assert "config OTA_PERIODIC_CHECK_INTERVAL_SECONDS" in kconfig
    interval_block = kconfig[kconfig.index("config OTA_PERIODIC_CHECK_INTERVAL_SECONDS") :]
    assert 'default 21600' in interval_block
    assert 'range 300 604800' in interval_block
    jitter_block = kconfig[kconfig.index("config OTA_PERIODIC_CHECK_JITTER_SECONDS") :]
    assert 'default 900' in jitter_block
    assert 'range 0 3600' in jitter_block

    assert "uint32_t periodic_ota_check_elapsed_seconds_ = 0;" in header
    assert "uint32_t periodic_ota_check_interval_seconds_ = 0;" in header
    assert "void MaybeSchedulePeriodicOtaCheck();" in header
    assert "uint32_t GetPeriodicOtaCheckIntervalSeconds() const;" in header

    clock_tick = function_body(source, "void Application::Run()")
    assert "MaybeSchedulePeriodicOtaCheck();" in clock_tick

    scheduler = function_body(source, "void Application::MaybeSchedulePeriodicOtaCheck()")
    assert "GetPeriodicOtaCheckIntervalSeconds()" in scheduler
    assert "periodic_ota_check_elapsed_seconds_" in scheduler


def test_active_firmware_configs_keep_the_six_hour_periodic_default() -> None:
    for path in (SDKCONFIG, SDKCONFIG_DEFAULTS, SDKCONFIG_DEFAULTS_XIAOZHI):
        assert read_config_value(path, "CONFIG_OTA_PERIODIC_CHECK_INTERVAL_SECONDS") == "21600"
        assert read_config_value(path, "CONFIG_OTA_PERIODIC_CHECK_JITTER_SECONDS") == "900"
        assert read_config_value(path, "CONFIG_OTA_RELEASE_CHANNEL") == "stable"
        assert read_config_value(path, "CONFIG_OTA_MIN_BATTERY_PERCENT") == "40"


def test_periodic_check_defers_until_the_device_is_safe_and_uses_a_temporary_offer() -> None:
    header = APPLICATION_HEADER.read_text(encoding="utf-8")
    source = APPLICATION.read_text(encoding="utf-8")

    for member in (
        "bool network_connected_ = false;",
        "bool periodic_ota_check_pending_ = false;",
        "void RunPeriodicOtaCheck();",
        "uint32_t GetPeriodicOtaCheckIntervalSeconds() const;",
        "bool CanRunPeriodicOtaCheck(const char** defer_reason);",
        "bool CanRunOtaInstallation(const char** defer_reason);",
        "bool CanRunStartupOtaInstallation(const char** defer_reason);",
    ):
        assert member in header

    connected = function_body(source, "void Application::HandleNetworkConnectedEvent()")
    disconnected = function_body(source, "void Application::HandleNetworkDisconnectedEvent()")
    scheduler = function_body(source, "void Application::MaybeSchedulePeriodicOtaCheck()")
    interval = function_body(source, "uint32_t Application::GetPeriodicOtaCheckIntervalSeconds()")
    safety = function_body(source, "bool Application::CanRunPeriodicOtaCheck(")
    installation_safety = function_body(source, "bool Application::CanRunOtaInstallation(")
    periodic_check = function_body(source, "void Application::RunPeriodicOtaCheck()")

    assert "network_connected_ = true;" in connected
    assert "network_connected_ = false;" in disconnected
    assert "CanRunPeriodicOtaCheck(&defer_reason)" in scheduler
    assert "periodic_ota_check_interval_seconds_" in scheduler
    assert '"Periodic OTA check deferred: %s"' in scheduler
    assert "Schedule([this]()" in scheduler

    for unsafe_condition in (
        "!network_connected_",
        "activation_task_handle_ != nullptr",
        "GetDeviceState() != kDeviceStateIdle",
        "tts_playback_session_.phase() != TtsPlaybackPhase::kIdle",
        "legacy_tts_active_",
        "!tts_ownership_gate_.CanAcquireOwnership()",
        "CanRunOtaInstallation(defer_reason)",
    ):
        assert unsafe_condition in safety

    assert "GetBatteryLevel(" in installation_safety
    assert "battery_state_unknown" in installation_safety
    assert "battery_below_ota_threshold" in installation_safety
    assert "CONFIG_OTA_MIN_BATTERY_PERCENT" in installation_safety
    assert "CONFIG_OTA_PERIODIC_CHECK_JITTER_SECONDS" in interval
    assert "SystemInfo::GetMacAddress()" in interval

    assert "std::make_unique<Ota>()" in periodic_check
    assert "periodic_ota->CheckVersion()" in periodic_check
    assert "periodic_ota->HasNewVersion()" in periodic_check
    assert "ota_ = std::move(periodic_ota);" in periodic_check
    assert "UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())" in periodic_check
    assert 'BootDiagnosticsMarkError("periodic_ota_check_failed", err)' in periodic_check
    assert '"Periodic OTA check failed: %s"' in periodic_check

    for forbidden in (
        "CheckNewVersion()",
        "StartActivationTask()",
        "InitializeProtocol()",
        "MarkCurrentVersionValid()",
    ):
        assert forbidden not in periodic_check


def test_periodic_check_is_single_flight_and_upgrade_failure_returns_to_idle() -> None:
    header = APPLICATION_HEADER.read_text(encoding="utf-8")
    source = APPLICATION.read_text(encoding="utf-8")

    assert "bool periodic_ota_check_scheduled_ = false;" in header

    scheduler = function_body(source, "void Application::MaybeSchedulePeriodicOtaCheck()")
    periodic_check = function_body(source, "void Application::RunPeriodicOtaCheck()")

    assert "periodic_ota_check_scheduled_" in scheduler
    assert "periodic_ota_check_scheduled_ = true;" in scheduler
    assert scheduler.index("periodic_ota_check_scheduled_ = true;") < scheduler.index("Schedule([this]()")
    assert "periodic_ota_check_scheduled_ = false;" in periodic_check
    assert periodic_check.index("periodic_ota_check_scheduled_ = false;") < periodic_check.index(
        "CanRunPeriodicOtaCheck(&defer_reason)"
    )

    assert periodic_check.index("ota_.reset();") < periodic_check.index("SetDeviceState(kDeviceStateIdle);")
    assert periodic_check.index("SetDeviceState(kDeviceStateIdle);") < periodic_check.index(
        'BootDiagnosticsMarkError("periodic_ota_upgrade_failed", ESP_FAIL);'
    )


def test_startup_update_is_deferred_when_the_power_policy_is_not_safe() -> None:
    header = APPLICATION_HEADER.read_text(encoding="utf-8")
    source = APPLICATION.read_text(encoding="utf-8")
    startup_check = function_body(source, "void Application::CheckNewVersion()")
    startup_safety = function_body(source, "bool Application::CanRunStartupOtaInstallation(")

    assert "bool CanRunStartupOtaInstallation(const char** defer_reason);" in header
    assert "CanRunStartupOtaInstallation(&defer_reason)" in startup_check
    assert "ota_upgrade_deferred" in startup_check
    assert startup_check.index("CanRunStartupOtaInstallation(&defer_reason)") < startup_check.index(
        "UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())"
    )
    for condition in (
        "!network_connected_",
        "CanRunOtaInstallation(defer_reason)",
        "tts_playback_session_.phase() != TtsPlaybackPhase::kIdle",
        "legacy_tts_active_",
        "!tts_ownership_gate_.CanAcquireOwnership()",
        "audio_open_request_pending_",
    ):
        assert condition in startup_safety
