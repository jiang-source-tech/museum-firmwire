from pathlib import Path


APPLICATION = Path("main/application.cc")
APPLICATION_HEADER = Path("main/application.h")
PENDING_VALIDATION_POLICY = Path("main/ota_pending_validation_policy.h")


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


def assert_ordered(source: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        index = source.find(needle, cursor)
        assert index != -1, f"missing ordered source fragment: {needle}"
        cursor = index + len(needle)


def test_discovered_update_preserves_the_checked_firmware_offer() -> None:
    source = APPLICATION.read_text(encoding="utf-8")
    check_version = function_body(source, "void Application::CheckNewVersion()")
    upgrade = function_body(source, "bool Application::UpgradeFirmware(")

    assert "UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())" in check_version
    assert "ota_->StartUpgrade(" in upgrade
    assert "Ota::Upgrade(" not in upgrade


def test_pending_ota_confirmation_uses_local_runtime_health_not_backend_reachability() -> None:
    source = APPLICATION.read_text(encoding="utf-8")
    header = APPLICATION_HEADER.read_text(encoding="utf-8")
    policy = PENDING_VALIDATION_POLICY.read_text(encoding="utf-8")
    initialize = function_body(source, "void Application::Initialize()")
    activation = function_body(source, "void Application::ActivationTask()")
    completion = function_body(source, "void Application::HandleActivationDoneEvent()")
    check_version = function_body(source, "void Application::CheckNewVersion()")
    validation = function_body(source, "void Application::MaybeCompletePendingOtaValidation()")
    policy_evaluate = function_body(policy, "OtaValidationAction Evaluate(")

    assert "std::atomic_bool ota_check_succeeded_{false};" in header
    assert "std::atomic_bool ota_local_runtime_ready_{false};" in header
    assert "std::atomic_bool ota_pending_verification_{false};" in header
    assert "std::atomic_bool ota_transport_probe_succeeded_{false};" in header
    assert "OtaPendingValidationPolicy ota_pending_validation_policy_;" in header
    assert "ota_->MarkCurrentVersionValid()" not in check_version

    assert_ordered(
        initialize,
        "audio_service_.Initialize(codec);",
        "audio_service_.Start();",
        "esp_timer_start_periodic(clock_timer_handle_, 1000000);",
        "ota_local_runtime_ready_ = true;",
    )

    ota_failure = activation.index("if (!ota_check_succeeded_)")
    doorbell_config = activation.index("const auto& doorbell_config")
    ota_failure_block = activation[ota_failure:doorbell_config]
    assert "return;" not in ota_failure_block
    assert "continuing without a successful OTA check" in ota_failure_block

    assert_ordered(
        activation,
        "ota_check_succeeded_ = false;",
        "CheckNewVersion();",
        "!ota_check_succeeded_",
        "doorbell_configuration_ready",
        "const bool protocol_started = InitializeProtocol();",
        "if (!protocol_started)",
        "ProbeOtaTransportHealth()",
        "xEventGroupSetBits(event_group_, MAIN_EVENT_ACTIVATION_DONE);",
    )
    assert "ota_local_runtime_ready_ = false;" not in activation
    assert "ota_validation_ready_" not in activation

    assert "MarkCurrentVersionValid" not in completion
    assert "ota_pending_verification_.load()" in completion
    assert "ota_.reset();" in completion

    assert "ota_pending_validation_policy_.Evaluate" in validation
    assert "Ota::MarkCurrentVersionValid()" in validation
    assert "Ota::MarkCurrentVersionInvalidAndReboot()" in validation
    assert "OtaPendingValidationSignals" not in validation
    for backend_signal in (
        "ota_check_succeeded_",
        "ota_transport_probe_succeeded_",
        "network_connected_",
    ):
        assert backend_signal not in validation
    assert "return gate_.Evaluate(now_seconds, local_runtime_ready);" in policy_evaluate


def test_startup_ota_check_is_bounded_and_defers_retries_to_the_periodic_path() -> None:
    source = APPLICATION.read_text(encoding="utf-8")
    check_version = function_body(source, "void Application::CheckNewVersion()")

    assert "constexpr int kMaxStartupOtaCheckAttempts = 2;" in check_version
    assert "constexpr int kStartupOtaRetryDelaySeconds = 5;" in check_version
    assert "retry_count >= kMaxStartupOtaCheckAttempts" in check_version
    assert "will retry periodically" in check_version
    assert "retry_delay *= 2" not in check_version
    assert "MAX_RETRY = 10" not in check_version


def test_upgrade_failure_records_a_diagnostic_and_resumes_activation() -> None:
    source = APPLICATION.read_text(encoding="utf-8")
    upgrade = function_body(source, "bool Application::UpgradeFirmware(")

    assert_ordered(
        upgrade,
        "if (!upgrade_success)",
        "audio_service_.Start()",
        "board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER)",
        "SetDeviceState(kDeviceStateActivating);",
        'BootDiagnosticsMarkError("ota_upgrade_failed", ESP_FAIL);',
    )
    assert "current firmware will continue running" in upgrade
