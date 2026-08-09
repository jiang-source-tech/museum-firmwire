#ifndef DOORBELL_MQTT_SUBACK_H
#define DOORBELL_MQTT_SUBACK_H

#include <cstdint>

enum class DoorbellMqttSubackResult {
    kAccepted,
    kRejected,
    kMalformed,
};

inline DoorbellMqttSubackResult EvaluateDoorbellMqttSuback(
    bool has_error_handle,
    bool subscribe_failed,
    const uint8_t* granted_qos,
    int granted_qos_count) {
    if (!has_error_handle || granted_qos_count <= 0 || granted_qos == nullptr) {
        return DoorbellMqttSubackResult::kMalformed;
    }
    if (subscribe_failed) {
        return DoorbellMqttSubackResult::kRejected;
    }

    for (int index = 0; index < granted_qos_count; ++index) {
        const uint8_t value = granted_qos[index];
        if (value == 0x80) {
            return DoorbellMqttSubackResult::kRejected;
        }
        if (value > 2) {
            return DoorbellMqttSubackResult::kMalformed;
        }
    }
    return DoorbellMqttSubackResult::kAccepted;
}

#endif  // DOORBELL_MQTT_SUBACK_H
