#ifndef OTA_VALIDATION_GATE_H
#define OTA_VALIDATION_GATE_H

#include <cstdint>

// The bootloader treats a newly selected OTA image as pending until the
// application explicitly confirms it.  Keep the decision logic independent
// from ESP-IDF so its timing and failure behaviour are host-testable.
enum class OtaValidationAction {
    kWait,
    kConfirm,
    kRollback,
};

class OtaValidationGate {
public:
    static constexpr uint32_t kHealthWindowSeconds = 30;
    static constexpr uint32_t kRollbackDeadlineSeconds = 120;

    void Arm(bool pending_verification, uint64_t now_seconds) {
        pending_verification_ = pending_verification;
        armed_at_seconds_ = now_seconds;
        healthy_since_seconds_ = kNoTimestamp;
    }

    bool IsArmed() const {
        return pending_verification_;
    }

    void Disarm() {
        pending_verification_ = false;
        healthy_since_seconds_ = kNoTimestamp;
    }

    OtaValidationAction Evaluate(uint64_t now_seconds, bool prerequisites_healthy) {
        if (!pending_verification_) {
            return OtaValidationAction::kWait;
        }

        if (!prerequisites_healthy) {
            healthy_since_seconds_ = kNoTimestamp;
        } else if (healthy_since_seconds_ == kNoTimestamp) {
            healthy_since_seconds_ = now_seconds;
        }

        if (healthy_since_seconds_ != kNoTimestamp &&
            now_seconds - healthy_since_seconds_ >= kHealthWindowSeconds) {
            return OtaValidationAction::kConfirm;
        }

        if (now_seconds - armed_at_seconds_ >= kRollbackDeadlineSeconds) {
            return OtaValidationAction::kRollback;
        }

        return OtaValidationAction::kWait;
    }

private:
    static constexpr uint64_t kNoTimestamp = UINT64_MAX;

    bool pending_verification_ = false;
    uint64_t armed_at_seconds_ = 0;
    uint64_t healthy_since_seconds_ = kNoTimestamp;
};

#endif  // OTA_VALIDATION_GATE_H
