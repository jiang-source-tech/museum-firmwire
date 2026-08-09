#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/xiaoxin_battery_state.h"

static xiaoxin_battery_snapshot_t feed(
  xiaoxin_battery_context_t* ctx,
  int voltage_mv,
  xiaoxin_battery_load_t load,
  uint32_t now_ms
) {
  return xiaoxin_battery_state_update(
    ctx,
    voltage_mv,
    true,
    XIAOXIN_BATTERY_POWER_HINT_UNKNOWN,
    load,
    now_ms
  );
}

static xiaoxin_battery_snapshot_t feed_with_power_hint(
  xiaoxin_battery_context_t* ctx,
  int voltage_mv,
  xiaoxin_battery_power_hint_t power_hint,
  uint32_t now_ms
) {
  return xiaoxin_battery_state_update(
    ctx,
    voltage_mv,
    true,
    power_hint,
    XIAOXIN_BATTERY_LOAD_IDLE,
    now_ms
  );
}

static xiaoxin_battery_snapshot_t enter_external_with_high_samples(
  xiaoxin_battery_context_t* ctx
) {
  feed(ctx, 4500, XIAOXIN_BATTERY_LOAD_IDLE, 1000);
  feed(ctx, 4510, XIAOXIN_BATTERY_LOAD_IDLE, 2000);
  feed(ctx, 4520, XIAOXIN_BATTERY_LOAD_IDLE, 3000);
  feed(ctx, 4530, XIAOXIN_BATTERY_LOAD_IDLE, 4000);
  feed(ctx, 4540, XIAOXIN_BATTERY_LOAD_IDLE, 5000);
  return feed(ctx, 4550, XIAOXIN_BATTERY_LOAD_IDLE, 6000);
}

static xiaoxin_battery_snapshot_t enter_battery_from_startup(
  xiaoxin_battery_context_t* ctx
) {
  const int voltages[] = {
    4050, 4100, 4040, 4090, 4030, 4080,
    4020, 4070, 4010, 4060, 4000, 4055,
  };
  xiaoxin_battery_snapshot_t snapshot = xiaoxin_battery_state_snapshot(ctx);
  for (int i = 0; i < (int)(sizeof(voltages) / sizeof(voltages[0])); ++i) {
    snapshot = feed(ctx, voltages[i], XIAOXIN_BATTERY_LOAD_IDLE, 1000 + (uint32_t)i * 1000);
  }
  snapshot = feed(ctx, 4045, XIAOXIN_BATTERY_LOAD_IDLE, 13000);
  snapshot = feed(ctx, 4050, XIAOXIN_BATTERY_LOAD_IDLE, 14000);
  snapshot = feed(ctx, 4035, XIAOXIN_BATTERY_LOAD_IDLE, 15000);
  snapshot = feed(ctx, 4040, XIAOXIN_BATTERY_LOAD_IDLE, 16000);
  snapshot = feed(ctx, 4050, XIAOXIN_BATTERY_LOAD_IDLE, 17000);
  return snapshot;
}

static void init_starts_unknown(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 1000);
  xiaoxin_battery_snapshot_t snapshot = xiaoxin_battery_state_snapshot(&ctx);
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_UNKNOWN);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_UNKNOWN);
  assert(snapshot.display_percent == 0);
  assert(snapshot.display_level == 0);
  assert(!snapshot.percent_reliable);
  assert(!snapshot.low_edge);
  assert(!snapshot.critical_edge);
  assert(!snapshot.recovered_edge);
}

static void valid_samples_become_normal_after_confirmation(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  xiaoxin_battery_snapshot_t snapshot = enter_battery_from_startup(&ctx);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_NORMAL);
}

static void one_low_sample_does_not_turn_low(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  enter_battery_from_startup(&ctx);
  xiaoxin_battery_snapshot_t snapshot =
    feed(&ctx, 3500, XIAOXIN_BATTERY_LOAD_IDLE, 18000);
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_NORMAL);
  assert(!snapshot.low_edge);
}

static void sustained_low_enters_low_once(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  enter_battery_from_startup(&ctx);
  uint32_t now_ms = 18000;
  xiaoxin_battery_snapshot_t snapshot = feed(
    &ctx,
    3500,
    XIAOXIN_BATTERY_LOAD_IDLE,
    now_ms
  );
  for (; snapshot.state != XIAOXIN_BATTERY_STATE_LOW && now_ms <= 200000; now_ms += 5000) {
    snapshot = feed(&ctx, 3500, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_LOW);
  assert(snapshot.low_edge);
  snapshot = feed(&ctx, 3500, XIAOXIN_BATTERY_LOAD_IDLE, now_ms + 5000);
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_LOW);
  assert(!snapshot.low_edge);
}

