from pathlib import Path


OTA_SOURCE = Path("main/ota.cc")
CMAKE_SOURCE = Path("main/CMakeLists.txt")


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


def test_ota_check_reports_the_compiled_device_identity() -> None:
    source = OTA_SOURCE.read_text(encoding="utf-8")
    cmake = CMAKE_SOURCE.read_text(encoding="utf-8")
    setup_http = function_body(source, "std::unique_ptr<Http> Ota::SetupHttp()")

    assert 'constexpr const char* kOtaPartitionLayoutId = "xiaoxin-ota-16m-v1";' in source
    assert 'http->SetHeader("Device-Model", BOARD_NAME);' in setup_http
    assert 'http->SetHeader("Board-Type", BOARD_TYPE);' in setup_http
    assert 'http->SetHeader("Partition-Layout-Id", kOtaPartitionLayoutId);' in setup_http
    assert 'http->SetHeader("Firmware-Channel", CONFIG_OTA_RELEASE_CHANNEL);' in setup_http

    assert 'set(BOARD_TYPE "esp32-s3-touch-lcd-1.46")' in cmake
    assert "set(BOARD_NAME ${BOARD_TYPE})" in cmake


def test_verified_offer_must_match_the_current_firmware_target() -> None:
    source = OTA_SOURCE.read_text(encoding="utf-8")
    header = Path("main/ota.h").read_text(encoding="utf-8")
    compatibility = function_body(source, "bool IsFirmwareOfferCompatible(")
    check_version = function_body(source, "esp_err_t Ota::CheckVersion()")

    assert "struct FirmwareOfferTarget" in header
    assert "bool MatchesTarget(const FirmwareOfferTarget& target) const" in header
    assert "!has_extended_fields" in header
    assert "model == target.model" in header
    assert "board_type == target.board_type" in header
    assert "partition_layout_id == target.partition_layout_id" in header
    assert "FirmwareOfferTarget target{BOARD_NAME, BOARD_TYPE, kOtaPartitionLayoutId}" in compatibility
    assert "return offer.MatchesTarget(target);" in compatibility

    assert_ordered(
        check_version,
        "ParseFirmwareOffer(firmware, &firmware_offer_)",
        "IsFirmwareOfferCompatible(firmware_offer_)",
        "IsNewVersionAvailable(current_version_, firmware_version_)",
    )


def test_wrong_model_board_or_layout_is_rejected_before_force_can_offer_an_update() -> None:
    source = OTA_SOURCE.read_text(encoding="utf-8")
    check_version = function_body(source, "esp_err_t Ota::CheckVersion()")

    rejection = check_version.index("!IsFirmwareOfferCompatible(firmware_offer_)")
    force = check_version.index('cJSON_GetObjectItem(firmware, "force")')
    assert rejection < force

    rejection_block = check_version[rejection:force]
    assert "has_new_version_ = false;" in rejection_block
    assert "ESP_ERR_INVALID_RESPONSE" in rejection_block
    assert "firmware_offer_ = FirmwareOffer{};" in rejection_block


def test_verified_offer_force_flag_cannot_override_downgrade_protection() -> None:
    source = OTA_SOURCE.read_text(encoding="utf-8")
    check_version = function_body(source, "esp_err_t Ota::CheckVersion()")

    force = check_version.index('cJSON_GetObjectItem(firmware, "force")')
    force_block = check_version[force:]
    assert "!firmware_offer_.has_extended_fields" in force_block
    assert "Ignoring force flag for verified firmware offer" in force_block
