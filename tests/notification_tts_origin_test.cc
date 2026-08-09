#include <cassert>
#include <optional>

#include "audio/notification_tts_origin.h"
#include "audio/tts_playback_session.h"

static TtsCompletionResult complete(
    TtsPlaybackSession& session,
    const char* sentence,
    uint32_t generation) {
    assert(session.MarkPlaying(generation));
    assert(session.BeginDraining(sentence, generation));
    return session.Complete(generation, "done", "");
}

static void old_close_cannot_clear_newer_notification_open_intent() {
    NotificationTtsOrigin origin;
    const auto old_intent = origin.BeginOpenIntent();
    const auto new_intent = origin.BeginOpenIntent();
    assert(!origin.ClearOpenIntent(old_intent));
    assert(origin.IsCurrent(new_intent));
    assert(origin.ConsumeForTtsStart());
    assert(!origin.HasOpenIntent());
}

static void failed_or_cancelled_open_only_clears_its_own_token() {
    NotificationTtsOrigin origin;
    const auto stale = origin.BeginOpenIntent();
    const auto current = origin.BeginOpenIntent();
    assert(!origin.ClearOpenIntent(stale));
    assert(origin.IsCurrent(current));
    assert(origin.ClearOpenIntent(current));
    assert(!origin.HasOpenIntent());

    TtsPlaybackSession ordinary;
    const auto start = ordinary.Start(
        "ordinary-conversation", TtsReturnState::kListening);
    const auto result = ordinary.Fail(start.generation, "decode_failed");
    assert(result.finalized);
    assert(result.return_state == TtsReturnState::kListening);
}

static void notification_supersession_inherits_idle_for_done_and_error() {
    {
        TtsPlaybackSession session;
        const auto first = session.Start("notification-a", TtsReturnState::kIdle);
        assert(session.MarkPlaying(first.generation));
        const auto second = session.Start("notification-b", std::nullopt);
        const auto result = complete(session, "notification-b", second.generation);
        assert(result.finalized);
        assert(result.return_state == TtsReturnState::kIdle);
    }
    {
        TtsPlaybackSession session;
        const auto first = session.Start("notification-a", TtsReturnState::kIdle);
        assert(session.MarkPlaying(first.generation));
        const auto second = session.Start("notification-b", std::nullopt);
        const auto result = session.Fail(second.generation, "decode_failed");
        assert(result.finalized);
        assert(result.return_state == TtsReturnState::kIdle);
    }
}

static void conversational_supersession_stays_listening() {
    TtsPlaybackSession session;
    const auto first = session.Start("conversation-a", TtsReturnState::kListening);
    assert(session.MarkPlaying(first.generation));
    const auto second = session.Start("conversation-b", std::nullopt);
    const auto result = complete(session, "conversation-b", second.generation);
    assert(result.finalized);
    assert(result.return_state == TtsReturnState::kListening);
}

static void explicit_new_origin_can_override_inheritance() {
    TtsPlaybackSession session;
    const auto first = session.Start("conversation", TtsReturnState::kListening);
    assert(session.MarkPlaying(first.generation));
    const auto second = session.Start("explicit-notification", TtsReturnState::kIdle);
    const auto result = session.Fail(second.generation, "decode_failed");
    assert(result.finalized);
    assert(result.return_state == TtsReturnState::kIdle);
}

int main() {
    old_close_cannot_clear_newer_notification_open_intent();
    failed_or_cancelled_open_only_clears_its_own_token();
    notification_supersession_inherits_idle_for_done_and_error();
    conversational_supersession_stays_listening();
    explicit_new_origin_can_override_inheritance();
    return 0;
}
