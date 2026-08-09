#include "device_location_heartbeat_state.h"

#include <cassert>
#include <iostream>

static void TestStalePendingReconnectIsCleared() {
    DeviceLocationHeartbeatRequestGate gate;

    gate.MarkStarted();
    assert(gate.request_in_flight_);

    gate.OnNetworkDisconnected();
    gate.MarkPending();
    assert(gate.request_pending_);

    gate.OnNetworkDisconnected();
    assert(!gate.request_pending_);

    assert(!gate.Complete(false));
    assert(!gate.request_in_flight_);
    assert(!gate.request_pending_);

    gate.MarkStarted();
    assert(!gate.Complete(true));
    assert(!gate.request_in_flight_);
    assert(!gate.request_pending_);
}

static void TestCurrentReconnectPendingRunsOnce() {
    DeviceLocationHeartbeatRequestGate gate;

    gate.MarkStarted();
    gate.OnNetworkDisconnected();
    gate.MarkPending();

    assert(gate.Complete(true));
    assert(gate.request_in_flight_);
    assert(!gate.request_pending_);

    assert(!gate.Complete(true));
    assert(!gate.request_in_flight_);
    assert(!gate.request_pending_);
}

int main() {
    TestStalePendingReconnectIsCleared();
    TestCurrentReconnectPendingRunsOnce();
    std::cout << "xiaoxin location heartbeat state tests passed\n";
    return 0;
}
