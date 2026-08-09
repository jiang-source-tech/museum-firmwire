from pathlib import Path


OTA_HEADER = Path("main/ota.h")
OTA_SOURCE = Path("main/ota.cc")


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


def test_extended_firmware_offer_has_a_single_integrity_contract() -> None:
    header = OTA_HEADER.read_text(encoding="utf-8")
    source = OTA_SOURCE.read_text(encoding="utf-8")

    assert "struct FirmwareOffer" in header
    for field in (
        "std::string release_id;",
        "std::string sha256;",
        "size_t size_bytes = 0;",
        "std::string model;",
        "std::string board_type;",
        "std::string partition_layout_id;",
    ):
        assert field in header

    check_version = function_body(source, "esp_err_t Ota::CheckVersion()")
    assert "ParseFirmwareOffer(firmware, &firmware_offer_)" in check_version
    for field in ("release_id", "sha256", "size_bytes", "model", "board_type", "partition_layout_id"):
        assert f'cJSON_GetObjectItem(firmware, "{field}")' in source


def test_partial_extended_offer_is_rejected_instead_of_downgrading_to_legacy() -> None:
    source = OTA_SOURCE.read_text(encoding="utf-8")
    parse_offer = function_body(source, "bool ParseFirmwareOffer(")

    assert "has_extended_fields" in parse_offer
    assert "parsed_offer.HasCompleteIntegrityMetadata()" in parse_offer
    assert "return false;" in parse_offer


def test_legacy_empty_url_means_no_update_but_not_a_malformed_extended_offer() -> None:
    source = OTA_SOURCE.read_text(encoding="utf-8")
    no_update = function_body(source, "bool IsLegacyNoUpdateResponse(")
    check_version = function_body(source, "esp_err_t Ota::CheckVersion()")

    assert "url->valuestring[0] != '\\0'" in no_update
    for field in (
        "schema_version",
        "release_id",
        "sha256",
        "size_bytes",
        "model",
        "board_type",
        "partition_layout_id",
    ):
        assert f'"{field}"' in no_update
    assert_ordered(
        check_version,
        "IsLegacyNoUpdateResponse(firmware)",
        "ParseFirmwareOffer(firmware, &firmware_offer_)",
        "IsAllowedOtaUrl(firmware_offer_.url, \"firmware\")",
    )


def test_verified_offer_hash_and_size_gate_boot_partition_selection() -> None:
    source = OTA_SOURCE.read_text(encoding="utf-8")
    header = OTA_HEADER.read_text(encoding="utf-8")

    assert '#include <mbedtls/sha256.h>' in source
    assert "bool Ota::UpgradeFirmwareOffer(" in source
    upgrade = function_body(source, "bool Ota::UpgradeFirmwareOffer(")

    assert "mbedtls_sha256_update" in upgrade
    assert "mbedtls_sha256_finish" in upgrade
    assert "offer.size_bytes" in upgrade
    assert "DigestMatchesExpectedSha256" in upgrade
    assert "esp_ota_abort(update_handle)" in upgrade
    assert_ordered(
        upgrade,
        "if (total_read != offer.size_bytes)",
        "if (!DigestMatchesExpectedSha256",
        "esp_ota_end(update_handle)",
        "esp_ota_set_boot_partition(update_partition)",
    )

    start_upgrade = function_body(source, "bool Ota::StartUpgrade(")
    assert "UpgradeFirmwareOffer(firmware_offer_, callback)" in start_upgrade
    assert "FirmwareOffer firmware_offer_;" in header


def test_url_only_upgrade_remains_the_explicit_legacy_compatibility_path() -> None:
    source = OTA_SOURCE.read_text(encoding="utf-8")
    upgrade = function_body(
        source,
        "bool Ota::Upgrade(const std::string& firmware_url, std::function<void(int progress, size_t speed)> callback)",
    )

    assert "FirmwareOffer legacy_offer" in upgrade
    assert "legacy_offer.url = firmware_url" in upgrade
    assert "UpgradeFirmwareOffer(legacy_offer, callback)" in upgrade
