#include "paopao_pet_trigger.h"

static const uint32_t k_default_reaction_ms = 1600;
static const uint32_t k_drag_reaction_ms = 900;
static const uint32_t k_alert_reaction_ms = 2200;
static const uint32_t k_sleep_idle_timeout_ms = 60000;
static const uint32_t k_idle_variant_min_ms = 20000;
static const uint32_t k_idle_variant_range_ms = 26000;

static bool time_reached(uint32_t now_ms, uint32_t target_ms) {
  return (int32_t)(now_ms - target_ms) >= 0;
}

static uint32_t next_idle_variant_time(uint32_t now_ms) {
  const uint32_t spread_ms = ((now_ms / 1000U) % 26U) * 1000U;
  return now_ms + k_idle_variant_min_ms + (spread_ms % k_idle_variant_range_ms);
}

static bool is_locked_state(paopao_pet_state_t state) {
  return state == PAOPAO_PET_STATE_FAILING;
}

static bool is_sleeping(const paopao_pet_trigger_context_t *ctx) {
  return ctx->base_state == PAOPAO_PET_STATE_SLEEPING &&
         !ctx->reaction_active;
}

static paopao_pet_state_t current_display_state(const paopao_pet_trigger_context_t *ctx) {
  return ctx->reaction_active ? ctx->reaction_state : ctx->base_state;
}

static void set_base_state(
  paopao_pet_trigger_context_t *ctx,
  paopao_pet_state_t state,
  uint32_t now_ms,
  bool clear_reaction
) {
  ctx->base_state = state;
  if (clear_reaction) {
    ctx->reaction_active = false;
  }
  if (state == PAOPAO_PET_STATE_IDLE) {
    ctx->next_idle_variant_ms = next_idle_variant_time(now_ms);
  }
}

void paopao_pet_trigger_init(paopao_pet_trigger_context_t *ctx, uint32_t now_ms) {
  ctx->base_state = PAOPAO_PET_STATE_IDLE;
  ctx->reaction_state = PAOPAO_PET_STATE_IDLE;
  ctx->displayed_state = PAOPAO_PET_STATE_IDLE;
  ctx->reaction_until_ms = 0;
  ctx->last_interaction_ms = now_ms;
  ctx->next_idle_variant_ms = now_ms + k_idle_variant_min_ms;
  ctx->reaction_active = false;
}

paopao_pet_state_t paopao_pet_trigger_play_reaction(
  paopao_pet_trigger_context_t *ctx,
  paopao_pet_state_t state,
  uint32_t duration_ms,
  uint32_t now_ms
) {
  if (is_locked_state(ctx->base_state)) {
    ctx->displayed_state = ctx->base_state;
    return ctx->displayed_state;
  }

  ctx->reaction_state = state;
  ctx->reaction_until_ms = now_ms + duration_ms;
  ctx->reaction_active = true;
  ctx->displayed_state = state;
  return ctx->displayed_state;
}

