#include "speaker_output_limiter.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kHalfPi = 1.5707963267948966;
constexpr double kSoftKneeCurve = 8.0;

int16_t soft_limit_sample(int16_t sample, int16_t peak) {
    int32_t magnitude = sample == INT16_MIN ? 32768 : std::abs(static_cast<int>(sample));
    int32_t safe_peak = peak > 0 ? peak : INT16_MAX;
    int32_t knee_start = std::max(static_cast<int32_t>(1), safe_peak * 95 / 100);
    if (magnitude <= knee_start) {
        return sample;
    }

    double input_range = static_cast<double>(INT16_MAX - knee_start);
    double output_range = static_cast<double>(safe_peak - knee_start);
    double position = input_range > 0.0
        ? static_cast<double>(magnitude - knee_start) / input_range
        : 1.0;
    position = std::clamp(position, 0.0, 1.0);

    double curved = (1.0 - std::exp(-kSoftKneeCurve * position)) /
        (1.0 - std::exp(-kSoftKneeCurve));
    int32_t shaped = knee_start + static_cast<int32_t>(
        std::lround(output_range * std::sin(curved * kHalfPi)));
    shaped = std::clamp(shaped, static_cast<int32_t>(0), safe_peak);
    return sample < 0 ? static_cast<int16_t>(-shaped) : static_cast<int16_t>(shaped);
}

}  // namespace

int16_t speaker_output_limiter_peak_from_db(float ceiling_db) {
    if (ceiling_db >= 0.0f) {
        return INT16_MAX;
    }

    float linear = std::pow(10.0f, ceiling_db / 20.0f);
    int peak = static_cast<int>(std::lround(static_cast<float>(INT16_MAX) * linear));
    peak = std::clamp(peak, 1, static_cast<int>(INT16_MAX));
    return static_cast<int16_t>(peak);
}

void speaker_output_limiter_apply(std::vector<int16_t>& pcm, int16_t peak) {
    int16_t safe_peak = peak > 0 ? peak : INT16_MAX;
    for (auto& sample : pcm) {
        sample = soft_limit_sample(sample, safe_peak);
    }
}
