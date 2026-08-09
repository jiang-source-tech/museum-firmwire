#include <assert.h>
#include <stdio.h>

#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_trigger.h"

static void local_reaction_restores_speaking_base_state(void) {
    paopao_pet_trigger_context_t ctx;
    paopao_pet_trigger_init(&ctx, 1000);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SPEAKING, 1100) == PAOPAO_PET_STATE_SPEAKING);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_LOCAL_TAP, 1200) == PAOPAO_PET_STATE_DONE);
    assert(paopao_pet_trigger_tick(&ctx, 2000) == PAOPAO_PET_STATE_DONE);
    assert(paopao_pet_trigger_tick(&ctx, 2900) == PAOPAO_PET_STATE_SPEAKING);
}

static void idle_eventually_sleeps_after_quiet_timeout(void) {
    paopao_pet_trigger_context_t ctx;
    paopao_pet_trigger_init(&ctx, 0);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_IDLE, 100) == PAOPAO_PET_STATE_IDLE);
    assert(paopao_pet_trigger_tick(&ctx, 59999) != PAOPAO_PET_STATE_SLEEPING);
    assert(paopao_pet_trigger_tick(&ctx, 60000) == PAOPAO_PET_STATE_SLEEPING);
}

static void local_hold_toggles_sleeping(void) {
    paopao_pet_trigger_context_t ctx;
    paopao_pet_trigger_init(&ctx, 0);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_LOCAL_HOLD, 100) == PAOPAO_PET_STATE_SLEEPING);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_LOCAL_HOLD, 300) == PAOPAO_PET_STATE_IDLE);
}

static void local_tap_wakes_sleeping_with_reaction(void) {
    paopao_pet_trigger_context_t ctx;
    paopao_pet_trigger_init(&ctx, 0);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_LOCAL_HOLD, 100) == PAOPAO_PET_STATE_SLEEPING);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_LOCAL_TAP, 200) == PAOPAO_PET_STATE_DONE);
    assert(ctx.base_state == PAOPAO_PET_STATE_IDLE);
    assert(paopao_pet_trigger_tick(&ctx, 1900) == PAOPAO_PET_STATE_IDLE);
}

static void local_shake_wakes_sleeping_with_giddy(void) {
    paopao_pet_trigger_context_t ctx;
    paopao_pet_trigger_init(&ctx, 0);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_LOCAL_HOLD, 100) == PAOPAO_PET_STATE_SLEEPING);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_LOCAL_SHAKE, 200) == PAOPAO_PET_STATE_GIDDY);
    assert(ctx.base_state == PAOPAO_PET_STATE_IDLE);
    assert(paopao_pet_trigger_tick(&ctx, 1900) == PAOPAO_PET_STATE_IDLE);
}

static void voice_state_wakes_sleeping_and_overrides_reaction(void) {
    paopao_pet_trigger_context_t ctx;
    paopao_pet_trigger_init(&ctx, 0);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_LOCAL_HOLD, 100) == PAOPAO_PET_STATE_SLEEPING);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SPEAKING, 200) == PAOPAO_PET_STATE_SPEAKING);
    assert(ctx.base_state == PAOPAO_PET_STATE_SPEAKING);
    assert(!ctx.reaction_active);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_LOCAL_TAP, 300) == PAOPAO_PET_STATE_DONE);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_LISTENING, 400) == PAOPAO_PET_STATE_WAITING);
    assert(!ctx.reaction_active);
}

static void service_suggestion_does_not_override_error_state(void) {
    paopao_pet_trigger_context_t ctx;
    paopao_pet_trigger_init(&ctx, 0);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_ERROR, 100) == PAOPAO_PET_STATE_FAILING);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SERVICE_HAPPY, 200) == PAOPAO_PET_STATE_FAILING);
    assert(paopao_pet_trigger_tick(&ctx, 3000) == PAOPAO_PET_STATE_FAILING);
}

static void neutral_service_suggestion_does_not_clear_voice_state(void) {
    paopao_pet_trigger_context_t ctx;
    paopao_pet_trigger_init(&ctx, 0);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_LISTENING, 100) == PAOPAO_PET_STATE_WAITING);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SERVICE_NEUTRAL, 200) == PAOPAO_PET_STATE_WAITING);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SPEAKING, 300) == PAOPAO_PET_STATE_SPEAKING);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SERVICE_NEUTRAL, 400) == PAOPAO_PET_STATE_SPEAKING);
}