paopao_pet_state_t paopao_pet_trigger_dispatch(
  paopao_pet_trigger_context_t *ctx,
  paopao_pet_trigger_event_t event,
  uint32_t now_ms
) {
  switch (event) {
    case PAOPAO_PET_TRIGGER_NONE:
      break;
    case PAOPAO_PET_TRIGGER_IDLE:
      set_base_state(ctx, PAOPAO_PET_STATE_IDLE, now_ms, false);
      break;
    case PAOPAO_PET_TRIGGER_CONNECTING:
      ctx->last_interaction_ms = now_ms;
      break;
    case PAOPAO_PET_TRIGGER_LISTENING:
      set_base_state(ctx, PAOPAO_PET_STATE_WAITING, now_ms, true);
      ctx->last_interaction_ms = now_ms;
      break;
    case PAOPAO_PET_TRIGGER_THINKING:
      set_base_state(ctx, PAOPAO_PET_STATE_THINKING, now_ms, true);
      ctx->last_interaction_ms = now_ms;
      break;
    case PAOPAO_PET_TRIGGER_SPEAKING:
      set_base_state(ctx, PAOPAO_PET_STATE_SPEAKING, now_ms, true);
      ctx->last_interaction_ms = now_ms;
      break;
    case PAOPAO_PET_TRIGGER_TASK_DONE:
      set_base_state(ctx, PAOPAO_PET_STATE_IDLE, now_ms, false);
      ctx->last_interaction_ms = now_ms;
      return paopao_pet_trigger_play_reaction(ctx, PAOPAO_PET_STATE_DONE, k_default_reaction_ms, now_ms);
    case PAOPAO_PET_TRIGGER_ERROR:
      set_base_state(ctx, PAOPAO_PET_STATE_FAILING, now_ms, true);
      ctx->last_interaction_ms = now_ms;
      break;
    case PAOPAO_PET_TRIGGER_LOCAL_TAP:
      ctx->last_interaction_ms = now_ms;
      if (is_sleeping(ctx)) {
        set_base_state(ctx, PAOPAO_PET_STATE_IDLE, now_ms, true);
      }
      return paopao_pet_trigger_play_reaction(ctx, PAOPAO_PET_STATE_DONE, k_default_reaction_ms, now_ms);
    case PAOPAO_PET_TRIGGER_LOCAL_HOLD:
      ctx->last_interaction_ms = now_ms;
      if (ctx->base_state == PAOPAO_PET_STATE_SLEEPING) {
        set_base_state(ctx, PAOPAO_PET_STATE_IDLE, now_ms, true);
      } else {
        set_base_state(ctx, PAOPAO_PET_STATE_SLEEPING, now_ms, true);
      }
      break;
    case PAOPAO_PET_TRIGGER_LOCAL_DRAG_LEFT:
      ctx->last_interaction_ms = now_ms;
      if (is_sleeping(ctx)) {
        set_base_state(ctx, PAOPAO_PET_STATE_IDLE, now_ms, true);
      }
      return paopao_pet_trigger_play_reaction(ctx, PAOPAO_PET_STATE_JUMPING, k_drag_reaction_ms, now_ms);
    case PAOPAO_PET_TRIGGER_LOCAL_DRAG_RIGHT:
      ctx->last_interaction_ms = now_ms;
      if (is_sleeping(ctx)) {
        set_base_state(ctx, PAOPAO_PET_STATE_IDLE, now_ms, true);
      }
      return paopao_pet_trigger_play_reaction(ctx, PAOPAO_PET_STATE_JUMPING, k_drag_reaction_ms, now_ms);
    case PAOPAO_PET_TRIGGER_LOCAL_SHAKE:
      ctx->last_interaction_ms = now_ms;
      if (is_sleeping(ctx)) {
        set_base_state(ctx, PAOPAO_PET_STATE_IDLE, now_ms, true);
      }
      return paopao_pet_trigger_play_reaction(ctx, PAOPAO_PET_STATE_GIDDY, k_default_reaction_ms, now_ms);
    case PAOPAO_PET_TRIGGER_WAKE:
      set_base_state(ctx, PAOPAO_PET_STATE_WAITING, now_ms, true);
      ctx->last_interaction_ms = now_ms;
      break;
    case PAOPAO_PET_TRIGGER_SERVICE_NEUTRAL:
      break;
    case PAOPAO_PET_TRIGGER_SERVICE_HAPPY:
      if (!is_locked_state(ctx->base_state) && !is_sleeping(ctx)) {
        return paopao_pet_trigger_play_reaction(ctx, PAOPAO_PET_STATE_HAPPY, k_default_reaction_ms, now_ms);
      }
      break;
    case PAOPAO_PET_TRIGGER_SERVICE_THINKING:
      if (!is_locked_state(ctx->base_state) && !is_sleeping(ctx)) {
        return paopao_pet_trigger_play_reaction(ctx, PAOPAO_PET_STATE_THINKING, k_default_reaction_ms, now_ms);
      }
      break;
    case PAOPAO_PET_TRIGGER_SERVICE_SLEEP:
      if (!is_locked_state(ctx->base_state)) {
        set_base_state(ctx, PAOPAO_PET_STATE_SLEEPING, now_ms, true);
      }
      break;
    case PAOPAO_PET_TRIGGER_SERVICE_GIDDY:
      break;
    case PAOPAO_PET_TRIGGER_SERVICE_FAILING:
      if (!is_locked_state(ctx->base_state)) {
        return paopao_pet_trigger_play_reaction(ctx, PAOPAO_PET_STATE_FAILING, k_alert_reaction_ms, now_ms);
      }
      break;
    case PAOPAO_PET_TRIGGER_SERVICE_CRYING:
      if (!is_locked_state(ctx->base_state) && !is_sleeping(ctx)) {
        return paopao_pet_trigger_play_reaction(ctx, PAOPAO_PET_STATE_CRYING, k_alert_reaction_ms, now_ms);
      }
      break;
    case PAOPAO_PET_TRIGGER_SERVICE_ANXIOUS:
      if (!is_locked_state(ctx->base_state) && !is_sleeping(ctx)) {
        return paopao_pet_trigger_play_reaction(ctx, PAOPAO_PET_STATE_ANXIETY, k_alert_reaction_ms, now_ms);
      }
      break;
    case PAOPAO_PET_TRIGGER_SERVICE_TIRED:
      if (!is_locked_state(ctx->base_state) && !is_sleeping(ctx)) {
        return paopao_pet_trigger_play_reaction(ctx, PAOPAO_PET_STATE_TIRED, k_alert_reaction_ms, now_ms);
      }
      break;
    case PAOPAO_PET_TRIGGER_SERVICE_ANGRY:
      if (!is_locked_state(ctx->base_state) && !is_sleeping(ctx)) {
        return paopao_pet_trigger_play_reaction(ctx, PAOPAO_PET_STATE_STAMP, k_alert_reaction_ms, now_ms);
      }
      break;
  }

  ctx->displayed_state = current_display_state(ctx);
  return ctx->displayed_state;
}

paopao_pet_state_t paopao_pet_trigger_tick(paopao_pet_trigger_context_t *ctx, uint32_t now_ms) {
  if (ctx->reaction_active && time_reached(now_ms, ctx->reaction_until_ms)) {
    ctx->reaction_active = false;
  }

  if (ctx->base_state == PAOPAO_PET_STATE_IDLE &&
      now_ms - ctx->last_interaction_ms >= k_sleep_idle_timeout_ms) {
    set_base_state(ctx, PAOPAO_PET_STATE_SLEEPING, now_ms, true);
  }

  ctx->displayed_state = current_display_state(ctx);
  return ctx->displayed_state;
}
