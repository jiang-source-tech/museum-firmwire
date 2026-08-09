#include "runtime_health.h"

#include "system_info.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs.h>

#define TAG "RuntimeHealth"

static constexpr const char* k_namespace = "runtime_health";
static constexpr const char* k_boot_count_key = "boot_count";
static constexpr const char* k_brownout_count_key = "brownout_count";
static constexpr const char* k_poweron_count_key = "poweron_count";
static constexpr const char* k_software_reset_count_key = "soft_count";
static constexpr const char* k_watchdog_count_key = "wdt_count";
static constexpr const char* k_panic_count_key = "panic_count";
static constexpr const char* k_short_run_streak_key = "short_streak";
static constexpr const char* k_last_runtime_sec_key = "last_sec";
static constexpr const char* k_max_runtime_sec_key = "max_sec";
static constexpr const char* k_current_checkpoint_sec_key = "cur_sec";
static constexpr const char* k_current_on_battery_key = "cur_bat";
static constexpr const char* k_previous_on_battery_key = "prev_bat";
static constexpr const char* k_last_reset_kind_key = "last_reset";
static constexpr const char* k_low_battery_shutdown_count_key = "lb_count";
static constexpr const char* k_last_low_battery_voltage_key = "lb_mv";
static constexpr const char* k_last_low_battery_stage_key = "lb_startup";

static constexpr uint32_t k_short_runtime_threshold_sec = 60U;

static xiaoxin_runtime_health_record_t s_record = {};
static bool s_started = false;

static xiaoxin_runtime_reset_kind_t MapResetReason(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:
            return XIAOXIN_RUNTIME_RESET_POWERON;
        case ESP_RST_EXT:
        case ESP_RST_USB:
        case ESP_RST_JTAG:
            return XIAOXIN_RUNTIME_RESET_EXTERNAL;
        case ESP_RST_BROWNOUT:
        case ESP_RST_PWR_GLITCH:
            return XIAOXIN_RUNTIME_RESET_BROWNOUT;
        case ESP_RST_SW:
            return XIAOXIN_RUNTIME_RESET_SOFTWARE;
        case ESP_RST_PANIC:
            return XIAOXIN_RUNTIME_RESET_PANIC;
        case ESP_RST_TASK_WDT:
        case ESP_RST_INT_WDT:
        case ESP_RST_WDT:
            return XIAOXIN_RUNTIME_RESET_WATCHDOG;
        case ESP_RST_DEEPSLEEP:
            return XIAOXIN_RUNTIME_RESET_DEEPSLEEP;
        default:
            return XIAOXIN_RUNTIME_RESET_UNKNOWN;
    }
}

static bool ReadU32(nvs_handle_t handle, const char* key, uint32_t* value) {
    uint32_t stored = 0;
    const esp_err_t err = nvs_get_u32(handle, key, &stored);
    if (err == ESP_OK) {
        *value = stored;
        return true;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "read %s failed: %s", key, esp_err_to_name(err));
    }
    return false;
}

static void WriteU32(nvs_handle_t handle, const char* key, uint32_t value) {
    const esp_err_t err = nvs_set_u32(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "write %s failed: %s", key, esp_err_to_name(err));
    }
}

static void LoadRecord(void) {
    s_record = {};

    nvs_handle_t handle = 0;
    const esp_err_t err = nvs_open(k_namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "open failed while loading record: %s", esp_err_to_name(err));
        }
        return;
    }

    uint32_t value = 0;
    ReadU32(handle, k_boot_count_key, &s_record.boot_count);
    ReadU32(handle, k_brownout_count_key, &s_record.brownout_count);
    ReadU32(handle, k_poweron_count_key, &s_record.poweron_count);
    ReadU32(handle, k_software_reset_count_key, &s_record.software_reset_count);
    ReadU32(handle, k_watchdog_count_key, &s_record.watchdog_count);
    ReadU32(handle, k_panic_count_key, &s_record.panic_count);
    ReadU32(handle, k_short_run_streak_key, &s_record.short_run_streak);
    ReadU32(handle, k_last_runtime_sec_key, &s_record.last_runtime_sec);
    ReadU32(handle, k_max_runtime_sec_key, &s_record.max_runtime_sec);
    ReadU32(handle, k_current_checkpoint_sec_key, &s_record.current_checkpoint_sec);
    ReadU32(handle, k_low_battery_shutdown_count_key, &s_record.low_battery_shutdown_count);
    ReadU32(handle, k_last_low_battery_voltage_key, &s_record.last_low_battery_shutdown_voltage_mv);
    if (ReadU32(handle, k_current_on_battery_key, &value)) {
        s_record.current_on_battery = value != 0;
    }
    if (ReadU32(handle, k_previous_on_battery_key, &value)) {
        s_record.previous_on_battery = value != 0;
    }
    if (ReadU32(handle, k_last_low_battery_stage_key, &value)) {
        s_record.last_low_battery_shutdown_startup_stage = value != 0;
    }
    if (ReadU32(handle, k_last_reset_kind_key, &value)) {
        s_record.last_reset_kind = (xiaoxin_runtime_reset_kind_t)value;
    }

    nvs_close(handle);
}

