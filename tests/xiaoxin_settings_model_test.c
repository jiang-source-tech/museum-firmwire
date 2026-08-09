#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_settings_model.h"

static bool contains_item(
  const xiaoxin_settings_item_t* items,
  uint8_t count,
  xiaoxin_settings_item_t expected
) {
  for (uint8_t i = 0; i < count; ++i) {
    if (items[i] == expected) {
      return true;
    }
  }
  return false;
}

static void default_target_items_exclude_audio_and_power_save(void) {
  const xiaoxin_settings_caps_t caps = {
    .has_audio_output = false,
    .has_vibration = false,
    .has_power_save_scheduler = false,
  };
  xiaoxin_settings_item_t items[8] = {};
  const uint8_t count = xiaoxin_settings_visible_items(caps, items, 8);

  assert(count == 3);
  assert(items[0] == XIAOXIN_SETTINGS_ITEM_BRIGHTNESS);
  assert(items[1] == XIAOXIN_SETTINGS_ITEM_WIFI);
  assert(items[2] == XIAOXIN_SETTINGS_ITEM_ABOUT);
  assert(!contains_item(items, count, XIAOXIN_SETTINGS_ITEM_VOLUME));
  assert(!contains_item(items, count, XIAOXIN_SETTINGS_ITEM_MUTE));
  assert(!contains_item(items, count, XIAOXIN_SETTINGS_ITEM_PROMPT_SOUND));
  assert(!contains_item(items, count, XIAOXIN_SETTINGS_ITEM_VIBRATION));
  assert(!contains_item(items, count, XIAOXIN_SETTINGS_ITEM_POWER_SAVE));
}

static void power_save_item_requires_scheduler_capability(void) {
  const xiaoxin_settings_caps_t caps = {
    .has_audio_output = false,
    .has_vibration = false,
    .has_power_save_scheduler = true,
  };
  xiaoxin_settings_item_t items[8] = {};
  const uint8_t count = xiaoxin_settings_visible_items(caps, items, 8);

  assert(count == 4);
  assert(contains_item(items, count, XIAOXIN_SETTINGS_ITEM_POWER_SAVE));
}

static void audio_items_require_audio_capability(void) {
  const xiaoxin_settings_caps_t caps = {
    .has_audio_output = true,
    .has_vibration = false,
    .has_power_save_scheduler = false,
  };
  xiaoxin_settings_item_t items[8] = {};
  const uint8_t count = xiaoxin_settings_visible_items(caps, items, 8);

  assert(contains_item(items, count, XIAOXIN_SETTINGS_ITEM_VOLUME));
  assert(contains_item(items, count, XIAOXIN_SETTINGS_ITEM_MUTE));
}

static void item_count_is_clamped_to_output_capacity(void) {
  const xiaoxin_settings_caps_t caps = {
    .has_audio_output = true,
    .has_vibration = true,
    .has_power_save_scheduler = true,
  };
  xiaoxin_settings_item_t items[2] = {};
  const uint8_t count = xiaoxin_settings_visible_items(caps, items, 2);

  assert(count == 2);
  assert(items[0] == XIAOXIN_SETTINGS_ITEM_BRIGHTNESS);
  assert(items[1] == XIAOXIN_SETTINGS_ITEM_WIFI);
}

static void every_runtime_state_can_open_settings(void) {
  assert(xiaoxin_settings_can_open(XIAOXIN_SETTINGS_RUNTIME_UNKNOWN));
  assert(xiaoxin_settings_can_open(XIAOXIN_SETTINGS_RUNTIME_STARTING));
  assert(xiaoxin_settings_can_open(XIAOXIN_SETTINGS_RUNTIME_WIFI_CONFIGURING));
  assert(xiaoxin_settings_can_open(XIAOXIN_SETTINGS_RUNTIME_IDLE));
  assert(xiaoxin_settings_can_open(XIAOXIN_SETTINGS_RUNTIME_CONNECTING));
  assert(xiaoxin_settings_can_open(XIAOXIN_SETTINGS_RUNTIME_LISTENING));
  assert(xiaoxin_settings_can_open(XIAOXIN_SETTINGS_RUNTIME_SPEAKING));
  assert(xiaoxin_settings_can_open(XIAOXIN_SETTINGS_RUNTIME_UPGRADING));
  assert(xiaoxin_settings_can_open(XIAOXIN_SETTINGS_RUNTIME_ACTIVATING));
  assert(xiaoxin_settings_can_open(XIAOXIN_SETTINGS_RUNTIME_AUDIO_TESTING));
  assert(xiaoxin_settings_can_open(XIAOXIN_SETTINGS_RUNTIME_FATAL_ERROR));
}

