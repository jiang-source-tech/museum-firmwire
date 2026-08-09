#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_low_power_clock_model.h"

static void valid_time_formats_as_hh_mm(void) {
  xiaoxin_low_power_clock_state_t state = {
    .time_valid = true,
    .hour = 9,
    .minute = 5,
    .second = 7,
  };
  xiaoxin_low_power_clock_snapshot_t snapshot = {};

  xiaoxin_low_power_clock_model_build(&state, &snapshot);

  assert(strcmp(snapshot.time_text, "09:05") == 0);
  assert(strcmp(snapshot.second_text, "07") == 0);
  assert(snapshot.brightness_percent == 48);
  assert(snapshot.brightness_percent == XIAOXIN_LOW_POWER_CLOCK_DEFAULT_BRIGHTNESS);
}

static void invalid_time_uses_placeholder(void) {
  xiaoxin_low_power_clock_state_t state = {
    .time_valid = false,
    .hour = 14,
    .minute = 32,
    .second = 45,
  };
  xiaoxin_low_power_clock_snapshot_t snapshot = {};

  xiaoxin_low_power_clock_model_build(&state, &snapshot);

  assert(strcmp(snapshot.time_text, "--:--") == 0);
  assert(strcmp(snapshot.second_text, "--") == 0);
}

static void valid_state_formats_orbit_secondary_text(void) {
  xiaoxin_low_power_clock_state_t state = {
    .time_valid = true,
    .hour = 9,
    .minute = 5,
    .second = 36,
    .month = 6,
    .day = 24,
    .weekday = 3,
    .battery_known = true,
    .battery_percent = 87,
    .sync_state = XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCED,
  };
  xiaoxin_low_power_clock_snapshot_t snapshot = {};

  xiaoxin_low_power_clock_model_build(&state, &snapshot);

  assert(strcmp(snapshot.time_text, "09:05") == 0);
  assert(strcmp(snapshot.second_text, "36") == 0);
  assert(strcmp(snapshot.date_text, "WED 06/24") == 0);
  assert(strcmp(snapshot.battery_text, "87%") == 0);
  assert(strcmp(snapshot.sync_text, "SYNC") == 0);
  assert(snapshot.sync_color_hex == 0x26D9FF);
}

static void invalid_time_marks_syncing_orbit_state(void) {
  xiaoxin_low_power_clock_state_t state = {
    .time_valid = false,
    .battery_known = false,
    .sync_state = XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCING,
  };
  xiaoxin_low_power_clock_snapshot_t snapshot = {};

  xiaoxin_low_power_clock_model_build(&state, &snapshot);

  assert(strcmp(snapshot.time_text, "--:--") == 0);
  assert(strcmp(snapshot.date_text, "WAITING") == 0);
  assert(strcmp(snapshot.battery_text, "--%") == 0);
  assert(strcmp(snapshot.sync_text, "NTP") == 0);
  assert(snapshot.sync_color_hex == 0xF5C542);
}

static void time_fields_are_clamped_to_displayable_range(void) {
  xiaoxin_low_power_clock_state_t state = {
    .time_valid = true,
    .hour = 128,
    .minute = -3,
    .second = 99,
  };
  xiaoxin_low_power_clock_snapshot_t snapshot = {};

  xiaoxin_low_power_clock_model_build(&state, &snapshot);

  assert(strcmp(snapshot.time_text, "23:00") == 0);
  assert(strcmp(snapshot.second_text, "59") == 0);
}

static void refresh_when_minute_or_second_changes(void) {
  assert(!xiaoxin_low_power_clock_should_refresh(32, 32, 14, 14));
  assert(xiaoxin_low_power_clock_should_refresh(32, 33, 14, 14));
  assert(xiaoxin_low_power_clock_should_refresh(59, 0, 14, 14));
  assert(xiaoxin_low_power_clock_should_refresh(32, 32, 14, 15));
  assert(xiaoxin_low_power_clock_should_refresh(32, 32, 59, 0));
}

static void animation_phase_wraps_to_circle_degrees(void) {
  assert(xiaoxin_low_power_clock_animation_phase(0) == 0);
  assert(xiaoxin_low_power_clock_animation_phase(1) == 12);
  assert(xiaoxin_low_power_clock_animation_phase(29) == 348);
  assert(xiaoxin_low_power_clock_animation_phase(30) == 0);
  assert(xiaoxin_low_power_clock_animation_phase(31) == 12);
  assert(XIAOXIN_LOW_POWER_CLOCK_ARC_SPAN_DEGREES == 76);
}

int main(void) {
  valid_time_formats_as_hh_mm();
  invalid_time_uses_placeholder();
  valid_state_formats_orbit_secondary_text();
  invalid_time_marks_syncing_orbit_state();
  time_fields_are_clamped_to_displayable_range();
  refresh_when_minute_or_second_changes();
  animation_phase_wraps_to_circle_degrees();
  return 0;
}
