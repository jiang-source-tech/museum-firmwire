#ifndef OTA_PENDING_VALIDATION_POLICY_H
#define OTA_PENDING_VALIDATION_POLICY_H

#include "ota_validation_gate.h"

// Backend reachability is useful release telemetry, but rolling back to an
// older image cannot repair a transient backend, DNS, or Wi-Fi failure.
class OtaPendingValidationPolicy {
public:
    void Arm(bool pending_verification, uint64_t now_seconds) {
        gate_.Arm(pending_verification, now_seconds);
    }

    bool IsArmed() const {
        return gate_.IsArmed();
    }

    void Disarm() {
        gate_.Disarm();
    }

    OtaValidationAction Evaluate(uint64_t now_seconds, bool local_runtime_ready) {
        return gate_.Evaluate(now_seconds, local_runtime_ready);
    }

private:
    OtaValidationGate gate_;
};

#endif  // OTA_PENDING_VALIDATION_POLICY_H
