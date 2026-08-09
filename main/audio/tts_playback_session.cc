#include "tts_playback_session.h"

TtsStartDecision TtsPlaybackSession::Start(
    const std::string& sentence_id,
    std::optional<TtsReturnState> explicit_return_state) {
    std::lock_guard<std::mutex> lock(mutex_);
    TtsStartDecision decision;
    decision.generation = generation_;
    if (sentence_id.empty()) return decision;
    const TtsFinalAck completed = FindFinalLocked(sentence_id);
    if (!completed.state.empty()) {
        decision.action = TtsStartAction::kReplayFinal;
        decision.final_ack = completed;
        return decision;
    }
    if (IsStaleLocked(sentence_id)) {
        decision.action = TtsStartAction::kRejectStale;
        return decision;
    }
    if (sentence_id == sentence_id_) {
        if (phase_ == TtsPlaybackPhase::kPreparing) decision.action = TtsStartAction::kContinuePreparing;
        if (phase_ == TtsPlaybackPhase::kPlaying) decision.action = TtsStartAction::kResendReady;
        if (phase_ == TtsPlaybackPhase::kDraining) decision.action = TtsStartAction::kContinueDraining;
        return decision;
    }
    const bool superseding_active = phase_ != TtsPlaybackPhase::kIdle;
    if (superseding_active) {
        decision.superseded_sentence_id = sentence_id_;
        RememberStaleLocked(sentence_id_);
    }
    ingress_queue_.clear();
    sentence_id_ = sentence_id;
    initial_preroll_ready_ = false;
    if (explicit_return_state.has_value()) {
        return_state_ = *explicit_return_state;
    } else if (!superseding_active) {
        return_state_ = TtsReturnState::kIdle;
    }
    phase_ = TtsPlaybackPhase::kPreparing;
    ++generation_;
    decision.action = TtsStartAction::kPrepare;
    decision.generation = generation_;
    ingress_empty_cv_.notify_all();
    return decision;
}

TtsIngressResult TtsPlaybackSession::Enqueue(std::unique_ptr<AudioStreamPacket> packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!packet || (phase_ != TtsPlaybackPhase::kPreparing && phase_ != TtsPlaybackPhase::kPlaying)) {
        return TtsIngressResult::kIgnored;
    }
    if (ingress_queue_.size() >= kMaxBufferedPackets) return TtsIngressResult::kOverflow;
    packet->playback_generation = generation_;
    ingress_queue_.push_back(std::move(packet));
    if (ingress_queue_.size() >= kInitialPrerollPackets) {
        initial_preroll_ready_ = true;
    }
    return TtsIngressResult::kAccepted;
}

size_t TtsPlaybackSession::Pump(const PacketSink& sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (phase_ != TtsPlaybackPhase::kPlaying && phase_ != TtsPlaybackPhase::kDraining) return 0;
    if (phase_ == TtsPlaybackPhase::kPlaying && !initial_preroll_ready_) return 0;
    size_t moved = 0;
    while (!ingress_queue_.empty() && sink(ingress_queue_.front())) {
        ingress_queue_.pop_front();
        ++moved;
    }
    if (ingress_queue_.empty()) ingress_empty_cv_.notify_all();
    return moved;
}

bool TtsPlaybackSession::MarkPlaying(uint32_t generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_ || phase_ != TtsPlaybackPhase::kPreparing) return false;
    phase_ = TtsPlaybackPhase::kPlaying;
    initial_preroll_ready_ =
        initial_preroll_ready_ ||
        ingress_queue_.size() >= kInitialPrerollPackets;
    return true;
}

bool TtsPlaybackSession::BeginDraining(const std::string& sentence_id, uint32_t generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_ || sentence_id != sentence_id_ || phase_ != TtsPlaybackPhase::kPlaying) return false;
    phase_ = TtsPlaybackPhase::kDraining;
    // A short sentence may legitimately contain fewer than the initial
    // preroll target. Once stop arrives, flush the partial preroll so drain
    // completion cannot wait on packets that are being held back.
    initial_preroll_ready_ = true;
    return true;
}

