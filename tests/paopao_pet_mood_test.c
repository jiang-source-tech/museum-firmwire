#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_mood.h"

static paopao_pet_mood_suggestion_t send_event(
    paopao_pet_mood_context_t* ctx,
    paopao_pet_mood_event_t event,
    uint32_t now_ms
) {
    const paopao_pet_mood_input_t input = {
        .event = event,
        .service_trigger = PAOPAO_PET_TRIGGER_NONE,
    };
    return paopao_pet_mood_handle_event(ctx, &input, now_ms);
}

static paopao_pet_mood_suggestion_t send_service(
    paopao_pet_mood_context_t* ctx,
    paopao_pet_trigger_event_t service_trigger,
    uint32_t now_ms
) {
    const paopao_pet_mood_input_t input = {
        .event = PAOPAO_PET_MOOD_EVENT_SERVICE_EMOTION,
        .service_trigger = service_trigger,
    };
    return paopao_pet_mood_handle_event(ctx, &input, now_ms);
}

static void init_sets_stable_defaults(void) {
    paopao_pet_mood_context_t ctx;
    paopao_pet_mood_init(&ctx, 1000);

    assert(ctx.energy == 70);
    assert(ctx.mood == 60);
    assert(ctx.last_interaction_ms == 1000);
    assert(ctx.last_reaction_ms == 0);
    assert(!ctx.low_battery);
    assert(ctx.wifi_connected);
}

static void low_battery_edge_triggers_tired_once_until_recovery(void) {
    paopao_pet_mood_context_t ctx;
    paopao_pet_mood_init(&ctx, 1000);

    paopao_pet_mood_suggestion_t first =
        send_event(&ctx, PAOPAO_PET_MOOD_EVENT_BATTERY_LOW, 1100);
    assert(first.has_trigger);
    assert(first.trigger == PAOPAO_PET_TRIGGER_SERVICE_TIRED);
    assert(strcmp(first.text, "鏈夌偣娌＄數浜?") == 0);
    assert(ctx.low_battery);
    assert(ctx.energy == 45);

    paopao_pet_mood_suggestion_t repeated =
        send_event(&ctx, PAOPAO_PET_MOOD_EVENT_BATTERY_LOW, 2000);
    assert(!repeated.has_trigger);
    assert(ctx.low_battery);

    paopao_pet_mood_suggestion_t recovered =
        send_event(&ctx, PAOPAO_PET_MOOD_EVENT_BATTERY_RECOVERED, 32000);
    assert(recovered.has_trigger);
    assert(recovered.trigger == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);
    assert(strcmp(recovered.text, "濂藉鍟?") == 0);
    assert(!ctx.low_battery);
    assert(ctx.energy == 60);
}

static void wifi_edges_trigger_anxious_and_done_with_cooldown(void) {
    paopao_pet_mood_context_t ctx;
    paopao_pet_mood_init(&ctx, 1000);

    paopao_pet_mood_suggestion_t disconnected =
        send_event(&ctx, PAOPAO_PET_MOOD_EVENT_WIFI_DISCONNECTED, 1200);
    assert(disconnected.has_trigger);
    assert(disconnected.trigger == PAOPAO_PET_TRIGGER_SERVICE_ANXIOUS);
    assert(strcmp(disconnected.text, "缃戠粶涓嶈浜?") == 0);
    assert(!ctx.wifi_connected);

    paopao_pet_mood_suggestion_t repeated =
        send_event(&ctx, PAOPAO_PET_MOOD_EVENT_WIFI_DISCONNECTED, 2000);
    assert(!repeated.has_trigger);

    paopao_pet_mood_suggestion_t connected =
        send_event(&ctx, PAOPAO_PET_MOOD_EVENT_WIFI_CONNECTED, 25000);
    assert(connected.has_trigger);
    assert(connected.trigger == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);
    assert(strcmp(connected.text, "杩炰笂鍟?") == 0);
    assert(ctx.wifi_connected);
}

