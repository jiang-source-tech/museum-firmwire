#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  PAOPAO_PET_STATE_IDLE = 0,
  PAOPAO_PET_STATE_WORKING,
  PAOPAO_PET_STATE_SPEAKING,
  PAOPAO_PET_STATE_THINKING,
  PAOPAO_PET_STATE_WAITING,
  PAOPAO_PET_STATE_DONE,
  PAOPAO_PET_STATE_SLEEPING,
  PAOPAO_PET_STATE_JUMPING,
  PAOPAO_PET_STATE_FAILING,
  PAOPAO_PET_STATE_GIDDY,
  PAOPAO_PET_STATE_REVIEW,
  PAOPAO_PET_STATE_HAPPY,
  PAOPAO_PET_STATE_CRYING,
  PAOPAO_PET_STATE_ANXIETY,
  PAOPAO_PET_STATE_TIRED,
  PAOPAO_PET_STATE_STAMP
} paopao_pet_state_t;

typedef enum {
  PAOPAO_PET_EVENT_WORK_STARTED = 0,
  PAOPAO_PET_EVENT_THINKING,
  PAOPAO_PET_EVENT_WAITING_FOR_INPUT,
  PAOPAO_PET_EVENT_TASK_DONE,
  PAOPAO_PET_EVENT_SHAKE,
  PAOPAO_PET_EVENT_SLEEP_REQUEST,
  PAOPAO_PET_EVENT_STILL_TIMEOUT,
  PAOPAO_PET_EVENT_WAKE_BUTTON
} paopao_pet_event_t;

typedef struct {
  paopao_pet_state_t state;
  uint32_t entered_at_ms;
  uint32_t last_interaction_ms;
} paopao_pet_context_t;

void paopao_pet_state_init(paopao_pet_context_t *ctx, uint32_t now_ms);
paopao_pet_state_t paopao_pet_dispatch_event(
  paopao_pet_context_t *ctx,
  paopao_pet_event_t event,
  uint32_t now_ms
);
paopao_pet_state_t paopao_pet_tick(paopao_pet_context_t *ctx, uint32_t now_ms);
const char *paopao_pet_state_name(paopao_pet_state_t state);

#ifdef __cplusplus
}
#endif

