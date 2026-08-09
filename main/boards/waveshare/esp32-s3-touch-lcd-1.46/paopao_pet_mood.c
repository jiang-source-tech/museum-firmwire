#include "paopao_pet_mood.h"

#include <stddef.h>
#include <string.h>

static const uint32_t k_low_battery_cooldown_ms = 30000;
static const uint32_t k_battery_recovered_cooldown_ms = 10000;
static const uint32_t k_wifi_alert_cooldown_ms = 20000;
static const uint32_t k_wifi_recovered_cooldown_ms = 10000;
static const uint32_t k_voice_error_cooldown_ms = 3000;
static const uint32_t k_service_emotion_cooldown_ms = 1800;

static int8_t clamp_score(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return (int8_t)value;
}

static bool time_reached(uint32_t now_ms, uint32_t target_ms) {
  return (int32_t)(now_ms - target_ms) >= 0;
}

static bool cooldown_elapsed(uint32_t last_ms, uint32_t now_ms, uint32_t cooldown_ms) {
  return last_ms == 0 || time_reached(now_ms, last_ms + cooldown_ms);
}

static paopao_pet_mood_suggestion_t no_suggestion(void) {
  const paopao_pet_mood_suggestion_t suggestion = {
    .has_trigger = false,
    .trigger = PAOPAO_PET_TRIGGER_NONE,
    .text = "",
    .priority = 0,
    .cooldown_ms = 0,
  };
  return suggestion;
}

static paopao_pet_mood_suggestion_t make_suggestion(
  paopao_pet_mood_context_t *ctx,
  paopao_pet_trigger_event_t trigger,
  const char *text,
  uint8_t priority,
  uint32_t cooldown_ms,
  uint32_t now_ms
) {
  if (ctx != NULL) {
    ctx->last_reaction_ms = now_ms;
  }

  const paopao_pet_mood_suggestion_t suggestion = {
    .has_trigger = trigger != PAOPAO_PET_TRIGGER_NONE,
    .trigger = trigger,
    .text = text != NULL ? text : "",
    .priority = priority,
    .cooldown_ms = cooldown_ms,
  };
  return suggestion;
}

static void record_interaction(paopao_pet_mood_context_t *ctx, uint32_t now_ms) {
  if (ctx == NULL) {
    return;
  }
  ctx->last_interaction_ms = now_ms;
}

static paopao_pet_mood_suggestion_t handle_service_emotion(
  paopao_pet_mood_context_t *ctx,
  paopao_pet_trigger_event_t trigger,
  uint32_t now_ms
) {
  if (ctx == NULL ||
      trigger == PAOPAO_PET_TRIGGER_NONE ||
      trigger == PAOPAO_PET_TRIGGER_SERVICE_NEUTRAL) {
    return no_suggestion();
  }

  if (!cooldown_elapsed(ctx->service_emotion_last_ms, now_ms, k_service_emotion_cooldown_ms)) {
    return no_suggestion();
  }

  ctx->service_emotion_last_ms = now_ms;
  if (trigger == PAOPAO_PET_TRIGGER_SERVICE_HAPPY) {
    ctx->mood = clamp_score(ctx->mood + 6);
  } else if (trigger == PAOPAO_PET_TRIGGER_SERVICE_CRYING ||
             trigger == PAOPAO_PET_TRIGGER_SERVICE_ANXIOUS ||
             trigger == PAOPAO_PET_TRIGGER_SERVICE_ANGRY ||
             trigger == PAOPAO_PET_TRIGGER_SERVICE_FAILING) {
    ctx->mood = clamp_score(ctx->mood - 4);
  }

  return make_suggestion(ctx, trigger, "鏀跺埌", 40, k_service_emotion_cooldown_ms, now_ms);
}

void paopao_pet_mood_init(paopao_pet_mood_context_t *ctx, uint32_t now_ms) {
  if (ctx == NULL) {
    return;
  }

  memset(ctx, 0, sizeof(*ctx));
  ctx->energy = 70;
  ctx->mood = 60;
  ctx->last_interaction_ms = now_ms;
  ctx->wifi_connected = true;
}

