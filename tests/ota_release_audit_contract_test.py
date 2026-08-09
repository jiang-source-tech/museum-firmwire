from pathlib import Path


APPLICATION = Path("main/application.cc")
AUDIT = Path("main/ota_release_audit.cc")
BOARD = Path("main/boards/common/board.cc")
CMAKE = Path("main/CMakeLists.txt")
HEADER = Path("main/ota.h")
OTA = Path("main/ota.cc")


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


def test_verified_offer_requires_release_id_and_persists_audit_identity() -> None:
    header = HEADER.read_text(encoding="utf-8")
    ota = OTA.read_text(encoding="utf-8")
    audit = AUDIT.read_text(encoding="utf-8")

    assert "std::string release_id;" in header
    assert "!release_id.empty()" in header
    assert 'cJSON_GetObjectItem(firmware, "release_id")' in ota
    assert "IsSafeReleaseId(release_id->valuestring)" in ota
    assert 'constexpr const char* kNamespace = "ota_audit";' in audit
    for key in ("release_id", "outcome", "version", "partition", "sha256"):
        assert key in audit


def test_existing_check_post_contains_only_optional_report_without_a_new_endpoint() -> None:
    board = BOARD.read_text(encoding="utf-8")
    audit = AUDIT.read_text(encoding="utf-8")
    cmake = CMAKE.read_text(encoding="utf-8")
    system_info = function_body(board, "std::string Board::GetSystemInfoJson()")

    assert '"ota_release_audit.cc"' in cmake
    report_call = system_info.index("OtaReleaseAuditBuildReportJson()")
    runtime_health = system_info.rindex('"runtime_health"')
    assert report_call < runtime_health
    assert '"ota_report":' in system_info
    assert "CreateHttp" not in audit
    assert '.Open("' not in audit
    build_report = function_body(audit, "std::string OtaReleaseAuditBuildReportJson()")
    for field in ("release_id", "outcome", "running_version", "running_partition", "sha256"):
        assert f'cJSON_AddStringToObject(root, "{field}"' in build_report
    assert build_report.count("cJSON_AddStringToObject(root,") == 5
    assert "IsOtaSlotLabel(running_partition)" in build_report
    assert "IsSha256Hex(record.sha256)" in build_report


def test_release_outcomes_cover_staging_health_confirmation_and_rollback() -> None:
    ota = OTA.read_text(encoding="utf-8")
    application = APPLICATION.read_text(encoding="utf-8")
    upgrade = function_body(ota, "bool Ota::UpgradeFirmwareOffer(")
    mark_valid = function_body(ota, "bool Ota::MarkCurrentVersionValid()")
    mark_invalid = function_body(ota, "bool Ota::MarkCurrentVersionInvalidAndReboot()")
    initialize = function_body(application, "void Application::Initialize()")

    assert "OtaReleaseAttempt release_attempt(offer);" in upgrade
    assert upgrade.index("release_attempt.MarkImageVerified()") < upgrade.index(
        "esp_ota_set_boot_partition(update_partition)"
    )
    assert upgrade.index("esp_ota_set_boot_partition(update_partition)") < upgrade.index(
        "release_attempt.MarkBootPartitionSelected()"
    )
    assert 'OtaReleaseAuditRecordOutcome("committed")' in mark_valid
    assert 'OtaReleaseAuditRecordOutcome("rolled_back")' in mark_invalid
    assert initialize.index("OtaReleaseAuditReconcileRunningImage()") < initialize.index(
        "Ota::IsRunningPartitionPendingVerification()"
    )


def test_pending_boot_inference_is_explicit_and_version_bound() -> None:
    audit = AUDIT.read_text(encoding="utf-8")
    reconcile = function_body(audit, "void OtaReleaseAuditReconcileRunningImage()")

    assert "ParseOtaReleaseOutcome(record.outcome) != OtaReleaseOutcome::kPending" in reconcile
    assert "InferPendingOtaReleaseOutcome(" in reconcile
    assert "record.version, running_version" in reconcile
    assert "IsRunningPartitionPendingVerification()" in reconcile


def test_audit_persistence_is_best_effort_and_cannot_abort_an_ota() -> None:
    audit = AUDIT.read_text(encoding="utf-8")
    write_record = function_body(audit, "bool WriteRecord(")

    assert '#include "settings.h"' not in audit
    assert "ESP_ERROR_CHECK" not in audit
    assert "nvs_open(kNamespace, NVS_READWRITE, &handle)" in write_record
    assert "nvs_set_str(handle, key, value.c_str())" in write_record
    assert "nvs_commit(handle)" in write_record
    assert "return false;" in write_record
