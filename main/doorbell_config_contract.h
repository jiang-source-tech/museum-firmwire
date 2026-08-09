#ifndef DOORBELL_CONFIG_CONTRACT_H
#define DOORBELL_CONFIG_CONTRACT_H

#include "doorbell_config.h"

#include <string>

struct DoorbellJsonNumberField {
    bool present = false;
    bool is_number = false;
    double value = 0;
};

struct DoorbellJsonBoolField {
    bool present = false;
    bool is_bool = false;
    bool value = false;
};

struct DoorbellJsonStringField {
    bool is_string = false;
    std::string value;
};

struct DoorbellMqttConfigInput {
    bool object_valid = false;
    DoorbellJsonNumberField version;
    DoorbellJsonBoolField enabled;
    DoorbellJsonStringField endpoint;
    DoorbellJsonStringField client_id;
    DoorbellJsonStringField username;
    DoorbellJsonStringField password;
    DoorbellJsonStringField status_topic;
    DoorbellJsonStringField notification_topic;
    DoorbellJsonStringField overview_topic;
    DoorbellJsonNumberField keepalive_seconds;
    DoorbellJsonNumberField qos;
};

DoorbellMqttConfig ParseDoorbellMqttConfigContract(
    const DoorbellMqttConfigInput& input);

#endif  // DOORBELL_CONFIG_CONTRACT_H
