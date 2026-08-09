#include "xiaoxin_settings_model.h"

static void append_item(
  xiaoxin_settings_item_t item,
  xiaoxin_settings_item_t* out,
  uint8_t max_count,
  uint8_t* count
) {
  if (out != 0 && *count < max_count) {
    out[*count] = item;
  }
  if (*count < max_count) {
    *count = (uint8_t)(*count + 1);
  }
}

uint8_t xiaoxin_settings_visible_items(
  xiaoxin_settings_caps_t caps,
  xiaoxin_settings_item_t* out,
  uint8_t max_count
) {
  uint8_t count = 0;
  append_item(XIAOXIN_SETTINGS_ITEM_BRIGHTNESS, out, max_count, &count);
  append_item(XIAOXIN_SETTINGS_ITEM_WIFI, out, max_count, &count);
  if (caps.has_power_save_scheduler) {
    append_item(XIAOXIN_SETTINGS_ITEM_POWER_SAVE, out, max_count, &count);
  }
  append_item(XIAOXIN_SETTINGS_ITEM_ABOUT, out, max_count, &count);
  if (caps.has_audio_output) {
    append_item(XIAOXIN_SETTINGS_ITEM_VOLUME, out, max_count, &count);
    append_item(XIAOXIN_SETTINGS_ITEM_MUTE, out, max_count, &count);
    append_item(XIAOXIN_SETTINGS_ITEM_PROMPT_SOUND, out, max_count, &count);
  }
  if (caps.has_vibration) {
    append_item(XIAOXIN_SETTINGS_ITEM_VIBRATION, out, max_count, &count);
  }
  return count;
}

bool xiaoxin_settings_can_open(xiaoxin_settings_runtime_state_t runtime_state) {
  (void)runtime_state;
  return true;
}

uint8_t xiaoxin_settings_clamp_percent(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return (uint8_t)value;
}

uint8_t xiaoxin_settings_brightness_from_x(int x, int left, int width) {
  if (width <= 0) {
    return 10;
  }
  const int offset = x - left;
  int value = 10 + (offset * 90 + width / 2) / width;
  if (value < 10) {
    value = 10;
  }
  return xiaoxin_settings_clamp_percent(value);
}

const char* xiaoxin_settings_power_save_value_label(bool enabled) {
  return enabled ? "已开启" : "已关闭";
}

uint32_t xiaoxin_settings_power_save_battery_color(
  bool power_save_enabled,
  bool low_battery,
  uint32_t normal_color,
  uint32_t low_color,
  uint32_t power_save_color
) {
  if (low_battery) {
    return low_color;
  }
  if (power_save_enabled) {
    return power_save_color;
  }
  return normal_color;
}

const char* xiaoxin_settings_item_title(xiaoxin_settings_item_t item) {
  switch (item) {
    case XIAOXIN_SETTINGS_ITEM_BRIGHTNESS:
      return "亮度";
    case XIAOXIN_SETTINGS_ITEM_WIFI:
      return "Wi-Fi";
    case XIAOXIN_SETTINGS_ITEM_POWER_SAVE:
      return "省电";
    case XIAOXIN_SETTINGS_ITEM_ABOUT:
      return "关于";
    case XIAOXIN_SETTINGS_ITEM_VOLUME:
      return "音量";
    case XIAOXIN_SETTINGS_ITEM_MUTE:
      return "静音";
    case XIAOXIN_SETTINGS_ITEM_PROMPT_SOUND:
      return "提示音";
    case XIAOXIN_SETTINGS_ITEM_VIBRATION:
      return "震动";
    default:
      return "";
  }
}
