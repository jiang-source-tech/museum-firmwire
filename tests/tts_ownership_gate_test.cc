#include <cassert>
#include <mutex>
#include <vector>

#include "audio/tts_ownership_gate.h"

int main() {
    TtsOwnershipGate gate;
    std::mutex control_mutex;
    std::vector<int> execution_order;
    TtsOwnershipGate::DeferredTasks deferred;

    {
        std::lock_guard<std::mutex> lock(control_mutex);
        assert(gate.ReserveCleanup());
        assert(!gate.CanAcquireOwnership());
        assert(gate.DeferIfCleanupReserved([&execution_order]() {
            execution_order.push_back(1);
        }));
        assert(gate.DeferIfCleanupReserved([&execution_order]() {
            execution_order.push_back(2);
        }));
        assert(execution_order.empty());
        deferred = gate.ReleaseCleanup();
    }

    assert(execution_order.empty());
    for (auto& task : deferred) {
        task();
    }
    assert((execution_order == std::vector<int>{1, 2}));

    {
        std::lock_guard<std::mutex> lock(control_mutex);
        assert(gate.CanAcquireOwnership());
        assert(!gate.DeferIfCleanupReserved([]() {}));
    }
    return 0;
}
