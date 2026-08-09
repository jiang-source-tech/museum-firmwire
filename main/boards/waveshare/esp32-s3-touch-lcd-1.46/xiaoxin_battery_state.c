#include "xiaoxin_battery_state.h"

#include "xiaoxin_battery_level.h"

#include <stddef.h>

enum {
  k_unknown_to_normal_percent = 25,
  k_normal_to_low_percent = 20,
  k_low_to_normal_percent = 30,
  k_low_to_critical_percent = 10,
  k_critical_to_low_percent = 18,
  k_unknown_to_normal_ms = 5000,
  k_normal_to_low_idle_ms = 20000,
  k_normal_to_low_voice_ms = 45000,
  k_low_to_normal_ms = 10000,
  k_low_to_critical_idle_ms = 10000,
  k_low_to_critical_voice_ms = 30000,
  k_critical_to_low_ms = 15000,
  k_max_valid_mv = 4400,
  k_unknown_display_hold_ms = 10000,
  k_discharge_window_ms = 60000,
  k_external_minimum_hold_ms = 30000,
  k_external_exit_confirm_ms = 20000,
  k_unknown_exit_confirm_ms = 10000,
};

typedef enum {
  XIAOXIN_BATTERY_SAMPLE_INVALID = 0,
  XIAOXIN_BATTERY_SAMPLE_EXTREME_LOW,
  XIAOXIN_BATTERY_SAMPLE_VALID,
  XIAOXIN_BATTERY_SAMPLE_HIGH,
} xiaoxin_battery_sample_quality_t;

static xiaoxin_battery_snapshot_t make_snapshot(
  const xiaoxin_battery_context_t* ctx,
  bool low_edge,
  bool critical_edge,
  bool recovered_edge
) {
  xiaoxin_battery_snapshot_t snapshot = {
    .state = ctx->state,
    .power_source = ctx->power_source,
    .estimated_percent = ctx->estimated_percent,
    .display_percent = ctx->display_percent,
    .display_level = ctx->display_level,
    .percent_reliable = ctx->percent_reliable,
    .smoothed_voltage_mv = ctx->smoothed_voltage_mv,
    .low_edge = low_edge,
    .critical_edge = critical_edge,
    .recovered_edge = recovered_edge,
  };
  return snapshot;
}

static bool elapsed(uint32_t since_ms, uint32_t now_ms, uint32_t required_ms) {
  return (uint32_t)(now_ms - since_ms) >= required_ms;
}

static void reset_candidate(
  xiaoxin_battery_context_t* ctx,
  xiaoxin_battery_state_t candidate,
  uint32_t now_ms
) {
  if (ctx->candidate_state != candidate) {
    ctx->candidate_state = candidate;
    ctx->candidate_since_ms = now_ms;
  }
}

static xiaoxin_battery_sample_quality_t classify_sample(
  int voltage_mv,
  bool sample_valid
) {
  if (!sample_valid || voltage_mv <= 0) {
    return XIAOXIN_BATTERY_SAMPLE_INVALID;
  }
  if (voltage_mv < 3300) {
    return XIAOXIN_BATTERY_SAMPLE_EXTREME_LOW;
  }
  if (voltage_mv > k_max_valid_mv) {
    return XIAOXIN_BATTERY_SAMPLE_HIGH;
  }
  return XIAOXIN_BATTERY_SAMPLE_VALID;
}

static void update_smoothed_sample(xiaoxin_battery_context_t* ctx, int voltage_mv) {
  if (!ctx->has_sample) {
    ctx->has_sample = true;
    ctx->smoothed_voltage_mv = voltage_mv;
  } else {
    ctx->smoothed_voltage_mv = (ctx->smoothed_voltage_mv * 85 + voltage_mv * 15 + 50) / 100;
  }
  ctx->estimated_percent = xiaoxin_battery_percent_from_mv(ctx->smoothed_voltage_mv);
}

