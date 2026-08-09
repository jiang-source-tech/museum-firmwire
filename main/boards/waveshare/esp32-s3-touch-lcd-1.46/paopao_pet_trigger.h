#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "paopao_pet_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  PAOPAO_PET_TRIGGER_NONE = 0,
  PAOPAO_PET_TRIGGER_IDLE,
  PAOPAO_PET_TRIGGER_CONNECTING,
  PAOPAO_PET_TRIGGER_LISTENING,
  PAOPAO_PET_TRIGGER_THINKING,
  PAOPAO_PET_TRIGGER_SPEAKING,
  PAOPAO_PET_TRIGGER_TASK_DONE,
  PAOPAO_PET_TRIGGER_ERROR,
  PAOPAO_PET_TRIGGER_LOCAL_TAP,
  PAOPAO_PET_TRIGGER_LOCAL_HOLD,
  PAOPAO_PET_TRIGGER_LOCAL_DRAG_LEFT,
  PAOPAO_PET_TRIGGER_LOCAL_DRAG_RIGHT,
  PAOPAO_PET_TRIGGER_LOCAL_SHAKE,
  PAOPAO_PET_TRIGGER_WAKE,
  PAOPAO_PET_TRIGGER_SERVICE_NEUTRAL,
  PAOPAO_PET_TRIGGER_SERVICE_HAPPY,
  PAOPAO_PET_TRIGGER_SERVICE_THINKING,
  PAOPAO_PET_TRIGGER_SERVICE_SLEEP,
  PAOPAO_PET_TRIGGER_SERVICE_GIDDY,
  PAOPAO_PET_TRIGGER_SERVICE_FAILING,
  PAOPAO_PET_TRIGGER_SERVICE_CRYING,
  PAOPAO_PET_TRIGGER_SERVICE_ANXIOUS,
  PAOPAO_PET_TRIGGER_SERVICE_TIRED,
  PAOPAO_PET_TRIGGER_SERVICE_ANGRY
} paopao_pet_trigger_event_t;

typedef struct {
  paopao_pet_state_t base_state;
  paopao_pet_state_t reaction_state;
  paopao_pet_state_t displayed_state;
  uint32_t reaction_until_ms;
  uint32_t last_interaction_ms;
  uint32_t next_idle_variant_ms;
  bool reaction_active;
} paopao_pet_trigger_context_t;

void paopao_pet_trigger_init(paopao_pet_trigger_context_t *ctx, uint32_t now_ms);
paopao_pet_state_t paopao_pet_trigger_dispatch(
  paopao_pet_trigger_context_t *ctx,
  paopao_pet_trigger_event_t event,
  uint32_t now_ms
);
paopao_pet_state_t paopao_pet_trigger_tick(paopao_pet_trigger_context_t *ctx, uint32_t now_ms);
paopao_pet_state_t paopao_pet_trigger_play_reaction(
  paopao_pet_trigger_context_t *ctx,
  paopao_pet_state_t state,
  uint32_t duration_ms,
  uint32_t now_ms
);

#ifdef __cplusplus
}
#endif
