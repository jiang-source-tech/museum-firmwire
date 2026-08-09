#include <assert.h>
#include <memory>
#include <string>
#include <vector>

#include "tts_playback_session.h"

static std::unique_ptr<AudioStreamPacket> packet(uint8_t id) {
    auto value = std::make_unique<AudioStreamPacket>();
    value->payload = {id};
    return value;
}

static void new_start_enters_preparing_and_duplicate_is_idempotent() {
    TtsPlaybackSession session;
    auto first = session.Start("s1");
    auto duplicate = session.Start("s1");
    assert(first.action == TtsStartAction::kPrepare);
    assert(duplicate.action == TtsStartAction::kContinuePreparing);
    assert(first.generation == duplicate.generation);
}

static void final_sentence_replays_ack_without_restarting() {
    TtsPlaybackSession session;
    auto first = session.Start("s1");
    assert(session.MarkPlaying(first.generation));
    assert(session.BeginDraining("s1", first.generation));
    assert(session.Complete(first.generation, "done", ""));
    auto replay = session.Start("s1");
    assert(replay.action == TtsStartAction::kReplayFinal);
    assert(replay.final_ack.state == "done");
}

static void failed_sentence_replays_the_same_error_without_restarting() {
    TtsPlaybackSession session;
    auto first = session.Start("s1");
    assert(session.Fail(first.generation, "preroll_overflow"));
    auto replay = session.Start("s1");
    assert(replay.action == TtsStartAction::kReplayFinal);
    assert(replay.final_ack.state == "error");
    assert(replay.final_ack.reason == "preroll_overflow");
}

static void aborted_sentence_requires_a_new_sentence_id() {
    TtsPlaybackSession session;
    session.Start("s1");
    session.AbortCurrent("connection_closed");
    assert(session.Start("s1").action == TtsStartAction::kRejectStale);
    assert(session.Start("s2").action == TtsStartAction::kPrepare);
}

static void older_completed_sentence_stays_terminal_after_newer_completion() {
    TtsPlaybackSession session;
    auto first = session.Start("s1");
    assert(session.MarkPlaying(first.generation));
    assert(session.BeginDraining("s1", first.generation));
    assert(session.Complete(first.generation, "done", ""));
    auto second = session.Start("s2");
    assert(session.MarkPlaying(second.generation));
    assert(session.BeginDraining("s2", second.generation));
    assert(session.Complete(second.generation, "done", ""));
    assert(session.Start("s1").action == TtsStartAction::kReplayFinal);
}

static void ingress_preserves_order_when_sink_temporarily_fills() {
    TtsPlaybackSession session;
    auto start = session.Start("s1");
    assert(session.Enqueue(packet(1)) == TtsIngressResult::kAccepted);
    assert(session.Enqueue(packet(2)) == TtsIngressResult::kAccepted);
    assert(session.Enqueue(packet(3)) == TtsIngressResult::kAccepted);
    assert(session.Enqueue(packet(4)) == TtsIngressResult::kAccepted);
    assert(session.Enqueue(packet(5)) == TtsIngressResult::kAccepted);
    assert(session.MarkPlaying(start.generation));
    std::vector<uint8_t> output;
    int available = 1;
    auto sink = [&output, &available](std::unique_ptr<AudioStreamPacket>& item) {
        if (available == 0) return false;
        output.push_back(item->payload.front());
        item.reset();
        --available;
        return true;
    };
    assert(session.Pump(sink) == 1);
    assert(output == std::vector<uint8_t>({1}));
    available = 1;
    assert(session.Enqueue(packet(6)) == TtsIngressResult::kAccepted);
    assert(session.Pump(sink) == 1);
    available = 1;
    assert(session.Pump(sink) == 1);
    assert(output == std::vector<uint8_t>({1, 2, 3}));
}

static void playback_waits_for_initial_preroll_before_pumping() {
    TtsPlaybackSession session;
    auto start = session.Start("s1");
    assert(session.MarkPlaying(start.generation));

    std::vector<uint8_t> output;
    auto sink = [&output](std::unique_ptr<AudioStreamPacket>& item) {
        output.push_back(item->payload.front());
        item.reset();
        return true;
    };

    for (uint8_t id = 1; id <= 4; ++id) {
        assert(session.Enqueue(packet(id)) == TtsIngressResult::kAccepted);
    }
    assert(session.Pump(sink) == 0);
    assert(output.empty());
    assert(session.buffered_packets() == 4);

    assert(session.Enqueue(packet(5)) == TtsIngressResult::kAccepted);
    assert(session.Pump(sink) == 5);
    assert(output == std::vector<uint8_t>({1, 2, 3, 4, 5}));
}

static void stopping_short_sentence_flushes_partial_preroll() {
    TtsPlaybackSession session;
    auto start = session.Start("s1");
    assert(session.MarkPlaying(start.generation));
    assert(session.Enqueue(packet(1)) == TtsIngressResult::kAccepted);
    assert(session.Enqueue(packet(2)) == TtsIngressResult::kAccepted);

    std::vector<uint8_t> output;
    auto sink = [&output](std::unique_ptr<AudioStreamPacket>& item) {
        output.push_back(item->payload.front());
        item.reset();
        return true;
    };

    assert(session.Pump(sink) == 0);
    assert(session.BeginDraining("s1", start.generation));
    assert(session.Pump(sink) == 2);
    assert(output == std::vector<uint8_t>({1, 2}));
}

static void eighty_fifth_buffered_packet_overflows_without_dropping_head() {
    TtsPlaybackSession session;
    session.Start("s1");
    for (int i = 0; i < 84; ++i) {
        assert(session.Enqueue(packet(static_cast<uint8_t>(i))) == TtsIngressResult::kAccepted);
    }
    assert(session.Enqueue(packet(84)) == TtsIngressResult::kOverflow);
    assert(session.buffered_packets() == 84);
}

int main() {
    new_start_enters_preparing_and_duplicate_is_idempotent();
    final_sentence_replays_ack_without_restarting();
    failed_sentence_replays_the_same_error_without_restarting();
    aborted_sentence_requires_a_new_sentence_id();
    older_completed_sentence_stays_terminal_after_newer_completion();
    ingress_preserves_order_when_sink_temporarily_fills();
    playback_waits_for_initial_preroll_before_pumping();
    stopping_short_sentence_flushes_partial_preroll();
    eighty_fifth_buffered_packet_overflows_without_dropping_head();
    return 0;
}
