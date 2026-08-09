#include "paopao_pet_gif_assets.h"

#include <stdio.h>
#include <string.h>

static int expect_name(paopao_pet_state_t state, const char* expected) {
    const char* actual = paopao_pet_gif_asset_name(state);
    if (actual == NULL || strcmp(actual, expected) != 0) {
        fprintf(stderr, "state %d expected %s, got %s\n", (int)state, expected, actual ? actual : "(null)");
        return 1;
    }
    return 0;
}

int main(void) {
    int failures = 0;
    failures += expect_name(PAOPAO_PET_STATE_IDLE, "idle.gif");
    failures += expect_name(PAOPAO_PET_STATE_WORKING, "working.gif");
    failures += expect_name(PAOPAO_PET_STATE_SPEAKING, "speaking_fixed.gif");
    failures += expect_name(PAOPAO_PET_STATE_THINKING, "thinking.gif");
    failures += expect_name(PAOPAO_PET_STATE_WAITING, "waiting.gif");
    failures += expect_name(PAOPAO_PET_STATE_DONE, "done.gif");
    failures += expect_name(PAOPAO_PET_STATE_SLEEPING, "sleeping.gif");
    failures += expect_name(PAOPAO_PET_STATE_JUMPING, "jumping.gif");
    failures += expect_name(PAOPAO_PET_STATE_FAILING, "failed.gif");
    failures += expect_name(PAOPAO_PET_STATE_GIDDY, "giddy.gif");
    failures += expect_name(PAOPAO_PET_STATE_REVIEW, "review.gif");
    failures += expect_name(PAOPAO_PET_STATE_HAPPY, "happy.gif");
    failures += expect_name(PAOPAO_PET_STATE_CRYING, "crying.gif");
    failures += expect_name(PAOPAO_PET_STATE_ANXIETY, "anxiety.gif");
    failures += expect_name(PAOPAO_PET_STATE_TIRED, "tired.gif");
    failures += expect_name(PAOPAO_PET_STATE_STAMP, "stamp.gif");

    if (failures != 0) {
        return 1;
    }

    printf("paopao pet GIF asset mapping tests passed\n");
    return 0;
}