static void voice_error_has_short_cooldown(void) {
    paopao_pet_mood_context_t ctx;
    paopao_pet_mood_init(&ctx, 1000);

    paopao_pet_mood_suggestion_t first =
        send_event(&ctx, PAOPAO_PET_MOOD_EVENT_VOICE_ERROR, 1200);
    assert(first.has_trigger);
    assert(first.trigger == PAOPAO_PET_TRIGGER_SERVICE_FAILING);
    assert(strcmp(first.text, "鎴戝啀鎯虫兂") == 0);

    paopao_pet_mood_suggestion_t suppressed =
        send_event(&ctx, PAOPAO_PET_MOOD_EVENT_VOICE_ERROR, 3000);
    assert(!suppressed.has_trigger);

    paopao_pet_mood_suggestion_t later =
        send_event(&ctx, PAOPAO_PET_MOOD_EVENT_VOICE_ERROR, 4300);
    assert(later.has_trigger);
    assert(later.trigger == PAOPAO_PET_TRIGGER_SERVICE_FAILING);
}

static void local_interactions_update_scores_without_replacing_direct_feedback(void) {
    paopao_pet_mood_context_t ctx;
    paopao_pet_mood_init(&ctx, 1000);

    paopao_pet_mood_suggestion_t tap =
        send_event(&ctx, PAOPAO_PET_MOOD_EVENT_LOCAL_TAP, 1500);
    assert(!tap.has_trigger);
    assert(ctx.last_interaction_ms == 1500);
    assert(ctx.mood == 68);
    assert(ctx.energy == 72);

    paopao_pet_mood_suggestion_t drag =
        send_event(&ctx, PAOPAO_PET_MOOD_EVENT_LOCAL_DRAG, 1800);
    assert(!drag.has_trigger);
    assert(ctx.last_interaction_ms == 1800);
    assert(ctx.energy == 76);

    paopao_pet_mood_suggestion_t shake =
        send_event(&ctx, PAOPAO_PET_MOOD_EVENT_LOCAL_SHAKE, 2200);
    assert(!shake.has_trigger);
    assert(ctx.last_interaction_ms == 2200);
    assert(ctx.mood == 65);
}

static void service_emotion_uses_existing_trigger_values_and_cooldown(void) {
    paopao_pet_mood_context_t ctx;
    paopao_pet_mood_init(&ctx, 1000);

    paopao_pet_mood_suggestion_t happy =
        send_service(&ctx, PAOPAO_PET_TRIGGER_SERVICE_HAPPY, 1200);
    assert(happy.has_trigger);
    assert(happy.trigger == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);
    assert(strcmp(happy.text, "鏀跺埌") == 0);

    paopao_pet_mood_suggestion_t suppressed =
        send_service(&ctx, PAOPAO_PET_TRIGGER_SERVICE_CRYING, 2000);
    assert(!suppressed.has_trigger);

    paopao_pet_mood_suggestion_t crying =
        send_service(&ctx, PAOPAO_PET_TRIGGER_SERVICE_CRYING, 3300);
    assert(crying.has_trigger);
    assert(crying.trigger == PAOPAO_PET_TRIGGER_SERVICE_CRYING);

    paopao_pet_mood_suggestion_t neutral =
        send_service(&ctx, PAOPAO_PET_TRIGGER_SERVICE_NEUTRAL, 6000);
    assert(!neutral.has_trigger);

    paopao_pet_mood_suggestion_t none =
        send_service(&ctx, PAOPAO_PET_TRIGGER_NONE, 9000);
    assert(!none.has_trigger);
}

static void null_input_is_safe_and_has_no_trigger(void) {
    paopao_pet_mood_context_t ctx;
    paopao_pet_mood_init(&ctx, 1000);

    paopao_pet_mood_suggestion_t suggestion =
        paopao_pet_mood_handle_event(&ctx, NULL, 1200);
    assert(!suggestion.has_trigger);
    assert(suggestion.trigger == PAOPAO_PET_TRIGGER_NONE);
}

int main(void) {
    init_sets_stable_defaults();
    low_battery_edge_triggers_tired_once_until_recovery();
    wifi_edges_trigger_anxious_and_done_with_cooldown();
    voice_error_has_short_cooldown();
    local_interactions_update_scores_without_replacing_direct_feedback();
    service_emotion_uses_existing_trigger_values_and_cooldown();
    null_input_is_safe_and_has_no_trigger();
    puts("paopao_pet_mood tests passed");
    return 0;
}