static void low_requires_hysteresis_to_recover(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  enter_battery_from_startup(&ctx);
  uint32_t now_ms = 18000;
  xiaoxin_battery_snapshot_t snapshot = feed(
    &ctx,
    3500,
    XIAOXIN_BATTERY_LOAD_IDLE,
    now_ms
  );
  for (; snapshot.state != XIAOXIN_BATTERY_STATE_LOW && now_ms <= 200000; now_ms += 5000) {
    snapshot = feed(&ctx, 3500, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_LOW);
  assert(!snapshot.recovered_edge);
  now_ms += 5000;
  snapshot = feed(&ctx, 3900, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  for (; snapshot.state != XIAOXIN_BATTERY_STATE_NORMAL && now_ms <= 300000; now_ms += 5000) {
    snapshot = feed(&ctx, 3900, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_NORMAL);
  assert(!snapshot.recovered_edge);
}

static void sustained_critical_enters_critical(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  enter_battery_from_startup(&ctx);
  uint32_t now_ms = 18000;
  xiaoxin_battery_snapshot_t snapshot = feed(
    &ctx,
    3450,
    XIAOXIN_BATTERY_LOAD_IDLE,
    now_ms
  );
  for (; snapshot.state != XIAOXIN_BATTERY_STATE_CRITICAL && now_ms <= 200000; now_ms += 5000) {
    snapshot = feed(&ctx, 3450, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_CRITICAL);
  assert(snapshot.critical_edge);
}

static void startup_sustained_low_enters_low(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  enter_battery_from_startup(&ctx);
  uint32_t now_ms = 18000;
  xiaoxin_battery_snapshot_t snapshot = feed(
    &ctx,
    3500,
    XIAOXIN_BATTERY_LOAD_IDLE,
    now_ms
  );
  for (; snapshot.state != XIAOXIN_BATTERY_STATE_LOW && now_ms <= 200000; now_ms += 5000) {
    snapshot = feed(&ctx, 3500, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_LOW);
  assert(snapshot.low_edge);
}

static void startup_sustained_critical_enters_critical(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  enter_battery_from_startup(&ctx);
  uint32_t now_ms = 18000;
  xiaoxin_battery_snapshot_t snapshot = feed(
    &ctx,
    3450,
    XIAOXIN_BATTERY_LOAD_IDLE,
    now_ms
  );
  for (; snapshot.state != XIAOXIN_BATTERY_STATE_CRITICAL && now_ms <= 200000; now_ms += 5000) {
    snapshot = feed(&ctx, 3450, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_CRITICAL);
  assert(snapshot.critical_edge);
}

static void unknown_can_enter_critical_without_low_first(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);

  uint32_t now_ms = 1000;
  xiaoxin_battery_snapshot_t snapshot = feed(
    &ctx,
    3600,
    XIAOXIN_BATTERY_LOAD_IDLE,
    now_ms
  );
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_UNKNOWN);

  for (; snapshot.state != XIAOXIN_BATTERY_STATE_CRITICAL && now_ms <= 60000; now_ms += 1000) {
    snapshot = feed(&ctx, 3600, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }

  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_CRITICAL);
  assert(snapshot.critical_edge);
  assert(!snapshot.low_edge);
}

static void critical_edge_is_one_shot_until_recovery(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  enter_battery_from_startup(&ctx);

  uint32_t now_ms = 18000;
  xiaoxin_battery_snapshot_t snapshot = feed(
    &ctx,
    3600,
    XIAOXIN_BATTERY_LOAD_IDLE,
    now_ms
  );
  for (; snapshot.state != XIAOXIN_BATTERY_STATE_CRITICAL && now_ms <= 90000; now_ms += 1000) {
    snapshot = feed(&ctx, 3600, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }

  assert(snapshot.state == XIAOXIN_BATTERY_STATE_CRITICAL);
  assert(snapshot.critical_edge);

  snapshot = feed(&ctx, 3600, XIAOXIN_BATTERY_LOAD_IDLE, now_ms + 1000);
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_CRITICAL);
  assert(!snapshot.critical_edge);
  assert(!snapshot.low_edge);

  for (; snapshot.state != XIAOXIN_BATTERY_STATE_LOW && now_ms <= 130000; now_ms += 1000) {
    snapshot = feed(&ctx, 3680, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }

  assert(snapshot.state == XIAOXIN_BATTERY_STATE_LOW);
  assert(snapshot.recovered_edge);
}

static void voice_active_extends_low_confirmation(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  enter_battery_from_startup(&ctx);
  feed(&ctx, 3500, XIAOXIN_BATTERY_LOAD_VOICE_ACTIVE, 18000);
  feed(&ctx, 3500, XIAOXIN_BATTERY_LOAD_VOICE_ACTIVE, 23000);
  feed(&ctx, 3500, XIAOXIN_BATTERY_LOAD_VOICE_ACTIVE, 28000);
  feed(&ctx, 3500, XIAOXIN_BATTERY_LOAD_VOICE_ACTIVE, 33000);
  xiaoxin_battery_snapshot_t snapshot =
    feed(&ctx, 3500, XIAOXIN_BATTERY_LOAD_VOICE_ACTIVE, 77000);
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_NORMAL);
  uint32_t now_ms = 82000;
  snapshot = feed(&ctx, 3500, XIAOXIN_BATTERY_LOAD_VOICE_ACTIVE, now_ms);
  for (; snapshot.state != XIAOXIN_BATTERY_STATE_LOW && now_ms <= 200000; now_ms += 5000) {
    snapshot = feed(&ctx, 3500, XIAOXIN_BATTERY_LOAD_VOICE_ACTIVE, now_ms);
  }
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_LOW);
  assert(snapshot.low_edge);
}

static void invalid_and_nonpositive_samples_do_not_change_state_or_ema(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  enter_battery_from_startup(&ctx);
  xiaoxin_battery_snapshot_t before = xiaoxin_battery_state_snapshot(&ctx);
  xiaoxin_battery_snapshot_t snapshot = xiaoxin_battery_state_update(
    &ctx,
    0,
    true,
    XIAOXIN_BATTERY_POWER_HINT_UNKNOWN,
    XIAOXIN_BATTERY_LOAD_IDLE,
    18000
  );
  assert(snapshot.state == before.state);
  assert(snapshot.power_source == before.power_source);
  assert(snapshot.smoothed_voltage_mv == before.smoothed_voltage_mv);
  assert(snapshot.display_percent == before.display_percent);
  assert(snapshot.display_level == before.display_level);
  assert(snapshot.percent_reliable == before.percent_reliable);
  assert(!snapshot.low_edge);
  assert(!snapshot.critical_edge);
  assert(!snapshot.recovered_edge);

  feed(&ctx, 3900, XIAOXIN_BATTERY_LOAD_IDLE, 23000);
  feed(&ctx, 3900, XIAOXIN_BATTERY_LOAD_IDLE, 28000);
  before = xiaoxin_battery_state_snapshot(&ctx);
  snapshot = xiaoxin_battery_state_update(
    &ctx,
    0,
    false,
    XIAOXIN_BATTERY_POWER_HINT_UNKNOWN,
    XIAOXIN_BATTERY_LOAD_IDLE,
    33000
  );
  assert(snapshot.state == before.state);
  assert(snapshot.power_source == before.power_source);
  assert(snapshot.smoothed_voltage_mv == before.smoothed_voltage_mv);
  assert(snapshot.display_percent == before.display_percent);
  assert(snapshot.display_level == before.display_level);
  assert(snapshot.percent_reliable == before.percent_reliable);
  assert(!snapshot.low_edge);
  assert(!snapshot.critical_edge);
  assert(!snapshot.recovered_edge);
}

static void extreme_low_samples_do_not_change_state_or_ema(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  enter_battery_from_startup(&ctx);
  xiaoxin_battery_snapshot_t before = xiaoxin_battery_state_snapshot(&ctx);
  xiaoxin_battery_snapshot_t snapshot = xiaoxin_battery_state_update(
    &ctx,
    2000,
    true,
    XIAOXIN_BATTERY_POWER_HINT_UNKNOWN,
    XIAOXIN_BATTERY_LOAD_IDLE,
    18000
  );
  assert(snapshot.state == before.state);
  assert(snapshot.power_source == before.power_source);
  assert(snapshot.smoothed_voltage_mv == before.smoothed_voltage_mv);
  assert(snapshot.display_percent == before.display_percent);
  assert(snapshot.display_level == before.display_level);
  assert(snapshot.percent_reliable == before.percent_reliable);
  assert(!snapshot.low_edge);
  assert(!snapshot.critical_edge);
  assert(!snapshot.recovered_edge);
}

static void init_snapshot_has_display_fields(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 1000);
  xiaoxin_battery_snapshot_t snapshot = xiaoxin_battery_state_snapshot(&ctx);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_UNKNOWN);
  assert(snapshot.display_percent == 0);
  assert(snapshot.display_level == 0);
  assert(!snapshot.percent_reliable);
}

static void display_level_has_hysteresis(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);

  xiaoxin_battery_snapshot_t snapshot = enter_battery_from_startup(&ctx);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(snapshot.display_level == 4);

  snapshot = feed(&ctx, 3950, XIAOXIN_BATTERY_LOAD_IDLE, 18000);
  assert(snapshot.display_level == 4);

  for (uint32_t now_ms = 23000; now_ms <= 33000; now_ms += 5000) {
    snapshot = feed(&ctx, 3800, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
    assert(snapshot.display_level == 4);
    assert(snapshot.display_percent >= 65);
  }

  snapshot = feed(&ctx, 3800, XIAOXIN_BATTERY_LOAD_IDLE, 38000);
  assert(snapshot.display_level == 3);
  assert(snapshot.display_percent < 65);

  snapshot = feed(&ctx, 3800, XIAOXIN_BATTERY_LOAD_IDLE, 43000);
  assert(snapshot.display_level == 3);
  assert(snapshot.display_percent < 65);
}

static void high_untrusted_samples_do_not_show_full_battery(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);

  xiaoxin_battery_snapshot_t snapshot = enter_battery_from_startup(&ctx);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(snapshot.display_level == 4);
  assert(snapshot.percent_reliable);
  const int before_mv = snapshot.smoothed_voltage_mv;
  const int before_percent = snapshot.display_percent;

  snapshot = xiaoxin_battery_state_update(
    &ctx,
    4500,
    true,
    XIAOXIN_BATTERY_POWER_HINT_UNKNOWN,
    XIAOXIN_BATTERY_LOAD_IDLE,
    18000
  );
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_NORMAL);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(snapshot.smoothed_voltage_mv == before_mv);
  assert(snapshot.display_percent == before_percent);
  assert(snapshot.display_level == 4);
  assert(snapshot.percent_reliable);
}

