#include "doorbell_config.h"
#include "doorbell_config_contract.h"
#include "doorbell_mqtt_contract.h"

#include "settings.h"

#include <cJSON.h>
#include <esp_log.h>

#define TAG "DoorbellConfig"

namespace {
constexpr const char* kSettingsNamespace = "doorbell_mqtt";
// NVS keys are limited to 15 characters, so the external field name cannot be
// used verbatim for notification_topic.
constexpr const char* kNotificationTopicKey = "notify_topic";

DoorbellJsonStringField JsonString(const cJSON* root, const char* name) {
    const cJSON* item = cJSON_GetObjectItem(root, name);
    return {cJSON_IsString(item) != 0,
            cJSON_IsString(item) ? std::string(item->valuestring) : std::string()};
}

DoorbellJsonNumberField JsonNumber(const cJSON* root, const char* name) {
    const cJSON* item = cJSON_GetObjectItem(root, name);
    return {item != nullptr, cJSON_IsNumber(item) != 0,
            cJSON_IsNumber(item) ? item->valuedouble : 0.0};
}

DoorbellJsonBoolField JsonBool(const cJSON* root, const char* name) {
    const cJSON* item = cJSON_GetObjectItem(root, name);
    return {item != nullptr, cJSON_IsBool(item) != 0, cJSON_IsTrue(item) != 0};
}
}  // namespace

bool DoorbellMqttConfig::IsUsable() const {
    return version == 1 && enabled && !endpoint.empty() && !client_id.empty() &&
           !username.empty() && !password.empty() && !status_topic.empty() &&
           !notification_topic.empty() &&
           keepalive_seconds >= kMinKeepaliveSeconds &&
           keepalive_seconds <= kMaxKeepaliveSeconds && qos == 1 &&
           HasValidDoorbellMqttTopics(*this);
}

DoorbellMqttConfig ParseDoorbellMqttConfig(const cJSON* root) {
    DoorbellMqttConfigInput input;
    input.object_valid = cJSON_IsObject(root);
    input.version = JsonNumber(root, "version");
    input.enabled = JsonBool(root, "enabled");
    input.endpoint = JsonString(root, "endpoint");
    input.client_id = JsonString(root, "client_id");
    input.username = JsonString(root, "username");
    input.password = JsonString(root, "password");
    input.status_topic = JsonString(root, "status_topic");
    input.notification_topic = JsonString(root, "notification_topic");
    input.overview_topic = JsonString(root, "overview_topic");
    input.keepalive_seconds = JsonNumber(root, "keepalive_seconds");
    input.qos = JsonNumber(root, "qos");
    const DoorbellMqttConfig config = ParseDoorbellMqttConfigContract(input);
    if (input.object_valid && config.version == 0) {
        ESP_LOGW(TAG, "Ignoring invalid doorbell MQTT config contract");
    }
    return config;
}

bool SaveDoorbellMqttConfig(const DoorbellMqttConfig& config) {
    if (!config.IsUsable()) {
        return false;
    }

    Settings settings(kSettingsNamespace, true);
    settings.SetInt("version", config.version);
    settings.SetBool("enabled", config.enabled);
    settings.SetInt("keepalive", config.keepalive_seconds);
    settings.SetInt("qos", config.qos);
    settings.SetString("endpoint", config.endpoint);
    settings.SetString("client_id", config.client_id);
    settings.SetString("username", config.username);
    settings.SetString("password", config.password);
    settings.SetString("status_topic", config.status_topic);
    settings.SetString(kNotificationTopicKey, config.notification_topic);
    settings.SetString("overview_topic", config.overview_topic);
    return true;
}

DoorbellMqttConfig LoadDoorbellMqttConfig() {
    Settings settings(kSettingsNamespace);
    DoorbellMqttConfig config;
    config.version = settings.GetInt("version");
    config.enabled = settings.GetBool("enabled");
    config.keepalive_seconds = settings.GetInt(
        "keepalive", DoorbellMqttConfig::kDefaultKeepaliveSeconds);
    config.qos = settings.GetInt("qos", 1);
    config.endpoint = settings.GetString("endpoint");
    config.client_id = settings.GetString("client_id");
    config.username = settings.GetString("username");
    config.password = settings.GetString("password");
    config.status_topic = settings.GetString("status_topic");
    config.notification_topic = settings.GetString(kNotificationTopicKey);
    config.overview_topic = settings.GetString("overview_topic");
    return config.IsUsable() ? config : DoorbellMqttConfig{};
}

void DisableDoorbellMqttConfig() {
    Settings settings("doorbell_mqtt", true);
    settings.EraseAll();
}
