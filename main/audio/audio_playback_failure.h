#ifndef AUDIO_PLAYBACK_FAILURE_H
#define AUDIO_PLAYBACK_FAILURE_H

#include <cstddef>
#include <cstdint>
#include <deque>

enum class AudioPlaybackFailureReason {
    kNone,
    kDecoderCreateFailed,
    kDecodeFailed,
    kResamplerCreateFailed,
    kOutputWriteTimeout,
};

inline const char* AudioPlaybackFailureReasonName(
    AudioPlaybackFailureReason reason) {
    switch (reason) {
        case AudioPlaybackFailureReason::kDecoderCreateFailed:
            return "decoder_create_failed";
        case AudioPlaybackFailureReason::kDecodeFailed:
            return "decode_failed";
        case AudioPlaybackFailureReason::kResamplerCreateFailed:
            return "resampler_create_failed";
        case AudioPlaybackFailureReason::kOutputWriteTimeout:
            return "output_write_timeout";
        case AudioPlaybackFailureReason::kNone:
        default:
            return "";
    }
}

struct AudioPlaybackDrainResult {
    bool drained = false;
    AudioPlaybackFailureReason failure = AudioPlaybackFailureReason::kNone;
};

class AudioPlaybackFailureState {
public:
    static constexpr size_t kHistorySize = 8;

    void Record(uint32_t generation, AudioPlaybackFailureReason reason) {
        if (generation == 0 || reason == AudioPlaybackFailureReason::kNone) return;
        for (auto& item : failures_) {
            if (item.generation == generation) {
                item.reason = reason;
                return;
            }
        }
        failures_.push_front({generation, reason});
        while (failures_.size() > kHistorySize) failures_.pop_back();
    }

    AudioPlaybackFailureReason FailureFor(uint32_t generation) const {
        if (generation == 0) return AudioPlaybackFailureReason::kNone;
        for (const auto& item : failures_) {
            if (item.generation == generation) return item.reason;
        }
        return AudioPlaybackFailureReason::kNone;
    }

    void Clear() {
        failures_.clear();
    }

private:
    struct Failure {
        uint32_t generation;
        AudioPlaybackFailureReason reason;
    };
    std::deque<Failure> failures_;
};

#endif
