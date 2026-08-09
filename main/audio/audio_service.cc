#include "audio_service.h"
#include <algorithm>
#include <esp_log.h>
#include <cstring>

#define RATE_CVT_CFG(_src_rate, _dest_rate, _channel)        \
    (esp_ae_rate_cvt_cfg_t)                                  \
    {                                                        \
        .src_rate        = (uint32_t)(_src_rate),            \
        .dest_rate       = (uint32_t)(_dest_rate),           \
        .channel         = (uint8_t)(_channel),              \
        .bits_per_sample = ESP_AUDIO_BIT16,                  \
        .complexity      = 2,                                \
        .perf_type       = ESP_AE_RATE_CVT_PERF_TYPE_SPEED,  \
    }

#define OPUS_DEC_CFG(_sample_rate, _frame_duration_ms)                                                    \
    (esp_opus_dec_cfg_t)                                                                                  \
    {                                                                                                     \
        .sample_rate    = (uint32_t)(_sample_rate),                                                       \
        .channel        = ESP_AUDIO_MONO,                                                                 \
        .frame_duration = (esp_opus_dec_frame_duration_t)AS_OPUS_GET_FRAME_DRU_ENUM(_frame_duration_ms),  \
        .self_delimited = false,                                                                          \
    }

#if CONFIG_USE_AUDIO_PROCESSOR
#include "processors/afe_audio_processor.h"
#else
#include "processors/no_audio_processor.h"
#endif

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
#include "wake_words/afe_wake_word.h"
#include "wake_words/custom_wake_word.h"
#else
#include "wake_words/esp_wake_word.h"
#endif

#define TAG "AudioService"

AudioService::AudioService() {
    event_group_ = xEventGroupCreate();
}

AudioService::~AudioService() {
    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
    }
    if (opus_encoder_ != nullptr) {
        esp_opus_enc_close(opus_encoder_);
    }
    if (opus_decoder_ != nullptr) {
        esp_opus_dec_close(opus_decoder_);
    }
    if (input_resampler_ != nullptr) {
        esp_ae_rate_cvt_close(input_resampler_);
    }
    if (output_resampler_ != nullptr) {
        esp_ae_rate_cvt_close(output_resampler_);
    }
}

void AudioService::Initialize(AudioCodec* codec) {
    codec_ = codec;
    codec_->Start();
#if CONFIG_SPEAKER_OUTPUT_ENHANCER
    speaker_output_enhancer_.Initialize(codec_->output_sample_rate());
#endif

    esp_opus_dec_cfg_t opus_dec_cfg = OPUS_DEC_CFG(codec->output_sample_rate(), OPUS_FRAME_DURATION_MS);
    auto ret = esp_opus_dec_open(&opus_dec_cfg, sizeof(esp_opus_dec_cfg_t), &opus_decoder_);
    if (opus_decoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create audio decoder, error code: %d", ret);
    } else {
        decoder_sample_rate_ = codec->output_sample_rate();
        decoder_duration_ms_ = OPUS_FRAME_DURATION_MS;
        decoder_frame_size_ = decoder_sample_rate_ / 1000 * OPUS_FRAME_DURATION_MS;
    }
    esp_opus_enc_config_t opus_enc_cfg = AS_OPUS_ENC_CONFIG();
    ret = esp_opus_enc_open(&opus_enc_cfg, sizeof(esp_opus_enc_config_t), &opus_encoder_);
    if (opus_encoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create audio encoder, error code: %d", ret);
    } else {
        encoder_sample_rate_ = 16000;
        encoder_duration_ms_ = OPUS_FRAME_DURATION_MS;
        esp_opus_enc_get_frame_size(opus_encoder_, &encoder_frame_size_, &encoder_outbuf_size_);
        encoder_frame_size_ = encoder_frame_size_ / sizeof(int16_t);
    }

    if (codec->input_sample_rate() != 16000) {
        esp_ae_rate_cvt_cfg_t input_resampler_cfg = RATE_CVT_CFG(
            codec->input_sample_rate(), ESP_AUDIO_SAMPLE_RATE_16K, codec->input_channels());
        auto resampler_ret = esp_ae_rate_cvt_open(&input_resampler_cfg, &input_resampler_);
        if (input_resampler_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create input resampler, error code: %d", resampler_ret);
        }
    }

#if CONFIG_USE_AUDIO_PROCESSOR
    audio_processor_ = std::make_unique<AfeAudioProcessor>();
#else
    audio_processor_ = std::make_unique<NoAudioProcessor>();
#endif

    audio_processor_->OnOutput([this](std::vector<int16_t>&& data) {
        PushTaskToEncodeQueue(kAudioTaskTypeEncodeToSendQueue, std::move(data), false);
    });

    audio_processor_->OnVadStateChange([this](bool speaking) {
        voice_detected_ = speaking;
        if (callbacks_.on_vad_change) {
            callbacks_.on_vad_change(speaking);
        }
    });

    esp_timer_create_args_t audio_power_timer_args = {
        .callback = [](void* arg) {
            AudioService* audio_service = (AudioService*)arg;
            audio_service->CheckAndUpdateAudioPowerState();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "audio_power_timer",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&audio_power_timer_args, &audio_power_timer_);
}

void AudioService::Start() {
    service_stopped_ = false;
    xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING | AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING);

    esp_timer_start_periodic(audio_power_timer_, 1000000);

#if CONFIG_USE_AUDIO_PROCESSOR
    /* Start the audio input task */
    xTaskCreatePinnedToCore([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->AudioInputTask();
        vTaskDelete(NULL);
    }, "audio_input", 2048 * 3, this, 8, &audio_input_task_handle_, 0);

    /* Start the audio output task */
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->AudioOutputTask();
        vTaskDelete(NULL);
    }, "audio_output", 2048 * 2, this, 4, &audio_output_task_handle_);
