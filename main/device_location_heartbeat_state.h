#ifndef DEVICE_LOCATION_HEARTBEAT_STATE_H
#define DEVICE_LOCATION_HEARTBEAT_STATE_H

#include <cstdint>

struct DeviceLocationHeartbeatRequestGate {
    bool request_in_flight_ = false;
    bool request_pending_ = false;
    uint32_t network_generation_ = 0;
    uint32_t pending_generation_ = 0;

    void MarkStarted() {
        request_in_flight_ = true;
    }

    void MarkStartFailed() {
        request_in_flight_ = false;
        request_pending_ = false;
    }

    void MarkPending() {
        if (!request_in_flight_) {
            return;
        }
        request_pending_ = true;
        pending_generation_ = network_generation_;
    }

    void OnNetworkDisconnected() {
        ++network_generation_;
        request_pending_ = false;
    }

    void CancelPending() {
        request_pending_ = false;
    }

    bool Complete(bool can_repeat) {
        const bool repeat = request_in_flight_ && request_pending_ &&
                            pending_generation_ == network_generation_ &&
                            can_repeat;
        request_pending_ = false;
        if (!repeat) {
            request_in_flight_ = false;
        }
        return repeat;
    }
};

#endif  // DEVICE_LOCATION_HEARTBEAT_STATE_H
