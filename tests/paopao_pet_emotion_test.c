#include <assert.h>
#include <stdio.h>

#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_emotion.h"

static void server_emotion_maps_to_core_pet_triggers(void) {
    assert(paopao_pet_trigger_for_emotion("happy") == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);
    assert(paopao_pet_trigger_for_emotion("laughing") == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);
    assert(paopao_pet_trigger_for_emotion("crying") == PAOPAO_PET_TRIGGER_SERVICE_CRYING);
    assert(paopao_pet_trigger_for_emotion("sad") == PAOPAO_PET_TRIGGER_SERVICE_CRYING);
    assert(paopao_pet_trigger_for_emotion("unhappy") == PAOPAO_PET_TRIGGER_SERVICE_CRYING);
    assert(paopao_pet_trigger_for_emotion("angry") == PAOPAO_PET_TRIGGER_SERVICE_ANGRY);
    assert(paopao_pet_trigger_for_emotion("frustrated") == PAOPAO_PET_TRIGGER_SERVICE_ANGRY);
    assert(paopao_pet_trigger_for_emotion("anxious") == PAOPAO_PET_TRIGGER_SERVICE_ANXIOUS);
    assert(paopao_pet_trigger_for_emotion("worried") == PAOPAO_PET_TRIGGER_SERVICE_ANXIOUS);
    assert(paopao_pet_trigger_for_emotion("tired") == PAOPAO_PET_TRIGGER_SERVICE_TIRED);
    assert(paopao_pet_trigger_for_emotion("sleepy") == PAOPAO_PET_TRIGGER_SERVICE_SLEEP);
    assert(paopao_pet_trigger_for_emotion("sleeping") == PAOPAO_PET_TRIGGER_SERVICE_SLEEP);
    assert(paopao_pet_trigger_for_emotion("thinking") == PAOPAO_PET_TRIGGER_SERVICE_THINKING);
    assert(paopao_pet_trigger_for_emotion("neutral") == PAOPAO_PET_TRIGGER_SERVICE_NEUTRAL);
    assert(paopao_pet_trigger_for_emotion("unknown") == PAOPAO_PET_TRIGGER_NONE);
    assert(paopao_pet_trigger_for_emotion(NULL) == PAOPAO_PET_TRIGGER_NONE);
}

static void service_emotion_triggers_use_distinct_gif_states(void) {
    paopao_pet_trigger_context_t ctx;
    paopao_pet_trigger_init(&ctx, 0);

    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SERVICE_HAPPY, 100) == PAOPAO_PET_STATE_HAPPY);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SERVICE_CRYING, 2000) == PAOPAO_PET_STATE_CRYING);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SERVICE_ANGRY, 4000) == PAOPAO_PET_STATE_STAMP);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SERVICE_ANXIOUS, 6000) == PAOPAO_PET_STATE_ANXIETY);
    assert(paopao_pet_trigger_dispatch(&ctx, PAOPAO_PET_TRIGGER_SERVICE_TIRED, 8000) == PAOPAO_PET_STATE_TIRED);
}

int main(void) {
    server_emotion_maps_to_core_pet_triggers();
    service_emotion_triggers_use_distinct_gif_states();
    puts("paopao pet emotion tests passed");
    return 0;
}