#else
    /* Start the audio input task */
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->AudioInputTask();
        vTaskDelete(NULL);
    }, "audio_input", 2048 * 2, this, 8, &audio_input_task_handle_);

    /* Start the audio output task */
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->AudioOutputTask();
        vTaskDelete(NULL);
    }, "audio_output", 2048, this, 4, &audio_output_task_handle_);
#endif

    /* Start the opus codec task */
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->OpusCodecTask();
        vTaskDelete(NULL);
    }, "opus_codec", 2048 * 12, this, 2, &opus_codec_task_handle_);
}

void AudioService::Stop() {
    esp_timer_stop(audio_power_timer_);
    service_stopped_ = true;
    xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING |
        AS_EVENT_WAKE_WORD_RUNNING |
        AS_EVENT_AUDIO_PROCESSOR_RUNNING);

    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    audio_encode_queue_.clear();
    audio_decode_queue_.clear();
    audio_playback_queue_.clear();
    audio_testing_queue_.clear();
    audio_queue_cv_.notify_all();
}

bool AudioService::ReadAudioData(std::vector<int16_t>& data, int sample_rate, int samples) {
    if (!codec_->input_enabled()) {
        esp_timer_stop(audio_power_timer_);
        esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
        codec_->EnableInput(true);
    }

    if (codec_->input_sample_rate() != sample_rate) {
        data.resize(samples * codec_->input_sample_rate() / sample_rate * codec_->input_channels());
        if (!codec_->InputData(data)) {
            return false;
        }
        if (input_resampler_ != nullptr) {
            std::lock_guard<std::mutex> lock(input_resampler_mutex_);
            uint32_t in_sample_num = data.size() / codec_->input_channels();
            uint32_t output_samples = 0;
            esp_ae_rate_cvt_get_max_out_sample_num(input_resampler_, in_sample_num, &output_samples);
            auto resampled = std::vector<int16_t>(output_samples * codec_->input_channels());
            uint32_t actual_output = output_samples;
            esp_ae_rate_cvt_process(input_resampler_, (esp_ae_sample_t)data.data(), in_sample_num,
                                   (esp_ae_sample_t)resampled.data(), &actual_output);
            resampled.resize(actual_output * codec_->input_channels());
            data = std::move(resampled);
        }
    } else {
        data.resize(samples * codec_->input_channels());
        if (!codec_->InputData(data)) {
            return false;
        }
    }

    /* Update the last input time */
    last_input_time_ = std::chrono::steady_clock::now();
    debug_statistics_.input_count++;

#if CONFIG_USE_AUDIO_DEBUGGER
    // 音频调试：发送原始音频数据
    if (audio_debugger_ == nullptr) {
        audio_debugger_ = std::make_unique<AudioDebugger>();
    }
    audio_debugger_->Feed(data);
#endif

    return true;
}

