#include "xiaoxin_power_control.h"

void xiaoxin_power_control_init(xiaoxin_power_control_t* power) {
  if (power == 0) {
    return;
  }

  power->power_hold = true;
  power->backlight_on = true;
  power->shutdown_requested = false;
}

void xiaoxin_power_control_handle_long_press(xiaoxin_power_control_t* power) {
  xiaoxin_power_control_request_shutdown(power);
}

void xiaoxin_power_control_request_shutdown(xiaoxin_power_control_t* power) {
  if (power == 0) {
    return;
  }

  power->shutdown_requested = true;
  power->backlight_on = false;
  power->power_hold = false;
}

bool xiaoxin_power_control_power_hold(const xiaoxin_power_control_t* power) {
  return power != 0 && power->power_hold;
}

bool xiaoxin_power_control_backlight_on(const xiaoxin_power_control_t* power) {
  return power != 0 && power->backlight_on;
}

bool xiaoxin_power_control_shutdown_requested(const xiaoxin_power_control_t* power) {
  return power != 0 && power->shutdown_requested;
}