static bool has_normal_discharge_feature(const xiaoxin_battery_context_t* ctx) {
  return ctx->discharge_window_max_mv > 0 &&
         ctx->discharge_window_min_mv > 0 &&
         ctx->discharge_window_max_mv - ctx->discharge_window_min_mv >= 50;
}

static void reset_discharge_window(xiaoxin_battery_context_t* ctx) {
  ctx->discharge_window_start_ms = 0;
  ctx->discharge_window_min_mv = 0;
  ctx->discharge_window_max_mv = 0;
}

static void update_discharge_window(
  xiaoxin_battery_context_t* ctx,
  int voltage_mv,
  uint32_t now_ms
) {
  if (ctx->discharge_window_start_ms == 0 ||
      elapsed(ctx->discharge_window_start_ms, now_ms, k_discharge_window_ms)) {
    ctx->discharge_window_start_ms = now_ms;
    ctx->discharge_window_min_mv = voltage_mv;
    ctx->discharge_window_max_mv = voltage_mv;
    return;
  }
  if (ctx->discharge_window_min_mv == 0 || voltage_mv < ctx->discharge_window_min_mv) {
    ctx->discharge_window_min_mv = voltage_mv;
  }
  if (ctx->discharge_window_max_mv == 0 || voltage_mv > ctx->discharge_window_max_mv) {
    ctx->discharge_window_max_mv = voltage_mv;
  }
}

static uint8_t display_level_for_percent(uint8_t current_level, int percent) {
  if (current_level >= 4) {
    return percent < 65 ? 3 : 4;
  }
  if (current_level == 3) {
    if (percent >= 70) {
      return 4;
    }
    return percent < 35 ? 2 : 3;
  }
  if (current_level == 2) {
    if (percent >= 40) {
      return 3;
    }
    return percent < 10 ? 1 : 2;
  }
  if (current_level == 1) {
    return percent >= 20 ? 2 : 1;
  }
  if (percent >= 70) {
    return 4;
  }
  if (percent >= 40) {
    return 3;
  }
  if (percent >= 15) {
    return 2;
  }
  return 1;
}

static void enforce_fixed_battery_display_level(xiaoxin_battery_context_t* ctx) {
  if (ctx->power_source != XIAOXIN_BATTERY_POWER_BATTERY) {
    return;
  }
  if (ctx->state == XIAOXIN_BATTERY_STATE_LOW) {
    ctx->display_level = 1;
  } else if (ctx->state == XIAOXIN_BATTERY_STATE_CRITICAL) {
    ctx->display_level = 0;
  }
}

static uint32_t required_ms_for(
  xiaoxin_battery_state_t from,
  xiaoxin_battery_state_t to,
  xiaoxin_battery_load_t load
) {
  if (from == XIAOXIN_BATTERY_STATE_UNKNOWN && to == XIAOXIN_BATTERY_STATE_NORMAL) {
    return k_unknown_to_normal_ms;
  }
  if (from == XIAOXIN_BATTERY_STATE_UNKNOWN && to == XIAOXIN_BATTERY_STATE_LOW) {
    return load == XIAOXIN_BATTERY_LOAD_VOICE_ACTIVE
      ? k_normal_to_low_voice_ms
      : k_normal_to_low_idle_ms;
  }
  if (from == XIAOXIN_BATTERY_STATE_UNKNOWN && to == XIAOXIN_BATTERY_STATE_CRITICAL) {
    return load == XIAOXIN_BATTERY_LOAD_VOICE_ACTIVE
      ? k_low_to_critical_voice_ms
      : k_low_to_critical_idle_ms;
  }
  if (from == XIAOXIN_BATTERY_STATE_NORMAL && to == XIAOXIN_BATTERY_STATE_LOW) {
    return load == XIAOXIN_BATTERY_LOAD_VOICE_ACTIVE
      ? k_normal_to_low_voice_ms
      : k_normal_to_low_idle_ms;
  }
  if (from == XIAOXIN_BATTERY_STATE_LOW && to == XIAOXIN_BATTERY_STATE_NORMAL) {
    return k_low_to_normal_ms;
  }
  if (from == XIAOXIN_BATTERY_STATE_LOW && to == XIAOXIN_BATTERY_STATE_CRITICAL) {
    return load == XIAOXIN_BATTERY_LOAD_VOICE_ACTIVE
      ? k_low_to_critical_voice_ms
      : k_low_to_critical_idle_ms;
  }
  if (from == XIAOXIN_BATTERY_STATE_CRITICAL && to == XIAOXIN_BATTERY_STATE_LOW) {
    return k_critical_to_low_ms;
  }
  return 0;
}