void AudioService::AudioInputTask() {
    while (true) {
        EventBits_t bits = xEventGroupWaitBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING |
            AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING,
            pdFALSE, pdFALSE, portMAX_DELAY);

        if (service_stopped_) {
            break;
        }
        if (audio_input_need_warmup_) {
            audio_input_need_warmup_ = false;
            vTaskDelay(pdMS_TO_TICKS(120));
            continue;
        }

        /* Used for audio testing in NetworkConfiguring mode by clicking the BOOT button */
        if (bits & AS_EVENT_AUDIO_TESTING_RUNNING) {
            if (audio_testing_queue_.size() >= AUDIO_TESTING_MAX_DURATION_MS / OPUS_FRAME_DURATION_MS) {
                ESP_LOGW(TAG, "Audio testing queue is full, stopping audio testing");
                EnableAudioTesting(false);
                continue;
            }
            std::vector<int16_t> data;
            int samples = OPUS_FRAME_DURATION_MS * 16000 / 1000;
            if (ReadAudioData(data, 16000, samples)) {
                // If input channels is 2, we need to fetch the left channel data
                if (codec_->input_channels() == 2) {
                    auto mono_data = std::vector<int16_t>(data.size() / 2);
                    for (size_t i = 0, j = 0; i < mono_data.size(); ++i, j += 2) {
                        mono_data[i] = data[j];
                    }
                    data = std::move(mono_data);
                }
                PushTaskToEncodeQueue(kAudioTaskTypeEncodeToTestingQueue, std::move(data));
                continue;
            }
        }

        /* Feed the wake word and/or audio processor */
        if (bits & (AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING)) {
            int samples = 160; // 10ms
            std::vector<int16_t> data;
            if (ReadAudioData(data, 16000, samples)) {
                if (bits & AS_EVENT_WAKE_WORD_RUNNING) {
                    wake_word_->Feed(data);
                }
                if (bits & AS_EVENT_AUDIO_PROCESSOR_RUNNING) {
                    audio_processor_->Feed(std::move(data));
                }
                continue;
            }
        }

        // Read timeout/error should not terminate the input task.
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGW(TAG, "Audio input task stopped");
}

