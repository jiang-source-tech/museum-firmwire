#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool power_hold;
  bool backlight_on;
  bool shutdown_requested;
} xiaoxin_power_control_t;

void xiaoxin_power_control_init(xiaoxin_power_control_t* power);
void xiaoxin_power_control_handle_long_press(xiaoxin_power_control_t* power);
void xiaoxin_power_control_request_shutdown(xiaoxin_power_control_t* power);

bool xiaoxin_power_control_power_hold(const xiaoxin_power_control_t* power);
bool xiaoxin_power_control_backlight_on(const xiaoxin_power_control_t* power);
bool xiaoxin_power_control_shutdown_requested(const xiaoxin_power_control_t* power);

#ifdef __cplusplus
}
#endif
