#include "ota.h"
#include "system_info.h"
#include "settings.h"
#include "ota_release_audit.h"
#include "assets/lang_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_system.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#ifdef SOC_HMAC_SUPPORTED
#include <esp_hmac.h>
#endif

#include <cmath>
#include <cctype>
#include <cstring>
#include <limits>
#include <vector>
#include <sstream>
#include <algorithm>
#include <utility>

#define TAG "Ota"

namespace {
constexpr const char* kOtaPartitionLayoutId = "xiaoxin-ota-16m-v1";

constexpr const char* kLegacyOtaUrls[] = {
    "http://124.222.121.103:8003/xiaozhi/ota/",
    "http://121.43.33.0:8003/xiaozhi/ota/",
    "http://121.43.33.0:8003/xiaoxin/ota/",
    "https://api.tenclass.net/xiaozhi/ota/",
};

bool IsLegacyOtaUrl(const std::string& url) {
    if (url.find("/xiaozhi/ota") != std::string::npos) {
        return true;
    }
    for (const char* legacy_url : kLegacyOtaUrls) {
        if (url == legacy_url) {
            return true;
        }
    }
    return false;
}

bool HasUrlSchemeAndAuthority(const std::string& url, const char* scheme) {
    const size_t scheme_length = strlen(scheme);
    if (url.size() <= scheme_length + 3 ||
        url[scheme_length] != ':' || url[scheme_length + 1] != '/' ||
        url[scheme_length + 2] != '/') {
        return false;
    }
    for (size_t index = 0; index < scheme_length; ++index) {
        if (std::tolower(static_cast<unsigned char>(url[index])) != scheme[index]) {
            return false;
        }
    }

    const size_t authority_start = scheme_length + 3;
    const size_t authority_end = url.find_first_of("/?#", authority_start);
    const size_t authority_length = authority_end == std::string::npos
                                        ? url.size() - authority_start
                                        : authority_end - authority_start;
    if (authority_length == 0) {
        return false;
    }
    for (size_t index = authority_start; index < authority_start + authority_length; ++index) {
        const unsigned char character = static_cast<unsigned char>(url[index]);
        if (std::isspace(character) || url[index] == '@') {
            return false;
        }
    }
    return true;
}

bool IsAllowedOtaUrl(const std::string& url, const char* purpose) {
    if (HasUrlSchemeAndAuthority(url, "https")) {
        return true;
    }
#if CONFIG_OTA_ALLOW_INSECURE_HTTP
    if (HasUrlSchemeAndAuthority(url, "http")) {
        ESP_LOGW(TAG, "Using development-only insecure HTTP for %s", purpose);
        return true;
    }
#endif
    ESP_LOGE(TAG, "Refusing non-HTTPS or malformed OTA %s URL", purpose);
    return false;
}

bool ParseNumericVersion(const std::string& version, std::vector<int>* parts) {
    if (parts == nullptr || version.empty()) {
        return false;
    }

    parts->clear();
    size_t component_start = 0;
    while (component_start < version.size()) {
        const size_t component_end = version.find('.', component_start);
        const size_t end = component_end == std::string::npos ? version.size() : component_end;
        if (end == component_start) {
            return false;
        }

        int value = 0;
        for (size_t index = component_start; index < end; ++index) {
            const char character = version[index];
            if (character < '0' || character > '9') {
                return false;
            }
            const int digit = character - '0';
            if (value > (std::numeric_limits<int>::max() - digit) / 10) {
                return false;
            }
            value = value * 10 + digit;
        }
        parts->push_back(value);

        if (component_end == std::string::npos) {
            return parts->size() == 3;
        }
        component_start = component_end + 1;
    }
    return false;
}

bool IsSupportedFirmwareVersion(const std::string& version) {
    std::vector<int> parts;
    return ParseNumericVersion(version, &parts);
}

bool IsSafeReleaseId(const char* value) {
    if (value == nullptr || *value == '\0') {
        return false;
    }
    size_t length = 0;
    for (const char* cursor = value; *cursor != '\0'; ++cursor) {
        ++length;
        if (length > 96) {
            return false;
        }
        const unsigned char character = static_cast<unsigned char>(*cursor);
        if (!(std::isalnum(character) || *cursor == '-' || *cursor == '_' ||
              *cursor == '.' || *cursor == ':')) {
            return false;
        }
    }
    return true;
}

int HexNibble(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool IsSha256Hex(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char character) {
        return HexNibble(character) >= 0;
    });
}

