#ifndef OTA_RELEASE_AUDIT_MODEL_H
#define OTA_RELEASE_AUDIT_MODEL_H

#include <string>

enum class OtaReleaseOutcome {
    kNone,
    kPending,
    kCommitted,
    kRolledBack,
    kFailed,
};

inline const char* OtaReleaseOutcomeName(OtaReleaseOutcome outcome) {
    switch (outcome) {
        case OtaReleaseOutcome::kPending:
            return "pending";
        case OtaReleaseOutcome::kCommitted:
            return "committed";
        case OtaReleaseOutcome::kRolledBack:
            return "rolled_back";
        case OtaReleaseOutcome::kFailed:
            return "failed";
        case OtaReleaseOutcome::kNone:
        default:
            return "";
    }
}

inline OtaReleaseOutcome ParseOtaReleaseOutcome(const std::string& value) {
    if (value == "pending") return OtaReleaseOutcome::kPending;
    if (value == "committed") return OtaReleaseOutcome::kCommitted;
    if (value == "rolled_back") return OtaReleaseOutcome::kRolledBack;
    if (value == "failed") return OtaReleaseOutcome::kFailed;
    return OtaReleaseOutcome::kNone;
}

// A pending staged release has booted successfully only when its expected
// version is running and the ESP-IDF rollback state is no longer pending.
// Any other version after a reboot is treated as a rollback.
inline OtaReleaseOutcome InferPendingOtaReleaseOutcome(
    const std::string& staged_version,
    const std::string& running_version,
    bool running_partition_pending_verification) {
    if (staged_version.empty() || running_version != staged_version) {
        return OtaReleaseOutcome::kRolledBack;
    }
    return running_partition_pending_verification
               ? OtaReleaseOutcome::kPending
               : OtaReleaseOutcome::kCommitted;
}

#endif  // OTA_RELEASE_AUDIT_MODEL_H