static void only_local_shake_triggers_giddy(void) {
    paopao_pet_trigger_context_t ctx;
    paopao_pet_trigger_init(&ctx, 0);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SERVICE_GIDDY, 100) == PAOPAO_PET_STATE_IDLE);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_LOCAL_SHAKE, 200) == PAOPAO_PET_STATE_GIDDY);
    assert(paopao_pet_trigger_tick(&ctx, 1900) == PAOPAO_PET_STATE_IDLE);
}

static void service_core_emotions_use_distinct_reaction_states(void) {
    paopao_pet_trigger_context_t ctx;
    paopao_pet_trigger_init(&ctx, 0);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SERVICE_HAPPY, 100) == PAOPAO_PET_STATE_HAPPY);
    assert(paopao_pet_trigger_tick(&ctx, 1700) == PAOPAO_PET_STATE_IDLE);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SERVICE_CRYING, 2000) == PAOPAO_PET_STATE_CRYING);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SERVICE_ANGRY, 4200) == PAOPAO_PET_STATE_STAMP);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SERVICE_ANXIOUS, 6400) == PAOPAO_PET_STATE_ANXIETY);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SERVICE_TIRED, 8600) == PAOPAO_PET_STATE_TIRED);
}

static void trigger_tick_does_not_own_idle_micro_actions(void) {
    paopao_pet_trigger_context_t ctx;
    paopao_pet_trigger_init(&ctx, 0);

    assert(paopao_pet_trigger_tick(&ctx, 19999) == PAOPAO_PET_STATE_IDLE);
    assert(paopao_pet_trigger_tick(&ctx, 20000) == PAOPAO_PET_STATE_IDLE);
    assert(paopao_pet_trigger_tick(&ctx, 59999) == PAOPAO_PET_STATE_IDLE);
    assert(paopao_pet_trigger_tick(&ctx, 60000) == PAOPAO_PET_STATE_SLEEPING);
}

static void background_connecting_does_not_replace_pet_state(void) {
    paopao_pet_trigger_context_t ctx;
    paopao_pet_trigger_init(&ctx, 0);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_CONNECTING, 100) == PAOPAO_PET_STATE_IDLE);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_LOCAL_TAP, 200) == PAOPAO_PET_STATE_DONE);
    assert(paopao_pet_trigger_tick(&ctx, 1900) == PAOPAO_PET_STATE_IDLE);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SPEAKING, 2000) == PAOPAO_PET_STATE_SPEAKING);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_CONNECTING, 2100) == PAOPAO_PET_STATE_SPEAKING);
}

static void local_drag_uses_jumping_reaction(void) {
    paopao_pet_trigger_context_t ctx;
    paopao_pet_trigger_init(&ctx, 0);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_LOCAL_DRAG_LEFT, 100) == PAOPAO_PET_STATE_JUMPING);
    assert(paopao_pet_trigger_tick(&ctx, 900) == PAOPAO_PET_STATE_JUMPING);
    assert(paopao_pet_trigger_tick(&ctx, 1000) == PAOPAO_PET_STATE_IDLE);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_LOCAL_DRAG_RIGHT, 1100) == PAOPAO_PET_STATE_JUMPING);
    assert(paopao_pet_trigger_tick(&ctx, 1900) == PAOPAO_PET_STATE_JUMPING);
    assert(paopao_pet_trigger_tick(&ctx, 2000) == PAOPAO_PET_STATE_IDLE);
}

int main(void) {
    local_reaction_restores_speaking_base_state();
    idle_eventually_sleeps_after_quiet_timeout();
    local_hold_toggles_sleeping();
    local_tap_wakes_sleeping_with_reaction();
    local_shake_wakes_sleeping_with_giddy();
    voice_state_wakes_sleeping_and_overrides_reaction();
    service_suggestion_does_not_override_error_state();
    neutral_service_suggestion_does_not_clear_voice_state();
    only_local_shake_triggers_giddy();
    service_core_emotions_use_distinct_reaction_states();
    trigger_tick_does_not_own_idle_micro_actions();
    background_connecting_does_not_replace_pet_state();
    local_drag_uses_jumping_reaction();
    puts("paopao_pet_trigger tests passed");
    return 0;
}