static void PersistRecord(void) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(k_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open failed while persisting record: %s", esp_err_to_name(err));
        return;
    }

    WriteU32(handle, k_boot_count_key, s_record.boot_count);
    WriteU32(handle, k_brownout_count_key, s_record.brownout_count);
    WriteU32(handle, k_poweron_count_key, s_record.poweron_count);
    WriteU32(handle, k_software_reset_count_key, s_record.software_reset_count);
    WriteU32(handle, k_watchdog_count_key, s_record.watchdog_count);
    WriteU32(handle, k_panic_count_key, s_record.panic_count);
    WriteU32(handle, k_short_run_streak_key, s_record.short_run_streak);
    WriteU32(handle, k_last_runtime_sec_key, s_record.last_runtime_sec);
    WriteU32(handle, k_max_runtime_sec_key, s_record.max_runtime_sec);
    WriteU32(handle, k_current_checkpoint_sec_key, s_record.current_checkpoint_sec);
    WriteU32(handle, k_low_battery_shutdown_count_key, s_record.low_battery_shutdown_count);
    WriteU32(handle, k_last_low_battery_voltage_key, s_record.last_low_battery_shutdown_voltage_mv);
    WriteU32(handle, k_current_on_battery_key, s_record.current_on_battery ? 1U : 0U);
    WriteU32(handle, k_previous_on_battery_key, s_record.previous_on_battery ? 1U : 0U);
    WriteU32(handle, k_last_low_battery_stage_key, s_record.last_low_battery_shutdown_startup_stage ? 1U : 0U);
    WriteU32(handle, k_last_reset_kind_key, (uint32_t)s_record.last_reset_kind);

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "commit failed while persisting record: %s", esp_err_to_name(err));
    }
}

void RuntimeHealthStart(bool on_battery) {
    LoadRecord();
    xiaoxin_runtime_health_apply_boot(
        &s_record,
        MapResetReason(esp_reset_reason()),
        on_battery,
        k_short_runtime_threshold_sec
    );
    s_started = true;
    PersistRecord();
}

void RuntimeHealthMaybeCheckpoint(void) {
    if (!s_started) {
        return;
    }

    const uint32_t now_sec = SystemInfo::GetUptimeSeconds();
    if (!xiaoxin_runtime_health_should_checkpoint(s_record.current_checkpoint_sec, now_sec)) {
        return;
    }
    s_record.current_checkpoint_sec = now_sec;
    PersistRecord();
}

void RuntimeHealthForceCheckpoint(void) {
    if (!s_started) {
        return;
    }

    s_record.current_checkpoint_sec = SystemInfo::GetUptimeSeconds();
    PersistRecord();
}

void RuntimeHealthRecordLowBatteryShutdown(int voltage_mv, bool startup_stage) {
    if (!s_started) {
        LoadRecord();
    }
    xiaoxin_runtime_health_record_low_battery_shutdown(
        &s_record,
        voltage_mv > 0 ? (uint32_t)voltage_mv : 0,
        startup_stage
    );
    PersistRecord();
}

bool RuntimeHealthReadSnapshot(xiaoxin_runtime_health_snapshot_t* out) {
    if (out == nullptr) {
        return false;
    }

    xiaoxin_runtime_health_record_t record = s_record;
    if (!s_started) {
        LoadRecord();
        record = s_record;
    }
    record.current_checkpoint_sec = SystemInfo::GetUptimeSeconds();
    xiaoxin_runtime_health_snapshot_from_record(&record, out);
    return true;
}

bool RuntimeHealthProtectionRecommended(void) {
    return xiaoxin_runtime_health_protection_recommended(&s_record);
}