static xiaoxin_battery_state_t desired_state_for(
  const xiaoxin_battery_context_t* ctx
) {
  const int percent = ctx->estimated_percent;
  switch (ctx->state) {
    case XIAOXIN_BATTERY_STATE_UNKNOWN:
      if (percent <= k_low_to_critical_percent) {
        return XIAOXIN_BATTERY_STATE_CRITICAL;
      }
      if (percent <= k_normal_to_low_percent) {
        return XIAOXIN_BATTERY_STATE_LOW;
      }
      if (percent >= k_unknown_to_normal_percent) {
        return XIAOXIN_BATTERY_STATE_NORMAL;
      }
      return XIAOXIN_BATTERY_STATE_UNKNOWN;
    case XIAOXIN_BATTERY_STATE_NORMAL:
      return percent <= k_normal_to_low_percent
        ? XIAOXIN_BATTERY_STATE_LOW
        : XIAOXIN_BATTERY_STATE_NORMAL;
    case XIAOXIN_BATTERY_STATE_LOW:
      if (percent <= k_low_to_critical_percent) {
        return XIAOXIN_BATTERY_STATE_CRITICAL;
      }
      if (percent >= k_low_to_normal_percent) {
        return XIAOXIN_BATTERY_STATE_NORMAL;
      }
      return XIAOXIN_BATTERY_STATE_LOW;
    case XIAOXIN_BATTERY_STATE_CRITICAL:
      return percent >= k_critical_to_low_percent
        ? XIAOXIN_BATTERY_STATE_LOW
        : XIAOXIN_BATTERY_STATE_CRITICAL;
  }
  return XIAOXIN_BATTERY_STATE_UNKNOWN;
}

static void propose_power_source(
  xiaoxin_battery_context_t* ctx,
  xiaoxin_battery_power_source_t source,
  uint32_t now_ms
) {
  if (ctx->candidate_power_source != source) {
    ctx->candidate_power_source = source;
    ctx->candidate_power_count = 1;
    return;
  }
  if (ctx->candidate_power_count < 3) {
    ctx->candidate_power_count++;
  }
  if (ctx->candidate_power_count >= 3 && ctx->power_source != source) {
    ctx->power_source = source;
    ctx->unknown_since_ms = 0;
    ctx->battery_candidate_since_ms = 0;
    ctx->power_source_since_ms = now_ms;
    reset_discharge_window(ctx);
  }
}

static void reset_power_source_candidate(
  xiaoxin_battery_context_t* ctx,
  xiaoxin_battery_power_source_t source
) {
  ctx->candidate_power_source = source;
  ctx->candidate_power_count = 0;
}

static void confirm_power_source(
  xiaoxin_battery_context_t* ctx,
  xiaoxin_battery_power_source_t source,
  uint32_t now_ms
) {
  if (ctx->power_source == source) {
    ctx->candidate_power_source = source;
    ctx->candidate_power_count = 0;
    return;
  }

  ctx->power_source = source;
  ctx->candidate_power_source = source;
  ctx->candidate_power_count = 0;
  ctx->power_source_since_ms = now_ms;
  ctx->unknown_since_ms = 0;
  ctx->battery_candidate_since_ms = 0;
  reset_discharge_window(ctx);

  if (source == XIAOXIN_BATTERY_POWER_BATTERY) {
    ctx->state_edges_suppressed_until_reconfirmed = true;
    ctx->candidate_state = ctx->state;
    ctx->candidate_since_ms = now_ms;
  } else if (source == XIAOXIN_BATTERY_POWER_EXTERNAL) {
    ctx->percent_reliable = false;
    ctx->display_level = 4;
    ctx->state_edges_suppressed_until_reconfirmed = false;
  }
}