paopao_pet_mood_suggestion_t paopao_pet_mood_handle_event(
  paopao_pet_mood_context_t *ctx,
  const paopao_pet_mood_input_t *input,
  uint32_t now_ms
) {
  if (ctx == NULL || input == NULL) {
    return no_suggestion();
  }

  switch (input->event) {
    case PAOPAO_PET_MOOD_EVENT_NONE:
    case PAOPAO_PET_MOOD_EVENT_TICK:
      break;
    case PAOPAO_PET_MOOD_EVENT_LOCAL_TAP:
      record_interaction(ctx, now_ms);
      ctx->mood = clamp_score(ctx->mood + 8);
      ctx->energy = clamp_score(ctx->energy + 2);
      break;
    case PAOPAO_PET_MOOD_EVENT_LOCAL_HOLD:
      record_interaction(ctx, now_ms);
      ctx->energy = clamp_score(ctx->energy - 2);
      break;
    case PAOPAO_PET_MOOD_EVENT_LOCAL_DRAG:
      record_interaction(ctx, now_ms);
      ctx->energy = clamp_score(ctx->energy + 4);
      break;
    case PAOPAO_PET_MOOD_EVENT_LOCAL_SHAKE:
      record_interaction(ctx, now_ms);
      ctx->mood = clamp_score(ctx->mood - 3);
      break;
    case PAOPAO_PET_MOOD_EVENT_BATTERY_LOW:
      if (ctx->low_battery) {
        return no_suggestion();
      }
      ctx->low_battery = true;
      ctx->energy = clamp_score(ctx->energy - 25);
      ctx->mood = clamp_score(ctx->mood - 5);
      if (!cooldown_elapsed(ctx->low_battery_last_ms, now_ms, k_low_battery_cooldown_ms)) {
        return no_suggestion();
      }
      ctx->low_battery_last_ms = now_ms;
      return make_suggestion(
        ctx,
        PAOPAO_PET_TRIGGER_SERVICE_TIRED,
        "鏈夌偣娌＄數浜?",
        80,
        k_low_battery_cooldown_ms,
        now_ms
      );
    case PAOPAO_PET_MOOD_EVENT_BATTERY_RECOVERED:
      if (!ctx->low_battery) {
        return no_suggestion();
      }
      ctx->low_battery = false;
      ctx->energy = clamp_score(ctx->energy + 15);
      if (!cooldown_elapsed(ctx->low_battery_last_ms, now_ms, k_battery_recovered_cooldown_ms)) {
        return no_suggestion();
      }
      ctx->low_battery_last_ms = now_ms;
      return make_suggestion(
        ctx,
        PAOPAO_PET_TRIGGER_SERVICE_HAPPY,
        "濂藉鍟?",
        60,
        k_battery_recovered_cooldown_ms,
        now_ms
      );
    case PAOPAO_PET_MOOD_EVENT_WIFI_DISCONNECTED:
      if (!ctx->wifi_connected) {
        return no_suggestion();
      }
      ctx->wifi_connected = false;
      ctx->mood = clamp_score(ctx->mood - 6);
      if (!cooldown_elapsed(ctx->wifi_alert_last_ms, now_ms, k_wifi_alert_cooldown_ms)) {
        return no_suggestion();
      }
      ctx->wifi_alert_last_ms = now_ms;
      return make_suggestion(
        ctx,
        PAOPAO_PET_TRIGGER_SERVICE_ANXIOUS,
        "缃戠粶涓嶈浜?",
        70,
        k_wifi_alert_cooldown_ms,
        now_ms
      );
    case PAOPAO_PET_MOOD_EVENT_WIFI_CONNECTED:
      if (ctx->wifi_connected) {
        return no_suggestion();
      }
      ctx->wifi_connected = true;
      ctx->mood = clamp_score(ctx->mood + 4);
      if (!cooldown_elapsed(ctx->wifi_alert_last_ms, now_ms, k_wifi_recovered_cooldown_ms)) {
        return no_suggestion();
      }
      ctx->wifi_alert_last_ms = now_ms;
      return make_suggestion(
        ctx,
        PAOPAO_PET_TRIGGER_SERVICE_HAPPY,
        "杩炰笂鍟?",
        55,
        k_wifi_recovered_cooldown_ms,
        now_ms
      );
    case PAOPAO_PET_MOOD_EVENT_VOICE_ERROR:
      ctx->mood = clamp_score(ctx->mood - 3);
      if (!cooldown_elapsed(ctx->voice_error_last_ms, now_ms, k_voice_error_cooldown_ms)) {
        return no_suggestion();
      }
      ctx->voice_error_last_ms = now_ms;
      return make_suggestion(
        ctx,
        PAOPAO_PET_TRIGGER_SERVICE_FAILING,
        "鎴戝啀鎯虫兂",
        75,
        k_voice_error_cooldown_ms,
        now_ms
      );
    case PAOPAO_PET_MOOD_EVENT_CHAT_STARTED:
      record_interaction(ctx, now_ms);
      ctx->energy = clamp_score(ctx->energy + 1);
      break;
    case PAOPAO_PET_MOOD_EVENT_ASSISTANT_REPLY:
      record_interaction(ctx, now_ms);
      ctx->mood = clamp_score(ctx->mood + 3);
      break;
    case PAOPAO_PET_MOOD_EVENT_SERVICE_EMOTION:
      return handle_service_emotion(ctx, input->service_trigger, now_ms);
  }

  return no_suggestion();
}