static void high_voltage_does_not_update_ema_or_show_full(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  xiaoxin_battery_snapshot_t snapshot = enter_battery_from_startup(&ctx);
  const int before_mv = snapshot.smoothed_voltage_mv;
  const uint8_t before_level = snapshot.display_level;

  snapshot = feed(&ctx, 4500, XIAOXIN_BATTERY_LOAD_IDLE, 18000);
  assert(snapshot.smoothed_voltage_mv == before_mv);
  assert(snapshot.display_level == before_level);
  assert(snapshot.estimated_percent < 100);
}

static void extreme_low_sample_does_not_update_ema_or_state(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  xiaoxin_battery_snapshot_t snapshot = enter_battery_from_startup(&ctx);
  const int before_mv = snapshot.smoothed_voltage_mv;
  const xiaoxin_battery_state_t before_state = snapshot.state;

  snapshot = feed(&ctx, 2000, XIAOXIN_BATTERY_LOAD_IDLE, 18000);
  assert(snapshot.smoothed_voltage_mv == before_mv);
  assert(snapshot.state == before_state);
}

static void single_high_voltage_spike_does_not_enter_external(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  feed(&ctx, 3900, XIAOXIN_BATTERY_LOAD_IDLE, 1000);
  xiaoxin_battery_snapshot_t snapshot =
    feed(&ctx, 4500, XIAOXIN_BATTERY_LOAD_IDLE, 2000);
  assert(snapshot.power_source != XIAOXIN_BATTERY_POWER_EXTERNAL);
}

