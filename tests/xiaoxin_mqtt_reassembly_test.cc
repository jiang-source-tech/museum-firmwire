#include "doorbell_mqtt_reassembly.h"

#include <cassert>
#include <cstring>
#include <string>

static void test_two_fragments_preserve_first_topic_and_complete_once() {
    DoorbellMqttReceiveState state;
    std::string topic;
    std::string payload;
    const char* configured_topic = "tenant/device/overview";
    const char* first = "{\"type\":";
    const char* second = "\"overview\"}";
    const int first_len = static_cast<int>(std::strlen(first));
    const int second_len = static_cast<int>(std::strlen(second));
    const int total = first_len + second_len;

    auto result = AppendDoorbellMqttFragment(
        state, configured_topic, static_cast<int>(std::strlen(configured_topic)),
        first, first_len, total, 0, topic, payload);
    assert(result == DoorbellMqttFragmentResult::kPending);
    assert(state.active);
    assert(topic.empty());
    assert(payload.empty());

    result = AppendDoorbellMqttFragment(
        state, nullptr, 0, second, second_len, total, first_len, topic, payload);
    assert(result == DoorbellMqttFragmentResult::kComplete);
    assert(topic == configured_topic);
    assert(payload == std::string(first) + second);
    assert(!state.active);
}

static void test_single_fragment_completes_without_retaining_state() {
    DoorbellMqttReceiveState state;
    std::string topic;
    std::string payload;
    const char* data = "{\"type\":\"wake\"}";
    const int data_len = static_cast<int>(std::strlen(data));

    const auto result = AppendDoorbellMqttFragment(
        state, "device/notification", 19, data, data_len, data_len, 0, topic, payload);
    assert(result == DoorbellMqttFragmentResult::kComplete);
    assert(topic == "device/notification");
    assert(payload == data);
    assert(!state.active);
}

static void test_invalid_fragment_sequences_fail_closed_and_clear_state() {
    DoorbellMqttReceiveState state;
    std::string topic;
    std::string payload;
    const char* first = "abc";

    auto result = AppendDoorbellMqttFragment(
        state, "overview", 8, first, 3, 6, 0, topic, payload);
    assert(result == DoorbellMqttFragmentResult::kPending);
    assert(state.active);

    result = AppendDoorbellMqttFragment(
        state, nullptr, 0, "x", 1, 6, 4, topic, payload);
    assert(result == DoorbellMqttFragmentResult::kRejected);
    assert(!state.active);

    result = AppendDoorbellMqttFragment(
        state, "overview", 8, first, 3, 6, 0, topic, payload);
    assert(result == DoorbellMqttFragmentResult::kPending);
    result = AppendDoorbellMqttFragment(
        state, "unexpected", 10, "def", 3, 6, 3, topic, payload);
    assert(result == DoorbellMqttFragmentResult::kRejected);
    assert(!state.active);

    result = AppendDoorbellMqttFragment(
        state, "overview", 8, first, 3, 6, 0, topic, payload);
    assert(result == DoorbellMqttFragmentResult::kPending);
    result = AppendDoorbellMqttFragment(
        state, nullptr, 0, "def", 3, 7, 3, topic, payload);
    assert(result == DoorbellMqttFragmentResult::kRejected);
    assert(!state.active);

    result = AppendDoorbellMqttFragment(
        state, "overview", 8, nullptr, 1, 1, 0, topic, payload);
    assert(result == DoorbellMqttFragmentResult::kRejected);
    assert(!state.active);

    result = AppendDoorbellMqttFragment(
        state, "overview", 8, "x", 1, 2049, 0, topic, payload);
    assert(result == DoorbellMqttFragmentResult::kRejected);
    assert(!state.active);

    result = AppendDoorbellMqttFragment(
        state, "overview", 8, "x", 1, -1, 0, topic, payload);
    assert(result == DoorbellMqttFragmentResult::kRejected);
    assert(!state.active);
}

static void test_new_first_fragment_discards_previous_partial_message() {
    DoorbellMqttReceiveState state;
    std::string topic;
    std::string payload;

    auto result = AppendDoorbellMqttFragment(
        state, "old", 3, "old", 3, 6, 0, topic, payload);
    assert(result == DoorbellMqttFragmentResult::kPending);

    result = AppendDoorbellMqttFragment(
        state, "new", 3, "new", 3, 3, 0, topic, payload);
    assert(result == DoorbellMqttFragmentResult::kComplete);
    assert(topic == "new");
    assert(payload == "new");
    assert(!state.active);
}

int main() {
    test_two_fragments_preserve_first_topic_and_complete_once();
    test_single_fragment_completes_without_retaining_state();
    test_invalid_fragment_sequences_fail_closed_and_clear_state();
    test_new_first_fragment_discards_previous_partial_message();
    return 0;
}
