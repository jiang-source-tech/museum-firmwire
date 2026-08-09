#include "runtime_health_model.h"

#include <stdio.h>
#include <string.h>

static bool is_unstable_reset(xiaoxin_runtime_reset_kind_t reset_kind) {
  return reset_kind == XIAOXIN_RUNTIME_RESET_BROWNOUT ||
         reset_kind == XIAOXIN_RUNTIME_RESET_POWERON;
}

void xiaoxin_runtime_health_apply_boot(
  xiaoxin_runtime_health_record_t* record,
  xiaoxin_runtime_reset_kind_t reset_kind,
  bool current_on_battery,
  uint32_t short_runtime_threshold_sec
) {
  if (record == NULL) {
    return;
  }

  if (record->current_checkpoint_sec > record->max_runtime_sec) {
    record->max_runtime_sec = record->current_checkpoint_sec;
  }

  record->last_runtime_sec = record->current_checkpoint_sec;
  record->previous_on_battery = record->current_on_battery;
  record->current_checkpoint_sec = 0;
  record->current_on_battery = current_on_battery;
  record->boot_count++;
  record->last_reset_kind = reset_kind;

  switch (reset_kind) {
    case XIAOXIN_RUNTIME_RESET_BROWNOUT:
      record->brownout_count++;
      break;
    case XIAOXIN_RUNTIME_RESET_POWERON:
      record->poweron_count++;
      break;
    case XIAOXIN_RUNTIME_RESET_SOFTWARE:
      record->software_reset_count++;
      break;
    case XIAOXIN_RUNTIME_RESET_PANIC:
      record->panic_count++;
      break;
    case XIAOXIN_RUNTIME_RESET_WATCHDOG:
      record->watchdog_count++;
      break;
    case XIAOXIN_RUNTIME_RESET_UNKNOWN:
    case XIAOXIN_RUNTIME_RESET_DEEPSLEEP:
    default:
      break;
  }

  if (is_unstable_reset(reset_kind) &&
      record->previous_on_battery &&
      record->last_runtime_sec < short_runtime_threshold_sec) {
    record->short_run_streak++;
  } else {
    record->short_run_streak = 0;
  }
}

bool xiaoxin_runtime_health_should_checkpoint(
  uint32_t last_saved_sec,
  uint32_t now_sec
) {
  if (now_sec < 60U) {
    return false;
  }

  if (last_saved_sec < 60U) {
    return true;
  }

  return now_sec - last_saved_sec >= 300U;
}

bool xiaoxin_runtime_health_protection_recommended(
  const xiaoxin_runtime_health_record_t* record
) {
  if (record == NULL) {
    return false;
  }

  return record->current_on_battery &&
         is_unstable_reset(record->last_reset_kind) &&
         record->short_run_streak >= 3U;
}

void xiaoxin_runtime_health_format_duration(
  char* buffer,
  size_t buffer_size,
  uint32_t seconds
) {
  if (buffer == NULL || buffer_size == 0U) {
    return;
  }

  if (seconds < 60U) {
    (void)snprintf(buffer, buffer_size, "<1m");
    return;
  }

  if (seconds < 3600U) {
    const uint32_t minutes = seconds / 60U;
    const uint32_t remaining_seconds = seconds % 60U;
    (void)snprintf(
      buffer,
      buffer_size,
      "%um %02us",
      (unsigned)minutes,
      (unsigned)remaining_seconds
    );
    return;
  }

  const uint32_t hours = seconds / 3600U;
  const uint32_t remaining_minutes = (seconds % 3600U) / 60U;
  (void)snprintf(
    buffer,
    buffer_size,
    "%uh %02um",
    (unsigned)hours,
    (unsigned)remaining_minutes
  );
}

const char* xiaoxin_runtime_health_reset_label(
  xiaoxin_runtime_reset_kind_t reset_kind
) {
  switch (reset_kind) {
    case XIAOXIN_RUNTIME_RESET_POWERON:
      return "poweron";
    case XIAOXIN_RUNTIME_RESET_EXTERNAL:
      return "external";
    case XIAOXIN_RUNTIME_RESET_BROWNOUT:
      return "brownout";
    case XIAOXIN_RUNTIME_RESET_SOFTWARE:
      return "software";
    case XIAOXIN_RUNTIME_RESET_PANIC:
      return "panic";
    case XIAOXIN_RUNTIME_RESET_WATCHDOG:
      return "watchdog";
    case XIAOXIN_RUNTIME_RESET_DEEPSLEEP:
      return "deepsleep";
    case XIAOXIN_RUNTIME_RESET_UNKNOWN:
    default:
      return "unknown";
  }
}

void xiaoxin_runtime_health_snapshot_from_record(
  const xiaoxin_runtime_health_record_t* record,
  xiaoxin_runtime_health_snapshot_t* snapshot
) {
  if (record == NULL || snapshot == NULL) {
    return;
  }

  snapshot->boot_count = record->boot_count;
  snapshot->brownout_count = record->brownout_count;
  snapshot->poweron_count = record->poweron_count;
  snapshot->software_reset_count = record->software_reset_count;
  snapshot->watchdog_count = record->watchdog_count;
  snapshot->panic_count = record->panic_count;
  snapshot->short_run_streak = record->short_run_streak;
  snapshot->current_runtime_sec = record->current_checkpoint_sec;
  snapshot->last_runtime_sec = record->last_runtime_sec;
  snapshot->max_runtime_sec = record->max_runtime_sec;
  snapshot->low_battery_shutdown_count = record->low_battery_shutdown_count;
  snapshot->last_low_battery_shutdown_voltage_mv =
    record->last_low_battery_shutdown_voltage_mv;
  snapshot->current_on_battery = record->current_on_battery;
  snapshot->previous_on_battery = record->previous_on_battery;
  snapshot->last_low_battery_shutdown_startup_stage =
    record->last_low_battery_shutdown_startup_stage;
  snapshot->last_reset_kind = record->last_reset_kind;
}

void xiaoxin_runtime_health_record_low_battery_shutdown(
  xiaoxin_runtime_health_record_t* record,
  uint32_t voltage_mv,
  bool startup_stage
) {
  if (record == NULL) {
    return;
  }

  record->low_battery_shutdown_count++;
  record->last_low_battery_shutdown_voltage_mv = voltage_mv;
  record->last_low_battery_shutdown_startup_stage = startup_stage;
}