void AudioService::AudioOutputTask() {
    while (true) {
        std::unique_lock<std::mutex> lock(audio_queue_mutex_);
        audio_queue_cv_.wait(lock, [this]() { return !audio_playback_queue_.empty() || service_stopped_; });
        if (service_stopped_) {
            break;
        }

        auto task = std::move(audio_playback_queue_.front());
        audio_playback_queue_.pop_front();
        audio_output_busy_ = true;
        audio_queue_cv_.notify_all();
        lock.unlock();

        std::chrono::steady_clock::time_point output_started;
        std::chrono::microseconds duration_us(0);
        AudioPlaybackFailureReason output_failure =
            AudioPlaybackFailureReason::kNone;
        const bool output_published = audio_pipeline_epoch_.PublishIfCurrent(
            task->pipeline_epoch,
            [this, &task, &output_started, &duration_us, &output_failure]() {
                if (!codec_->output_enabled()) {
                    esp_timer_stop(audio_power_timer_);
                    esp_timer_start_periodic(
                        audio_power_timer_,
                        AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
                    codec_->EnableOutput(true);
                }

#if CONFIG_SPEAKER_OUTPUT_ENHANCER
                if (speaker_output_enhancer_.sample_rate() !=
                    codec_->output_sample_rate()) {
                    speaker_output_enhancer_.Initialize(
                        codec_->output_sample_rate());
                }
                speaker_output_enhancer_.Process(task->pcm);
#endif

                output_started = std::chrono::steady_clock::now();
                const bool write_succeeded = codec_->OutputData(task->pcm);
                const auto output_finished = std::chrono::steady_clock::now();
                output_failure = ClassifyAudioOutputAttempt(
                    write_succeeded, output_finished - output_started);
                const int channels = std::max(codec_->output_channels(), 1);
                const int sample_rate =
                    std::max(codec_->output_sample_rate(), 1);
                duration_us = std::chrono::microseconds(
                    static_cast<int64_t>(task->pcm.size()) * 1000000LL /
                    (static_cast<int64_t>(sample_rate) * channels));
            });

        if (output_published &&
            output_failure != AudioPlaybackFailureReason::kNone) {
            ReportPlaybackFailure(
                task->playback_generation,
                task->pipeline_epoch,
                output_failure);
        }

        lock.lock();
        if (output_published &&
            output_failure == AudioPlaybackFailureReason::kNone) {
            const auto queued_from =
                std::max(last_output_expected_end_, output_started);
            last_output_expected_end_ = queued_from + duration_us;
            last_output_time_ = output_started;
        }
        audio_output_busy_ = false;
        ++audio_output_sequence_;
        audio_queue_cv_.notify_all();
        lock.unlock();
        if (output_published &&
            output_failure == AudioPlaybackFailureReason::kNone) {
            debug_statistics_.playback_count++;
        } else if (!output_published) {
            ESP_LOGI(
                TAG,
                "Discarded stale PCM after audio pipeline reset (epoch=%llu)",
                static_cast<unsigned long long>(task->pipeline_epoch));
        }

#if CONFIG_USE_SERVER_AEC
        /* Record the timestamp for server AEC */
        if (output_published &&
            output_failure == AudioPlaybackFailureReason::kNone &&
            task->timestamp > 0) {
            lock.lock();
            timestamp_queue_.push_back(task->timestamp);
        }
#endif
    }

    ESP_LOGW(TAG, "Audio output task stopped");
}

void AudioService::OpusCodecTask() {
    while (true) {
        std::unique_lock<std::mutex> lock(audio_queue_mutex_);
        audio_queue_cv_.wait(lock, [this]() {
            return service_stopped_ ||
                (!audio_encode_queue_.empty() && audio_send_queue_.size() < MAX_SEND_PACKETS_IN_QUEUE) ||
                (!audio_decode_queue_.empty() && audio_playback_queue_.size() < MAX_PLAYBACK_TASKS_IN_QUEUE);
        });
        if (service_stopped_) {
            break;
        }

        /* Decode the audio from decode queue */
        if (!audio_decode_queue_.empty() && audio_playback_queue_.size() < MAX_PLAYBACK_TASKS_IN_QUEUE) {
            auto packet = std::move(audio_decode_queue_.front());
            audio_decode_queue_.pop_front();
            audio_decode_busy_ = true;
            audio_queue_cv_.notify_all();
            lock.unlock();
            if (callbacks_.on_decode_queue_available) {
                callbacks_.on_decode_queue_available();
            }

            auto task = std::make_unique<AudioTask>();
            task->type = kAudioTaskTypeDecodeToPlaybackQueue;
            task->timestamp = packet->timestamp;
            task->playback_generation = packet->playback_generation;
            task->pipeline_epoch = packet->pipeline_epoch;

            AudioPlaybackFailureReason failure =
                SetDecodeSampleRate(packet->sample_rate, packet->frame_duration);
            if (failure == AudioPlaybackFailureReason::kNone) {
                esp_audio_dec_in_raw_t raw = {
                    .buffer = (uint8_t *)(packet->payload.data()),
                    .len = (uint32_t)(packet->payload.size()),
                    .consumed = 0,
                    .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
                };
                esp_audio_dec_out_frame_t out_frame = {
                    .buffer = (uint8_t *)(task->pcm.data()),
                    .len = (uint32_t)(task->pcm.size() * sizeof(int16_t)),
                    .decoded_size = 0,
                };
                esp_audio_dec_info_t dec_info = {};
                std::unique_lock<std::mutex> decoder_lock(decoder_mutex_);
                esp_audio_err_t ret = ESP_AUDIO_ERR_FAIL;
                if (opus_decoder_ != nullptr) {
                    task->pcm.resize(decoder_frame_size_);
                    out_frame.buffer =
                        reinterpret_cast<uint8_t*>(task->pcm.data());
                    out_frame.len =
                        static_cast<uint32_t>(task->pcm.size() * sizeof(int16_t));
                    ret = esp_opus_dec_decode(
                        opus_decoder_, &raw, &out_frame, &dec_info);
                }
                decoder_lock.unlock();
                if (ret == ESP_AUDIO_ERR_OK) {
                    task->pcm.resize(out_frame.decoded_size / sizeof(int16_t));
                    if (decoder_sample_rate_ != codec_->output_sample_rate() && output_resampler_ != nullptr) {
                        uint32_t target_size = 0;
                        esp_ae_rate_cvt_get_max_out_sample_num(output_resampler_, task->pcm.size(), &target_size);
                        std::vector<int16_t> resampled(target_size);
                        uint32_t actual_output = target_size;
                        esp_ae_rate_cvt_process(output_resampler_, (esp_ae_sample_t)task->pcm.data(), task->pcm.size(),
                                                (esp_ae_sample_t)resampled.data(), &actual_output);
                        resampled.resize(actual_output);
                        task->pcm = std::move(resampled);
                    }
                    const bool published =
                        audio_pipeline_epoch_.PublishIfCurrent(
                            packet->pipeline_epoch,
                            [this, &task]() {
                                std::lock_guard<std::mutex> queue_lock(
                                    audio_queue_mutex_);
                                audio_playback_queue_.push_back(
                                    std::move(task));
                                audio_queue_cv_.notify_all();
                            });
                    if (published) {
                        debug_statistics_.decode_count++;
                    } else {
                        ESP_LOGI(
                            TAG,
                            "Discarded stale decoded PCM after reset (epoch=%llu)",
                            static_cast<unsigned long long>(
                                packet->pipeline_epoch));
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to decode audio after resize, error code: %d", ret);
                    failure = AudioPlaybackFailureReason::kDecodeFailed;
                }
            }
            if (failure != AudioPlaybackFailureReason::kNone) {
                ReportPlaybackFailure(
                    packet->playback_generation,
                    packet->pipeline_epoch,
                    failure);
            }
            lock.lock();
            audio_decode_busy_ = false;
            audio_queue_cv_.notify_all();
        }
        /* Encode the audio to send queue */
        if (!audio_encode_queue_.empty() && audio_send_queue_.size() < MAX_SEND_PACKETS_IN_QUEUE) {
            auto task = std::move(audio_encode_queue_.front());
            audio_encode_queue_.pop_front();
            audio_queue_cv_.notify_all();
            lock.unlock();

            auto packet = std::make_unique<AudioStreamPacket>();
            packet->frame_duration = OPUS_FRAME_DURATION_MS;
            packet->sample_rate = 16000;
            packet->timestamp = task->timestamp;

            if (opus_encoder_ != nullptr && task->pcm.size() == encoder_frame_size_) {
                std::vector<uint8_t> buf(encoder_outbuf_size_);
                esp_audio_enc_in_frame_t in = {
                    .buffer = (uint8_t *)(task->pcm.data()),
                    .len = (uint32_t)(encoder_frame_size_ * sizeof(int16_t)),
                };
                esp_audio_enc_out_frame_t out = {
                    .buffer = buf.data(),
                    .len = (uint32_t)encoder_outbuf_size_,
                    .encoded_bytes = 0,
                };
                auto ret = esp_opus_enc_process(opus_encoder_, &in, &out);
                if (ret == ESP_AUDIO_ERR_OK) {
                    packet->payload.assign(buf.data(), buf.data() + out.encoded_bytes);

                    if (task->type == kAudioTaskTypeEncodeToSendQueue) {
                        {
                            std::lock_guard<std::mutex> lock2(audio_queue_mutex_);
                            audio_send_queue_.push_back(std::move(packet));
                        }
                        if (callbacks_.on_send_queue_available) {
                            callbacks_.on_send_queue_available();
                        }
                    } else if (task->type == kAudioTaskTypeEncodeToTestingQueue) {
                        std::lock_guard<std::mutex> lock2(audio_queue_mutex_);
                        audio_testing_queue_.push_back(std::move(packet));
                    }
                    debug_statistics_.encode_count++;
                } else {
                    ESP_LOGE(TAG, "Failed to encode audio, error code: %d", ret);
                }
            } else {
                ESP_LOGE(TAG, "Failed to encode audio: encoder not configured or invalid frame size (got %u, expected %u)",
                         task->pcm.size(), encoder_frame_size_);
            }
            lock.lock();
        }
    }

    ESP_LOGW(TAG, "Opus codec task stopped");
}

AudioPlaybackFailureReason AudioService::SetDecodeSampleRate(
    int sample_rate, int frame_duration) {
    std::lock_guard<std::mutex> decoder_lock(decoder_mutex_);
    const bool decoder_matches =
        opus_decoder_ != nullptr &&
        decoder_sample_rate_ == sample_rate &&
        decoder_duration_ms_ == frame_duration;
    if (!decoder_matches) {
        if (opus_decoder_ != nullptr) {
            esp_opus_dec_close(opus_decoder_);
            opus_decoder_ = nullptr;
        }
        if (output_resampler_ != nullptr) {
            esp_ae_rate_cvt_close(output_resampler_);
            output_resampler_ = nullptr;
        }
        decoder_sample_rate_ = 0;
        decoder_frame_size_ = 0;
        esp_opus_dec_cfg_t opus_dec_cfg =
            OPUS_DEC_CFG(sample_rate, frame_duration);
        const auto ret = esp_opus_dec_open(
            &opus_dec_cfg,
            sizeof(esp_opus_dec_cfg_t),
            &opus_decoder_);
        if (opus_decoder_ == nullptr) {
            ESP_LOGE(
                TAG,
                "Failed to create audio decoder, error code: %d",
                ret);
            return AudioPlaybackFailureReason::kDecoderCreateFailed;
        }
        decoder_sample_rate_ = sample_rate;
        decoder_duration_ms_ = frame_duration;
        decoder_frame_size_ =
            decoder_sample_rate_ / 1000 * frame_duration;
    }

    const int output_sample_rate = codec_->output_sample_rate();
    if (decoder_sample_rate_ == output_sample_rate) {
        if (output_resampler_ != nullptr) {
            esp_ae_rate_cvt_close(output_resampler_);
            output_resampler_ = nullptr;
        }
        return AudioPlaybackFailureReason::kNone;
    }
    if (output_resampler_ != nullptr) {
        return AudioPlaybackFailureReason::kNone;
    }

    ESP_LOGI(
        TAG,
        "Resampling audio from %d to %d",
        decoder_sample_rate_,
        output_sample_rate);
    esp_ae_rate_cvt_cfg_t output_resampler_cfg = RATE_CVT_CFG(
        decoder_sample_rate_, output_sample_rate, ESP_AUDIO_MONO);
    const auto resampler_ret = esp_ae_rate_cvt_open(
        &output_resampler_cfg, &output_resampler_);
    if (output_resampler_ == nullptr) {
        ESP_LOGE(
            TAG,
            "Failed to create output resampler, error code: %d",
            resampler_ret);
        return AudioPlaybackFailureReason::kResamplerCreateFailed;
    }
    return AudioPlaybackFailureReason::kNone;
}

void AudioService::ReportPlaybackFailure(
    uint32_t generation,
    uint64_t pipeline_epoch,
    AudioPlaybackFailureReason reason) {
    bool recorded = false;
    const bool current = audio_pipeline_epoch_.PublishIfCurrent(
        pipeline_epoch,
        [this, generation, reason, &recorded]() {
            std::lock_guard<std::mutex> lock(audio_queue_mutex_);
            if (generation != 0) {
                playback_failures_.Record(generation, reason);
                recorded = true;
            }
            audio_queue_cv_.notify_all();
        });
    if (!current) {
        ESP_LOGI(
            TAG,
            "Ignoring stale playback failure after reset: generation=%lu epoch=%llu reason=%s",
            static_cast<unsigned long>(generation),
            static_cast<unsigned long long>(pipeline_epoch),
            AudioPlaybackFailureReasonName(reason));
        return;
    }
    if (!recorded) {
        ESP_LOGE(
            TAG,
            "Legacy audio playback failed: reason=%s",
            AudioPlaybackFailureReasonName(reason));
        return;
    }
    if (callbacks_.on_playback_failure) {
        callbacks_.on_playback_failure(generation, reason);
    }
}

bool AudioService::PushTaskToEncodeQueue(AudioTaskType type, std::vector<int16_t>&& pcm, bool wait) {
    auto task = std::make_unique<AudioTask>();
    task->type = type;
    task->pcm = std::move(pcm);
    /* Push the task to the encode queue */
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);

    auto action = audio_backpressure_action(audio_encode_queue_.size(), MAX_ENCODE_TASKS_IN_QUEUE, wait);
    if (action == kAudioBackpressureDrop) {
        debug_statistics_.encode_drop_count++;
        if (debug_statistics_.encode_drop_count == 1 || debug_statistics_.encode_drop_count % 50 == 0) {
            ESP_LOGW(TAG, "Dropping realtime audio frame because encode queue is full (%u drops)",
                     debug_statistics_.encode_drop_count);
        }
        return false;
    }
    if (action == kAudioBackpressureWait) {
        audio_queue_cv_.wait(lock, [this]() { return audio_encode_queue_.size() < MAX_ENCODE_TASKS_IN_QUEUE; });
    }

    /* If the task is to send queue, we need to set the timestamp */
    if (type == kAudioTaskTypeEncodeToSendQueue && !timestamp_queue_.empty()) {
        if (timestamp_queue_.size() <= MAX_TIMESTAMPS_IN_QUEUE) {
            task->timestamp = timestamp_queue_.front();
        } else {
            ESP_LOGW(TAG, "Timestamp queue (%u) is full, dropping timestamp", timestamp_queue_.size());
        }
        timestamp_queue_.pop_front();
    }

    audio_encode_queue_.push_back(std::move(task));
    audio_queue_cv_.notify_all();
    return true;
}

bool AudioService::TryPushPacketToDecodeQueue(
    std::unique_ptr<AudioStreamPacket>& packet) {
    if (!packet) return false;
    bool admitted = false;
    audio_pipeline_epoch_.PublishCurrent([this, &packet, &admitted](uint64_t epoch) {
        std::lock_guard<std::mutex> lock(audio_queue_mutex_);
        if (audio_decode_queue_.size() >= MAX_DECODE_PACKETS_IN_QUEUE) return;
        packet->pipeline_epoch = epoch;
        audio_decode_queue_.push_back(std::move(packet));
        admitted = true;
        audio_queue_cv_.notify_all();
    });
    return admitted;
}

bool AudioService::PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet, bool wait) {
    if (!wait) {
        return TryPushPacketToDecodeQueue(packet);
    }
    while (packet) {
        {
            std::unique_lock<std::mutex> lock(audio_queue_mutex_);
            audio_queue_cv_.wait(lock, [this]() {
                return service_stopped_ ||
                    audio_decode_queue_.size() < MAX_DECODE_PACKETS_IN_QUEUE;
            });
            if (service_stopped_) return false;
        }
        if (TryPushPacketToDecodeQueue(packet)) return true;
    }
    return false;
}

