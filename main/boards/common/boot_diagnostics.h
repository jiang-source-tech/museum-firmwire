#ifndef BOOT_DIAGNOSTICS_H
#define BOOT_DIAGNOSTICS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_DIAGNOSTICS_TRACE_MAX 1024

void BootDiagnosticsStart(bool on_battery);
void BootDiagnosticsMark(const char* stage);
void BootDiagnosticsMarkError(const char* stage, int error_code);
void BootDiagnosticsFlush(void);
bool BootDiagnosticsReadPrevious(char* buffer, size_t buffer_size, bool* on_battery);
bool BootDiagnosticsReadCurrent(char* buffer, size_t buffer_size, bool* on_battery);

#ifdef __cplusplus
}
#endif

#endif
