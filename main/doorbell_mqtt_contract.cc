#include "doorbell_mqtt_contract.h"

#include <cstdint>
#include <cstring>

namespace {
constexpr const char* kMqttScheme = "mqtt://";
constexpr int kDefaultMqttPort = 1883;

bool IsValidHostnameChar(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_';
}

bool ContainsWildcard(const std::string& topic) {
    return topic.find('+') != std::string::npos ||
           topic.find('#') != std::string::npos;
}
}  // namespace

bool NormalizeDoorbellMqttEndpoint(const std::string& endpoint, std::string& uri) {
    std::string authority;
    if (endpoint.rfind(kMqttScheme, 0) == 0) {
        authority = endpoint.substr(std::strlen(kMqttScheme));
    } else {
        if (endpoint.find("://") != std::string::npos) {
            return false;
        }
        authority = endpoint;
    }

    if (authority.empty() || authority.find('/') != std::string::npos ||
        authority.find('?') != std::string::npos ||
        authority.find('#') != std::string::npos ||
        authority.find('@') != std::string::npos ||
        authority.find('[') != std::string::npos ||
        authority.find(']') != std::string::npos) {
        return false;
    }

    const size_t port_separator = authority.find(':');
    if (port_separator != std::string::npos &&
        authority.find(':') != authority.rfind(':')) {
        return false;
    }
    const std::string host = port_separator == std::string::npos
                                 ? authority
                                 : authority.substr(0, port_separator);
    const std::string port_text = port_separator == std::string::npos
                                      ? std::to_string(kDefaultMqttPort)
                                      : authority.substr(port_separator + 1);
    if (host.empty() || port_text.empty()) {
        return false;
    }
    for (char ch : host) {
        if (!IsValidHostnameChar(ch)) {
            return false;
        }
    }
    uint32_t port = 0;
    for (char ch : port_text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        port = port * 10 + static_cast<uint32_t>(ch - '0');
        if (port > 65535) {
            return false;
        }
    }
    if (port == 0) {
        return false;
    }
    uri = std::string(kMqttScheme) + host + ":" + port_text;
    return true;
}

bool HasValidDoorbellMqttTopics(const DoorbellMqttConfig& config) {
    if (config.status_topic.empty() || config.notification_topic.empty() ||
        ContainsWildcard(config.status_topic) ||
        ContainsWildcard(config.notification_topic) ||
        ContainsWildcard(config.overview_topic) ||
        config.status_topic == config.notification_topic) {
        return false;
    }
    return config.overview_topic.empty() ||
           (config.overview_topic != config.status_topic &&
            config.overview_topic != config.notification_topic);
}

bool IsDoorbellMqttConfigValidForDevice(const DoorbellMqttConfig& config,
                                        const std::string& device_id) {
    std::string normalized_endpoint;
    if (config.version != 1 || !config.enabled || config.endpoint.empty() ||
        config.client_id.empty() || config.username.empty() ||
        config.password.empty() ||
        config.keepalive_seconds < DoorbellMqttConfig::kMinKeepaliveSeconds ||
        config.keepalive_seconds > DoorbellMqttConfig::kMaxKeepaliveSeconds ||
        config.qos != 1 || device_id.empty() ||
        !NormalizeDoorbellMqttEndpoint(config.endpoint, normalized_endpoint) ||
        !HasValidDoorbellMqttTopics(config)) {
        return false;
    }
    const std::string prefix = "device/" + device_id + "/";
    return config.status_topic == prefix + "status" &&
           config.notification_topic == prefix + "notification" &&
           (config.overview_topic.empty() ||
            config.overview_topic == prefix + "overview");
}

std::string DoorbellTelemetryTopic(const DoorbellMqttConfig& config) {
    constexpr const char* kStatusSuffix = "/status";
    const size_t suffix_length = std::strlen(kStatusSuffix);
    if (config.status_topic.size() <= suffix_length ||
        config.status_topic.compare(
            config.status_topic.size() - suffix_length,
            suffix_length,
            kStatusSuffix) != 0) {
        return {};
    }
    return config.status_topic.substr(
               0, config.status_topic.size() - suffix_length) +
           "/telemetry";
}

DoorbellMqttTopic ClassifyDoorbellMqttTopic(const DoorbellMqttConfig& config,
                                            const std::string& topic) {
    if (topic == config.status_topic) {
        return DoorbellMqttTopic::kStatus;
    }
    if (topic == config.notification_topic) {
        return DoorbellMqttTopic::kNotification;
    }
    if (!config.overview_topic.empty() && topic == config.overview_topic) {
        return DoorbellMqttTopic::kOverview;
    }
    return DoorbellMqttTopic::kUnknown;
}
