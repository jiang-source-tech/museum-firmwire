#include "boot_diagnostics.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs.h>
#include <stdio.h>
#include <string.h>

#define TAG "BootDiagnostics"

static constexpr const char* k_namespace = "boot_diag";
static constexpr const char* k_current_trace_key = "current";
static constexpr const char* k_previous_trace_key = "previous";
static constexpr const char* k_current_battery_key = "cur_bat";
static constexpr const char* k_previous_battery_key = "prev_bat";

static char s_trace[BOOT_DIAGNOSTICS_TRACE_MAX] = {};
static bool s_started = false;
static bool s_on_battery = false;
static bool s_flushed_once = false;

static bool ReadTraceKey(
    const char* trace_key,
    const char* battery_key,
    char* buffer,
    size_t buffer_size,
    bool* on_battery
) {
    if (buffer == nullptr || buffer_size == 0) {
        return false;
    }

    buffer[0] = '\0';
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(k_namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t length = buffer_size;
    err = nvs_get_str(handle, trace_key, buffer, &length);
    uint8_t battery = 0;
    esp_err_t battery_err = nvs_get_u8(handle, battery_key, &battery);
    nvs_close(handle);

    if (on_battery != nullptr) {
        *on_battery = battery_err == ESP_OK && battery != 0;
    }

    return err == ESP_OK && buffer[0] != '\0';
}

static void PersistCurrentTrace() {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(k_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open failed while persisting trace: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_str(handle, k_current_trace_key, s_trace);
    nvs_set_u8(handle, k_current_battery_key, s_on_battery ? 1 : 0);
    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "commit failed while persisting trace: %s", esp_err_to_name(err));
    }
}

static void PersistPreviousTrace(const char* trace, bool on_battery) {
    if (trace == nullptr || trace[0] == '\0') {
        return;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(k_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open failed while persisting previous trace: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_str(handle, k_previous_trace_key, trace);
    nvs_set_u8(handle, k_previous_battery_key, on_battery ? 1 : 0);
    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "commit failed while persisting previous trace: %s", esp_err_to_name(err));
    }
}

static void AppendText(const char* text) {
    if (text == nullptr) {
        return;
    }
    const size_t used = strlen(s_trace);
    if (used < sizeof(s_trace) - 1) {
        strncat(s_trace, text, sizeof(s_trace) - used - 1);
    }
}

static void AppendUnsigned(unsigned long long value) {
    char digits[24] = {};
    size_t index = sizeof(digits);
    digits[--index] = '\0';
    do {
        digits[--index] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && index > 0);
    AppendText(&digits[index]);
}

static void AppendSigned(int value) {
    if (value < 0) {
        AppendText("-");
        AppendUnsigned((unsigned long long)(-(long long)value));
        return;
    }
    AppendUnsigned((unsigned long long)value);
}

static void AppendTraceEntry(const char* stage, bool has_error, int error_code) {
    if (stage == nullptr || stage[0] == '\0') {
        return;
    }

    AppendUnsigned((unsigned long long)(esp_timer_get_time() / 1000));
    AppendText(":");
    AppendText(stage);
    if (has_error) {
        AppendText("=");
        AppendSigned(error_code);
    }
    AppendText(";");

    if (s_flushed_once) {
        PersistCurrentTrace();
    }
}

void BootDiagnosticsStart(bool on_battery) {
    s_started = true;
    s_flushed_once = false;
    s_on_battery = on_battery;
    s_trace[0] = '\0';
    BootDiagnosticsMark(on_battery ? "boot_start_battery" : "boot_start_usb");
}

void BootDiagnosticsMark(const char* stage) {
    if (!s_started) {
        s_started = true;
        s_on_battery = false;
        s_trace[0] = '\0';
    }
    AppendTraceEntry(stage, false, 0);
}

void BootDiagnosticsMarkError(const char* stage, int error_code) {
    if (!s_started) {
        s_started = true;
        s_on_battery = false;
        s_trace[0] = '\0';
    }
    AppendTraceEntry(stage, true, error_code);
}

void BootDiagnosticsFlush() {
    if (!s_started) {
        return;
    }

    if (!s_flushed_once) {
        char previous_trace[BOOT_DIAGNOSTICS_TRACE_MAX] = {};
        bool previous_on_battery = false;
        if (BootDiagnosticsReadCurrent(previous_trace, sizeof(previous_trace), &previous_on_battery)) {
            PersistPreviousTrace(previous_trace, previous_on_battery);
        }
        s_flushed_once = true;
    }
    PersistCurrentTrace();
}

bool BootDiagnosticsReadPrevious(char* buffer, size_t buffer_size, bool* on_battery) {
    return ReadTraceKey(
        k_previous_trace_key,
        k_previous_battery_key,
        buffer,
        buffer_size,
        on_battery
    );
}

bool BootDiagnosticsReadCurrent(char* buffer, size_t buffer_size, bool* on_battery) {
    return ReadTraceKey(
        k_current_trace_key,
        k_current_battery_key,
        buffer,
        buffer_size,
        on_battery
    );
}
