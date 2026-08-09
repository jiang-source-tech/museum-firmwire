#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "xiaoxin_battery_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XIAOXIN_SYSTEM_OVERLAY_ACTIVE_COLOR 0x168a73u
#define XIAOXIN_SYSTEM_OVERLAY_MUTED_COLOR 0x7f9890u
#define XIAOXIN_SYSTEM_OVERLAY_LOW_BATTERY_COLOR 0xf2a43au
#define XIAOXIN_SYSTEM_OVERLAY_CRITICAL_BATTERY_COLOR 0xff5e5bu
#define XIAOXIN_SYSTEM_OVERLAY_ACTIVE_OPA 255u
#define XIAOXIN_SYSTEM_OVERLAY_MUTED_OPA 118u

typedef enum {
  XIAOXIN_SYSTEM_OVERLAY_NETWORK_CONNECTED = 0,
  XIAOXIN_SYSTEM_OVERLAY_NETWORK_DISCONNECTED,
  XIAOXIN_SYSTEM_OVERLAY_NETWORK_CONFIGURING,
} xiaoxin_system_overlay_network_state_t;

typedef struct {
  uint32_t network_color;
  uint8_t network_opa;
  bool network_disconnected;
  uint32_t battery_color;
} xiaoxin_system_overlay_style_t;

xiaoxin_system_overlay_style_t xiaoxin_system_overlay_style(
  xiaoxin_system_overlay_network_state_t network_state,
  xiaoxin_battery_state_t battery_state,
  xiaoxin_battery_power_source_t power_source
);

#ifdef __cplusplus
}
#endif
