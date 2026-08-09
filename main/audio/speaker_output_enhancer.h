#ifndef SPEAKER_OUTPUT_ENHANCER_H
#define SPEAKER_OUTPUT_ENHANCER_H

#include <stdint.h>
#include <vector>

class SpeakerOutputEnhancer {
public:
    struct Config {
        Config(bool enabled = true, bool enable_eq = true, bool enable_drc = true, bool enable_limiter = true,
               float limiter_ceiling_db = -5.5f, float makeup_gain_db = 2.5f)
            : enabled(enabled),
              enable_eq(enable_eq),
              enable_drc(enable_drc),
              enable_limiter(enable_limiter),
              limiter_ceiling_db(limiter_ceiling_db),
              makeup_gain_db(makeup_gain_db) {
        }

        bool enabled;
        bool enable_eq;
        bool enable_drc;
        bool enable_limiter;
        float limiter_ceiling_db;
        float makeup_gain_db;
    };

    explicit SpeakerOutputEnhancer(Config config = {});
    ~SpeakerOutputEnhancer();

    bool Initialize(int sample_rate);
    void Process(std::vector<int16_t>& pcm);
    void SetConfig(const Config& config);
    bool IsActive() const;
    int sample_rate() const;

private:
    Config config_;
    int sample_rate_ = 0;
    bool active_ = false;
    bool warned_process_failure_ = false;
    void* eq_handle_ = nullptr;
    void* drc_handle_ = nullptr;

    void Close();
};

#endif  // SPEAKER_OUTPUT_ENHANCER_H
