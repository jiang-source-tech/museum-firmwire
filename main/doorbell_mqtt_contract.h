#ifndef DOORBELL_MQTT_CONTRACT_H
#define DOORBELL_MQTT_CONTRACT_H

#include "doorbell_config.h"

#include <string>

enum class DoorbellMqttTopic {
    kUnknown,
    kStatus,
    kNotification,
    kOverview,
};

bool NormalizeDoorbellMqttEndpoint(const std::string& endpoint, std::string& uri);
bool HasValidDoorbellMqttTopics(const DoorbellMqttConfig& config);
bool IsDoorbellMqttConfigValidForDevice(const DoorbellMqttConfig& config,
                                        const std::string& device_id);
std::string DoorbellTelemetryTopic(const DoorbellMqttConfig& config);
DoorbellMqttTopic ClassifyDoorbellMqttTopic(const DoorbellMqttConfig& config,
                                            const std::string& topic);

#endif  // DOORBELL_MQTT_CONTRACT_H