static void high_voltage_requires_three_qualifying_proposals(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  feed(&ctx, 3900, XIAOXIN_BATTERY_LOAD_IDLE, 1000);
  feed(&ctx, 4500, XIAOXIN_BATTERY_LOAD_IDLE, 2000);
  feed(&ctx, 4510, XIAOXIN_BATTERY_LOAD_IDLE, 3000);
  xiaoxin_battery_snapshot_t snapshot = feed(
    &ctx,
    4520,
    XIAOXIN_BATTERY_LOAD_IDLE,
    4000
  );
  assert(ctx.candidate_power_count == 1);
  assert(snapshot.power_source != XIAOXIN_BATTERY_POWER_EXTERNAL);

  snapshot = feed(&ctx, 4530, XIAOXIN_BATTERY_LOAD_IDLE, 5000);
  assert(ctx.candidate_power_count == 2);
  assert(snapshot.power_source != XIAOXIN_BATTERY_POWER_EXTERNAL);

  snapshot = feed(&ctx, 4540, XIAOXIN_BATTERY_LOAD_IDLE, 6000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);
  assert(snapshot.display_level == 4);
  assert(!snapshot.percent_reliable);
}

static void external_power_hint_enters_external_immediately(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);

  xiaoxin_battery_snapshot_t snapshot = feed_with_power_hint(
    &ctx,
    3900,
    XIAOXIN_BATTERY_POWER_HINT_EXTERNAL,
    1000
  );

  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);
  assert(snapshot.display_level == 4);
  assert(!snapshot.percent_reliable);
}

