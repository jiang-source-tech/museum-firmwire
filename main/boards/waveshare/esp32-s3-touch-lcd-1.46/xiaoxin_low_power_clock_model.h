#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XIAOXIN_LOW_POWER_CLOCK_TIME_MAX 8
#define XIAOXIN_LOW_POWER_CLOCK_DATE_MAX 16
#define XIAOXIN_LOW_POWER_CLOCK_BATTERY_MAX 8
#define XIAOXIN_LOW_POWER_CLOCK_SYNC_MAX 8
#define XIAOXIN_LOW_POWER_CLOCK_HINT_MAX 32
#define XIAOXIN_LOW_POWER_CLOCK_SECOND_MAX 4
#define XIAOXIN_LOW_POWER_CLOCK_DEFAULT_BRIGHTNESS 48
#define XIAOXIN_LOW_POWER_CLOCK_ICON_TEXT "\xEF\x83\xB3"
#define XIAOXIN_LOW_POWER_CLOCK_ARC_SPAN_DEGREES 76

typedef enum {
  XIAOXIN_LOW_POWER_CLOCK_SYNC_IDLE = 0,
  XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCING,
  XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCED,
} xiaoxin_low_power_clock_sync_state_t;

typedef struct {
  bool time_valid;
  int hour;
  int minute;
  int second;
  int month;
  int day;
  int weekday;
  bool battery_known;
  int battery_percent;
  xiaoxin_low_power_clock_sync_state_t sync_state;
} xiaoxin_low_power_clock_state_t;

typedef struct {
  char icon_text[8];
  char time_text[XIAOXIN_LOW_POWER_CLOCK_TIME_MAX];
  char date_text[XIAOXIN_LOW_POWER_CLOCK_DATE_MAX];
  char battery_text[XIAOXIN_LOW_POWER_CLOCK_BATTERY_MAX];
  char sync_text[XIAOXIN_LOW_POWER_CLOCK_SYNC_MAX];
  char hint_text[XIAOXIN_LOW_POWER_CLOCK_HINT_MAX];
  char second_text[XIAOXIN_LOW_POWER_CLOCK_SECOND_MAX];
  uint32_t sync_color_hex;
  uint8_t brightness_percent;
} xiaoxin_low_power_clock_snapshot_t;

void xiaoxin_low_power_clock_model_build(
  const xiaoxin_low_power_clock_state_t* state,
  xiaoxin_low_power_clock_snapshot_t* snapshot
);

bool xiaoxin_low_power_clock_should_refresh(
  uint8_t previous_minute,
  uint8_t current_minute,
  uint8_t previous_second,
  uint8_t current_second
);

uint16_t xiaoxin_low_power_clock_animation_phase(uint32_t tick);

#ifdef __cplusplus
}
#endif