std::unique_ptr<AudioStreamPacket> AudioService::PopPacketFromSendQueue() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    if (audio_send_queue_.empty()) {
        return nullptr;
    }
    auto packet = std::move(audio_send_queue_.front());
    audio_send_queue_.pop_front();
    audio_queue_cv_.notify_all();
    return packet;
}

void AudioService::EncodeWakeWord() {
    if (wake_word_) {
        wake_word_->EncodeWakeWordData();
    }
}

const std::string& AudioService::GetLastWakeWord() const {
    return wake_word_->GetLastDetectedWakeWord();
}

std::unique_ptr<AudioStreamPacket> AudioService::PopWakeWordPacket() {
    auto packet = std::make_unique<AudioStreamPacket>();
    if (wake_word_->GetWakeWordOpus(packet->payload)) {
        return packet;
    }
    return nullptr;
}

void AudioService::EnableWakeWordDetection(bool enable) {
    if (!wake_word_) {
        return;
    }

    ESP_LOGD(TAG, "%s wake word detection", enable ? "Enabling" : "Disabling");
    if (enable) {
        if (!wake_word_initialized_) {
            if (!wake_word_->Initialize(codec_, models_list_)) {
                ESP_LOGE(TAG, "Failed to initialize wake word");
                return;
            }
            wake_word_initialized_ = true;
        }
        // Reset input resampler to clear cached data from previous mode (e.g. AudioProcessor)
        // This prevents buffer overflow when switching between different feed sizes
        {
            std::lock_guard<std::mutex> lock(input_resampler_mutex_);
            if (input_resampler_ != nullptr) {
                esp_ae_rate_cvt_reset(input_resampler_);
            }
        }
        wake_word_->Start();
        xEventGroupSetBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
    } else {
        wake_word_->Stop();
        xEventGroupClearBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
    }
}