bool ParseSizeBytes(const cJSON* value, size_t* size_bytes) {
    if (!cJSON_IsNumber(value) || size_bytes == nullptr) {
        return false;
    }

    const double raw_size = value->valuedouble;
    if (!std::isfinite(raw_size) || raw_size <= 0 ||
        raw_size > static_cast<double>(std::numeric_limits<size_t>::max()) ||
        std::floor(raw_size) != raw_size) {
        return false;
    }

    *size_bytes = static_cast<size_t>(raw_size);
    return true;
}

// The legacy OTA contract represents "no update" as the device's current
// version with an empty URL.  It is not an installable firmware offer and must
// remain a successful check even in production, where URL-only *updates* are
// deliberately rejected.  Do not accept an empty URL if any extended release
// metadata is present: that would hide a malformed modern offer as no update.
bool IsLegacyNoUpdateResponse(const cJSON* firmware) {
    if (!cJSON_IsObject(firmware)) {
        return false;
    }

    const cJSON* version = cJSON_GetObjectItem(firmware, "version");
    const cJSON* url = cJSON_GetObjectItem(firmware, "url");
    if (!cJSON_IsString(version) || version->valuestring == nullptr ||
        !IsSupportedFirmwareVersion(version->valuestring) ||
        !cJSON_IsString(url) || url->valuestring == nullptr ||
        url->valuestring[0] != '\0') {
        return false;
    }

    constexpr const char* kExtendedFields[] = {
        "schema_version", "release_id", "sha256", "size_bytes", "model",
        "board_type", "partition_layout_id", "channel", "mandatory",
        "min_current_version",
    };
    for (const char* field : kExtendedFields) {
        if (cJSON_GetObjectItem(firmware, field) != nullptr) {
            return false;
        }
    }
    return true;
}

bool ParseFirmwareOffer(const cJSON* firmware, FirmwareOffer* offer) {
    if (!cJSON_IsObject(firmware) || offer == nullptr) {
        return false;
    }

    const cJSON* version = cJSON_GetObjectItem(firmware, "version");
    const cJSON* url = cJSON_GetObjectItem(firmware, "url");
    if (!cJSON_IsString(version) || !cJSON_IsString(url) ||
        version->valuestring == nullptr || url->valuestring == nullptr ||
        !IsSupportedFirmwareVersion(version->valuestring)) {
        return false;
    }

    FirmwareOffer parsed_offer;
    parsed_offer.version = version->valuestring;
    parsed_offer.url = url->valuestring;

    const cJSON* release_id = cJSON_GetObjectItem(firmware, "release_id");
    const cJSON* sha256 = cJSON_GetObjectItem(firmware, "sha256");
    const cJSON* size_bytes = cJSON_GetObjectItem(firmware, "size_bytes");
    const cJSON* model = cJSON_GetObjectItem(firmware, "model");
    const cJSON* board_type = cJSON_GetObjectItem(firmware, "board_type");
    const cJSON* partition_layout_id = cJSON_GetObjectItem(firmware, "partition_layout_id");
    const bool has_extended_fields = release_id != nullptr || sha256 != nullptr || size_bytes != nullptr || model != nullptr ||
                                      board_type != nullptr || partition_layout_id != nullptr;
    parsed_offer.has_extended_fields = has_extended_fields;
    if (!has_extended_fields) {
#if CONFIG_OTA_ALLOW_LEGACY_UNSIGNED_OFFERS
        *offer = std::move(parsed_offer);
        return true;
#else
        ESP_LOGE(TAG, "Refusing unsigned legacy firmware offer in production");
        return false;
#endif
    }

    if (!cJSON_IsString(release_id) || !IsSafeReleaseId(release_id->valuestring) ||
        !cJSON_IsString(sha256) || sha256->valuestring == nullptr ||
        !IsSha256Hex(sha256->valuestring) ||
        !ParseSizeBytes(size_bytes, &parsed_offer.size_bytes) ||
        !cJSON_IsString(model) || model->valuestring == nullptr ||
        !cJSON_IsString(board_type) || board_type->valuestring == nullptr ||
        !cJSON_IsString(partition_layout_id) || partition_layout_id->valuestring == nullptr) {
        return false;
    }

    parsed_offer.release_id = release_id->valuestring;
    parsed_offer.sha256 = sha256->valuestring;
    parsed_offer.model = model->valuestring;
    parsed_offer.board_type = board_type->valuestring;
    parsed_offer.partition_layout_id = partition_layout_id->valuestring;
    if (!parsed_offer.HasCompleteIntegrityMetadata()) {
        return false;
    }

    *offer = std::move(parsed_offer);
    return true;
}

