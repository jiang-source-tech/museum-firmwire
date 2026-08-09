#ifndef OTA_RELEASE_AUDIT_H
#define OTA_RELEASE_AUDIT_H

#include <string>

// Persists only release-identifying metadata; no credentials, URLs, tokens or
// user data are included in the existing OTA check request's ota_report.
void OtaReleaseAuditStageRelease(const std::string& release_id,
                                 const std::string& version,
                                 const std::string& sha256);
void OtaReleaseAuditRecordOutcome(const char* outcome);
void OtaReleaseAuditReconcileRunningImage();

// Returns an empty string when this device has not staged a tracked release;
// otherwise returns one complete JSON object suitable for a top-level
// `ota_report` property in the existing check POST body.
std::string OtaReleaseAuditBuildReportJson();

#endif  // OTA_RELEASE_AUDIT_H
