#include <assert.h>
#include <stdio.h>

#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_power_control.h"

static void init_keeps_power_latch_on(void) {
    xiaoxin_power_control_t power;
    xiaoxin_power_control_init(&power);

    assert(xiaoxin_power_control_power_hold(&power));
    assert(xiaoxin_power_control_backlight_on(&power));
    assert(!xiaoxin_power_control_shutdown_requested(&power));
}

static void long_press_requests_shutdown_instead_of_toggling(void) {
    xiaoxin_power_control_t power;
    xiaoxin_power_control_init(&power);

    xiaoxin_power_control_handle_long_press(&power);

    assert(!xiaoxin_power_control_power_hold(&power));
    assert(!xiaoxin_power_control_backlight_on(&power));
    assert(xiaoxin_power_control_shutdown_requested(&power));

    xiaoxin_power_control_handle_long_press(&power);

    assert(!xiaoxin_power_control_power_hold(&power));
    assert(!xiaoxin_power_control_backlight_on(&power));
    assert(xiaoxin_power_control_shutdown_requested(&power));
}

static void request_shutdown_matches_long_press_latch_behavior(void) {
    xiaoxin_power_control_t power;
    xiaoxin_power_control_init(&power);

    xiaoxin_power_control_request_shutdown(&power);

    assert(!xiaoxin_power_control_power_hold(&power));
    assert(!xiaoxin_power_control_backlight_on(&power));
    assert(xiaoxin_power_control_shutdown_requested(&power));

    xiaoxin_power_control_request_shutdown(&power);

    assert(!xiaoxin_power_control_power_hold(&power));
    assert(!xiaoxin_power_control_backlight_on(&power));
    assert(xiaoxin_power_control_shutdown_requested(&power));
}

int main(void) {
    init_keeps_power_latch_on();
    long_press_requests_shutdown_instead_of_toggling();
    request_shutdown_matches_long_press_latch_behavior();
    puts("xiaoxin_power_control tests passed");
    return 0;
}