static void battery_power_hint_returns_from_external_after_hold(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);

  xiaoxin_battery_snapshot_t snapshot = feed_with_power_hint(
    &ctx,
    3900,
    XIAOXIN_BATTERY_POWER_HINT_EXTERNAL,
    1000
  );
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);

  snapshot = feed_with_power_hint(
    &ctx,
    3900,
    XIAOXIN_BATTERY_POWER_HINT_BATTERY,
    32000
  );

  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(snapshot.percent_reliable);
}

static void rapid_rise_enters_external_after_confirmation(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  feed(&ctx, 3700, XIAOXIN_BATTERY_LOAD_IDLE, 1000);
  uint32_t now_ms = 5000;
  xiaoxin_battery_snapshot_t snapshot = xiaoxin_battery_state_snapshot(&ctx);

  while (ctx.candidate_power_count == 0 && now_ms <= 120000) {
    snapshot = feed(&ctx, 4200, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
    now_ms += 5000;
  }
  assert(ctx.candidate_power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);
  assert(ctx.candidate_power_count == 1);
  assert(snapshot.power_source != XIAOXIN_BATTERY_POWER_EXTERNAL);

  while (ctx.candidate_power_count == 1 && now_ms <= 120000) {
    snapshot = feed(&ctx, 4200, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
    now_ms += 5000;
  }
  assert(ctx.candidate_power_count == 2);
  assert(snapshot.power_source != XIAOXIN_BATTERY_POWER_EXTERNAL);

  while (ctx.candidate_power_count == 2 && now_ms <= 120000) {
    snapshot = feed(&ctx, 4200, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
    now_ms += 5000;
  }
  assert(ctx.candidate_power_count == 3);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);
}

static void steady_high_voltage_enters_external(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  xiaoxin_battery_snapshot_t snapshot = xiaoxin_battery_state_snapshot(&ctx);
  uint32_t now_ms = 1000;

  while (ctx.candidate_power_count == 0 && now_ms <= 180000) {
    snapshot = feed(&ctx, 4200, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
    now_ms += 10000;
  }
  assert(ctx.candidate_power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);
  assert(ctx.candidate_power_count == 1);
  assert(snapshot.power_source != XIAOXIN_BATTERY_POWER_EXTERNAL);

  while (ctx.candidate_power_count == 1 && now_ms <= 180000) {
    snapshot = feed(&ctx, 4200, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
    now_ms += 10000;
  }
  assert(ctx.candidate_power_count == 2);
  assert(snapshot.power_source != XIAOXIN_BATTERY_POWER_EXTERNAL);

  while (ctx.candidate_power_count == 2 && now_ms <= 180000) {
    snapshot = feed(&ctx, 4200, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
    now_ms += 10000;
  }
  assert(ctx.candidate_power_count == 3);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);
}

static void alternating_sample_types_enter_external(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  feed(&ctx, 3900, XIAOXIN_BATTERY_LOAD_IDLE, 1000);
  feed(&ctx, 4500, XIAOXIN_BATTERY_LOAD_IDLE, 2000);
  feed(&ctx, 3910, XIAOXIN_BATTERY_LOAD_IDLE, 3000);
  feed(&ctx, 4510, XIAOXIN_BATTERY_LOAD_IDLE, 4000);
  xiaoxin_battery_snapshot_t snapshot =
    feed(&ctx, 3920, XIAOXIN_BATTERY_LOAD_IDLE, 5000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);
}

static void steady_high_requires_low_ripple_across_full_interval(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  xiaoxin_battery_snapshot_t snapshot = xiaoxin_battery_state_snapshot(&ctx);

  snapshot = feed(&ctx, 4090, XIAOXIN_BATTERY_LOAD_IDLE, 1000);
  snapshot = feed(&ctx, 4160, XIAOXIN_BATTERY_LOAD_IDLE, 11000);
  snapshot = feed(&ctx, 4090, XIAOXIN_BATTERY_LOAD_IDLE, 21000);
  snapshot = feed(&ctx, 4160, XIAOXIN_BATTERY_LOAD_IDLE, 31000);
  snapshot = feed(&ctx, 4090, XIAOXIN_BATTERY_LOAD_IDLE, 41000);
  snapshot = feed(&ctx, 4160, XIAOXIN_BATTERY_LOAD_IDLE, 51000);
  snapshot = feed(&ctx, 4090, XIAOXIN_BATTERY_LOAD_IDLE, 61000);
  snapshot = feed(&ctx, 4110, XIAOXIN_BATTERY_LOAD_IDLE, 71000);
  snapshot = feed(&ctx, 4120, XIAOXIN_BATTERY_LOAD_IDLE, 81000);
  snapshot = feed(&ctx, 4110, XIAOXIN_BATTERY_LOAD_IDLE, 91000);
  snapshot = feed(&ctx, 4120, XIAOXIN_BATTERY_LOAD_IDLE, 101000);
  snapshot = feed(&ctx, 4110, XIAOXIN_BATTERY_LOAD_IDLE, 111000);
  snapshot = feed(&ctx, 4120, XIAOXIN_BATTERY_LOAD_IDLE, 121000);
  snapshot = feed(&ctx, 4110, XIAOXIN_BATTERY_LOAD_IDLE, 131000);

  assert(snapshot.power_source != XIAOXIN_BATTERY_POWER_EXTERNAL);
  assert(ctx.candidate_power_count == 0);
}

static void invalid_samples_enter_unknown_after_ten_reads(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  enter_battery_from_startup(&ctx);

  xiaoxin_battery_snapshot_t snapshot = xiaoxin_battery_state_snapshot(&ctx);
  for (uint32_t now_ms = 18000; now_ms <= 27000; now_ms += 1000) {
    snapshot = xiaoxin_battery_state_update(
      &ctx,
      0,
      true,
      XIAOXIN_BATTERY_POWER_HINT_UNKNOWN,
      XIAOXIN_BATTERY_LOAD_IDLE,
      now_ms
    );
  }

  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_UNKNOWN);
  assert(snapshot.display_level != 0);
}

static void unknown_display_level_drops_after_ten_seconds(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  enter_battery_from_startup(&ctx);
  for (uint32_t now_ms = 18000; now_ms <= 27000; now_ms += 1000) {
    xiaoxin_battery_state_update(&ctx, 0, true, XIAOXIN_BATTERY_POWER_HINT_UNKNOWN, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }

  xiaoxin_battery_snapshot_t snapshot =
    xiaoxin_battery_state_update(&ctx, 0, true, XIAOXIN_BATTERY_POWER_HINT_UNKNOWN, XIAOXIN_BATTERY_LOAD_IDLE, 38000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_UNKNOWN);
  assert(snapshot.display_level == 0);
  assert(!snapshot.percent_reliable);
}

static void external_minimum_hold_blocks_early_exit(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  xiaoxin_battery_snapshot_t snapshot =
    enter_external_with_high_samples(&ctx);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);

  snapshot = feed(&ctx, 3800, XIAOXIN_BATTERY_LOAD_IDLE, 10000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);
}

static void external_invalid_samples_do_not_break_minimum_hold(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  xiaoxin_battery_snapshot_t snapshot =
    enter_external_with_high_samples(&ctx);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);
  assert(snapshot.display_level == 4);

  for (uint32_t now_ms = 7000; now_ms <= 16000; now_ms += 1000) {
    snapshot = xiaoxin_battery_state_update(
      &ctx,
      0,
      false,
      XIAOXIN_BATTERY_POWER_HINT_UNKNOWN,
      XIAOXIN_BATTERY_LOAD_IDLE,
      now_ms
    );
  }

  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);
  assert(snapshot.display_level == 4);
}

static void external_returns_to_battery_after_stable_discharge_window(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  xiaoxin_battery_snapshot_t snapshot = enter_external_with_high_samples(&ctx);
  const int voltages[] = {3950, 3890, 3920, 3860, 3900, 3880};
  for (int i = 0; i < 6; ++i) {
    snapshot = feed(
      &ctx,
      voltages[i],
      XIAOXIN_BATTERY_LOAD_IDLE,
      40000 + (uint32_t)i * 5000
    );
  }

  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(snapshot.percent_reliable);
}

static void external_exit_requires_fresh_sixty_second_discharge_window(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  xiaoxin_battery_snapshot_t snapshot = enter_external_with_high_samples(&ctx);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);

  snapshot = feed(&ctx, 3950, XIAOXIN_BATTERY_LOAD_IDLE, 10000);
  snapshot = feed(&ctx, 3890, XIAOXIN_BATTERY_LOAD_IDLE, 15000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);

  snapshot = feed(&ctx, 3900, XIAOXIN_BATTERY_LOAD_IDLE, 80000);
  snapshot = feed(&ctx, 3900, XIAOXIN_BATTERY_LOAD_IDLE, 85000);
  snapshot = feed(&ctx, 3900, XIAOXIN_BATTERY_LOAD_IDLE, 90000);
  snapshot = feed(&ctx, 3900, XIAOXIN_BATTERY_LOAD_IDLE, 95000);
  snapshot = feed(&ctx, 3900, XIAOXIN_BATTERY_LOAD_IDLE, 100000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);

  snapshot = feed(&ctx, 3950, XIAOXIN_BATTERY_LOAD_IDLE, 105000);
  snapshot = feed(&ctx, 3890, XIAOXIN_BATTERY_LOAD_IDLE, 110000);
  snapshot = feed(&ctx, 3920, XIAOXIN_BATTERY_LOAD_IDLE, 115000);
  snapshot = feed(&ctx, 3880, XIAOXIN_BATTERY_LOAD_IDLE, 120000);
  snapshot = feed(&ctx, 3910, XIAOXIN_BATTERY_LOAD_IDLE, 125000);
  snapshot = feed(&ctx, 3890, XIAOXIN_BATTERY_LOAD_IDLE, 130000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
}

static void external_to_battery_does_not_emit_low_edge_without_confirmation(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  xiaoxin_battery_snapshot_t snapshot = enter_external_with_high_samples(&ctx);
  const int voltages[] = {3500, 3440, 3490, 3430, 3480, 3470};
  for (int i = 0; i < 6; ++i) {
    snapshot = feed(
      &ctx,
      voltages[i],
      XIAOXIN_BATTERY_LOAD_IDLE,
      40000 + (uint32_t)i * 5000
    );
  }

  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(!snapshot.low_edge);
  assert(!snapshot.critical_edge);
}

static void external_exit_window_requires_continuous_valid_samples(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  xiaoxin_battery_snapshot_t snapshot = enter_external_with_high_samples(&ctx);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);

  snapshot = feed(&ctx, 3950, XIAOXIN_BATTERY_LOAD_IDLE, 40000);
  snapshot = feed(&ctx, 3890, XIAOXIN_BATTERY_LOAD_IDLE, 45000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);

  snapshot = xiaoxin_battery_state_update(
    &ctx,
    0,
    false,
    XIAOXIN_BATTERY_POWER_HINT_UNKNOWN,
    XIAOXIN_BATTERY_LOAD_IDLE,
    50000
  );
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);

  snapshot = feed(&ctx, 3920, XIAOXIN_BATTERY_LOAD_IDLE, 55000);
  snapshot = feed(&ctx, 3860, XIAOXIN_BATTERY_LOAD_IDLE, 60000);
  snapshot = feed(&ctx, 3900, XIAOXIN_BATTERY_LOAD_IDLE, 65000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL);
}

