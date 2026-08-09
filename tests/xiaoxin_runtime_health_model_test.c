#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../main/boards/common/runtime_health_model.h"

static void brownout_short_runs_increment_streak(void) {
  xiaoxin_runtime_health_record_t record = {0};
  record.current_checkpoint_sec = 42;
  record.current_on_battery = true;

  xiaoxin_runtime_health_apply_boot(
    &record,
    XIAOXIN_RUNTIME_RESET_BROWNOUT,
    true,
    60
  );

  assert(record.boot_count == 1);
  assert(record.brownout_count == 1);
  assert(record.last_runtime_sec == 42);
  assert(record.short_run_streak == 1);
}

static void checkpoint_policy_waits_until_a_minute_then_every_five_minutes(void) {
  assert(!xiaoxin_runtime_health_should_checkpoint(0, 59));
  assert(xiaoxin_runtime_health_should_checkpoint(0, 60));
  assert(xiaoxin_runtime_health_should_checkpoint(59, 60));
  assert(!xiaoxin_runtime_health_should_checkpoint(60, 359));
  assert(xiaoxin_runtime_health_should_checkpoint(60, 360));
}

static void duration_formatting_is_compact_and_readable(void) {
  char text[32] = {0};

  xiaoxin_runtime_health_format_duration(text, sizeof(text), 0);
  assert(strcmp(text, "<1m") == 0);

  xiaoxin_runtime_health_format_duration(text, sizeof(text), 59);
  assert(strcmp(text, "<1m") == 0);

  xiaoxin_runtime_health_format_duration(text, sizeof(text), 60);
  assert(strcmp(text, "1m 00s") == 0);

  xiaoxin_runtime_health_format_duration(text, sizeof(text), 61);
  assert(strcmp(text, "1m 01s") == 0);

  xiaoxin_runtime_health_format_duration(text, sizeof(text), 3600);
  assert(strcmp(text, "1h 00m") == 0);

  xiaoxin_runtime_health_format_duration(text, sizeof(text), 3661);
  assert(strcmp(text, "1h 01m") == 0);
}

static void reset_labels_cover_known_reset_kinds(void) {
  assert(strcmp(xiaoxin_runtime_health_reset_label(XIAOXIN_RUNTIME_RESET_UNKNOWN), "unknown") == 0);
  assert(strcmp(xiaoxin_runtime_health_reset_label(XIAOXIN_RUNTIME_RESET_POWERON), "poweron") == 0);
  assert(strcmp(xiaoxin_runtime_health_reset_label(XIAOXIN_RUNTIME_RESET_BROWNOUT), "brownout") == 0);
  assert(strcmp(xiaoxin_runtime_health_reset_label(XIAOXIN_RUNTIME_RESET_SOFTWARE), "software") == 0);
  assert(strcmp(xiaoxin_runtime_health_reset_label(XIAOXIN_RUNTIME_RESET_PANIC), "panic") == 0);
  assert(strcmp(xiaoxin_runtime_health_reset_label(XIAOXIN_RUNTIME_RESET_WATCHDOG), "watchdog") == 0);
  assert(strcmp(xiaoxin_runtime_health_reset_label(XIAOXIN_RUNTIME_RESET_DEEPSLEEP), "deepsleep") == 0);
}

static void protection_requires_three_short_battery_unstable_boots(void) {
  xiaoxin_runtime_health_record_t record = {0};
  record.short_run_streak = 2;
  record.current_on_battery = true;
  record.last_reset_kind = XIAOXIN_RUNTIME_RESET_BROWNOUT;
  bool recommended = xiaoxin_runtime_health_protection_recommended(&record);
  assert(!recommended);

  record.short_run_streak = 3;
  recommended = xiaoxin_runtime_health_protection_recommended(&record);
  assert(recommended);

  record.last_reset_kind = XIAOXIN_RUNTIME_RESET_POWERON;
  recommended = xiaoxin_runtime_health_protection_recommended(&record);
  assert(recommended);

  record.current_on_battery = false;
  recommended = xiaoxin_runtime_health_protection_recommended(&record);
  assert(!recommended);
}

static void low_battery_shutdown_diagnostics_are_recorded(void) {
  xiaoxin_runtime_health_record_t record = {0};
  xiaoxin_runtime_health_snapshot_t snapshot = {0};

  xiaoxin_runtime_health_record_low_battery_shutdown(&record, 3590, true);

  assert(record.low_battery_shutdown_count == 1);
  assert(record.last_low_battery_shutdown_voltage_mv == 3590);
  assert(record.last_low_battery_shutdown_startup_stage);

  xiaoxin_runtime_health_record_low_battery_shutdown(&record, 3620, false);

  assert(record.low_battery_shutdown_count == 2);
  assert(record.last_low_battery_shutdown_voltage_mv == 3620);
  assert(!record.last_low_battery_shutdown_startup_stage);

  xiaoxin_runtime_health_snapshot_from_record(&record, &snapshot);

  assert(snapshot.low_battery_shutdown_count == 2);
  assert(snapshot.last_low_battery_shutdown_voltage_mv == 3620);
  assert(!snapshot.last_low_battery_shutdown_startup_stage);
}

int main(void) {
  brownout_short_runs_increment_streak();
  checkpoint_policy_waits_until_a_minute_then_every_five_minutes();
  duration_formatting_is_compact_and_readable();
  reset_labels_cover_known_reset_kinds();
  protection_requires_three_short_battery_unstable_boots();
  low_battery_shutdown_diagnostics_are_recorded();
  puts("xiaoxin_runtime_health_model tests passed");
  return 0;
}
