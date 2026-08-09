#include <assert.h>
#include <stdio.h>

#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.h"

static void service_emotion_is_cached_while_speaking(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 1000);

    paopao_pet_behavior_set_voice_state(&ctx, PAOPAO_PET_BEHAVIOR_VOICE_SPEAKING, 1100);
    paopao_pet_behavior_decision_t during_speech =
        paopao_pet_behavior_handle_service_trigger(&ctx, PAOPAO_PET_TRIGGER_SERVICE_HAPPY, 1200);

    assert(!during_speech.has_trigger);
    assert(ctx.pending_service_trigger == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);

    paopao_pet_behavior_set_voice_state(&ctx, PAOPAO_PET_BEHAVIOR_VOICE_IDLE, 3000);
    paopao_pet_behavior_decision_t after_reply =
        paopao_pet_behavior_tick(&ctx, 3000);

    assert(after_reply.has_trigger);
    assert(after_reply.trigger == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);
    assert(ctx.pending_service_trigger == PAOPAO_PET_TRIGGER_NONE);
}

static void latest_service_emotion_wins_during_one_reply(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 1000);

    paopao_pet_behavior_set_voice_state(&ctx, PAOPAO_PET_BEHAVIOR_VOICE_THINKING, 1100);
    assert(!paopao_pet_behavior_handle_service_trigger(&ctx, PAOPAO_PET_TRIGGER_SERVICE_HAPPY, 1200).has_trigger);
    assert(!paopao_pet_behavior_handle_service_trigger(&ctx, PAOPAO_PET_TRIGGER_SERVICE_TIRED, 1300).has_trigger);

    paopao_pet_behavior_set_voice_state(&ctx, PAOPAO_PET_BEHAVIOR_VOICE_IDLE, 3000);
    paopao_pet_behavior_decision_t decision = paopao_pet_behavior_tick(&ctx, 3000);

    assert(decision.has_trigger);
    assert(decision.trigger == PAOPAO_PET_TRIGGER_SERVICE_TIRED);
}

static void neutral_service_emotion_is_ignored(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 1000);

    paopao_pet_behavior_set_voice_state(&ctx, PAOPAO_PET_BEHAVIOR_VOICE_SPEAKING, 1100);
    assert(!paopao_pet_behavior_handle_service_trigger(&ctx, PAOPAO_PET_TRIGGER_SERVICE_NEUTRAL, 1200).has_trigger);
    assert(ctx.pending_service_trigger == PAOPAO_PET_TRIGGER_NONE);
}

static void service_emotion_can_play_immediately_when_idle(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 1000);

    paopao_pet_behavior_decision_t decision =
        paopao_pet_behavior_handle_service_trigger(&ctx, PAOPAO_PET_TRIGGER_SERVICE_HAPPY, 1200);

    assert(decision.has_trigger);
    assert(decision.trigger == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);
    assert(ctx.pending_service_trigger == PAOPAO_PET_TRIGGER_NONE);
}

static void direct_idle_service_emotion_defers_idle_micro_action(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 0);

    paopao_pet_behavior_decision_t decision =
        paopao_pet_behavior_handle_service_trigger(&ctx, PAOPAO_PET_TRIGGER_SERVICE_HAPPY, 12000);

    assert(decision.has_trigger);
    assert(decision.trigger == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);
    assert(ctx.pending_service_trigger == PAOPAO_PET_TRIGGER_NONE);

    paopao_pet_behavior_decision_t next_frame = paopao_pet_behavior_tick(&ctx, 12001);
    assert(!next_frame.has_trigger);
}

static void service_emotion_is_cached_while_voice_state_is_protected(
    paopao_pet_behavior_voice_state_t voice_state
) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 1000);

    paopao_pet_behavior_set_voice_state(&ctx, voice_state, 1100);
    paopao_pet_behavior_decision_t cached =
        paopao_pet_behavior_handle_service_trigger(&ctx, PAOPAO_PET_TRIGGER_SERVICE_HAPPY, 1200);

    assert(!cached.has_trigger);
    assert(ctx.pending_service_trigger == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);

    paopao_pet_behavior_set_voice_state(&ctx, PAOPAO_PET_BEHAVIOR_VOICE_IDLE, 3000);
    paopao_pet_behavior_decision_t replayed = paopao_pet_behavior_tick(&ctx, 3000);

    assert(replayed.has_trigger);
    assert(replayed.trigger == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);
    assert(ctx.pending_service_trigger == PAOPAO_PET_TRIGGER_NONE);
}

