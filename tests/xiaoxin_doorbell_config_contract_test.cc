#include "doorbell_config_contract.h"

#include <cassert>
#include <iostream>
#include <limits>

namespace {
DoorbellMqttConfigInput ValidInput() {
    DoorbellMqttConfigInput input;
    input.object_valid = true;
    input.version = {true, true, 1.0};
    input.enabled = {true, true, true};
    input.endpoint = {true, "broker.internal:1883"};
    input.client_id = {true, "client"};
    input.username = {true, "user"};
    input.password = {true, "secret"};
    input.status_topic = {true, "device/aabbcc/status"};
    input.notification_topic = {true, "device/aabbcc/notification"};
    input.overview_topic = {true, "device/aabbcc/overview"};
    return input;
}
}  // namespace

int main() {
    DoorbellMqttConfig defaults = ParseDoorbellMqttConfigContract(ValidInput());
    assert(defaults.version == 1);
    assert(defaults.enabled);
    assert(defaults.keepalive_seconds == DoorbellMqttConfig::kDefaultKeepaliveSeconds);
    assert(defaults.qos == 1);

    DoorbellMqttConfigInput explicit_values = ValidInput();
    explicit_values.keepalive_seconds = {true, true, 600.0};
    explicit_values.qos = {true, true, 1.0};
    DoorbellMqttConfig parsed = ParseDoorbellMqttConfigContract(explicit_values);
    assert(parsed.keepalive_seconds == 600);

    struct InvalidCase {
        const char* name;
        DoorbellMqttConfigInput input;
    };
    DoorbellMqttConfigInput fractional_version = ValidInput();
    fractional_version.version.value = 1.5;
    DoorbellMqttConfigInput infinite_version = ValidInput();
    infinite_version.version.value = std::numeric_limits<double>::infinity();
    DoorbellMqttConfigInput wrong_enabled_type = ValidInput();
    wrong_enabled_type.enabled.is_bool = false;
    DoorbellMqttConfigInput bad_keepalive_type = ValidInput();
    bad_keepalive_type.keepalive_seconds = {true, false, 60.0};
    DoorbellMqttConfigInput bad_qos = ValidInput();
    bad_qos.qos = {true, true, 0.0};
    DoorbellMqttConfigInput not_object = ValidInput();
    not_object.object_valid = false;

    const InvalidCase invalid_cases[] = {
        {"fractional version", fractional_version},
        {"infinite version", infinite_version},
        {"enabled wrong type", wrong_enabled_type},
        {"keepalive wrong type", bad_keepalive_type},
        {"unsupported qos", bad_qos},
        {"not object", not_object},
    };
    for (const auto& test : invalid_cases) {
        const DoorbellMqttConfig invalid = ParseDoorbellMqttConfigContract(test.input);
        assert(invalid.version == 0);
        assert(!invalid.enabled);
    }

    DoorbellMqttConfigInput disabled = ValidInput();
    disabled.enabled.value = false;
    DoorbellMqttConfig disabled_config = ParseDoorbellMqttConfigContract(disabled);
    assert(disabled_config.version == 1);
    assert(!disabled_config.enabled);

    std::cout << "xiaoxin doorbell config contract tests passed\n";
    return 0;
}
