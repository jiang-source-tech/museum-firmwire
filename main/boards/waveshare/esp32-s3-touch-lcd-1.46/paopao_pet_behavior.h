#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "paopao_pet_trigger.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  PAOPAO_PET_BEHAVIOR_VOICE_IDLE = 0,
  PAOPAO_PET_BEHAVIOR_VOICE_LISTENING,
  PAOPAO_PET_BEHAVIOR_VOICE_THINKING,
  PAOPAO_PET_BEHAVIOR_VOICE_SPEAKING,
  PAOPAO_PET_BEHAVIOR_VOICE_SLEEPING,
  PAOPAO_PET_BEHAVIOR_VOICE_FAILING
} paopao_pet_behavior_voice_state_t;

typedef struct {
  bool has_trigger;
  paopao_pet_trigger_event_t trigger;
} paopao_pet_behavior_decision_t;

typedef struct {
  paopao_pet_behavior_voice_state_t voice_state;
  paopao_pet_trigger_event_t pending_service_trigger;
  paopao_pet_state_t last_idle_variant_state;
  uint32_t last_idle_variant_ms;
  uint32_t next_idle_variant_ms;
  uint32_t last_interaction_ms;
  uint8_t idle_variant_index;
} paopao_pet_behavior_context_t;

void paopao_pet_behavior_init(paopao_pet_behavior_context_t *ctx, uint32_t now_ms);

void paopao_pet_behavior_set_voice_state(
  paopao_pet_behavior_context_t *ctx,
  paopao_pet_behavior_voice_state_t voice_state,
  uint32_t now_ms
);

paopao_pet_behavior_decision_t paopao_pet_behavior_handle_service_trigger(
  paopao_pet_behavior_context_t *ctx,
  paopao_pet_trigger_event_t trigger,
  uint32_t now_ms
);

void paopao_pet_behavior_record_interaction(
  paopao_pet_behavior_context_t *ctx,
  uint32_t now_ms
);

paopao_pet_behavior_decision_t paopao_pet_behavior_tick(
  paopao_pet_behavior_context_t *ctx,
  uint32_t now_ms
);

#ifdef __cplusplus
}
#endif
