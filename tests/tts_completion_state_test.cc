#include <cassert>

#include "audio/notification_tts_origin.h"
#include "audio/tts_playback_session.h"

struct CompletionHarness {
    TtsReturnState state = TtsReturnState::kIdle;
    bool voice_processing_enabled = false;
    int listen_start_count = 0;

    void Apply(const TtsCompletionResult& result) {
        if (!result.finalized) return;
        state = result.return_state;
        if (state == TtsReturnState::kListening) {
            voice_processing_enabled = true;
            ++listen_start_count;
        } else {
            voice_processing_enabled = false;
        }
    }
};

static void idle_notification_done_returns_idle_without_listening() {
    TtsPlaybackSession session;
    CompletionHarness harness;
    const auto start = session.Start("notification-done", TtsReturnState::kIdle);
    assert(session.MarkPlaying(start.generation));
    assert(session.BeginDraining("notification-done", start.generation));

    harness.Apply(session.Complete(start.generation, "done", ""));

    assert(harness.state == TtsReturnState::kIdle);
    assert(!harness.voice_processing_enabled);
    assert(harness.listen_start_count == 0);
}

static void idle_notification_error_returns_idle_without_listening() {
    TtsPlaybackSession session;
    CompletionHarness harness;
    const auto start = session.Start("notification-error", TtsReturnState::kIdle);

    harness.Apply(session.Fail(start.generation, "decode_failed"));

    assert(harness.state == TtsReturnState::kIdle);
    assert(!harness.voice_processing_enabled);
    assert(harness.listen_start_count == 0);
}

static void conversational_tts_restores_captured_listening_state() {
    TtsPlaybackSession session;
    CompletionHarness harness;
    const auto start = session.Start("conversation", TtsReturnState::kListening);
    assert(session.MarkPlaying(start.generation));
    assert(session.BeginDraining("conversation", start.generation));

    harness.Apply(session.Complete(start.generation, "done", ""));

    assert(harness.state == TtsReturnState::kListening);
    assert(harness.voice_processing_enabled);
    assert(harness.listen_start_count == 1);
}

static void stale_generation_cannot_restore_its_return_state() {
    TtsPlaybackSession session;
    CompletionHarness harness;
    const auto old_start = session.Start("old", TtsReturnState::kIdle);
    const auto current_start = session.Start("current", TtsReturnState::kListening);

    harness.Apply(session.Fail(old_start.generation, "decode_failed"));
    assert(harness.state == TtsReturnState::kIdle);
    assert(!harness.voice_processing_enabled);
    assert(harness.listen_start_count == 0);

    harness.Apply(session.Fail(current_start.generation, "decode_failed"));
    assert(harness.state == TtsReturnState::kListening);
    assert(harness.voice_processing_enabled);
    assert(harness.listen_start_count == 1);
}

static void pending_notification_origin_survives_old_close_and_returns_idle() {
    for (const bool fail : {false, true}) {
        NotificationTtsOrigin origin;
        CompletionHarness harness;
        const auto token = origin.BeginOpenIntent();
        assert(origin.IsCurrent(token));
        assert(origin.ConsumeForTtsStart());

        TtsPlaybackSession session;
        const auto start = session.Start("notification", TtsReturnState::kIdle);
        if (fail) {
            harness.Apply(session.Fail(start.generation, "decode_failed"));
        } else {
            assert(session.MarkPlaying(start.generation));
            assert(session.BeginDraining("notification", start.generation));
            harness.Apply(session.Complete(start.generation, "done", ""));
        }
        assert(harness.state == TtsReturnState::kIdle);
        assert(!harness.voice_processing_enabled);
        assert(harness.listen_start_count == 0);
    }
}

int main() {
    idle_notification_done_returns_idle_without_listening();
    idle_notification_error_returns_idle_without_listening();
    conversational_tts_restores_captured_listening_state();
    stale_generation_cannot_restore_its_return_state();
    pending_notification_origin_survives_old_close_and_returns_idle();
    return 0;
}