static void service_emotion_is_cached_while_sleeping(void) {
    service_emotion_is_cached_while_voice_state_is_protected(
        PAOPAO_PET_BEHAVIOR_VOICE_SLEEPING
    );
}

static void service_emotion_is_cached_while_failing(void) {
    service_emotion_is_cached_while_voice_state_is_protected(
        PAOPAO_PET_BEHAVIOR_VOICE_FAILING
    );
}

static void returning_to_idle_after_long_voice_session_defers_idle_micro_action(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 0);

    paopao_pet_behavior_set_voice_state(&ctx, PAOPAO_PET_BEHAVIOR_VOICE_SPEAKING, 1000);
    assert(!paopao_pet_behavior_tick(&ctx, 60000).has_trigger);

    paopao_pet_behavior_set_voice_state(&ctx, PAOPAO_PET_BEHAVIOR_VOICE_IDLE, 61000);
    assert(!paopao_pet_behavior_tick(&ctx, 61001).has_trigger);
    assert(!paopao_pet_behavior_tick(&ctx, 72999).has_trigger);
    assert(paopao_pet_behavior_tick(&ctx, 73000).has_trigger);
}

static void repeated_idle_voice_state_sync_does_not_starve_idle_micro_action(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 0);

    for (uint32_t now_ms = 1000; now_ms < 12000; now_ms += 1000) {
        paopao_pet_behavior_set_voice_state(&ctx, PAOPAO_PET_BEHAVIOR_VOICE_IDLE, now_ms);
        assert(!paopao_pet_behavior_tick(&ctx, now_ms).has_trigger);
    }

    paopao_pet_behavior_set_voice_state(&ctx, PAOPAO_PET_BEHAVIOR_VOICE_IDLE, 12000);
    assert(paopao_pet_behavior_tick(&ctx, 12000).has_trigger);
}

static void idle_tick_chooses_light_micro_actions_without_repeating(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 0);

    paopao_pet_behavior_decision_t first = paopao_pet_behavior_tick(&ctx, 12000);
    assert(first.has_trigger);
    assert(first.trigger == PAOPAO_PET_TRIGGER_SERVICE_THINKING);

    paopao_pet_behavior_decision_t during_cooldown = paopao_pet_behavior_tick(&ctx, 13000);
    assert(!during_cooldown.has_trigger);

    paopao_pet_behavior_decision_t second = paopao_pet_behavior_tick(&ctx, 25000);
    assert(second.has_trigger);
    assert(second.trigger == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);
    assert(second.trigger != first.trigger);
}

static void idle_tick_does_not_emit_strong_reactions(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 0);

    for (uint32_t now_ms = 12000; now_ms < 90000; now_ms += 13000) {
        paopao_pet_behavior_decision_t decision = paopao_pet_behavior_tick(&ctx, now_ms);
        if (!decision.has_trigger) {
            continue;
        }
        assert(decision.trigger != PAOPAO_PET_TRIGGER_SERVICE_FAILING);
        assert(decision.trigger != PAOPAO_PET_TRIGGER_SERVICE_CRYING);
        assert(decision.trigger != PAOPAO_PET_TRIGGER_SERVICE_ANXIOUS);
        assert(decision.trigger != PAOPAO_PET_TRIGGER_SERVICE_ANGRY);
    }
}

static void local_interaction_resets_idle_micro_action_timer(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 0);

    paopao_pet_behavior_record_interaction(&ctx, 10000);
    assert(!paopao_pet_behavior_tick(&ctx, 11999).has_trigger);
    assert(!paopao_pet_behavior_tick(&ctx, 21999).has_trigger);
    assert(paopao_pet_behavior_tick(&ctx, 22000).has_trigger);
}

int main(void) {
    service_emotion_is_cached_while_speaking();
    latest_service_emotion_wins_during_one_reply();
    neutral_service_emotion_is_ignored();
    service_emotion_can_play_immediately_when_idle();
    direct_idle_service_emotion_defers_idle_micro_action();
    service_emotion_is_cached_while_sleeping();
    service_emotion_is_cached_while_failing();
    returning_to_idle_after_long_voice_session_defers_idle_micro_action();
    repeated_idle_voice_state_sync_does_not_starve_idle_micro_action();
    idle_tick_chooses_light_micro_actions_without_repeating();
    idle_tick_does_not_emit_strong_reactions();
    local_interaction_resets_idle_micro_action_timer();
    puts("paopao_pet_behavior tests passed");
    return 0;
}
