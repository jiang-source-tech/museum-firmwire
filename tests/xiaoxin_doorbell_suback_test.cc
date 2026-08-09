#include "doorbell_mqtt_suback.h"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    const uint8_t qos1[] = {1};
    assert(EvaluateDoorbellMqttSuback(true, false, qos1, 1) ==
           DoorbellMqttSubackResult::kAccepted);

    assert(EvaluateDoorbellMqttSuback(true, true, qos1, 1) ==
           DoorbellMqttSubackResult::kRejected);
    assert(EvaluateDoorbellMqttSuback(false, false, qos1, 1) ==
           DoorbellMqttSubackResult::kMalformed);
    assert(EvaluateDoorbellMqttSuback(true, false, nullptr, 0) ==
           DoorbellMqttSubackResult::kMalformed);

    const uint8_t rejected[] = {0x80};
    assert(EvaluateDoorbellMqttSuback(true, false, rejected, 1) ==
           DoorbellMqttSubackResult::kRejected);

    const uint8_t mixed[] = {1, 0x80};
    assert(EvaluateDoorbellMqttSuback(true, false, mixed, 2) ==
           DoorbellMqttSubackResult::kRejected);

    const uint8_t invalid[] = {3};
    assert(EvaluateDoorbellMqttSuback(true, false, invalid, 1) ==
           DoorbellMqttSubackResult::kMalformed);

    std::cout << "xiaoxin doorbell suback tests passed\n";
    return 0;
}
