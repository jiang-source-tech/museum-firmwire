#include <cassert>
#include <mutex>

#include "device_state_machine.h"

int main() {
    DeviceStateMachine state_machine;
    std::mutex control_mutex;
    bool listener_called = false;

    assert(state_machine.TransitionTo(kDeviceStateStarting));
    state_machine.AddStateChangeListener(
        [&control_mutex, &listener_called](DeviceState old_state, DeviceState new_state) {
            assert(old_state == kDeviceStateStarting);
            assert(new_state == kDeviceStateActivating);
            assert(control_mutex.try_lock());
            control_mutex.unlock();
            listener_called = true;
        });

    DeviceStateTransition transition;
    {
        std::lock_guard<std::mutex> lock(control_mutex);
        transition = state_machine.CommitTransition(kDeviceStateActivating);
        assert(transition.committed);
        assert(state_machine.GetState() == kDeviceStateActivating);
        assert(!listener_called);
    }

    assert(state_machine.PublishTransition(transition));
    assert(listener_called);
    return 0;
}
