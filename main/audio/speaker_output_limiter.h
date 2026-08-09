#ifndef SPEAKER_OUTPUT_LIMITER_H
#define SPEAKER_OUTPUT_LIMITER_H

#include <stdint.h>
#include <vector>

int16_t speaker_output_limiter_peak_from_db(float ceiling_db);
void speaker_output_limiter_apply(std::vector<int16_t>& pcm, int16_t peak);

#endif  // SPEAKER_OUTPUT_LIMITER_H
