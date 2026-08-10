#ifndef _OTA_H
#define _OTA_H

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <esp_err.h>
#include "board.h"

// The immutable identity of the firmware currently running on this device.
// It is deliberately separate from FirmwareOffer so the compatibility rule is
// executable and can be shared by every place that consumes an OTA offer.
struct FirmwareOfferTarget {
    std::string model;
    std::string board_type;
    std::string partition_layout_id;
};

// A firmware offer is legacy only when the server omits every extension field.
// Once an extension is present, all metadata is required before the image can
// be installed. This prevents a partially deployed server response from
// silently downgrading back to URL-only installation.
struct FirmwareOffer {
    std::string release_id;
    std::string version;
    std::string url;
    std::string sha256;
    size_t size_bytes = 0;
    std::string model;
    std::string board_type;
    std::string partition_layout_id;
    bool has_extended_fields = false;

    bool HasIntegrityMetadata() const {
        return !sha256.empty() && size_bytes > 0;
    }

    bool HasCompleteIntegrityMetadata() const {
        return !release_id.empty() && HasIntegrityMetadata() && !model.empty() && !board_type.empty() &&
               !partition_layout_id.empty();
    }

    bool MatchesTarget(const FirmwareOfferTarget& target) const {
        return !has_extended_fields ||
               (model == target.model && board_type == target.board_type &&
                partition_layout_id == target.partition_layout_id);
    }
};

class Ota {
public:
    Ota();
    ~Ota();

    esp_err_t CheckVersion();
    esp_err_t Activate();
    bool HasActivationChallenge() { return has_activation_challenge_; }
    bool HasNewVersion() { return has_new_version_; }
    bool HasMqttConfig() { return has_mqtt_config_; }
    bool HasWebsocketConfig() { return has_websocket_config_; }
    bool HasActivationCode() { return has_activation_code_; }
    bool HasServerTime() { return has_server_time_; }
    bool StartUpgrade(std::function<void(int progress, size_t speed)> callback);
    static bool Upgrade(const std::string& firmware_url, std::function<void(int progress, size_t speed)> callback);
    static bool IsRunningPartitionPendingVerification();
    static bool MarkCurrentVersionValid();
    static bool MarkCurrentVersionInvalidAndReboot();

    const std::string& GetFirmwareVersion() const { return firmware_version_; }
    const std::string& GetCurrentVersion() const { return current_version_; }
    const std::string& GetFirmwareUrl() const { return firmware_url_; }
    const FirmwareOffer& GetFirmwareOffer() const { return firmware_offer_; }
    const std::string& GetActivationMessage() const { return activation_message_; }
    const std::string& GetActivationCode() const { return activation_code_; }
    std::string GetCheckVersionUrl();

private:
    std::string activation_message_;
    std::string activation_code_;
    bool has_new_version_ = false;
    bool has_mqtt_config_ = false;
    bool has_websocket_config_ = false;
    bool has_server_time_ = false;
    bool has_activation_code_ = false;
    bool has_serial_number_ = false;
    bool has_activation_challenge_ = false;
    std::string current_version_;
    std::string firmware_version_;
    std::string firmware_url_;
    std::string activation_challenge_;
    std::string serial_number_;
    int activation_timeout_ms_ = 30000;
    FirmwareOffer firmware_offer_;

    std::function<void(int progress, size_t speed)> upgrade_callback_;
    static bool UpgradeFirmwareOffer(const FirmwareOffer& offer,
                                     std::function<void(int progress, size_t speed)> callback);
    std::vector<int> ParseVersion(const std::string& version);
    bool IsNewVersionAvailable(const std::string& currentVersion, const std::string& newVersion);
    std::string GetActivationPayload();
    std::unique_ptr<Http> SetupHttp();
};

#endif // _OTA_H
