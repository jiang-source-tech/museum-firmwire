#include <assert.h>
#include <stdio.h>

#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_battery_state.h"
#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_system_overlay.h"

static void connected_network_uses_active_color(void) {
  const xiaoxin_system_overlay_style_t style = xiaoxin_system_overlay_style(
    XIAOXIN_SYSTEM_OVERLAY_NETWORK_CONNECTED,
    XIAOXIN_BATTERY_STATE_NORMAL,
    XIAOXIN_BATTERY_POWER_BATTERY
  );
  assert(style.network_color == XIAOXIN_SYSTEM_OVERLAY_ACTIVE_COLOR);
  assert(style.network_opa == XIAOXIN_SYSTEM_OVERLAY_ACTIVE_OPA);
  assert(!style.network_disconnected);
  assert(style.battery_color == XIAOXIN_SYSTEM_OVERLAY_ACTIVE_COLOR);
}

static void disconnected_network_is_muted(void) {
  const xiaoxin_system_overlay_style_t style = xiaoxin_system_overlay_style(
    XIAOXIN_SYSTEM_OVERLAY_NETWORK_DISCONNECTED,
    XIAOXIN_BATTERY_STATE_NORMAL,
    XIAOXIN_BATTERY_POWER_BATTERY
  );
  assert(style.network_color == XIAOXIN_SYSTEM_OVERLAY_MUTED_COLOR);
  assert(style.network_opa == XIAOXIN_SYSTEM_OVERLAY_MUTED_OPA);
  assert(style.network_disconnected);
}

static void low_battery_uses_warning_color(void) {
  const xiaoxin_system_overlay_style_t style = xiaoxin_system_overlay_style(
    XIAOXIN_SYSTEM_OVERLAY_NETWORK_CONNECTED,
    XIAOXIN_BATTERY_STATE_LOW,
    XIAOXIN_BATTERY_POWER_BATTERY
  );
  assert(style.battery_color == XIAOXIN_SYSTEM_OVERLAY_LOW_BATTERY_COLOR);
}

static void critical_battery_uses_critical_color(void) {
  const xiaoxin_system_overlay_style_t style = xiaoxin_system_overlay_style(
    XIAOXIN_SYSTEM_OVERLAY_NETWORK_CONNECTED,
    XIAOXIN_BATTERY_STATE_CRITICAL,
    XIAOXIN_BATTERY_POWER_BATTERY
  );
  assert(style.battery_color == XIAOXIN_SYSTEM_OVERLAY_CRITICAL_BATTERY_COLOR);
}

static void unknown_battery_is_muted(void) {
  const xiaoxin_system_overlay_style_t style = xiaoxin_system_overlay_style(
    XIAOXIN_SYSTEM_OVERLAY_NETWORK_CONNECTED,
    XIAOXIN_BATTERY_STATE_UNKNOWN,
    XIAOXIN_BATTERY_POWER_BATTERY
  );
  assert(style.battery_color == XIAOXIN_SYSTEM_OVERLAY_MUTED_COLOR);
}

static void external_power_uses_active_battery_color(void) {
  const xiaoxin_system_overlay_style_t style = xiaoxin_system_overlay_style(
    XIAOXIN_SYSTEM_OVERLAY_NETWORK_CONNECTED,
    XIAOXIN_BATTERY_STATE_UNKNOWN,
    XIAOXIN_BATTERY_POWER_EXTERNAL
  );
  assert(style.battery_color == XIAOXIN_SYSTEM_OVERLAY_ACTIVE_COLOR);
}

static void unknown_power_uses_muted_battery_color(void) {
  const xiaoxin_system_overlay_style_t style = xiaoxin_system_overlay_style(
    XIAOXIN_SYSTEM_OVERLAY_NETWORK_CONNECTED,
    XIAOXIN_BATTERY_STATE_NORMAL,
    XIAOXIN_BATTERY_POWER_UNKNOWN
  );
  assert(style.battery_color == XIAOXIN_SYSTEM_OVERLAY_MUTED_COLOR);
}

int main(void) {
  connected_network_uses_active_color();
  disconnected_network_is_muted();
  low_battery_uses_warning_color();
  critical_battery_uses_critical_color();
  unknown_battery_is_muted();
  external_power_uses_active_battery_color();
  unknown_power_uses_muted_battery_color();
  puts("xiaoxin_system_overlay tests passed");
  return 0;
}