bool IsFirmwareOfferCompatible(const FirmwareOffer& offer) {
    const FirmwareOfferTarget target{BOARD_NAME, BOARD_TYPE, kOtaPartitionLayoutId};
    return offer.MatchesTarget(target);
}

bool DigestMatchesExpectedSha256(const unsigned char digest[32], const std::string& expected_hex) {
    if (digest == nullptr || !IsSha256Hex(expected_hex)) {
        return false;
    }

    unsigned char difference = 0;
    for (size_t index = 0; index < 32; ++index) {
        const int high = HexNibble(expected_hex[index * 2]);
        const int low = HexNibble(expected_hex[index * 2 + 1]);
        const unsigned char expected = static_cast<unsigned char>((high << 4) | low);
        difference |= static_cast<unsigned char>(digest[index] ^ expected);
    }
    return difference == 0;
}

class OtaReleaseAttempt {
public:
    explicit OtaReleaseAttempt(const FirmwareOffer& offer) {
        if (!offer.has_extended_fields || offer.release_id.empty()) {
            return;
        }
        active_ = true;
        OtaReleaseAuditStageRelease(offer.release_id, offer.version, offer.sha256);
    }

    ~OtaReleaseAttempt() {
        if (active_ && !boot_partition_selected_) {
            OtaReleaseAuditRecordOutcome("failed");
        }
    }

    void MarkImageVerified() {
        if (active_) {
            OtaReleaseAuditRecordOutcome("pending");
        }
    }

    void MarkBootPartitionSelected() {
        if (active_) {
            boot_partition_selected_ = true;
            OtaReleaseAuditRecordOutcome("pending");
        }
    }

private:
    bool active_ = false;
    bool boot_partition_selected_ = false;
};
}

Ota::Ota() {
#ifdef ESP_EFUSE_BLOCK_USR_DATA
    // Read Serial Number from efuse user_data
    uint8_t serial_number[33] = {0};
    if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, serial_number, 32 * 8) == ESP_OK) {
        if (serial_number[0] == 0) {
            has_serial_number_ = false;
        } else {
            serial_number_ = std::string(reinterpret_cast<char*>(serial_number), 32);
            has_serial_number_ = true;
        }
    }
#endif
}

Ota::~Ota() {
}

std::string Ota::GetCheckVersionUrl() {
    Settings settings("wifi", true);
    std::string url = settings.GetString("ota_url");
    if (IsLegacyOtaUrl(url)) {
        ESP_LOGW(TAG, "Ignoring legacy OTA URL override: %s", url.c_str());
        settings.EraseKey("ota_url");
        url.clear();
    }
    if (url.empty()) {
        url = CONFIG_OTA_URL;
    }
    return url;
}

std::unique_ptr<Http> Ota::SetupHttp() {
    auto& board = Board::GetInstance();
    auto network = board.GetNetwork();
    auto http = network->CreateHttp(0);
    auto user_agent = SystemInfo::GetUserAgent();
    http->SetHeader("Activation-Version", has_serial_number_ ? "2" : "1");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", board.GetUuid());
    if (has_serial_number_) {
        http->SetHeader("Serial-Number", serial_number_.c_str());
        ESP_LOGI(TAG, "Setup HTTP, User-Agent: %s, Serial-Number: %s", user_agent.c_str(), serial_number_.c_str());
    }
    http->SetHeader("User-Agent", user_agent);
    http->SetHeader("Accept-Language", Lang::CODE);
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Device-Model", BOARD_NAME);
    http->SetHeader("Board-Type", BOARD_TYPE);
    http->SetHeader("Partition-Layout-Id", kOtaPartitionLayoutId);
    http->SetHeader("Firmware-Channel", CONFIG_OTA_RELEASE_CHANNEL);

    return http;
}

