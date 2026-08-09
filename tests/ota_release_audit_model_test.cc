#include <assert.h>
#include <stdio.h>

#include "ota_release_audit_model.h"

int main() {
    assert(ParseOtaReleaseOutcome("pending") == OtaReleaseOutcome::kPending);
    assert(ParseOtaReleaseOutcome("unknown") == OtaReleaseOutcome::kNone);
    assert(std::string(OtaReleaseOutcomeName(OtaReleaseOutcome::kCommitted)) == "committed");

    assert(InferPendingOtaReleaseOutcome("1.2.3", "1.2.3", true) ==
           OtaReleaseOutcome::kPending);
    assert(InferPendingOtaReleaseOutcome("1.2.3", "1.2.3", false) ==
           OtaReleaseOutcome::kCommitted);
    assert(InferPendingOtaReleaseOutcome("1.2.3", "1.2.2", true) ==
           OtaReleaseOutcome::kRolledBack);
    assert(InferPendingOtaReleaseOutcome("", "1.2.3", false) ==
           OtaReleaseOutcome::kRolledBack);

    puts("ota release audit model tests passed");
    return 0;
}
