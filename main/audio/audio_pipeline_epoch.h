#ifndef AUDIO_PIPELINE_EPOCH_H
#define AUDIO_PIPELINE_EPOCH_H

#include <cstdint>
#include <mutex>
#include <utility>

// Serializes reset against publication of decoded PCM and speaker output.
// Decode work may run outside this barrier, but it can only publish while its
// captured epoch is still current. Speaker output runs inside the barrier so a
// reset either invalidates it before it starts or waits until it finishes.
class AudioPipelineEpoch {
public:
    uint64_t Capture() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return epoch_;
    }

    template <typename Callback>
    void PublishCurrent(Callback&& callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::forward<Callback>(callback)(epoch_);
    }

    template <typename Callback>
    bool PublishIfCurrent(uint64_t epoch, Callback&& callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (epoch != epoch_) return false;
        std::forward<Callback>(callback)();
        return true;
    }

    template <typename Callback>
    uint64_t Reset(Callback&& callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++epoch_;
        std::forward<Callback>(callback)();
        return epoch_;
    }

private:
    mutable std::mutex mutex_;
    uint64_t epoch_ = 1;
};

#endif
