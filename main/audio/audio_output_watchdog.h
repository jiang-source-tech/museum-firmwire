#ifndef AUDIO_OUTPUT_WATCHDOG_H
#define AUDIO_OUTPUT_WATCHDOG_H

#include <chrono>

#include "audio_playback_failure.h"

inline constexpr auto kNoAudioCodecWriteTimeout =
    std::chrono::milliseconds(1000);
inline constexpr auto kAudioOutputDeadline =
    std::chrono::milliseconds(1500);
inline constexpr auto kReliableTtsDrainWatchdog =
    std::chrono::milliseconds(8000);

static_assert(kNoAudioCodecWriteTimeout < kAudioOutputDeadline);
static_assert(kAudioOutputDeadline < kReliableTtsDrainWatchdog);

inline AudioPlaybackFailureReason ClassifyAudioOutputAttempt(
    bool write_succeeded,
    std::chrono::steady_clock::duration elapsed) {
    if (!write_succeeded || elapsed > kAudioOutputDeadline) {
        return AudioPlaybackFailureReason::kOutputWriteTimeout;
    }
    return AudioPlaybackFailureReason::kNone;
}

#endif