void xiaoxin_battery_state_init(
  xiaoxin_battery_context_t* ctx,
  uint32_t now_ms
) {
  if (ctx == NULL) {
    return;
  }
  ctx->state = XIAOXIN_BATTERY_STATE_UNKNOWN;
  ctx->power_source = XIAOXIN_BATTERY_POWER_UNKNOWN;
  ctx->candidate_power_source = XIAOXIN_BATTERY_POWER_UNKNOWN;
  ctx->estimated_percent = 0;
  ctx->display_percent = 0;
  ctx->display_level = 0;
  ctx->percent_reliable = false;
  ctx->smoothed_voltage_mv = 0;
  ctx->has_sample = false;
  ctx->candidate_since_ms = now_ms;
  ctx->candidate_power_count = 0;
  ctx->power_source_since_ms = now_ms;
  ctx->invalid_sample_count = 0;
  ctx->unknown_since_ms = 0;
  ctx->battery_candidate_since_ms = 0;
  ctx->high_sample_count = 0;
  ctx->trend_window_min_mv = 0;
  ctx->trend_window_max_mv = 0;
  ctx->trend_window_start_ms = now_ms;
  ctx->discharge_window_min_mv = 0;
  ctx->discharge_window_max_mv = 0;
  ctx->discharge_window_start_ms = now_ms;
  ctx->rise_window_start_mv = 0;
  ctx->rise_window_start_ms = now_ms;
  ctx->steady_high_since_ms = 0;
  ctx->alternation_window_start_ms = now_ms;
  ctx->alternating_sample_count = 0;
  ctx->last_sample_quality = XIAOXIN_BATTERY_SAMPLE_INVALID;
  ctx->state_edges_suppressed_until_reconfirmed = false;
  ctx->candidate_state = XIAOXIN_BATTERY_STATE_UNKNOWN;
  ctx->last_snapshot = make_snapshot(ctx, false, false, false);
}

xiaoxin_battery_snapshot_t xiaoxin_battery_state_snapshot(
  const xiaoxin_battery_context_t* ctx
) {
  if (ctx == NULL) {
    xiaoxin_battery_context_t empty;
    xiaoxin_battery_state_init(&empty, 0);
    return empty.last_snapshot;
  }
  return ctx->last_snapshot;
}

