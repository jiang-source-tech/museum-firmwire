#include <assert.h>
#include <stdio.h>

#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_battery_level.h"

static void maps_common_lipo_voltage_to_percent(void) {
    assert(xiaoxin_battery_percent_from_mv(4200) == 100);
    assert(xiaoxin_battery_percent_from_mv(4100) == 90);
    assert(xiaoxin_battery_percent_from_mv(3900) == 60);
    assert(xiaoxin_battery_percent_from_mv(3700) == 20);
    assert(xiaoxin_battery_percent_from_mv(3600) == 10);
    assert(xiaoxin_battery_percent_from_mv(3200) == 0);
}

static void clamps_out_of_range_voltage(void) {
    assert(xiaoxin_battery_percent_from_mv(5000) == 100);
    assert(xiaoxin_battery_percent_from_mv(0) == 0);
}

int main(void) {
    maps_common_lipo_voltage_to_percent();
    clamps_out_of_range_voltage();
    puts("xiaoxin_battery_level tests passed");
    return 0;
}
