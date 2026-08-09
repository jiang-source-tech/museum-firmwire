#include "doorbell_config.h"
#include "doorbell_mqtt_contract.h"

#include <cassert>
#include <iostream>
#include <string>

namespace {
DoorbellMqttConfig ValidConfig() {
    DoorbellMqttConfig config;
    config.version = 1;
    config.enabled = true;
    config.endpoint = "broker.internal:1883";
    config.client_id = "device-client";
    config.username = "device-user";
    config.password = "device-secret";
    config.status_topic = "device/aabbcc/status";
    config.notification_topic = "device/aabbcc/notification";
    config.overview_topic = "device/aabbcc/overview";
    return config;
}
}  // namespace

int main() {
    struct ValidationCase {
        const char* name;
        DoorbellMqttConfig config;
        const char* device_id;
        bool expected;
    };

    DoorbellMqttConfig wildcard = ValidConfig();
    wildcard.notification_topic = "device/+/notification";
    DoorbellMqttConfig collision = ValidConfig();
    collision.overview_topic = collision.notification_topic;
    DoorbellMqttConfig wrong_device = ValidConfig();
    wrong_device.status_topic = "device/other/status";
    DoorbellMqttConfig bad_scheme = ValidConfig();
    bad_scheme.endpoint = "mqtts://broker.internal:8883";
    DoorbellMqttConfig endpoint_path = ValidConfig();
    endpoint_path.endpoint = "broker.internal/path";
    DoorbellMqttConfig wake_only = ValidConfig();
    wake_only.overview_topic.clear();

    const ValidationCase validation_cases[] = {
        {"valid overview config", ValidConfig(), "aabbcc", true},
        {"valid wake-only config", wake_only, "aabbcc", true},
        {"wildcard rejected", wildcard, "aabbcc", false},
        {"topic collision rejected", collision, "aabbcc", false},
        {"wrong device rejected", wrong_device, "aabbcc", false},
        {"unsupported endpoint scheme rejected", bad_scheme, "aabbcc", false},
        {"endpoint path rejected", endpoint_path, "aabbcc", false},
        {"empty device rejected", ValidConfig(), "", false},
    };
    for (const auto& test : validation_cases) {
        assert(IsDoorbellMqttConfigValidForDevice(test.config, test.device_id) ==
               test.expected);
    }

    const DoorbellMqttConfig config = ValidConfig();
    assert(DoorbellTelemetryTopic(config) == "device/aabbcc/telemetry");
    struct ClassificationCase {
        const char* topic;
        DoorbellMqttTopic expected;
    };
    const ClassificationCase classification_cases[] = {
        {"device/aabbcc/status", DoorbellMqttTopic::kStatus},
        {"device/aabbcc/notification", DoorbellMqttTopic::kNotification},
        {"device/aabbcc/overview", DoorbellMqttTopic::kOverview},
        {"device/aabbcc/notification/extra", DoorbellMqttTopic::kUnknown},
        {"device/other/overview", DoorbellMqttTopic::kUnknown},
    };
    for (const auto& test : classification_cases) {
        assert(ClassifyDoorbellMqttTopic(config, test.topic) == test.expected);
    }

    std::string uri;
    assert(NormalizeDoorbellMqttEndpoint("broker.internal", uri));
    assert(uri == "mqtt://broker.internal:1883");
    assert(!NormalizeDoorbellMqttEndpoint("mqtt://[::1]:1883", uri));

    std::cout << "xiaoxin doorbell mqtt contract tests passed\n";
    return 0;
}
