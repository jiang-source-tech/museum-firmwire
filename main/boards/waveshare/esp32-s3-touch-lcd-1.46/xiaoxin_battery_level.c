#include "xiaoxin_battery_level.h"

typedef struct {
  int voltage_mv;
  uint8_t percent;
} xiaoxin_battery_curve_point_t;

static const xiaoxin_battery_curve_point_t k_lipo_discharge_curve[] = {
  {4200, 100},
  {4100, 90},
  {4000, 75},
  {3900, 60},
  {3800, 40},
  {3700, 20},
  {3600, 10},
  {3500, 5},
  {3300, 0},
};

uint8_t xiaoxin_battery_percent_from_mv(int voltage_mv) {
  const int point_count =
    (int)(sizeof(k_lipo_discharge_curve) / sizeof(k_lipo_discharge_curve[0]));

  if (voltage_mv >= k_lipo_discharge_curve[0].voltage_mv) {
    return k_lipo_discharge_curve[0].percent;
  }
  if (voltage_mv <= k_lipo_discharge_curve[point_count - 1].voltage_mv) {
    return k_lipo_discharge_curve[point_count - 1].percent;
  }

  for (int i = 0; i < point_count - 1; ++i) {
    const xiaoxin_battery_curve_point_t high = k_lipo_discharge_curve[i];
    const xiaoxin_battery_curve_point_t low = k_lipo_discharge_curve[i + 1];
    if (voltage_mv <= high.voltage_mv && voltage_mv >= low.voltage_mv) {
      const int voltage_span = high.voltage_mv - low.voltage_mv;
      const int percent_span = high.percent - low.percent;
      const int offset_mv = voltage_mv - low.voltage_mv;
      return (uint8_t)(low.percent + (percent_span * offset_mv + voltage_span / 2) / voltage_span);
    }
  }

  return 0;
}
