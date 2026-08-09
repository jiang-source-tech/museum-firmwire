#pragma once

enum class TimeSyncStatus {
    Idle,
    Syncing,
    Synced,
};

void MarkTimeSyncStarted();
void MarkTimeSyncSucceeded();
TimeSyncStatus GetTimeSyncStatus();
