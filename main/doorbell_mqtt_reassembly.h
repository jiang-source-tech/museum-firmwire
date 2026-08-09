#ifndef DOORBELL_MQTT_REASSEMBLY_H
#define DOORBELL_MQTT_REASSEMBLY_H

#include <cstddef>
#include <string>
#include <utility>

constexpr size_t kDoorbellMqttMaxPayloadBytes = 2048;

enum class DoorbellMqttFragmentResult {
    kPending,
    kComplete,
    kRejected,
};

struct DoorbellMqttReceiveState {
    std::string topic;
    std::string payload;
    size_t expected_total = 0;
    size_t next_offset = 0;
    bool active = false;

    void Clear() {
        topic.clear();
        payload.clear();
        expected_total = 0;
        next_offset = 0;
        active = false;
    }
};

inline DoorbellMqttFragmentResult AppendDoorbellMqttFragment(
    DoorbellMqttReceiveState& state,
    const char* topic_data,
    int topic_len,
    const char* data,
    int data_len,
    int total_data_len,
    int current_data_offset,
    std::string& complete_topic,
    std::string& complete_payload) {
    complete_topic.clear();
    complete_payload.clear();

    if (topic_len < 0 || data_len < 0 || total_data_len < 0 ||
        current_data_offset < 0 ||
        total_data_len > static_cast<int>(kDoorbellMqttMaxPayloadBytes) ||
        current_data_offset > total_data_len ||
        data_len > total_data_len - current_data_offset ||
        (data_len > 0 && data == nullptr)) {
        state.Clear();
        return DoorbellMqttFragmentResult::kRejected;
    }

    if (current_data_offset == 0) {
        state.Clear();
        if (topic_len == 0 || topic_data == nullptr) {
            return DoorbellMqttFragmentResult::kRejected;
        }

        if (data_len == total_data_len) {
            complete_topic.assign(topic_data, static_cast<size_t>(topic_len));
            if (data_len > 0) {
                complete_payload.assign(data, static_cast<size_t>(data_len));
            }
            return DoorbellMqttFragmentResult::kComplete;
        }

        state.topic.assign(topic_data, static_cast<size_t>(topic_len));
        if (data_len > 0) {
            state.payload.assign(data, static_cast<size_t>(data_len));
        }
        state.expected_total = static_cast<size_t>(total_data_len);
        state.next_offset = static_cast<size_t>(data_len);
        state.active = true;
        return DoorbellMqttFragmentResult::kPending;
    }

    if (topic_len != 0 || !state.active ||
        static_cast<size_t>(current_data_offset) != state.next_offset ||
        static_cast<size_t>(total_data_len) != state.expected_total) {
        state.Clear();
        return DoorbellMqttFragmentResult::kRejected;
    }

    if (data_len > 0) {
        state.payload.append(data, static_cast<size_t>(data_len));
    }
    state.next_offset += static_cast<size_t>(data_len);
    if (state.next_offset < state.expected_total) {
        return DoorbellMqttFragmentResult::kPending;
    }

    complete_topic = std::move(state.topic);
    complete_payload = std::move(state.payload);
    state.Clear();
    return DoorbellMqttFragmentResult::kComplete;
}

#endif  // DOORBELL_MQTT_REASSEMBLY_H
