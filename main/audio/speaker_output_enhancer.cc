#include "speaker_output_enhancer.h"

#include "esp_log.h"
#include "esp_ae_drc.h"
#include "esp_ae_eq.h"
#include "speaker_output_limiter.h"

namespace {

constexpr char kTag[] = "SpeakerOutputEnhancer";
constexpr uint8_t kChannelCount = 1;
constexpr uint8_t kBitsPerSample = 16;

esp_ae_eq_filter_para_t EqFilter(esp_ae_eq_filter_type_t type, uint32_t frequency, float q, float gain) {
    esp_ae_eq_filter_para_t filter = {};
    filter.filter_type = type;
    filter.fc = frequency;
    filter.q = q;
    filter.gain = gain;
    return filter;
}

}  // namespace

SpeakerOutputEnhancer::SpeakerOutputEnhancer(Config config) : config_(config) {
}

SpeakerOutputEnhancer::~SpeakerOutputEnhancer() {
    Close();
}

bool SpeakerOutputEnhancer::Initialize(int sample_rate) {
    if (!config_.enabled) {
        Close();
        return false;
    }
    if (sample_rate <= 0) {
        ESP_LOGW(kTag, "Invalid sample rate: %d", sample_rate);
        Close();
        return false;
    }
    if (active_ && sample_rate_ == sample_rate) {
        return true;
    }

    Close();
    sample_rate_ = sample_rate;

    if (config_.enable_eq) {
        esp_ae_eq_filter_para_t filters[] = {
            EqFilter(ESP_AE_EQ_FILTER_HIGH_PASS, 150, 0.7f, 0.0f),
            EqFilter(ESP_AE_EQ_FILTER_LOW_SHELF, 220, 0.7f, -2.5f),
            EqFilter(ESP_AE_EQ_FILTER_PEAK, 2100, 1.0f, 2.5f),
            EqFilter(ESP_AE_EQ_FILTER_PEAK, 3800, 1.0f, 0.8f),
        };

        esp_ae_eq_cfg_t eq_cfg = {};
        eq_cfg.sample_rate = static_cast<uint32_t>(sample_rate);
        eq_cfg.channel = kChannelCount;
        eq_cfg.bits_per_sample = kBitsPerSample;
        eq_cfg.filter_num = static_cast<uint8_t>(sizeof(filters) / sizeof(filters[0]));
        eq_cfg.para = filters;

        esp_ae_eq_handle_t eq_handle = nullptr;
        esp_ae_err_t err = esp_ae_eq_open(&eq_cfg, &eq_handle);
        if (err != ESP_AE_ERR_OK || eq_handle == nullptr) {
            ESP_LOGW(kTag, "Failed to open EQ: %d", err);
            Close();
            return false;
        }
        eq_handle_ = eq_handle;
    }

    if (config_.enable_drc) {
        esp_ae_drc_curve_point curve_points[] = {
            {0.0f, -3.0f},
            {-18.0f, -15.0f},
            {-60.0f, -55.0f},
            {-100.0f, -100.0f},
        };

        esp_ae_drc_cfg_t drc_cfg = {};
        drc_cfg.sample_rate = static_cast<uint32_t>(sample_rate);
        drc_cfg.channel = kChannelCount;
        drc_cfg.bits_per_sample = kBitsPerSample;
        drc_cfg.drc_para.point = curve_points;
        drc_cfg.drc_para.point_num = static_cast<uint8_t>(sizeof(curve_points) / sizeof(curve_points[0]));
        drc_cfg.drc_para.makeup_gain = config_.makeup_gain_db;
        drc_cfg.drc_para.knee_width = 3.0f;
        drc_cfg.drc_para.attack_time = 6;
        drc_cfg.drc_para.release_time = 180;
        drc_cfg.drc_para.hold_time = 0;

        esp_ae_drc_handle_t drc_handle = nullptr;
        esp_ae_err_t err = esp_ae_drc_open(&drc_cfg, &drc_handle);
        if (err != ESP_AE_ERR_OK || drc_handle == nullptr) {
            ESP_LOGW(kTag, "Failed to open DRC: %d", err);
            Close();
            return false;
        }
        drc_handle_ = drc_handle;
    }

    active_ = eq_handle_ != nullptr || drc_handle_ != nullptr || config_.enable_limiter;
    ESP_LOGI(kTag, "Initialized speaker enhancer at %d Hz (active=%s)", sample_rate, active_ ? "true" : "false");
    return active_;
}

void SpeakerOutputEnhancer::Process(std::vector<int16_t>& pcm) {
    if (!config_.enabled || pcm.empty()) {
        return;
    }

    if (active_) {
        uint32_t sample_num = static_cast<uint32_t>(pcm.size());
        esp_ae_sample_t samples = static_cast<esp_ae_sample_t>(pcm.data());

        if (eq_handle_ != nullptr) {
            esp_ae_err_t err = esp_ae_eq_process(static_cast<esp_ae_eq_handle_t>(eq_handle_), sample_num, samples, samples);
            if (err != ESP_AE_ERR_OK && !warned_process_failure_) {
                ESP_LOGW(kTag, "EQ process failed: %d", err);
                warned_process_failure_ = true;
            }
        }

        if (drc_handle_ != nullptr) {
            esp_ae_err_t err = esp_ae_drc_process(static_cast<esp_ae_drc_handle_t>(drc_handle_), sample_num, samples, samples);
            if (err != ESP_AE_ERR_OK && !warned_process_failure_) {
                ESP_LOGW(kTag, "DRC process failed: %d", err);
                warned_process_failure_ = true;
            }
        }
    }

    if (config_.enable_limiter) {
        speaker_output_limiter_apply(pcm, speaker_output_limiter_peak_from_db(config_.limiter_ceiling_db));
    }
}

void SpeakerOutputEnhancer::SetConfig(const Config& config) {
    config_ = config;
    Close();
}

bool SpeakerOutputEnhancer::IsActive() const {
    return active_;
}

int SpeakerOutputEnhancer::sample_rate() const {
    return sample_rate_;
}

void SpeakerOutputEnhancer::Close() {
    if (eq_handle_ != nullptr) {
        esp_ae_eq_close(static_cast<esp_ae_eq_handle_t>(eq_handle_));
        eq_handle_ = nullptr;
    }
    if (drc_handle_ != nullptr) {
        esp_ae_drc_close(static_cast<esp_ae_drc_handle_t>(drc_handle_));
        drc_handle_ = nullptr;
    }
    active_ = false;
    sample_rate_ = 0;
    warned_process_failure_ = false;
}