xiaoxin_battery_snapshot_t xiaoxin_battery_state_update(
  xiaoxin_battery_context_t* ctx,
  int voltage_mv,
  bool sample_valid,
  xiaoxin_battery_power_hint_t power_hint,
  xiaoxin_battery_load_t load,
  uint32_t now_ms
) {
  if (ctx == NULL) {
    xiaoxin_battery_context_t empty;
    xiaoxin_battery_state_init(&empty, now_ms);
    return empty.last_snapshot;
  }

  bool low_edge = false;
  bool critical_edge = false;
  bool recovered_edge = false;

  const xiaoxin_battery_sample_quality_t quality =
    classify_sample(voltage_mv, sample_valid);

  if (quality == XIAOXIN_BATTERY_SAMPLE_HIGH) {
    ctx->high_sample_count++;
  } else {
    ctx->high_sample_count = 0;
  }

  if (quality == XIAOXIN_BATTERY_SAMPLE_INVALID) {
    ctx->invalid_sample_count++;
  } else {
    ctx->invalid_sample_count = 0;
  }

  if (ctx->invalid_sample_count >= 10 &&
      ctx->power_source != XIAOXIN_BATTERY_POWER_UNKNOWN) {
    const bool external_hold_active =
      ctx->power_source == XIAOXIN_BATTERY_POWER_EXTERNAL &&
      !elapsed(ctx->power_source_since_ms, now_ms, k_external_minimum_hold_ms);
    if (!external_hold_active) {
    ctx->power_source = XIAOXIN_BATTERY_POWER_UNKNOWN;
    ctx->candidate_power_source = XIAOXIN_BATTERY_POWER_UNKNOWN;
    ctx->candidate_power_count = 0;
    ctx->power_source_since_ms = now_ms;
    ctx->unknown_since_ms = now_ms;
    ctx->battery_candidate_since_ms = 0;
    ctx->percent_reliable = false;
    ctx->state_edges_suppressed_until_reconfirmed = false;
    reset_discharge_window(ctx);
    }
  }

  const bool alternating_candidate =
    quality == XIAOXIN_BATTERY_SAMPLE_VALID ||
    quality == XIAOXIN_BATTERY_SAMPLE_HIGH ||
    quality == XIAOXIN_BATTERY_SAMPLE_INVALID;

  if (!alternating_candidate ||
      (uint32_t)(now_ms - ctx->alternation_window_start_ms) > 10000) {
    ctx->alternation_window_start_ms = now_ms;
    ctx->alternating_sample_count = 0;
  } else if (ctx->last_sample_quality != quality &&
             ctx->last_sample_quality != XIAOXIN_BATTERY_SAMPLE_EXTREME_LOW) {
    ctx->alternating_sample_count++;
  }
  ctx->last_sample_quality = (uint8_t)quality;

  if (quality == XIAOXIN_BATTERY_SAMPLE_VALID) {
    update_smoothed_sample(ctx, voltage_mv);

    if (ctx->rise_window_start_mv == 0 ||
        (uint32_t)(now_ms - ctx->rise_window_start_ms) > 60000) {
      ctx->rise_window_start_ms = now_ms;
      ctx->rise_window_start_mv = ctx->smoothed_voltage_mv;
    }

    if (ctx->smoothed_voltage_mv > 4080) {
      if (ctx->steady_high_since_ms == 0) {
        ctx->steady_high_since_ms = now_ms;
        ctx->trend_window_start_ms = now_ms;
        ctx->trend_window_min_mv = voltage_mv;
        ctx->trend_window_max_mv = voltage_mv;
      } else {
        if (voltage_mv < ctx->trend_window_min_mv) {
          ctx->trend_window_min_mv = voltage_mv;
        }
        if (voltage_mv > ctx->trend_window_max_mv) {
          ctx->trend_window_max_mv = voltage_mv;
        }
      }
    } else if (ctx->power_source != XIAOXIN_BATTERY_POWER_EXTERNAL &&
               !(ctx->power_source == XIAOXIN_BATTERY_POWER_UNKNOWN &&
                 ctx->unknown_since_ms != 0)) {
      ctx->steady_high_since_ms = 0;
      ctx->trend_window_start_ms = 0;
      ctx->trend_window_min_mv = 0;
      ctx->trend_window_max_mv = 0;
    }
  }

  if (power_hint == XIAOXIN_BATTERY_POWER_HINT_EXTERNAL) {
    confirm_power_source(ctx, XIAOXIN_BATTERY_POWER_EXTERNAL, now_ms);
  } else if (
    power_hint == XIAOXIN_BATTERY_POWER_HINT_BATTERY &&
    (
      ctx->power_source != XIAOXIN_BATTERY_POWER_EXTERNAL ||
      elapsed(ctx->power_source_since_ms, now_ms, k_external_minimum_hold_ms)
    )
  ) {
    confirm_power_source(ctx, XIAOXIN_BATTERY_POWER_BATTERY, now_ms);
  }

  const bool condition_a = ctx->high_sample_count >= 3;
  const bool condition_b =
    quality == XIAOXIN_BATTERY_SAMPLE_VALID &&
    (uint32_t)(now_ms - ctx->rise_window_start_ms) <= 60000 &&
    ctx->smoothed_voltage_mv - ctx->rise_window_start_mv > 300 &&
    ctx->smoothed_voltage_mv > 4050;
  const bool condition_c =
    quality == XIAOXIN_BATTERY_SAMPLE_VALID &&
    ctx->steady_high_since_ms != 0 &&
    elapsed(ctx->steady_high_since_ms, now_ms, 120000) &&
    ctx->smoothed_voltage_mv > 4080 &&
    ctx->trend_window_max_mv - ctx->trend_window_min_mv < 50;
  const bool condition_d = ctx->alternating_sample_count >= 3;

  if (condition_a || condition_b || condition_c || condition_d) {
    propose_power_source(ctx, XIAOXIN_BATTERY_POWER_EXTERNAL, now_ms);
  } else if (ctx->candidate_power_source == XIAOXIN_BATTERY_POWER_EXTERNAL) {
    reset_power_source_candidate(ctx, ctx->power_source);
  }

  if (ctx->power_source == XIAOXIN_BATTERY_POWER_EXTERNAL &&
      quality == XIAOXIN_BATTERY_SAMPLE_VALID) {
    update_discharge_window(ctx, voltage_mv, now_ms);
    if (elapsed(ctx->power_source_since_ms, now_ms, k_external_minimum_hold_ms) &&
        ctx->smoothed_voltage_mv < 4080 &&
        has_normal_discharge_feature(ctx)) {
      if (ctx->battery_candidate_since_ms == 0) {
        ctx->battery_candidate_since_ms = now_ms;
      }
      if (elapsed(
            ctx->battery_candidate_since_ms,
            now_ms,
            k_external_exit_confirm_ms
          )) {
        ctx->power_source = XIAOXIN_BATTERY_POWER_BATTERY;
        ctx->candidate_power_source = XIAOXIN_BATTERY_POWER_BATTERY;
        ctx->candidate_power_count = 0;
        ctx->power_source_since_ms = now_ms;
        ctx->unknown_since_ms = 0;
        ctx->battery_candidate_since_ms = 0;
        ctx->state_edges_suppressed_until_reconfirmed = true;
        reset_discharge_window(ctx);
        ctx->candidate_state = ctx->state;
        ctx->candidate_since_ms = now_ms;
      }
    } else {
      ctx->battery_candidate_since_ms = 0;
    }
  } else if (ctx->power_source == XIAOXIN_BATTERY_POWER_UNKNOWN &&
             quality == XIAOXIN_BATTERY_SAMPLE_VALID) {
    if (ctx->unknown_since_ms == 0) {
      ctx->unknown_since_ms = now_ms;
    }
    update_discharge_window(ctx, voltage_mv, now_ms);
    const bool critically_low_percent =
      xiaoxin_battery_percent_from_mv(voltage_mv) <= k_low_to_critical_percent;
    if ((ctx->smoothed_voltage_mv < 4080 && has_normal_discharge_feature(ctx)) ||
        critically_low_percent) {
      if (ctx->battery_candidate_since_ms == 0) {
        ctx->battery_candidate_since_ms = now_ms;
      }
      if (elapsed(
            ctx->battery_candidate_since_ms,
            now_ms,
            k_unknown_exit_confirm_ms
          )) {
        ctx->power_source = XIAOXIN_BATTERY_POWER_BATTERY;
        ctx->candidate_power_source = XIAOXIN_BATTERY_POWER_BATTERY;
        ctx->candidate_power_count = 0;
        ctx->power_source_since_ms = now_ms;
        ctx->unknown_since_ms = 0;
        ctx->battery_candidate_since_ms = 0;
        ctx->state_edges_suppressed_until_reconfirmed = true;
        reset_discharge_window(ctx);
        ctx->candidate_state = ctx->state;
        ctx->candidate_since_ms = now_ms;
      }
    } else {
      ctx->battery_candidate_since_ms = 0;
    }
  }

  if (ctx->power_source == XIAOXIN_BATTERY_POWER_EXTERNAL) {
    ctx->percent_reliable = false;
    ctx->display_level = 4;
  } else if (ctx->power_source == XIAOXIN_BATTERY_POWER_UNKNOWN &&
             ctx->unknown_since_ms != 0) {
    ctx->percent_reliable = false;
    if (elapsed(ctx->unknown_since_ms, now_ms, k_unknown_display_hold_ms)) {
      ctx->display_level = 0;
    }
  } else if (quality == XIAOXIN_BATTERY_SAMPLE_VALID) {
    ctx->percent_reliable = true;
    ctx->display_percent = ctx->estimated_percent;
    ctx->display_level = display_level_for_percent(
      ctx->display_level,
      ctx->display_percent
    );
  }

  if (quality != XIAOXIN_BATTERY_SAMPLE_VALID) {
    if (ctx->power_source == XIAOXIN_BATTERY_POWER_EXTERNAL ||
        ctx->power_source == XIAOXIN_BATTERY_POWER_UNKNOWN) {
      ctx->battery_candidate_since_ms = 0;
    }
    if (ctx->power_source == XIAOXIN_BATTERY_POWER_UNKNOWN &&
        ctx->unknown_since_ms != 0) {
      ctx->percent_reliable = false;
      if (elapsed(ctx->unknown_since_ms, now_ms, k_unknown_display_hold_ms)) {
        ctx->display_level = 0;
      }
    }
    ctx->candidate_state = ctx->state;
    ctx->candidate_since_ms = now_ms;
    enforce_fixed_battery_display_level(ctx);
    ctx->last_snapshot = make_snapshot(ctx, false, false, false);
    return ctx->last_snapshot;
  }

  if (ctx->power_source != XIAOXIN_BATTERY_POWER_BATTERY) {
    ctx->candidate_state = ctx->state;
    ctx->candidate_since_ms = now_ms;
    enforce_fixed_battery_display_level(ctx);
    ctx->last_snapshot = make_snapshot(ctx, false, false, false);
    return ctx->last_snapshot;
  }

  const xiaoxin_battery_state_t desired = desired_state_for(ctx);
  if (desired == ctx->state) {
    ctx->candidate_state = ctx->state;
    ctx->candidate_since_ms = now_ms;
    if (ctx->state_edges_suppressed_until_reconfirmed) {
      ctx->state_edges_suppressed_until_reconfirmed = false;
    }
    enforce_fixed_battery_display_level(ctx);
    ctx->last_snapshot = make_snapshot(ctx, false, false, false);
    return ctx->last_snapshot;
  }

  reset_candidate(ctx, desired, now_ms);
  const uint32_t required_ms = required_ms_for(ctx->state, desired, load);
  if (required_ms == 0 || elapsed(ctx->candidate_since_ms, now_ms, required_ms)) {
    const xiaoxin_battery_state_t previous = ctx->state;
    ctx->state = desired;
    ctx->candidate_state = desired;
    ctx->candidate_since_ms = now_ms;
    low_edge = desired == XIAOXIN_BATTERY_STATE_LOW &&
               previous != XIAOXIN_BATTERY_STATE_LOW;
    critical_edge = desired == XIAOXIN_BATTERY_STATE_CRITICAL &&
                    previous != XIAOXIN_BATTERY_STATE_CRITICAL;
    recovered_edge =
      previous == XIAOXIN_BATTERY_STATE_CRITICAL &&
      desired == XIAOXIN_BATTERY_STATE_LOW;
  }

  if (ctx->state_edges_suppressed_until_reconfirmed && desired == ctx->state) {
    ctx->state_edges_suppressed_until_reconfirmed = false;
  }

  if (ctx->power_source != XIAOXIN_BATTERY_POWER_BATTERY ||
      ctx->state_edges_suppressed_until_reconfirmed) {
    low_edge = false;
    critical_edge = false;
    recovered_edge = false;
  }

  enforce_fixed_battery_display_level(ctx);

  ctx->last_snapshot = make_snapshot(ctx, low_edge, critical_edge, recovered_edge);
  return ctx->last_snapshot;
}
