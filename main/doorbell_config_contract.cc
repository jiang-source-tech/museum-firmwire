#include "doorbell_config_contract.h"

#include <cmath>

namespace {
bool IsExactNumberInRange(double value, double minimum, double maximum) {
    return std::isfinite(value) && std::floor(value) == value &&
           value >= minimum && value <= maximum;
}

std::string StringValue(const DoorbellJsonStringField& field) {
    return field.is_string ? field.value : std::string();
}
}  // namespace

DoorbellMqttConfig ParseDoorbellMqttConfigContract(
    const DoorbellMqttConfigInput& input) {
    if (!input.object_valid || !input.version.present ||
        !input.version.is_number || !input.enabled.present ||
        !input.enabled.is_bool ||
        !IsExactNumberInRange(input.version.value, 1.0, 1.0)) {
        return {};
    }

    DoorbellMqttConfig config;
    config.version = 1;
    config.enabled = input.enabled.value;
    config.endpoint = StringValue(input.endpoint);
    config.client_id = StringValue(input.client_id);
    config.username = StringValue(input.username);
    config.password = StringValue(input.password);
    config.status_topic = StringValue(input.status_topic);
    config.notification_topic = StringValue(input.notification_topic);
    config.overview_topic = StringValue(input.overview_topic);

    if (input.keepalive_seconds.present) {
        if (!input.keepalive_seconds.is_number ||
            !IsExactNumberInRange(
                input.keepalive_seconds.value,
                DoorbellMqttConfig::kMinKeepaliveSeconds,
                DoorbellMqttConfig::kMaxKeepaliveSeconds)) {
            return {};
        }
        config.keepalive_seconds =
            static_cast<int>(input.keepalive_seconds.value);
    }
    if (input.qos.present) {
        if (!input.qos.is_number ||
            !IsExactNumberInRange(input.qos.value, 1.0, 1.0)) {
            return {};
        }
        config.qos = 1;
    }
    return config;
}