static void unknown_exit_window_requires_continuous_valid_samples(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);

  enter_battery_from_startup(&ctx);
  xiaoxin_battery_snapshot_t snapshot = xiaoxin_battery_state_snapshot(&ctx);
  for (uint32_t now_ms = 18000; now_ms <= 27000; now_ms += 1000) {
    snapshot = xiaoxin_battery_state_update(
      &ctx,
      0,
      true,
      XIAOXIN_BATTERY_POWER_HINT_UNKNOWN,
      XIAOXIN_BATTERY_LOAD_IDLE,
      now_ms
    );
  }

  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_UNKNOWN);

  snapshot = feed(&ctx, 3950, XIAOXIN_BATTERY_LOAD_IDLE, 30000);
  snapshot = feed(&ctx, 3890, XIAOXIN_BATTERY_LOAD_IDLE, 35000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_UNKNOWN);

  snapshot = xiaoxin_battery_state_update(
    &ctx,
    0,
    false,
    XIAOXIN_BATTERY_POWER_HINT_UNKNOWN,
    XIAOXIN_BATTERY_LOAD_IDLE,
    40000
  );
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_UNKNOWN);

  snapshot = feed(&ctx, 3910, XIAOXIN_BATTERY_LOAD_IDLE, 45000);
  snapshot = feed(&ctx, 3860, XIAOXIN_BATTERY_LOAD_IDLE, 50000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_UNKNOWN);
}

