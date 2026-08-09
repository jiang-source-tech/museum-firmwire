#include "ota_release_audit.h"

#include "ota_release_audit_model.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <nvs.h>

#include <utility>

namespace {
constexpr const char* TAG = "OtaReleaseAudit";
constexpr const char* kNamespace = "ota_audit";
constexpr const char* kReleaseIdKey = "release_id";
constexpr const char* kOutcomeKey = "outcome";
constexpr const char* kVersionKey = "version";
constexpr const char* kPartitionKey = "partition";
constexpr const char* kSha256Key = "sha256";

struct OtaReleaseAuditRecord {
    std::string release_id;
    std::string outcome;
    std::string version;
    std::string partition;
    std::string sha256;
};

std::string ReadString(nvs_handle_t handle, const char* key) {
    size_t length = 0;
    if (nvs_get_str(handle, key, nullptr, &length) != ESP_OK || length == 0) {
        return {};
    }

    std::string value(length, '\0');
    if (nvs_get_str(handle, key, value.data(), &length) != ESP_OK) {
        return {};
    }
    while (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
}

OtaReleaseAuditRecord ReadRecord() {
    nvs_handle_t handle = 0;
    const esp_err_t error = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "OTA release audit is unavailable for reading: %s", esp_err_to_name(error));
        return {};
    }

    const OtaReleaseAuditRecord record{
        ReadString(handle, kReleaseIdKey),
        ReadString(handle, kOutcomeKey),
        ReadString(handle, kVersionKey),
        ReadString(handle, kPartitionKey),
        ReadString(handle, kSha256Key),
    };
    nvs_close(handle);
    return record;
}

bool WriteRecord(const OtaReleaseAuditRecord& record) {
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "OTA release audit is unavailable for writing: %s", esp_err_to_name(error));
        return false;
    }

    const std::pair<const char*, const std::string&> values[] = {
        {kReleaseIdKey, record.release_id},
        {kOutcomeKey, record.outcome},
        {kVersionKey, record.version},
        {kPartitionKey, record.partition},
        {kSha256Key, record.sha256},
    };
    for (const auto& [key, value] : values) {
        error = nvs_set_str(handle, key, value.c_str());
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "Failed to persist OTA release audit: %s", esp_err_to_name(error));
            nvs_close(handle);
            return false;
        }
    }

    error = nvs_commit(handle);
    nvs_close(handle);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to commit OTA release audit: %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool IsRunningPartitionPendingVerification() {
    const esp_partition_t* partition = esp_ota_get_running_partition();
    if (partition == nullptr) {
        return false;
    }
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(partition, &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

std::string RunningPartitionLabel() {
    const esp_partition_t* partition = esp_ota_get_running_partition();
    return partition == nullptr ? "" : partition->label;
}

bool IsOtaSlotLabel(const std::string& label) {
    return label == "ota_0" || label == "ota_1";
}

bool IsSha256Hex(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    for (unsigned char character : value) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f') ||
              (character >= 'A' && character <= 'F'))) {
            return false;
        }
    }
    return true;
}
}  // namespace

void OtaReleaseAuditStageRelease(const std::string& release_id,
                                 const std::string& version,
                                 const std::string& sha256) {
    if (release_id.empty() || version.empty() || sha256.empty()) {
        return;
    }
    WriteRecord(OtaReleaseAuditRecord{
        release_id,
        OtaReleaseOutcomeName(OtaReleaseOutcome::kFailed),
        version,
        "",
        sha256,
    });
}

void OtaReleaseAuditRecordOutcome(const char* outcome) {
    if (outcome == nullptr || *outcome == '\0') {
        return;
    }
    OtaReleaseAuditRecord record = ReadRecord();
    if (record.release_id.empty()) {
        return;
    }
    record.outcome = outcome;
    record.partition = RunningPartitionLabel();
    WriteRecord(record);
}

void OtaReleaseAuditReconcileRunningImage() {
    OtaReleaseAuditRecord record = ReadRecord();
    if (record.release_id.empty() ||
        ParseOtaReleaseOutcome(record.outcome) != OtaReleaseOutcome::kPending) {
        return;
    }

    const esp_app_desc_t* app_desc = esp_app_get_description();
    const std::string running_version = app_desc == nullptr ? "" : app_desc->version;
    record.outcome = OtaReleaseOutcomeName(InferPendingOtaReleaseOutcome(
        record.version, running_version, IsRunningPartitionPendingVerification()));
    record.partition = RunningPartitionLabel();
    WriteRecord(record);
}

std::string OtaReleaseAuditBuildReportJson() {
    const OtaReleaseAuditRecord record = ReadRecord();
    const OtaReleaseOutcome outcome = ParseOtaReleaseOutcome(record.outcome);
    if (record.release_id.empty() || outcome == OtaReleaseOutcome::kNone ||
        !IsSha256Hex(record.sha256)) {
        return {};
    }

    const esp_app_desc_t* app_desc = esp_app_get_description();
    const std::string running_version = app_desc == nullptr ? "" : app_desc->version;
    const std::string running_partition = RunningPartitionLabel();
    if (!IsOtaSlotLabel(running_partition)) {
        return {};
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "release_id", record.release_id.c_str());
    cJSON_AddStringToObject(root, "outcome", OtaReleaseOutcomeName(outcome));
    cJSON_AddStringToObject(root, "running_version", running_version.c_str());
    cJSON_AddStringToObject(root, "running_partition", running_partition.c_str());
    cJSON_AddStringToObject(root, "sha256", record.sha256.c_str());
    char* printed = cJSON_PrintUnformatted(root);
    std::string json = printed == nullptr ? "" : printed;
    if (printed != nullptr) {
        cJSON_free(printed);
    }
    cJSON_Delete(root);
    return json;
}
