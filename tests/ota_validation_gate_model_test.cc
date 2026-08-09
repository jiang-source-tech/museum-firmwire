#include <assert.h>
#include <stdio.h>

#include "ota_pending_validation_policy.h"

int main() {
    OtaPendingValidationPolicy policy;
    constexpr bool kLocalRuntimeReady = true;
    constexpr bool kLocalRuntimeNotReady = false;

    policy.Arm(false, 0);
    assert(policy.Evaluate(999, kLocalRuntimeReady) == OtaValidationAction::kWait);

    // A backend outage must not roll back an otherwise stable image.
    policy.Arm(true, 0);
    assert(policy.Evaluate(0, kLocalRuntimeReady) == OtaValidationAction::kWait);
    assert(policy.Evaluate(29, kLocalRuntimeReady) == OtaValidationAction::kWait);
    assert(policy.Evaluate(30, kLocalRuntimeReady) == OtaValidationAction::kConfirm);

    // Losing Wi-Fi after an initially successful probe remains diagnostic.
    policy.Arm(true, 0);
    assert(policy.Evaluate(0, kLocalRuntimeReady) == OtaValidationAction::kWait);
    assert(policy.Evaluate(10, kLocalRuntimeReady) == OtaValidationAction::kWait);
    assert(policy.Evaluate(30, kLocalRuntimeReady) == OtaValidationAction::kConfirm);

    policy.Arm(true, 0);
    assert(policy.Evaluate(119, kLocalRuntimeNotReady) == OtaValidationAction::kWait);
    assert(policy.Evaluate(120, kLocalRuntimeNotReady) == OtaValidationAction::kRollback);

    puts("ota validation gate model tests passed");
    return 0;
}
