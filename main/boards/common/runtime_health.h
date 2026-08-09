#ifndef RUNTIME_HEALTH_H
#define RUNTIME_HEALTH_H

#include "runtime_health_model.h"

void RuntimeHealthStart(bool on_battery);
void RuntimeHealthMaybeCheckpoint(void);
void RuntimeHealthForceCheckpoint(void);
void RuntimeHealthRecordLowBatteryShutdown(int voltage_mv, bool startup_stage);
bool RuntimeHealthReadSnapshot(xiaoxin_runtime_health_snapshot_t* out);
bool RuntimeHealthProtectionRecommended(void);

#endif
