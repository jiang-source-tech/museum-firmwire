#include <cassert>
#include <chrono>
#include <string>

#include "audio/audio_output_watchdog.h"

using namespace std::chrono_literals;

int main() {
    assert(ClassifyAudioOutputAttempt(true, 50ms) ==
           AudioPlaybackFailureReason::kNone);
    assert(ClassifyAudioOutputAttempt(false, 50ms) ==
           AudioPlaybackFailureReason::kOutputWriteTimeout);
    assert(ClassifyAudioOutputAttempt(true, kAudioOutputDeadline + 1ms) ==
           AudioPlaybackFailureReason::kOutputWriteTimeout);
    assert(std::string(AudioPlaybackFailureReasonName(
               AudioPlaybackFailureReason::kOutputWriteTimeout)) ==
           "output_write_timeout");
    static_assert(kNoAudioCodecWriteTimeout < kAudioOutputDeadline);
    static_assert(kAudioOutputDeadline < kReliableTtsDrainWatchdog);

    AudioPlaybackFailureState failures;
    failures.Record(40, AudioPlaybackFailureReason::kOutputWriteTimeout);
    assert(failures.FailureFor(40) ==
           AudioPlaybackFailureReason::kOutputWriteTimeout);
    assert(failures.FailureFor(41) == AudioPlaybackFailureReason::kNone);
    return 0;
}
