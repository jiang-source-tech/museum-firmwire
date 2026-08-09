#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  XIAOXIN_SETTINGS_ITEM_BRIGHTNESS = 0,
  XIAOXIN_SETTINGS_ITEM_WIFI,
  XIAOXIN_SETTINGS_ITEM_POWER_SAVE,
  XIAOXIN_SETTINGS_ITEM_ABOUT,
  XIAOXIN_SETTINGS_ITEM_VOLUME,
  XIAOXIN_SETTINGS_ITEM_MUTE,
  XIAOXIN_SETTINGS_ITEM_PROMPT_SOUND,
  XIAOXIN_SETTINGS_ITEM_VIBRATION,
} xiaoxin_settings_item_t;

typedef enum {
  XIAOXIN_SETTINGS_RUNTIME_UNKNOWN = 0,
  XIAOXIN_SETTINGS_RUNTIME_STARTING,
  XIAOXIN_SETTINGS_RUNTIME_WIFI_CONFIGURING,
  XIAOXIN_SETTINGS_RUNTIME_IDLE,
  XIAOXIN_SETTINGS_RUNTIME_CONNECTING,
  XIAOXIN_SETTINGS_RUNTIME_LISTENING,
  XIAOXIN_SETTINGS_RUNTIME_SPEAKING,
  XIAOXIN_SETTINGS_RUNTIME_UPGRADING,
  XIAOXIN_SETTINGS_RUNTIME_ACTIVATING,
  XIAOXIN_SETTINGS_RUNTIME_AUDIO_TESTING,
  XIAOXIN_SETTINGS_RUNTIME_FATAL_ERROR,
} xiaoxin_settings_runtime_state_t;

typedef struct {
  bool has_audio_output;
  bool has_vibration;
  bool has_power_save_scheduler;
} xiaoxin_settings_caps_t;

uint8_t xiaoxin_settings_visible_items(
  xiaoxin_settings_caps_t caps,
  xiaoxin_settings_item_t* out,
  uint8_t max_count
);

bool xiaoxin_settings_can_open(xiaoxin_settings_runtime_state_t runtime_state);
uint8_t xiaoxin_settings_clamp_percent(int value);
uint8_t xiaoxin_settings_brightness_from_x(int x, int left, int width);
const char* xiaoxin_settings_power_save_value_label(bool enabled);
uint32_t xiaoxin_settings_power_save_battery_color(
  bool power_save_enabled,
  bool low_battery,
  uint32_t normal_color,
  uint32_t low_color,
  uint32_t power_save_color
);
const char* xiaoxin_settings_item_title(xiaoxin_settings_item_t item);

#ifdef __cplusplus
}
#endif