/* 
 * Specification: https://ccnphfhqs21z.feishu.cn/wiki/FjW6wZmisimNBBkov6OcmfvknVd
 */
esp_err_t Ota::CheckVersion() {
    auto& board = Board::GetInstance();
    auto app_desc = esp_app_get_description();

    // Check if there is a new firmware version available
    current_version_ = app_desc->version;
    ESP_LOGI(TAG, "Current version: %s", current_version_.c_str());

    std::string url = GetCheckVersionUrl();
    if (url.length() < 10) {
        ESP_LOGE(TAG, "Check version URL is not properly set");
        return ESP_ERR_INVALID_ARG;
    }
    if (!IsAllowedOtaUrl(url, "check")) {
        return ESP_ERR_INVALID_ARG;
    }

    auto http = SetupHttp();

    std::string data = board.GetSystemInfoJson();
    std::string method = data.length() > 0 ? "POST" : "GET";
    http->SetContent(std::move(data));

    if (!http->Open(method, url)) {
        int last_error = http->GetLastError();
        ESP_LOGE(TAG, "Failed to open HTTP connection, code=0x%x", last_error);
        return last_error;
    }

    auto status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Failed to check version, status code: %d", status_code);
        return status_code;
    }

    data = http->ReadAll();
    http->Close();

    // Response: { "firmware": { "version": "1.0.0", "url": "http://" } }
    // Parse the JSON response and check if the version is newer
    // If it is, set has_new_version_ to true and store the new version and URL
    
    cJSON *root = cJSON_Parse(data.c_str());
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return ESP_ERR_INVALID_RESPONSE;
    }

    has_activation_code_ = false;
    has_activation_challenge_ = false;
    cJSON *activation = cJSON_GetObjectItem(root, "activation");
    if (cJSON_IsObject(activation)) {
        cJSON* message = cJSON_GetObjectItem(activation, "message");
        if (cJSON_IsString(message)) {
            activation_message_ = message->valuestring;
        }
        cJSON* code = cJSON_GetObjectItem(activation, "code");
        if (cJSON_IsString(code)) {
            activation_code_ = code->valuestring;
            has_activation_code_ = true;
        }
        cJSON* challenge = cJSON_GetObjectItem(activation, "challenge");
        if (cJSON_IsString(challenge)) {
            activation_challenge_ = challenge->valuestring;
            has_activation_challenge_ = true;
        }
        cJSON* timeout_ms = cJSON_GetObjectItem(activation, "timeout_ms");
        if (cJSON_IsNumber(timeout_ms)) {
            activation_timeout_ms_ = timeout_ms->valueint;
        }
    }

    cJSON *doorbell_mqtt = cJSON_GetObjectItem(root, "doorbell_mqtt");
    if (cJSON_IsObject(doorbell_mqtt)) {
        DoorbellMqttConfig config = ParseDoorbellMqttConfig(doorbell_mqtt);
        if (config.version != 1) {
            doorbell_mqtt_config_ = LoadDoorbellMqttConfig();
        } else if (!config.enabled) {
            DisableDoorbellMqttConfig();
            doorbell_mqtt_config_ = DoorbellMqttConfig{};
        } else {
            if (SaveDoorbellMqttConfig(config)) {
                doorbell_mqtt_config_ = std::move(config);
            } else {
                doorbell_mqtt_config_ = LoadDoorbellMqttConfig();
            }
        }
    } else {
        doorbell_mqtt_config_ = LoadDoorbellMqttConfig();
    }

    has_mqtt_config_ = false;
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (cJSON_IsObject(mqtt)) {
        Settings settings("mqtt", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, mqtt) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
            }
        }
        has_mqtt_config_ = true;
    } else {
        ESP_LOGI(TAG, "No mqtt section found !");
    }

    has_websocket_config_ = false;
    cJSON *websocket = cJSON_GetObjectItem(root, "websocket");
    if (cJSON_IsObject(websocket)) {
        Settings settings("websocket", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, websocket) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
            }
        }
        has_websocket_config_ = true;
    } else {
        ESP_LOGI(TAG, "No websocket section found!");
    }

    has_server_time_ = false;
    cJSON *server_time = cJSON_GetObjectItem(root, "server_time");
    if (cJSON_IsObject(server_time)) {
        cJSON *timestamp = cJSON_GetObjectItem(server_time, "timestamp");
        
        if (cJSON_IsNumber(timestamp)) {
            struct timeval tv;
            double ts = timestamp->valuedouble;

            tv.tv_sec = (time_t)(ts / 1000);
            tv.tv_usec = (suseconds_t)((long long)ts % 1000) * 1000;
            settimeofday(&tv, NULL);
            has_server_time_ = true;
        }
    } else {
        ESP_LOGW(TAG, "No server_time section found!");
    }

    has_new_version_ = false;
    firmware_offer_ = FirmwareOffer{};
    firmware_version_.clear();
    firmware_url_.clear();
    cJSON *firmware = cJSON_GetObjectItem(root, "firmware");
    if (cJSON_IsObject(firmware)) {
        if (IsLegacyNoUpdateResponse(firmware)) {
            cJSON* version = cJSON_GetObjectItem(firmware, "version");
            firmware_version_ = version->valuestring;
            ESP_LOGI(TAG, "Current is the latest version");
        } else if (!ParseFirmwareOffer(firmware, &firmware_offer_)) {
            ESP_LOGE(TAG, "Invalid firmware offer: extended metadata must be complete and valid");
            cJSON_Delete(root);
            return ESP_ERR_INVALID_RESPONSE;
        } else if (!IsAllowedOtaUrl(firmware_offer_.url, "firmware")) {
            ESP_LOGE(TAG, "Rejecting firmware offer with an insecure URL");
            cJSON_Delete(root);
            return ESP_ERR_INVALID_RESPONSE;
        } else if (!IsFirmwareOfferCompatible(firmware_offer_)) {
            ESP_LOGW(TAG, "Rejecting incompatible firmware offer for this compiled target");
            has_new_version_ = false;
            firmware_offer_ = FirmwareOffer{};
            firmware_version_.clear();
            firmware_url_.clear();
            cJSON_Delete(root);
            return ESP_ERR_INVALID_RESPONSE;
        } else {
            firmware_version_ = firmware_offer_.version;
            firmware_url_ = firmware_offer_.url;
            // Check if the version is newer, for example, 0.1.0 is newer than 0.0.1
            has_new_version_ = IsNewVersionAvailable(current_version_, firmware_version_);
            if (has_new_version_) {
                ESP_LOGI(TAG, "New version available: %s%s", firmware_version_.c_str(),
                         firmware_offer_.has_extended_fields ? " (verified offer)" : " (legacy offer)");
            } else {
                ESP_LOGI(TAG, "Current is the latest version");
            }
            // A verified release cannot force a downgrade. The legacy force
            // escape hatch remains available only in explicitly enabled
            // compatibility deployments.
            cJSON *force = cJSON_GetObjectItem(firmware, "force");
            if (cJSON_IsNumber(force) && force->valueint == 1 &&
                !firmware_offer_.has_extended_fields) {
                has_new_version_ = true;
            } else if (cJSON_IsNumber(force) && force->valueint == 1) {
                ESP_LOGW(TAG, "Ignoring force flag for verified firmware offer");
            }
        }
    } else {
        ESP_LOGW(TAG, "No firmware section found!");
    }

    cJSON_Delete(root);
    return ESP_OK;
}

