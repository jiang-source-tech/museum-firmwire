#include "paopao_pet_state.h"

#include <stdbool.h>

static const uint32_t k_reaction_timeout_ms = 2200;
static const uint32_t k_sleep_idle_timeout_ms = 60000;

static void enter_state(paopao_pet_context_t *ctx, paopao_pet_state_t state, uint32_t now_ms) {
  ctx->state = state;
  ctx->entered_at_ms = now_ms;
}

void paopao_pet_state_init(paopao_pet_context_t *ctx, uint32_t now_ms) {
  ctx->state = PAOPAO_PET_STATE_IDLE;
  ctx->entered_at_ms = now_ms;
  ctx->last_interaction_ms = now_ms;
}

paopao_pet_state_t paopao_pet_dispatch_event(
  paopao_pet_context_t *ctx,
  paopao_pet_event_t event,
  uint32_t now_ms
) {
  switch (event) {
    case PAOPAO_PET_EVENT_WORK_STARTED:
      ctx->last_interaction_ms = now_ms;
      enter_state(ctx, PAOPAO_PET_STATE_WORKING, now_ms);
      break;
    case PAOPAO_PET_EVENT_THINKING:
      ctx->last_interaction_ms = now_ms;
      enter_state(ctx, PAOPAO_PET_STATE_THINKING, now_ms);
      break;
    case PAOPAO_PET_EVENT_WAITING_FOR_INPUT:
      ctx->last_interaction_ms = now_ms;
      enter_state(ctx, PAOPAO_PET_STATE_WAITING, now_ms);
      break;
    case PAOPAO_PET_EVENT_TASK_DONE:
      ctx->last_interaction_ms = now_ms;
      enter_state(ctx, PAOPAO_PET_STATE_DONE, now_ms);
      break;
    case PAOPAO_PET_EVENT_SHAKE:
      ctx->last_interaction_ms = now_ms;
      enter_state(ctx, PAOPAO_PET_STATE_GIDDY, now_ms);
      break;
    case PAOPAO_PET_EVENT_SLEEP_REQUEST:
      ctx->last_interaction_ms = now_ms;
      enter_state(ctx, PAOPAO_PET_STATE_SLEEPING, now_ms);
      break;
    case PAOPAO_PET_EVENT_STILL_TIMEOUT:
      enter_state(ctx, PAOPAO_PET_STATE_IDLE, now_ms);
      break;
    case PAOPAO_PET_EVENT_WAKE_BUTTON:
      ctx->last_interaction_ms = now_ms;
      enter_state(ctx, PAOPAO_PET_STATE_WAITING, now_ms);
      break;
  }

  return ctx->state;
}

paopao_pet_state_t paopao_pet_tick(paopao_pet_context_t *ctx, uint32_t now_ms) {
  const uint32_t state_age_ms = now_ms - ctx->entered_at_ms;
  const uint32_t quiet_age_ms = now_ms - ctx->last_interaction_ms;
  const bool transient_state =
    ctx->state == PAOPAO_PET_STATE_THINKING ||
    ctx->state == PAOPAO_PET_STATE_DONE ||
    ctx->state == PAOPAO_PET_STATE_GIDDY;

  if (transient_state && state_age_ms >= k_reaction_timeout_ms) {
    return paopao_pet_dispatch_event(ctx, PAOPAO_PET_EVENT_STILL_TIMEOUT, now_ms);
  }

  if (ctx->state == PAOPAO_PET_STATE_IDLE && quiet_age_ms >= k_sleep_idle_timeout_ms) {
    enter_state(ctx, PAOPAO_PET_STATE_SLEEPING, now_ms);
  }

  return ctx->state;
}

const char *paopao_pet_state_name(paopao_pet_state_t state) {
  switch (state) {
    case PAOPAO_PET_STATE_IDLE:
      return "idle";
    case PAOPAO_PET_STATE_WORKING:
      return "working";
    case PAOPAO_PET_STATE_SPEAKING:
      return "speaking";
    case PAOPAO_PET_STATE_THINKING:
      return "thinking";
    case PAOPAO_PET_STATE_WAITING:
      return "waiting";
    case PAOPAO_PET_STATE_DONE:
      return "done";
    case PAOPAO_PET_STATE_SLEEPING:
      return "sleeping";
    case PAOPAO_PET_STATE_JUMPING:
      return "jumping";
    case PAOPAO_PET_STATE_FAILING:
      return "failing";
    case PAOPAO_PET_STATE_GIDDY:
      return "giddy";
    case PAOPAO_PET_STATE_REVIEW:
      return "review";
    case PAOPAO_PET_STATE_HAPPY:
      return "happy";
    case PAOPAO_PET_STATE_CRYING:
      return "crying";
    case PAOPAO_PET_STATE_ANXIETY:
      return "anxiety";
    case PAOPAO_PET_STATE_TIRED:
      return "tired";
    case PAOPAO_PET_STATE_STAMP:
      return "stamp";
  }

  return "unknown";
}