void AudioService::EnableVoiceProcessing(bool enable) {
    ESP_LOGD(TAG, "%s voice processing", enable ? "Enabling" : "Disabling");
    if (enable) {
        if (!audio_processor_initialized_) {
            audio_processor_->Initialize(codec_, OPUS_FRAME_DURATION_MS, models_list_);
            audio_processor_initialized_ = true;
        }

        /* We should make sure no audio is playing */
        ResetDecoder();
        audio_input_need_warmup_ = true;
        // Reset input resampler to clear cached data from previous mode (e.g. WakeWord)
        // This prevents buffer overflow when switching between different feed sizes
        {
            std::lock_guard<std::mutex> lock(input_resampler_mutex_);
            if (input_resampler_ != nullptr) {
                esp_ae_rate_cvt_reset(input_resampler_);
            }
        }
        audio_processor_->Start();
        xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_PROCESSOR_RUNNING);
    } else {
        audio_processor_->Stop();
        xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_PROCESSOR_RUNNING);
    }
}

void AudioService::EnableAudioTesting(bool enable) {
    ESP_LOGI(TAG, "%s audio testing", enable ? "Enabling" : "Disabling");
    if (enable) {
        xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING);
    } else {
        xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING);
        /* Copy audio_testing_queue_ to audio_decode_queue_ */
        audio_pipeline_epoch_.PublishCurrent([this](uint64_t epoch) {
            std::lock_guard<std::mutex> lock(audio_queue_mutex_);
            for (auto& packet : audio_testing_queue_) {
                packet->pipeline_epoch = epoch;
            }
            audio_decode_queue_ = std::move(audio_testing_queue_);
            audio_queue_cv_.notify_all();
        });
    }
}