bool Ota::IsRunningPartitionPendingVerification() {
    auto partition = esp_ota_get_running_partition();
    if (partition == nullptr) {
        ESP_LOGE(TAG, "Failed to get running OTA partition");
        return false;
    }

    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get state of running partition");
        return false;
    }
    return state == ESP_OTA_IMG_PENDING_VERIFY;
}

bool Ota::MarkCurrentVersionValid() {
    auto partition = esp_ota_get_running_partition();
    if (partition == nullptr) {
        ESP_LOGE(TAG, "Failed to get running OTA partition");
        return false;
    }

    ESP_LOGI(TAG, "Running partition: %s", partition->label);
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get state of partition");
        return false;
    }

    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Marking firmware as valid");
        const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to mark firmware valid: %s", esp_err_to_name(err));
            return false;
        }
        OtaReleaseAuditRecordOutcome("committed");
    }
    return true;
}

bool Ota::MarkCurrentVersionInvalidAndReboot() {
    auto partition = esp_ota_get_running_partition();
    if (partition == nullptr) {
        ESP_LOGE(TAG, "Failed to get running OTA partition for rollback");
        esp_restart();
        return false;
    }

    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get state of running partition for rollback");
        esp_restart();
        return false;
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "Rollback requested but running partition is not pending verification");
        return true;
    }

    ESP_LOGE(TAG, "OTA health gate expired; invalidating %s and rebooting for rollback", partition->label);
    OtaReleaseAuditRecordOutcome("rolled_back");
    const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    if (err == ESP_OK) {
        return true;
    }

    ESP_LOGE(TAG, "Failed to mark pending firmware invalid: %s; rebooting without confirmation", esp_err_to_name(err));
    esp_restart();
    return false;
}

