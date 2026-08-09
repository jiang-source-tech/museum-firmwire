#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

#include "../main/audio/speaker_output_limiter.h"

static void peak_from_minus_3_dbfs_leaves_headroom() {
    int16_t peak = speaker_output_limiter_peak_from_db(-3.0f);
    assert(peak >= 23190);
    assert(peak <= 23210);
}

static void limiter_clamps_positive_and_negative_samples() {
    std::vector<int16_t> pcm = {-32768, -20000, 0, 20000, 32767};
    speaker_output_limiter_apply(pcm, 20000);
    assert(pcm[0] == -20000);
    assert(pcm[1] >= -20000);
    assert(pcm[1] <= -19000);
    assert(pcm[2] == 0);
    assert(pcm[3] >= 19000);
    assert(pcm[3] <= 20000);
    assert(pcm[4] == 20000);
}

static void limiter_softens_over_ceiling_transients_without_flat_topping_them() {
    std::vector<int16_t> pcm = {19000, 21000, 24000, 30000};
    speaker_output_limiter_apply(pcm, 20000);

    assert(pcm[0] == 19000);
    assert(pcm[1] > 19000);
    assert(pcm[1] < 20000);
    assert(pcm[2] > pcm[1]);
    assert(pcm[2] < 20000);
    assert(pcm[3] > pcm[2]);
    assert(pcm[3] <= 20000);
}

static void limiter_output_remains_monotonic_around_ceiling() {
    std::vector<int16_t> pcm = {19000, 20000, 21000};
    speaker_output_limiter_apply(pcm, 20000);

    assert(pcm[0] < pcm[1]);
    assert(pcm[1] < pcm[2]);
    assert(pcm[2] <= 20000);
}

static void limiter_keeps_length_for_empty_and_normal_frames() {
    std::vector<int16_t> empty;
    speaker_output_limiter_apply(empty, 20000);
    assert(empty.empty());

    std::vector<int16_t> pcm = {-1000, 1000};
    speaker_output_limiter_apply(pcm, 20000);
    assert(pcm.size() == 2);
    assert(pcm[0] == -1000);
    assert(pcm[1] == 1000);
}

static void invalid_peak_falls_back_to_full_scale() {
    std::vector<int16_t> pcm = {-32768, 32767};
    speaker_output_limiter_apply(pcm, 0);
    assert(pcm[0] == -32767);
    assert(pcm[1] == 32767);
}

int main(void) {
    peak_from_minus_3_dbfs_leaves_headroom();
    limiter_clamps_positive_and_negative_samples();
    limiter_softens_over_ceiling_transients_without_flat_topping_them();
    limiter_output_remains_monotonic_around_ceiling();
    limiter_keeps_length_for_empty_and_normal_frames();
    invalid_peak_falls_back_to_full_scale();
    puts("speaker_output_limiter tests passed");
    return 0;
}
