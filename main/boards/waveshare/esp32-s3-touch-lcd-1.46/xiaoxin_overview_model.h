#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "xiaoxin_battery_state.h"
#include "xiaoxin_card_pager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XIAOXIN_OVERVIEW_ITEM_COUNT 5
#define XIAOXIN_OVERVIEW_BODY_MAX 40
#define XIAOXIN_OVERVIEW_DETAIL_MAX 64

typedef struct {
  bool time_valid;
  int hour;
  int minute;
  int month;
  int day;
  uint8_t weekday;
  bool network_connected;
  xiaoxin_battery_state_t battery_state;
  xiaoxin_battery_power_source_t battery_power_source;
  int battery_percent;
  uint8_t battery_level;
  bool battery_known;
  bool weather_available;
  bool weather_configured;
  const char* weather_summary;
  const char* weather_detail;
  bool course_configured;
  bool course_available_today;
  const char* course_title;
  const char* course_detail;
  bool todo_configured;
  uint8_t todo_count;
  const char* todo_detail;
  bool companion_available;
  uint8_t xiaoxin_age;
  const char* growth_summary;
} xiaoxin_overview_state_t;

typedef struct {
  char time_text[8];
  char date_text[24];
  xiaoxin_card_item_t items[XIAOXIN_OVERVIEW_ITEM_COUNT];
  char body_storage[XIAOXIN_OVERVIEW_ITEM_COUNT][XIAOXIN_OVERVIEW_BODY_MAX];
  char detail_storage[XIAOXIN_OVERVIEW_ITEM_COUNT][XIAOXIN_OVERVIEW_DETAIL_MAX];
  uint8_t item_count;
} xiaoxin_overview_snapshot_t;

void xiaoxin_overview_model_build(
  const xiaoxin_overview_state_t* state,
  xiaoxin_overview_snapshot_t* snapshot
);

#ifdef __cplusplus
}
#endif