static void brightness_percent_is_clamped(void) {
  assert(xiaoxin_settings_clamp_percent(-20) == 0);
  assert(xiaoxin_settings_clamp_percent(0) == 0);
  assert(xiaoxin_settings_clamp_percent(67) == 67);
  assert(xiaoxin_settings_clamp_percent(100) == 100);
  assert(xiaoxin_settings_clamp_percent(125) == 100);
}

static void brightness_slider_maps_x_to_safe_range(void) {
  assert(xiaoxin_settings_brightness_from_x(10, 10, 180) == 10);
  assert(xiaoxin_settings_brightness_from_x(190, 10, 180) == 100);
  assert(xiaoxin_settings_brightness_from_x(100, 10, 180) == 55);
  assert(xiaoxin_settings_brightness_from_x(-20, 10, 180) == 10);
  assert(xiaoxin_settings_brightness_from_x(260, 10, 180) == 100);
  assert(xiaoxin_settings_brightness_from_x(60, 10, 0) == 10);
}

static void titles_are_valid_utf8_chinese(void) {
  assert(strcmp(xiaoxin_settings_item_title(XIAOXIN_SETTINGS_ITEM_BRIGHTNESS), "亮度") == 0);
  assert(strcmp(xiaoxin_settings_item_title(XIAOXIN_SETTINGS_ITEM_WIFI), "Wi-Fi") == 0);
  assert(strcmp(xiaoxin_settings_item_title(XIAOXIN_SETTINGS_ITEM_POWER_SAVE), "省电") == 0);
  assert(strcmp(xiaoxin_settings_item_title(XIAOXIN_SETTINGS_ITEM_ABOUT), "关于") == 0);
  assert(strcmp(xiaoxin_settings_item_title(XIAOXIN_SETTINGS_ITEM_VOLUME), "音量") == 0);
  assert(strcmp(xiaoxin_settings_item_title(XIAOXIN_SETTINGS_ITEM_MUTE), "静音") == 0);
  assert(strcmp(xiaoxin_settings_item_title(XIAOXIN_SETTINGS_ITEM_PROMPT_SOUND), "提示音") == 0);
  assert(strcmp(xiaoxin_settings_item_title(XIAOXIN_SETTINGS_ITEM_VIBRATION), "震动") == 0);
}

static void power_save_value_label_reflects_enabled_state(void) {
  assert(strcmp(xiaoxin_settings_power_save_value_label(true), "已开启") == 0);
  assert(strcmp(xiaoxin_settings_power_save_value_label(false), "已关闭") == 0);
}

static void power_save_battery_color_preserves_low_battery_priority(void) {
  assert(xiaoxin_settings_power_save_battery_color(false, false, 0x00ff00, 0xff0000, 0xffb84d) == 0x00ff00);
  assert(xiaoxin_settings_power_save_battery_color(true, false, 0x00ff00, 0xff0000, 0xffb84d) == 0xffb84d);
  assert(xiaoxin_settings_power_save_battery_color(true, true, 0x00ff00, 0xff0000, 0xffb84d) == 0xff0000);
}

int main(void) {
  default_target_items_exclude_audio_and_power_save();
  power_save_item_requires_scheduler_capability();
  audio_items_require_audio_capability();
  item_count_is_clamped_to_output_capacity();
  every_runtime_state_can_open_settings();
  brightness_percent_is_clamped();
  brightness_slider_maps_x_to_safe_range();
  titles_are_valid_utf8_chinese();
  power_save_value_label_reflects_enabled_state();
  power_save_battery_color_preserves_low_battery_priority();
  puts("xiaoxin_settings_model tests passed");
  return 0;
}
