#ifndef TTS_PLAYBACK_SESSION_H
#define TTS_PLAYBACK_SESSION_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "protocol.h"

enum class TtsPlaybackPhase { kIdle, kPreparing, kPlaying, kDraining };
enum class TtsReturnState { kIdle, kListening };
enum class TtsStartAction {
    kPrepare,
    kContinuePreparing,
    kResendReady,
    kContinueDraining,
    kReplayFinal,
    kRejectStale,
};
enum class TtsIngressResult { kAccepted, kOverflow, kIgnored };

struct TtsFinalAck {
    std::string sentence_id;
    std::string state;
    std::string reason;
};

struct TtsStartDecision {
    TtsStartAction action = TtsStartAction::kRejectStale;
    uint32_t generation = 0;
    std::string superseded_sentence_id;
    TtsFinalAck final_ack;
};

struct TtsCompletionResult {
    bool finalized = false;
    TtsReturnState return_state = TtsReturnState::kIdle;

    explicit operator bool() const { return finalized; }
};

class TtsPlaybackSession {
public:
    static constexpr size_t kMaxBufferedPackets = 84;
    static constexpr size_t kInitialPrerollPackets = 5;
    static constexpr size_t kAckHistorySize = 8;
    using PacketSink = std::function<bool(std::unique_ptr<AudioStreamPacket>&)>;

    TtsStartDecision Start(
        const std::string& sentence_id,
        std::optional<TtsReturnState> explicit_return_state = std::nullopt);
    TtsIngressResult Enqueue(std::unique_ptr<AudioStreamPacket> packet);
    size_t Pump(const PacketSink& sink);
    bool MarkPlaying(uint32_t generation);
    bool BeginDraining(const std::string& sentence_id, uint32_t generation);
    bool WaitForIngressEmpty(uint32_t generation, std::chrono::milliseconds timeout);
    TtsCompletionResult Complete(
        uint32_t generation,
        const std::string& state,
        const std::string& reason);
    TtsCompletionResult Fail(uint32_t generation, const std::string& reason);
    void AbortCurrent(const std::string& reason);

    bool IsCurrent(uint32_t generation, const std::string& sentence_id) const;
    bool OwnsPlaybackPipeline() const;
    uint32_t generation() const;
    std::string sentence_id() const;
    TtsPlaybackPhase phase() const;
    size_t buffered_packets() const;
    TtsFinalAck FinalAckFor(const std::string& sentence_id) const;

private:
    mutable std::mutex mutex_;
    std::condition_variable ingress_empty_cv_;
    TtsPlaybackPhase phase_ = TtsPlaybackPhase::kIdle;
    uint32_t generation_ = 0;
    TtsReturnState return_state_ = TtsReturnState::kIdle;
    bool initial_preroll_ready_ = false;
    std::string sentence_id_;
    std::deque<std::string> stale_sentence_ids_;
    std::deque<TtsFinalAck> final_acks_;
    std::deque<std::unique_ptr<AudioStreamPacket>> ingress_queue_;

    TtsFinalAck FindFinalLocked(const std::string& sentence_id) const;
    bool IsStaleLocked(const std::string& sentence_id) const;
    void RememberFinalLocked(const TtsFinalAck& ack);
    void RememberStaleLocked(const std::string& sentence_id);
};

#endif