bool Ota::UpgradeFirmwareOffer(const FirmwareOffer& offer,
                               std::function<void(int progress, size_t speed)> callback) {
    if (offer.url.empty()) {
        ESP_LOGE(TAG, "Cannot upgrade without a firmware URL");
        return false;
    }
    if (!IsAllowedOtaUrl(offer.url, "firmware")) {
        return false;
    }

    const bool requires_integrity_verification = offer.has_extended_fields;
    if (requires_integrity_verification && !offer.HasCompleteIntegrityMetadata()) {
        ESP_LOGE(TAG, "Refusing incomplete extended firmware offer");
        return false;
    }
    if (!requires_integrity_verification) {
#if !CONFIG_OTA_ALLOW_LEGACY_UNSIGNED_OFFERS
        ESP_LOGE(TAG, "Refusing unsigned legacy firmware upgrade in production");
        return false;
#endif
    }

    ESP_LOGI(TAG, "Upgrading firmware from %s (%s)", offer.url.c_str(),
             requires_integrity_verification ? "sha256 verified" : "legacy compatibility");
    esp_ota_handle_t update_handle = 0;
    auto update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Failed to get update partition");
        return false;
    }

    if (requires_integrity_verification && offer.size_bytes > update_partition->size) {
        ESP_LOGE(TAG, "Firmware size %u exceeds update partition %s capacity %u",
                 static_cast<unsigned>(offer.size_bytes), update_partition->label,
                 static_cast<unsigned>(update_partition->size));
        return false;
    }

    OtaReleaseAttempt release_attempt(offer);

    ESP_LOGI(TAG, "Writing to partition %s at offset 0x%lx", update_partition->label, update_partition->address);
    bool image_header_checked = false;
    bool update_started = false;
    std::string image_header;

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    if (!http->Open("GET", offer.url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return false;
    }

    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "Failed to get firmware, status code: %d", http->GetStatusCode());
        http->Close();
        return false;
    }

    size_t content_length = http->GetBodyLength();
    if (content_length == 0) {
        ESP_LOGE(TAG, "Failed to get content length");
        http->Close();
        return false;
    }
    if (requires_integrity_verification && content_length != offer.size_bytes) {
        ESP_LOGE(TAG, "Firmware content length %u does not match offer size %u",
                 static_cast<unsigned>(content_length), static_cast<unsigned>(offer.size_bytes));
        http->Close();
        return false;
    }
    if (content_length > update_partition->size) {
        ESP_LOGE(TAG, "Firmware content length %u exceeds update partition %s capacity %u",
                 static_cast<unsigned>(content_length), update_partition->label,
                 static_cast<unsigned>(update_partition->size));
        http->Close();
        return false;
    }

    constexpr size_t PAGE_SIZE = 4096;
    char* buffer = (char*)heap_caps_malloc(PAGE_SIZE, MALLOC_CAP_INTERNAL);
    if (buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        http->Close();
        return false;
    }

    mbedtls_sha256_context sha256_context;
    bool sha256_started = false;
    mbedtls_sha256_init(&sha256_context);
    if (requires_integrity_verification) {
        if (mbedtls_sha256_starts(&sha256_context, 0) != 0) {
            ESP_LOGE(TAG, "Failed to initialize SHA-256");
            mbedtls_sha256_free(&sha256_context);
            heap_caps_free(buffer);
            http->Close();
            return false;
        }
        sha256_started = true;
    }

    auto abort_upgrade = [&]() {
        if (update_started) {
            esp_ota_abort(update_handle);
            update_started = false;
        }
        if (sha256_started) {
            mbedtls_sha256_free(&sha256_context);
            sha256_started = false;
        }
        heap_caps_free(buffer);
        http->Close();
    };

    size_t buffer_offset = 0;  // Current data size in buffer
    size_t total_read = 0, recent_read = 0;
    auto last_calc_time = esp_timer_get_time();
    while (true) {
        int ret = http->Read(buffer + buffer_offset, PAGE_SIZE - buffer_offset);
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to read HTTP data: %s", esp_err_to_name(ret));
            abort_upgrade();
            return false;
        }

        if (ret > 0 && static_cast<size_t>(ret) > content_length - total_read) {
            ESP_LOGE(TAG, "Firmware download exceeds declared content length");
            abort_upgrade();
            return false;
        }

        if (ret > 0 && requires_integrity_verification &&
            mbedtls_sha256_update(&sha256_context,
                                  reinterpret_cast<const unsigned char*>(buffer + buffer_offset),
                                  static_cast<size_t>(ret)) != 0) {
            ESP_LOGE(TAG, "Failed to update SHA-256");
            abort_upgrade();
            return false;
        }

        // Calculate speed and progress every second
        recent_read += ret;
        total_read += ret;
        buffer_offset += ret;
        if (esp_timer_get_time() - last_calc_time >= 1000000 || ret == 0) {
            size_t progress = total_read * 100 / content_length;
            ESP_LOGI(TAG, "Progress: %u%% (%u/%u), Speed: %uB/s", progress, total_read, content_length, recent_read);
            if (callback) {
                callback(progress, recent_read);
            }
            last_calc_time = esp_timer_get_time();
            recent_read = 0;
        }

        if (!image_header_checked) {
            image_header.append(buffer, buffer_offset);
            if (image_header.size() >= sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                esp_app_desc_t new_app_info;
                memcpy(&new_app_info, image_header.data() + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(esp_app_desc_t));
                if (requires_integrity_verification) {
                    const size_t image_version_length =
                        strnlen(new_app_info.version, sizeof(new_app_info.version));
                    const std::string image_version(
                        new_app_info.version, image_version_length);
                    if (image_version != offer.version) {
                        ESP_LOGE(TAG, "Firmware descriptor version %s does not match offer version %s",
                                 image_version.c_str(), offer.version.c_str());
                        abort_upgrade();
                        return false;
                    }
                }

                const esp_err_t begin_error = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                if (begin_error != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to begin OTA: %s", esp_err_to_name(begin_error));
                    abort_upgrade();
                    return false;
                }

                update_started = true;
                image_header_checked = true;
                std::string().swap(image_header);
            }
        }

        // Write to flash when buffer is full (4KB) or it's the last chunk
        bool is_last_chunk = (ret == 0);
        if (is_last_chunk && !image_header_checked) {
            ESP_LOGE(TAG, "Firmware image is too short to contain an application descriptor");
            abort_upgrade();
            return false;
        }
        if (buffer_offset == PAGE_SIZE || (is_last_chunk && buffer_offset > 0)) {
            auto err = esp_ota_write(update_handle, buffer, buffer_offset);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to write OTA data: %s", esp_err_to_name(err));
                abort_upgrade();
                return false;
            }

            buffer_offset = 0;
        }

        if (is_last_chunk) {
            break;
        }
    }

    if (!image_header_checked) {
        ESP_LOGE(TAG, "Firmware image is too short to contain an application descriptor");
        abort_upgrade();
        return false;
    }

    if (total_read != content_length) {
        ESP_LOGE(TAG, "Firmware download truncated: got %u bytes, expected %u bytes",
                 static_cast<unsigned>(total_read), static_cast<unsigned>(content_length));
        abort_upgrade();
        return false;
    }

    if (requires_integrity_verification) {
        if (total_read != offer.size_bytes) {
            ESP_LOGE(TAG, "Firmware download size %u does not match offer size %u",
                     static_cast<unsigned>(total_read), static_cast<unsigned>(offer.size_bytes));
            abort_upgrade();
            return false;
        }
    }

    if (requires_integrity_verification) {
        unsigned char digest[32] = {};
        if (mbedtls_sha256_finish(&sha256_context, digest) != 0) {
            ESP_LOGE(TAG, "Failed to finalize SHA-256");
            abort_upgrade();
            return false;
        }
        mbedtls_sha256_free(&sha256_context);
        sha256_started = false;

        if (!DigestMatchesExpectedSha256(digest, offer.sha256)) {
            ESP_LOGE(TAG, "Firmware SHA-256 does not match the offer");
            abort_upgrade();
            return false;
        }
        release_attempt.MarkImageVerified();
    } else {
        mbedtls_sha256_free(&sha256_context);
    }

    heap_caps_free(buffer);
    http->Close();

    esp_err_t err = esp_ota_end(update_handle);
    update_started = false;
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "Failed to end OTA: %s", esp_err_to_name(err));
        }
        return false;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
        return false;
    }
    release_attempt.MarkBootPartitionSelected();

    ESP_LOGI(TAG, "Firmware upgrade successful");
    return true;
}

