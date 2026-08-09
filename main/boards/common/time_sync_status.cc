#include "time_sync_status.h"

static volatile TimeSyncStatus g_time_sync_status = TimeSyncStatus::Idle;

void MarkTimeSyncStarted() {
    if (g_time_sync_status != TimeSyncStatus::Synced) {
        g_time_sync_status = TimeSyncStatus::Syncing;
    }
}

void MarkTimeSyncSucceeded() {
    g_time_sync_status = TimeSyncStatus::Synced;
}

TimeSyncStatus GetTimeSyncStatus() {
    return g_time_sync_status;
}