static void unknown_start_requires_ten_seconds_before_battery(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);

  xiaoxin_battery_snapshot_t snapshot =
    feed(&ctx, 3900, XIAOXIN_BATTERY_LOAD_IDLE, 1000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_UNKNOWN);

  snapshot = feed(&ctx, 3940, XIAOXIN_BATTERY_LOAD_IDLE, 5000);
  snapshot = feed(&ctx, 3880, XIAOXIN_BATTERY_LOAD_IDLE, 9000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_UNKNOWN);

  snapshot = feed(&ctx, 3920, XIAOXIN_BATTERY_LOAD_IDLE, 18000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_UNKNOWN);

  snapshot = feed(&ctx, 3880, XIAOXIN_BATTERY_LOAD_IDLE, 28000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(snapshot.percent_reliable);
}

static void unknown_critical_sample_starts_confirmation_immediately(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);

  feed(&ctx, 3950, XIAOXIN_BATTERY_LOAD_IDLE, 1000);
  feed(&ctx, 3940, XIAOXIN_BATTERY_LOAD_IDLE, 2000);
  feed(&ctx, 3930, XIAOXIN_BATTERY_LOAD_IDLE, 3000);
  feed(&ctx, 3920, XIAOXIN_BATTERY_LOAD_IDLE, 4000);

  xiaoxin_battery_snapshot_t snapshot =
    feed(&ctx, 3450, XIAOXIN_BATTERY_LOAD_IDLE, 5000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_UNKNOWN);

  for (uint32_t now_ms = 6000; now_ms <= 15000; now_ms += 1000) {
    snapshot = feed(&ctx, 3450, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }

  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_UNKNOWN);

  for (uint32_t now_ms = 16000; now_ms <= 25000; now_ms += 1000) {
    snapshot = feed(&ctx, 3450, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }

  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_CRITICAL);
  assert(snapshot.critical_edge);
}