bool TtsPlaybackSession::WaitForIngressEmpty(uint32_t generation, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return ingress_empty_cv_.wait_for(lock, timeout, [this, generation]() {
        return generation != generation_ || ingress_queue_.empty();
    }) && generation == generation_ && ingress_queue_.empty();
}

TtsCompletionResult TtsPlaybackSession::Complete(
    uint32_t generation,
    const std::string& state,
    const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_ || phase_ != TtsPlaybackPhase::kDraining) return {};
    const TtsReturnState return_state = return_state_;
    RememberFinalLocked({sentence_id_, state, reason});
    sentence_id_.clear();
    ingress_queue_.clear();
    phase_ = TtsPlaybackPhase::kIdle;
    initial_preroll_ready_ = false;
    ingress_empty_cv_.notify_all();
    return {true, return_state};
}

TtsCompletionResult TtsPlaybackSession::Fail(
    uint32_t generation, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_ || phase_ == TtsPlaybackPhase::kIdle) return {};
    const TtsReturnState return_state = return_state_;
    RememberFinalLocked({sentence_id_, "error", reason});
    sentence_id_.clear();
    ingress_queue_.clear();
    phase_ = TtsPlaybackPhase::kIdle;
    initial_preroll_ready_ = false;
    ingress_empty_cv_.notify_all();
    return {true, return_state};
}

void TtsPlaybackSession::AbortCurrent(const std::string&) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!sentence_id_.empty()) RememberStaleLocked(sentence_id_);
    sentence_id_.clear();
    ingress_queue_.clear();
    phase_ = TtsPlaybackPhase::kIdle;
    initial_preroll_ready_ = false;
    return_state_ = TtsReturnState::kIdle;
    ++generation_;
    ingress_empty_cv_.notify_all();
}

bool TtsPlaybackSession::IsCurrent(uint32_t generation, const std::string& sentence_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return generation == generation_ && sentence_id == sentence_id_;
}

bool TtsPlaybackSession::OwnsPlaybackPipeline() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return phase_ == TtsPlaybackPhase::kPlaying || phase_ == TtsPlaybackPhase::kDraining;
}

uint32_t TtsPlaybackSession::generation() const { std::lock_guard<std::mutex> lock(mutex_); return generation_; }
std::string TtsPlaybackSession::sentence_id() const { std::lock_guard<std::mutex> lock(mutex_); return sentence_id_; }
TtsPlaybackPhase TtsPlaybackSession::phase() const { std::lock_guard<std::mutex> lock(mutex_); return phase_; }
size_t TtsPlaybackSession::buffered_packets() const { std::lock_guard<std::mutex> lock(mutex_); return ingress_queue_.size(); }
TtsFinalAck TtsPlaybackSession::FinalAckFor(const std::string& sentence_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return FindFinalLocked(sentence_id);
}

TtsFinalAck TtsPlaybackSession::FindFinalLocked(
    const std::string& sentence_id) const {
    for (const auto& ack : final_acks_) {
        if (ack.sentence_id == sentence_id) return ack;
    }
    return {};
}

bool TtsPlaybackSession::IsStaleLocked(const std::string& sentence_id) const {
    for (const auto& stale : stale_sentence_ids_) {
        if (stale == sentence_id) return true;
    }
    return false;
}

void TtsPlaybackSession::RememberFinalLocked(const TtsFinalAck& ack) {
    final_acks_.push_front(ack);
    while (final_acks_.size() > kAckHistorySize) final_acks_.pop_back();
}

void TtsPlaybackSession::RememberStaleLocked(const std::string& sentence_id) {
    if (sentence_id.empty()) return;
    stale_sentence_ids_.push_front(sentence_id);
    while (stale_sentence_ids_.size() > kAckHistorySize) stale_sentence_ids_.pop_back();
}