void AudioService::EnableDeviceAec(bool enable) {
    ESP_LOGI(TAG, "%s device AEC", enable ? "Enabling" : "Disabling");
    if (!audio_processor_initialized_) {
        audio_processor_->Initialize(codec_, OPUS_FRAME_DURATION_MS, models_list_);
        audio_processor_initialized_ = true;
    }

    audio_processor_->EnableDeviceAec(enable);
}

void AudioService::SetCallbacks(AudioServiceCallbacks& callbacks) {
    callbacks_ = callbacks;
}

void AudioService::PlaySound(const std::string_view& ogg) {
    if (!codec_->output_enabled()) {
        esp_timer_stop(audio_power_timer_);
        esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
        codec_->EnableOutput(true);
    }

    const auto* buf = reinterpret_cast<const uint8_t*>(ogg.data());
    size_t size = ogg.size();

    auto demuxer = std::make_unique<OggDemuxer>();
    demuxer->OnDemuxerFinished([this](const uint8_t* data, int sample_rate, size_t size){
        auto packet = std::make_unique<AudioStreamPacket>();
        packet->sample_rate = sample_rate;
        packet->frame_duration = 60;
        packet->payload.resize(size);
        std::memcpy(packet->payload.data(), data, size);
        PushPacketToDecodeQueue(std::move(packet), true);
    });
    demuxer->Reset();
    demuxer->Process(buf, size);
}

bool AudioService::IsIdle() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    return audio_encode_queue_.empty() && audio_decode_queue_.empty() && audio_playback_queue_.empty() && audio_testing_queue_.empty();
}

void AudioService::WaitForPlaybackQueueEmpty() {
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);
    audio_queue_cv_.wait(lock, [this]() { 
        return service_stopped_ || (audio_decode_queue_.empty() && audio_playback_queue_.empty()); 
    });
}

bool AudioService::WaitForPlaybackDrained(std::chrono::milliseconds timeout) {
    return WaitForPlaybackDrainResult(0, timeout).drained;
}