static void confirmed_battery_low_state_uses_display_level_one(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  enter_battery_from_startup(&ctx);

  uint32_t now_ms = 18000;
  xiaoxin_battery_snapshot_t snapshot = feed(
    &ctx,
    3540,
    XIAOXIN_BATTERY_LOAD_IDLE,
    now_ms
  );
  for (; snapshot.state != XIAOXIN_BATTERY_STATE_LOW && now_ms <= 200000; now_ms += 5000) {
    snapshot = feed(&ctx, 3540, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }

  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_LOW);
  assert(snapshot.display_level == 1);

  snapshot = feed(&ctx, 3800, XIAOXIN_BATTERY_LOAD_IDLE, now_ms + 5000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_LOW);
  assert(snapshot.display_level == 1);
}

static void confirmed_battery_critical_state_uses_display_level_zero(void) {
  xiaoxin_battery_context_t ctx;
  xiaoxin_battery_state_init(&ctx, 0);
  enter_battery_from_startup(&ctx);

  uint32_t now_ms = 18000;
  xiaoxin_battery_snapshot_t snapshot = feed(
    &ctx,
    3450,
    XIAOXIN_BATTERY_LOAD_IDLE,
    now_ms
  );
  for (; snapshot.state != XIAOXIN_BATTERY_STATE_CRITICAL && now_ms <= 200000; now_ms += 5000) {
    snapshot = feed(&ctx, 3450, XIAOXIN_BATTERY_LOAD_IDLE, now_ms);
  }

  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_CRITICAL);
  assert(snapshot.display_level == 0);

  snapshot = feed(&ctx, 3600, XIAOXIN_BATTERY_LOAD_IDLE, now_ms + 5000);
  assert(snapshot.power_source == XIAOXIN_BATTERY_POWER_BATTERY);
  assert(snapshot.state == XIAOXIN_BATTERY_STATE_CRITICAL);
  assert(snapshot.display_level == 0);
}

int main(void) {
  init_starts_unknown();
  init_snapshot_has_display_fields();
  valid_samples_become_normal_after_confirmation();
  one_low_sample_does_not_turn_low();
  sustained_low_enters_low_once();
  low_requires_hysteresis_to_recover();
  sustained_critical_enters_critical();
  startup_sustained_low_enters_low();
  startup_sustained_critical_enters_critical();
  unknown_can_enter_critical_without_low_first();
  critical_edge_is_one_shot_until_recovery();
  voice_active_extends_low_confirmation();
  invalid_and_nonpositive_samples_do_not_change_state_or_ema();
  extreme_low_samples_do_not_change_state_or_ema();
  display_level_has_hysteresis();
  high_untrusted_samples_do_not_show_full_battery();
  high_voltage_does_not_update_ema_or_show_full();
  extreme_low_sample_does_not_update_ema_or_state();
  single_high_voltage_spike_does_not_enter_external();
  high_voltage_requires_three_qualifying_proposals();
  external_power_hint_enters_external_immediately();
  battery_power_hint_returns_from_external_after_hold();
  rapid_rise_enters_external_after_confirmation();
  steady_high_voltage_enters_external();
  alternating_sample_types_enter_external();
  steady_high_requires_low_ripple_across_full_interval();
  invalid_samples_enter_unknown_after_ten_reads();
  unknown_display_level_drops_after_ten_seconds();
  external_minimum_hold_blocks_early_exit();
  external_invalid_samples_do_not_break_minimum_hold();
  external_returns_to_battery_after_stable_discharge_window();
  external_exit_requires_fresh_sixty_second_discharge_window();
  external_to_battery_does_not_emit_low_edge_without_confirmation();
  external_exit_window_requires_continuous_valid_samples();
  unknown_exit_window_requires_continuous_valid_samples();
  unknown_start_requires_ten_seconds_before_battery();
  unknown_critical_sample_starts_confirmation_immediately();
  confirmed_battery_low_state_uses_display_level_one();
  confirmed_battery_critical_state_uses_display_level_zero();
  puts("xiaoxin_battery_state tests passed");
  return 0;
}