bool Ota::Upgrade(const std::string& firmware_url, std::function<void(int progress, size_t speed)> callback) {
    FirmwareOffer legacy_offer;
    legacy_offer.url = firmware_url;
    return UpgradeFirmwareOffer(legacy_offer, callback);
}

bool Ota::StartUpgrade(std::function<void(int progress, size_t speed)> callback) {
    return UpgradeFirmwareOffer(firmware_offer_, callback);
}


std::vector<int> Ota::ParseVersion(const std::string& version) {
    std::vector<int> versionNumbers;
    if (!ParseNumericVersion(version, &versionNumbers)) {
        ESP_LOGW(TAG, "Unsupported firmware version format: %s", version.c_str());
        versionNumbers.clear();
    }
    return versionNumbers;
}

bool Ota::IsNewVersionAvailable(const std::string& currentVersion, const std::string& newVersion) {
    std::vector<int> current = ParseVersion(currentVersion);
    std::vector<int> newer = ParseVersion(newVersion);
    if (current.empty() || newer.empty()) {
        ESP_LOGE(TAG, "Refusing firmware comparison with an unsupported version format");
        return false;
    }
    
    for (size_t i = 0; i < std::min(current.size(), newer.size()); ++i) {
        if (newer[i] > current[i]) {
            return true;
        } else if (newer[i] < current[i]) {
            return false;
        }
    }
    
    return newer.size() > current.size();
}

