#ifndef DOORBELL_CONFIG_H
#define DOORBELL_CONFIG_H

#include <string>

struct cJSON;

struct DoorbellMqttConfig {
    static constexpr int kDefaultKeepaliveSeconds = 240;
    static constexpr int kMinKeepaliveSeconds = 30;
    static constexpr int kMaxKeepaliveSeconds = 3600;

    int version = 0;
    bool enabled = false;
    int keepalive_seconds = kDefaultKeepaliveSeconds;
    int qos = 1;
    std::string endpoint;
    std::string client_id;
    std::string username;
    std::string password;
    std::string status_topic;
    std::string notification_topic;
    std::string overview_topic;

    bool IsUsable() const;
};

DoorbellMqttConfig ParseDoorbellMqttConfig(const cJSON* root);
bool SaveDoorbellMqttConfig(const DoorbellMqttConfig& config);
DoorbellMqttConfig LoadDoorbellMqttConfig();
void DisableDoorbellMqttConfig();

#endif  // DOORBELL_CONFIG_H
