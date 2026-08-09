#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "paopao_pet_trigger.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  PAOPAO_PET_MOOD_EVENT_NONE = 0,
  PAOPAO_PET_MOOD_EVENT_TICK,
  PAOPAO_PET_MOOD_EVENT_LOCAL_TAP,
  PAOPAO_PET_MOOD_EVENT_LOCAL_HOLD,
  PAOPAO_PET_MOOD_EVENT_LOCAL_DRAG,
  PAOPAO_PET_MOOD_EVENT_LOCAL_SHAKE,
  PAOPAO_PET_MOOD_EVENT_BATTERY_LOW,
  PAOPAO_PET_MOOD_EVENT_BATTERY_RECOVERED,
  PAOPAO_PET_MOOD_EVENT_WIFI_DISCONNECTED,
  PAOPAO_PET_MOOD_EVENT_WIFI_CONNECTED,
  PAOPAO_PET_MOOD_EVENT_VOICE_ERROR,
  PAOPAO_PET_MOOD_EVENT_CHAT_STARTED,
  PAOPAO_PET_MOOD_EVENT_ASSISTANT_REPLY,
  PAOPAO_PET_MOOD_EVENT_SERVICE_EMOTION
} paopao_pet_mood_event_t;

typedef struct {
  paopao_pet_mood_event_t event;
  paopao_pet_trigger_event_t service_trigger;
} paopao_pet_mood_input_t;

typedef struct {
  int8_t energy;
  int8_t mood;
  uint32_t last_interaction_ms;
  uint32_t last_reaction_ms;
  uint32_t low_battery_last_ms;
  uint32_t wifi_alert_last_ms;
  uint32_t voice_error_last_ms;
  uint32_t service_emotion_last_ms;
  bool low_battery;
  bool wifi_connected;
} paopao_pet_mood_context_t;

typedef struct {
  bool has_trigger;
  paopao_pet_trigger_event_t trigger;
  const char *text;
  uint8_t priority;
  uint32_t cooldown_ms;
} paopao_pet_mood_suggestion_t;

void paopao_pet_mood_init(paopao_pet_mood_context_t *ctx, uint32_t now_ms);

paopao_pet_mood_suggestion_t paopao_pet_mood_handle_event(
  paopao_pet_mood_context_t *ctx,
  const paopao_pet_mood_input_t *input,
  uint32_t now_ms
);

#ifdef __cplusplus
}
#endif