std::string Ota::GetActivationPayload() {
    if (!has_serial_number_) {
        return "{}";
    }

    std::string hmac_hex;
#ifdef SOC_HMAC_SUPPORTED
    uint8_t hmac_result[32]; // SHA-256 输出为32字节
    
    // 使用Key0计算HMAC
    esp_err_t ret = esp_hmac_calculate(HMAC_KEY0, (uint8_t*)activation_challenge_.data(), activation_challenge_.size(), hmac_result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HMAC calculation failed: %s", esp_err_to_name(ret));
        return "{}";
    }

    for (size_t i = 0; i < sizeof(hmac_result); i++) {
        char buffer[3];
        sprintf(buffer, "%02x", hmac_result[i]);
        hmac_hex += buffer;
    }
#endif

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "algorithm", "hmac-sha256");
    cJSON_AddStringToObject(payload, "serial_number", serial_number_.c_str());
    cJSON_AddStringToObject(payload, "challenge", activation_challenge_.c_str());
    cJSON_AddStringToObject(payload, "hmac", hmac_hex.c_str());
    auto json_str = cJSON_PrintUnformatted(payload);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(payload);

    ESP_LOGI(TAG, "Activation payload: %s", json.c_str());
    return json;
}

esp_err_t Ota::Activate() {
    if (!has_activation_challenge_) {
        ESP_LOGW(TAG, "No activation challenge found");
        return ESP_FAIL;
    }

    std::string url = GetCheckVersionUrl();
    if (url.back() != '/') {
        url += "/activate";
    } else {
        url += "activate";
    }

    auto http = SetupHttp();

    std::string data = GetActivationPayload();
    http->SetContent(std::move(data));

    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return ESP_FAIL;
    }
    
    auto status_code = http->GetStatusCode();
    if (status_code == 202) {
        return ESP_ERR_TIMEOUT;
    }
    if (status_code != 200) {
        ESP_LOGE(TAG, "Failed to activate, code: %d, body: %s", status_code, http->ReadAll().c_str());
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Activation successful");
    return ESP_OK;
}
