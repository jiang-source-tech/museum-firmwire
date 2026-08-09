#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  XIAOXIN_BATTERY_STATE_UNKNOWN = 0,
  XIAOXIN_BATTERY_STATE_NORMAL,
  XIAOXIN_BATTERY_STATE_LOW,
  XIAOXIN_BATTERY_STATE_CRITICAL,
} xiaoxin_battery_state_t;

typedef enum {
  XIAOXIN_BATTERY_POWER_BATTERY = 0,
  XIAOXIN_BATTERY_POWER_EXTERNAL,
  XIAOXIN_BATTERY_POWER_UNKNOWN,
} xiaoxin_battery_power_source_t;

typedef enum {
  XIAOXIN_BATTERY_POWER_HINT_UNKNOWN = 0,
  XIAOXIN_BATTERY_POWER_HINT_BATTERY,
  XIAOXIN_BATTERY_POWER_HINT_EXTERNAL,
} xiaoxin_battery_power_hint_t;

typedef enum {
  XIAOXIN_BATTERY_LOAD_IDLE = 0,
  XIAOXIN_BATTERY_LOAD_VOICE_ACTIVE,
} xiaoxin_battery_load_t;

typedef struct {
  xiaoxin_battery_state_t state;
  xiaoxin_battery_power_source_t power_source;
  int estimated_percent;
  int display_percent;
  uint8_t display_level;
  bool percent_reliable;
  int smoothed_voltage_mv;
  bool low_edge;
  bool critical_edge;
  bool recovered_edge;
} xiaoxin_battery_snapshot_t;

typedef struct {
  xiaoxin_battery_state_t state;
  xiaoxin_battery_power_source_t power_source;
  xiaoxin_battery_power_source_t candidate_power_source;
  int estimated_percent;
  int display_percent;
  uint8_t display_level;
  bool percent_reliable;
  int smoothed_voltage_mv;
  bool has_sample;
  uint32_t candidate_since_ms;
  uint8_t candidate_power_count;
  uint32_t power_source_since_ms;
  uint8_t invalid_sample_count;
  uint32_t unknown_since_ms;
  uint32_t battery_candidate_since_ms;
  uint8_t high_sample_count;
  int trend_window_min_mv;
  int trend_window_max_mv;
  uint32_t trend_window_start_ms;
  int discharge_window_min_mv;
  int discharge_window_max_mv;
  uint32_t discharge_window_start_ms;
  int rise_window_start_mv;
  uint32_t rise_window_start_ms;
  uint32_t steady_high_since_ms;
  uint32_t alternation_window_start_ms;
  uint8_t alternating_sample_count;
  uint8_t last_sample_quality;
  bool state_edges_suppressed_until_reconfirmed;
  xiaoxin_battery_state_t candidate_state;
  xiaoxin_battery_snapshot_t last_snapshot;
} xiaoxin_battery_context_t;

void xiaoxin_battery_state_init(
  xiaoxin_battery_context_t* ctx,
  uint32_t now_ms
);

xiaoxin_battery_snapshot_t xiaoxin_battery_state_update(
  xiaoxin_battery_context_t* ctx,
  int voltage_mv,
  bool sample_valid,
  xiaoxin_battery_power_hint_t power_hint,
  xiaoxin_battery_load_t load,
  uint32_t now_ms
);

xiaoxin_battery_snapshot_t xiaoxin_battery_state_snapshot(
  const xiaoxin_battery_context_t* ctx
);

#ifdef __cplusplus
}
#endif
