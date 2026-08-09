#ifndef TTS_OWNERSHIP_GATE_H
#define TTS_OWNERSHIP_GATE_H

#include <deque>
#include <functional>
#include <utility>

// Application synchronizes this non-blocking gate with tts_control_mutex_.
// Ownership callers either proceed immediately or enqueue a FIFO retry while
// exclusive cleanup is active.
class TtsOwnershipGate {
public:
    using DeferredTasks = std::deque<std::function<void()>>;

    bool CanAcquireOwnership() const {
        return !cleanup_reserved_;
    }

    bool ReserveCleanup() {
        if (cleanup_reserved_) return false;
        cleanup_reserved_ = true;
        return true;
    }

    bool DeferIfCleanupReserved(std::function<void()>&& callback) {
        if (!cleanup_reserved_) return false;
        deferred_tasks_.push_back(std::move(callback));
        return true;
    }

    DeferredTasks ReleaseCleanup() {
        cleanup_reserved_ = false;
        return std::move(deferred_tasks_);
    }

private:
    bool cleanup_reserved_ = false;
    DeferredTasks deferred_tasks_;
};

#endif