AudioPlaybackDrainResult AudioService::WaitForPlaybackDrainResult(
    uint32_t generation,
    std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);
    while (!service_stopped_) {
        const auto failure = playback_failures_.FailureFor(generation);
        if (failure != AudioPlaybackFailureReason::kNone) {
            return {false, failure};
        }
        const bool software_empty =
            audio_decode_queue_.empty() &&
            audio_playback_queue_.empty() &&
            !audio_decode_busy_ &&
            !audio_output_busy_;
        const auto now = std::chrono::steady_clock::now();
        const auto speaker_end =
            last_output_expected_end_ + std::chrono::milliseconds(20);
        if (software_empty && now >= speaker_end) {
            return {true, AudioPlaybackFailureReason::kNone};
        }
        if (now >= deadline) {
            return {false, AudioPlaybackFailureReason::kNone};
        }

        const auto wake_at = software_empty
            ? std::min(speaker_end, deadline)
            : deadline;
        const auto observed_decode_queue_size = audio_decode_queue_.size();
        const auto observed_playback_queue_size = audio_playback_queue_.size();
        const bool observed_decode_busy = audio_decode_busy_;
        const bool observed_output_busy = audio_output_busy_;
        const auto observed_output_sequence = audio_output_sequence_;
        audio_queue_cv_.wait_until(lock, wake_at, [this,
                                                   observed_decode_queue_size,
                                                   observed_playback_queue_size,
                                                   observed_decode_busy,
                                                   observed_output_busy,
                                                   observed_output_sequence,
                                                   generation]() {
            return service_stopped_ ||
                   playback_failures_.FailureFor(generation) !=
                       AudioPlaybackFailureReason::kNone ||
                   audio_decode_queue_.size() != observed_decode_queue_size ||
                   audio_playback_queue_.size() != observed_playback_queue_size ||
                   audio_decode_busy_ != observed_decode_busy ||
                   audio_output_busy_ != observed_output_busy ||
                   audio_output_sequence_ != observed_output_sequence;
        });
    }
    return {false, AudioPlaybackFailureReason::kNone};
}

void AudioService::ResetDecoder() {
    audio_pipeline_epoch_.Reset([this]() {
        std::lock_guard<std::mutex> queue_lock(audio_queue_mutex_);
        std::lock_guard<std::mutex> decoder_lock(decoder_mutex_);
        if (opus_decoder_ != nullptr) {
            esp_opus_dec_reset(opus_decoder_);
        }
        timestamp_queue_.clear();
        audio_decode_queue_.clear();
        audio_playback_queue_.clear();
        audio_testing_queue_.clear();
        playback_failures_.Clear();
        audio_queue_cv_.notify_all();
    });
}

void AudioService::CheckAndUpdateAudioPowerState() {
    auto now = std::chrono::steady_clock::now();
    auto input_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_input_time_).count();
    auto output_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_output_time_).count();
    if (input_elapsed > AUDIO_POWER_TIMEOUT_MS && codec_->input_enabled()) {
        codec_->EnableInput(false);
    }
    if (output_elapsed > AUDIO_POWER_TIMEOUT_MS && codec_->output_enabled()) {
        // Keep TX clock when duplex RX is active; otherwise RX may stall on some boards.
        if (!(codec_->duplex() && codec_->input_enabled())) {
            codec_->EnableOutput(false);
        }
    }
    if (!codec_->input_enabled() && !codec_->output_enabled()) {
        esp_timer_stop(audio_power_timer_);
    }
}

void AudioService::SetModelsList(srmodel_list_t* models_list) {
    models_list_ = models_list;

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
    if (esp_srmodel_filter(models_list_, ESP_MN_PREFIX, NULL) != nullptr) {
        wake_word_ = std::make_unique<CustomWakeWord>();
    } else if (esp_srmodel_filter(models_list_, ESP_WN_PREFIX, NULL) != nullptr) {
        wake_word_ = std::make_unique<AfeWakeWord>();
    } else {
        wake_word_ = nullptr;
    }
#else
    if (esp_srmodel_filter(models_list_, ESP_WN_PREFIX, NULL) != nullptr) {
        wake_word_ = std::make_unique<EspWakeWord>();
    } else {
        wake_word_ = nullptr;
    }
#endif

    if (wake_word_) {
        wake_word_->OnWakeWordDetected([this](const std::string& wake_word) {
            if (callbacks_.on_wake_word_detected) {
                callbacks_.on_wake_word_detected(wake_word);
            }
        });
    }
}

bool AudioService::IsAfeWakeWord() {
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
    return wake_word_ != nullptr && dynamic_cast<AfeWakeWord*>(wake_word_.get()) != nullptr;
#else
    return false;
#endif
}
