from pathlib import Path


APPLICATION = Path("main/application.cc")
KCONFIG = Path("main/Kconfig.projbuild")
OTA_SOURCE = Path("main/ota.cc")
SDKCONFIG = Path("sdkconfig")
SDKCONFIG_DEFAULTS = Path("sdkconfig.defaults")
WEBSOCKET_PROTOCOL = Path("main/protocols/websocket_protocol.cc")


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


def test_production_kconfig_requires_https_and_bootstrap_http_is_explicit() -> None:
    kconfig = KCONFIG.read_text(encoding="utf-8")
    source = OTA_SOURCE.read_text(encoding="utf-8")

    for option in ("OTA_ALLOW_INSECURE_HTTP", "OTA_ALLOW_LEGACY_UNSIGNED_OFFERS"):
        block = kconfig[kconfig.index(f"config {option}") :]
        assert "default n" in block

    # The tracked private-IP bootstrap is intentionally a development profile;
    # it must never silently enable unsigned offers.
    for config in (SDKCONFIG.read_text(encoding="utf-8"), SDKCONFIG_DEFAULTS.read_text(encoding="utf-8")):
        assert "CONFIG_OTA_ALLOW_INSECURE_HTTP=y" in config
        assert "# CONFIG_OTA_ALLOW_LEGACY_UNSIGNED_OFFERS is not set" in config

    check_version = function_body(source, "esp_err_t Ota::CheckVersion()")
    upgrade = function_body(source, "bool Ota::UpgradeFirmwareOffer(")
    parse_offer = function_body(source, "bool ParseFirmwareOffer(")

    assert "IsAllowedOtaUrl(url, \"check\")" in check_version
    assert "IsAllowedOtaUrl(firmware_offer_.url, \"firmware\")" in check_version
    assert "IsAllowedOtaUrl(offer.url, \"firmware\")" in upgrade
    assert "#if !CONFIG_OTA_ALLOW_LEGACY_UNSIGNED_OFFERS" in upgrade
    assert "#if CONFIG_OTA_ALLOW_LEGACY_UNSIGNED_OFFERS" in parse_offer


def test_offer_version_is_strict_and_matches_the_downloaded_image_descriptor() -> None:
    source = OTA_SOURCE.read_text(encoding="utf-8")
    upgrade = function_body(source, "bool Ota::UpgradeFirmwareOffer(")

    assert "std::stoi" not in source
    assert "return parts->size() == 3;" in source
    assert "!IsSupportedFirmwareVersion(version->valuestring)" in source

    descriptor_check = upgrade.index("image_version != offer.version")
    ota_begin = upgrade.index("esp_ota_begin(update_partition")
    assert descriptor_check < ota_begin
    assert "Firmware descriptor version" in upgrade


def test_pending_ota_confirmation_uses_local_runtime_health_and_a_window() -> None:
    application = APPLICATION.read_text(encoding="utf-8")
    websocket = WEBSOCKET_PROTOCOL.read_text(encoding="utf-8")

    probe = function_body(application, "bool Application::ProbeOtaTransportHealth()")
    validation = function_body(application, "void Application::MaybeCompletePendingOtaValidation()")
    activation = function_body(application, "void Application::ActivationTask()")
    websocket_open = function_body(websocket, "bool WebsocketProtocol::OpenAudioChannel()")

    assert "protocol_->OpenAudioChannel()" in probe
    assert "opened && transport_connected && channel_open" in probe
    assert "protocol_->CloseAudioChannel(false)" in probe
    assert "ota_transport_probe_succeeded_ = healthy" in probe

    assert "ota_local_runtime_ready_.load()" in validation
    assert "ota_transport_probe_succeeded_.load()" not in validation
    assert "ota_transport_connected_.load()" not in validation
    assert "network_connected_" not in validation
    assert "OtaValidationAction::kRollback" in validation
    assert "MarkCurrentVersionInvalidAndReboot" in validation

    assert "if (ota_pending_verification_.load())" in activation
    assert "ProbeOtaTransportHealth()" in activation
    assert "if (!ota_check_succeeded_)" in activation
    assert "activation will continue without OTA confirmation" in activation

    assert websocket_open.index("on_connected_") < websocket_open.index("on_audio_channel_opened_")
