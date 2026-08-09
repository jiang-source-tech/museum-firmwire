#include "xiaoxin_low_power_clock_model.h"

#include <stdio.h>
#include <string.h>

static int clamp_int(int value, int min, int max) {
  if (value < min) {
    return min;
  }
  if (value > max) {
    return max;
  }
  return value;
}

static const char* weekday_text(int weekday) {
  static const char* names[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  if (weekday < 0 || weekday > 6) {
    return "---";
  }
  return names[weekday];
}

static void build_sync_fields(
  xiaoxin_low_power_clock_sync_state_t sync_state,
  xiaoxin_low_power_clock_snapshot_t* snapshot
) {
  if (sync_state == XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCED) {
    snprintf(snapshot->sync_text, sizeof(snapshot->sync_text), "%s", "SYNC");
    snapshot->sync_color_hex = 0x26D9FF;
  } else if (sync_state == XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCING) {
    snprintf(snapshot->sync_text, sizeof(snapshot->sync_text), "%s", "NTP");
    snapshot->sync_color_hex = 0xF5C542;
  } else {
    snprintf(snapshot->sync_text, sizeof(snapshot->sync_text), "%s", "OFF");
    snapshot->sync_color_hex = 0x7C8794;
  }
}

void xiaoxin_low_power_clock_model_build(
  const xiaoxin_low_power_clock_state_t* state,
  xiaoxin_low_power_clock_snapshot_t* snapshot
) {
  if (snapshot == 0) {
    return;
  }

  memset(snapshot, 0, sizeof(*snapshot));
  snprintf(snapshot->icon_text, sizeof(snapshot->icon_text), "%s", XIAOXIN_LOW_POWER_CLOCK_ICON_TEXT);
  snprintf(snapshot->hint_text, sizeof(snapshot->hint_text), "%s", "POWER \xE5\x94\xA4\xE9\x86\x92");
  snapshot->brightness_percent = XIAOXIN_LOW_POWER_CLOCK_DEFAULT_BRIGHTNESS;

  build_sync_fields(state != 0 ? state->sync_state : XIAOXIN_LOW_POWER_CLOCK_SYNC_IDLE, snapshot);

  if (state == 0 || !state->battery_known) {
    snprintf(snapshot->battery_text, sizeof(snapshot->battery_text), "%s", "--%");
  } else {
    snprintf(
      snapshot->battery_text,
      sizeof(snapshot->battery_text),
      "%d%%",
      clamp_int(state->battery_percent, 0, 100)
    );
  }

  if (state == 0 || !state->time_valid) {
    snprintf(snapshot->time_text, sizeof(snapshot->time_text), "%s", "--:--");
    snprintf(snapshot->date_text, sizeof(snapshot->date_text), "%s", "WAITING");
    snprintf(snapshot->second_text, sizeof(snapshot->second_text), "%s", "--");
    return;
  }

  const int hour = clamp_int(state->hour, 0, 23);
  const int minute = clamp_int(state->minute, 0, 59);
  const int second = clamp_int(state->second, 0, 59);
  snprintf(snapshot->time_text, sizeof(snapshot->time_text), "%02d:%02d", hour, minute);
  snprintf(snapshot->second_text, sizeof(snapshot->second_text), "%02d", second);
  snprintf(
    snapshot->date_text,
    sizeof(snapshot->date_text),
    "%s %02d/%02d",
    weekday_text(state->weekday),
    clamp_int(state->month, 1, 12),
    clamp_int(state->day, 1, 31)
  );
}

bool xiaoxin_low_power_clock_should_refresh(
  uint8_t previous_minute,
  uint8_t current_minute,
  uint8_t previous_second,
  uint8_t current_second
) {
  return previous_minute != current_minute || previous_second != current_second;
}

uint16_t xiaoxin_low_power_clock_animation_phase(uint32_t tick) {
  return (uint16_t)((tick % 30U) * 12U);
}
