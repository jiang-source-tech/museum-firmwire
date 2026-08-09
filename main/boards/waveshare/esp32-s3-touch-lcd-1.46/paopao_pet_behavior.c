#include "paopao_pet_behavior.h"

#include <stddef.h>
#include <string.h>

static const uint32_t k_idle_variant_min_ms = 12000;

static paopao_pet_behavior_decision_t no_decision(void) {
  const paopao_pet_behavior_decision_t decision = {
    .has_trigger = false,
    .trigger = PAOPAO_PET_TRIGGER_NONE,
  };
  return decision;
}

static paopao_pet_behavior_decision_t trigger_decision(paopao_pet_trigger_event_t trigger) {
  const paopao_pet_behavior_decision_t decision = {
    .has_trigger = trigger != PAOPAO_PET_TRIGGER_NONE,
    .trigger = trigger,
  };
  return decision;
}

static bool is_protected_voice_state(paopao_pet_behavior_voice_state_t voice_state) {
  return voice_state == PAOPAO_PET_BEHAVIOR_VOICE_LISTENING ||
         voice_state == PAOPAO_PET_BEHAVIOR_VOICE_THINKING ||
         voice_state == PAOPAO_PET_BEHAVIOR_VOICE_SPEAKING ||
         voice_state == PAOPAO_PET_BEHAVIOR_VOICE_SLEEPING ||
         voice_state == PAOPAO_PET_BEHAVIOR_VOICE_FAILING;
}

static bool is_ignorable_service_trigger(paopao_pet_trigger_event_t trigger) {
  return trigger == PAOPAO_PET_TRIGGER_NONE ||
         trigger == PAOPAO_PET_TRIGGER_SERVICE_NEUTRAL ||
         trigger == PAOPAO_PET_TRIGGER_SERVICE_GIDDY;
}

static uint32_t next_idle_variant_time(uint32_t now_ms) {
  return now_ms + k_idle_variant_min_ms;
}

static bool time_reached(uint32_t now_ms, uint32_t target_ms) {
  return (int32_t)(now_ms - target_ms) >= 0;
}

static paopao_pet_trigger_event_t idle_variant_at(uint8_t index) {
  static const paopao_pet_trigger_event_t variants[] = {
    PAOPAO_PET_TRIGGER_SERVICE_THINKING,
    PAOPAO_PET_TRIGGER_SERVICE_HAPPY,
    PAOPAO_PET_TRIGGER_SERVICE_TIRED,
  };
  return variants[index % (uint8_t)(sizeof(variants) / sizeof(variants[0]))];
}

void paopao_pet_behavior_init(paopao_pet_behavior_context_t *ctx, uint32_t now_ms) {
  if (ctx == NULL) {
    return;
  }

  memset(ctx, 0, sizeof(*ctx));
  ctx->voice_state = PAOPAO_PET_BEHAVIOR_VOICE_IDLE;
  ctx->pending_service_trigger = PAOPAO_PET_TRIGGER_NONE;
  ctx->last_idle_variant_state = PAOPAO_PET_STATE_IDLE;
  ctx->last_interaction_ms = now_ms;
  ctx->next_idle_variant_ms = next_idle_variant_time(now_ms);
}

void paopao_pet_behavior_set_voice_state(
  paopao_pet_behavior_context_t *ctx,
  paopao_pet_behavior_voice_state_t voice_state,
  uint32_t now_ms
) {
  if (ctx == NULL) {
    return;
  }

  if (ctx->voice_state != voice_state) {
    ctx->voice_state = voice_state;
    ctx->last_interaction_ms = now_ms;
    ctx->next_idle_variant_ms = next_idle_variant_time(now_ms);
  }
}

paopao_pet_behavior_decision_t paopao_pet_behavior_handle_service_trigger(
  paopao_pet_behavior_context_t *ctx,
  paopao_pet_trigger_event_t trigger,
  uint32_t now_ms
) {
  if (ctx == NULL || is_ignorable_service_trigger(trigger)) {
    return no_decision();
  }

  if (is_protected_voice_state(ctx->voice_state)) {
    ctx->pending_service_trigger = trigger;
    return no_decision();
  }

  ctx->last_interaction_ms = now_ms;
  ctx->next_idle_variant_ms = next_idle_variant_time(now_ms);
  return trigger_decision(trigger);
}

void paopao_pet_behavior_record_interaction(
  paopao_pet_behavior_context_t *ctx,
  uint32_t now_ms
) {
  if (ctx == NULL) {
    return;
  }

  ctx->last_interaction_ms = now_ms;
  ctx->next_idle_variant_ms = next_idle_variant_time(now_ms);
}

paopao_pet_behavior_decision_t paopao_pet_behavior_tick(
  paopao_pet_behavior_context_t *ctx,
  uint32_t now_ms
) {
  if (ctx == NULL) {
    return no_decision();
  }

  if (ctx->voice_state != PAOPAO_PET_BEHAVIOR_VOICE_IDLE) {
    return no_decision();
  }

  if (ctx->pending_service_trigger != PAOPAO_PET_TRIGGER_NONE) {
    const paopao_pet_trigger_event_t pending = ctx->pending_service_trigger;
    ctx->pending_service_trigger = PAOPAO_PET_TRIGGER_NONE;
    ctx->last_interaction_ms = now_ms;
    ctx->next_idle_variant_ms = next_idle_variant_time(now_ms);
    return trigger_decision(pending);
  }

  if (!time_reached(now_ms, ctx->next_idle_variant_ms)) {
    return no_decision();
  }

  const paopao_pet_trigger_event_t trigger = idle_variant_at(ctx->idle_variant_index);
  ctx->idle_variant_index++;
  ctx->last_idle_variant_ms = now_ms;
  ctx->next_idle_variant_ms = next_idle_variant_time(now_ms);
  return trigger_decision(trigger);
}
