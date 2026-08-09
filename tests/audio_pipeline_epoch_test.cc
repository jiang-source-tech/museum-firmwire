#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <string>
#include <thread>

#include "audio/audio_pipeline_epoch.h"
#include "audio/audio_playback_failure.h"

using namespace std::chrono_literals;

static void reset_waits_for_in_flight_output_and_invalidates_old_work() {
    AudioPipelineEpoch pipeline;
    const uint64_t old_epoch = pipeline.Capture();
    std::promise<void> output_started;
    std::promise<void> release_output;
    auto release_future = release_output.get_future().share();
    std::promise<void> decode_popped;
    std::promise<void> release_decode;
    auto release_decode_future = release_decode.get_future().share();
    std::atomic<bool> output_called{false};
    std::atomic<bool> old_decode_published{false};
    std::atomic<bool> old_decode_result{true};

    std::thread output([&]() {
        const bool published = pipeline.PublishIfCurrent(old_epoch, [&]() {
            output_called = true;
            output_started.set_value();
            release_future.wait();
        });
        assert(published);
    });
    output_started.get_future().wait();

    std::thread decode([&]() {
        decode_popped.set_value();
        release_decode_future.wait();
        old_decode_result = pipeline.PublishIfCurrent(old_epoch, [&]() {
            old_decode_published = true;
        });
    });
    decode_popped.get_future().wait();

    std::promise<void> reset_calling;
    auto reset = std::async(std::launch::async, [&]() {
        reset_calling.set_value();
        return pipeline.Reset([]() {});
    });
    reset_calling.get_future().wait();
    assert(reset.wait_for(0ms) == std::future_status::timeout);

    release_output.set_value();
    output.join();
    const uint64_t new_epoch = reset.get();
    assert(output_called);
    assert(new_epoch != old_epoch);

    release_decode.set_value();
    decode.join();
    assert(!old_decode_result);
    assert(!old_decode_published);

    bool old_output_called = false;
    assert(!pipeline.PublishIfCurrent(old_epoch, [&]() {
        old_output_called = true;
    }));
    assert(!old_output_called);

    bool new_work_published = false;
    assert(pipeline.PublishIfCurrent(new_epoch, [&]() {
        new_work_published = true;
    }));
    assert(new_work_published);
}

static void typed_failures_are_exact_and_generation_isolated() {
    AudioPlaybackFailureState failures;
    failures.Record(17, AudioPlaybackFailureReason::kDecoderCreateFailed);
    failures.Record(18, AudioPlaybackFailureReason::kDecodeFailed);
    failures.Record(19, AudioPlaybackFailureReason::kResamplerCreateFailed);

    assert(failures.FailureFor(17) == AudioPlaybackFailureReason::kDecoderCreateFailed);
    assert(failures.FailureFor(18) == AudioPlaybackFailureReason::kDecodeFailed);
    assert(failures.FailureFor(19) == AudioPlaybackFailureReason::kResamplerCreateFailed);
    assert(failures.FailureFor(20) == AudioPlaybackFailureReason::kNone);
    assert(std::string(AudioPlaybackFailureReasonName(
               AudioPlaybackFailureReason::kDecoderCreateFailed)) ==
           "decoder_create_failed");
    assert(std::string(AudioPlaybackFailureReasonName(
               AudioPlaybackFailureReason::kDecodeFailed)) ==
           "decode_failed");
    assert(std::string(AudioPlaybackFailureReasonName(
               AudioPlaybackFailureReason::kResamplerCreateFailed)) ==
           "resampler_create_failed");

    failures.Clear();
    assert(failures.FailureFor(17) == AudioPlaybackFailureReason::kNone);
    failures.Record(17, AudioPlaybackFailureReason::kDecodeFailed);
    assert(failures.FailureFor(21) == AudioPlaybackFailureReason::kNone);
}

int main() {
    reset_waits_for_in_flight_output_and_invalidates_old_work();
    typed_failures_are_exact_and_generation_isolated();
    return 0;
}
