#include "device_status.h"

#include "board.h"
#include "runtime_health.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_random.h>
#include <esp_system.h>

#include <cinttypes>
#include <cstdio>
#include <string>

namespace xiaoxin {

namespace {

std::string BuildBootId() {
    xiaoxin_runtime_health_snapshot_t snapshot = {};
    RuntimeHealthReadSnapshot(&snapshot);

    char boot_id[48] = {};
    std::snprintf(
        boot_id,
        sizeof(boot_id),
        "%08" PRIx32 "-%08" PRIx32 "-%08" PRIx32,
        snapshot.boot_count,
        esp_random(),
        esp_random()
    );
    return boot_id;
}

const char* CurrentBootId() {
    static const std::string boot_id = BuildBootId();
    return boot_id.c_str();
}

}  // namespace

cJSON* BuildDeviceStatusJson() {
    cJSON* status = cJSON_CreateObject();
    const esp_app_desc_t* app_desc = esp_app_get_description();
    cJSON_AddStringToObject(status, "firmware_version", app_desc->version);

    xiaoxin_runtime_health_snapshot_t snapshot = {};
    const bool has_runtime_health = RuntimeHealthReadSnapshot(&snapshot);
    cJSON_AddStringToObject(status, "boot_id", CurrentBootId());
    cJSON_AddStringToObject(
        status,
        "reset_reason",
        has_runtime_health
            ? xiaoxin_runtime_health_reset_label(snapshot.last_reset_kind)
            : "unknown"
    );

    int battery_percent = 0;
    bool charging = false;
    bool discharging = false;
    if (Board::GetInstance().GetBatteryLevel(
            battery_percent, charging, discharging)) {
        cJSON_AddNumberToObject(status, "battery_percent", battery_percent);
    }

    int battery_level = 0;
    if (Board::GetInstance().GetBatteryDisplayLevel(battery_level)) {
        cJSON_AddNumberToObject(status, "battery_level", battery_level);
    }
    return status;
}

}  // namespace xiaoxin
