#include "lvgl_gif.h"
#include <esp_log.h>
#include <cstring>

#define TAG "LvglGif"

LvglGif::LvglGif(
        const lv_img_dsc_t* img_dsc,
        bool force_opaque_background,
        uint32_t background_rgb,
        bool output_rgb565
    )
    : gif_(nullptr), img_dsc_(), rgb565_data_(nullptr),
      force_opaque_background_(force_opaque_background),
      output_rgb565_(output_rgb565),
      background_r_((background_rgb >> 16) & 0xFF),
      background_g_((background_rgb >> 8) & 0xFF),
      background_b_(background_rgb & 0xFF),
      timer_(nullptr), last_call_(0), playing_(false), loaded_(false),
      loop_delay_ms_(0), loop_waiting_(false), loop_wait_start_(0) {
    if (!img_dsc || !img_dsc->data) {
        ESP_LOGE(TAG, "Invalid image descriptor");
        return;
    }

    gif_ = gd_open_gif_data(img_dsc->data);
    if (!gif_) {
        ESP_LOGE(TAG, "Failed to open GIF from image descriptor");
        return;
    }

    // Setup LVGL image descriptor
    memset(&img_dsc_, 0, sizeof(img_dsc_));
    img_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    img_dsc_.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
    img_dsc_.header.w = gif_->width;
    img_dsc_.header.h = gif_->height;
    if (output_rgb565_) {
        const size_t rgb565_size = gif_->width * gif_->height * 2;
        rgb565_data_ = static_cast<uint8_t*>(lv_malloc(rgb565_size));
        if (rgb565_data_ == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate RGB565 GIF buffer");
            gd_close_gif(gif_);
            gif_ = nullptr;
            return;
        }
        img_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        img_dsc_.header.stride = gif_->width * 2;
        img_dsc_.data = rgb565_data_;
        img_dsc_.data_size = rgb565_size;
    } else {
        img_dsc_.header.cf = LV_COLOR_FORMAT_ARGB8888;
        img_dsc_.header.stride = gif_->width * 4;
        img_dsc_.data = gif_->canvas;
        img_dsc_.data_size = gif_->width * gif_->height * 4;
    }

    // Render first frame
    if (gif_->canvas) {
        gd_render_frame(gif_, gif_->canvas);
        UpdateImageData();
    }

    loaded_ = true;
    ESP_LOGD(TAG, "GIF loaded from image descriptor: %dx%d", gif_->width, gif_->height);
}

// Destructor
LvglGif::~LvglGif() {
    Cleanup();
}

// LvglImage interface implementation
const lv_img_dsc_t* LvglGif::image_dsc() const {
    if (!loaded_) {
        return nullptr;
    }
    return &img_dsc_;
}

// Animation control methods
void LvglGif::Start() {
    if (!loaded_ || !gif_) {
        ESP_LOGW(TAG, "GIF not loaded, cannot start");
        return;
    }

    if (!timer_) {
        timer_ = lv_timer_create([](lv_timer_t* timer) {
            LvglGif* gif_obj = static_cast<LvglGif*>(lv_timer_get_user_data(timer));
            gif_obj->NextFrame();
        }, 10, this);
    }

    if (timer_) {
        playing_ = true;
        loop_waiting_ = false;  // Reset loop waiting state
        last_call_ = lv_tick_get();
        lv_timer_resume(timer_);
        lv_timer_reset(timer_);
        
        // Render first frame
        NextFrame();
        
        ESP_LOGD(TAG, "GIF animation started");
    }
}

void LvglGif::Pause() {
    if (timer_) {
        playing_ = false;
        lv_timer_pause(timer_);
        ESP_LOGD(TAG, "GIF animation paused");
    }
}

void LvglGif::Resume() {
    if (!loaded_ || !gif_) {
        ESP_LOGW(TAG, "GIF not loaded, cannot resume");
        return;
    }

    if (timer_) {
        playing_ = true;
        lv_timer_resume(timer_);
        ESP_LOGD(TAG, "GIF animation resumed");
    }
}

void LvglGif::Stop() {
    if (timer_) {
        playing_ = false;
        lv_timer_pause(timer_);
    }

    // Reset loop waiting state
    loop_waiting_ = false;

    if (gif_) {
        gd_rewind(gif_);
        // Render first frame without advancing
        if (gif_->canvas) {
            gd_render_frame(gif_, gif_->canvas);
            UpdateImageData();
        }
        ESP_LOGD(TAG, "GIF animation stopped and rewound");
    }
}

bool LvglGif::IsPlaying() const {
    return playing_;
}

bool LvglGif::IsLoaded() const {
    return loaded_;
}

int32_t LvglGif::GetLoopCount() const {
    if (!loaded_ || !gif_) {
        return -1;
    }
    return gif_->loop_count;
}

void LvglGif::SetLoopCount(int32_t count) {
    if (!loaded_ || !gif_) {
        ESP_LOGW(TAG, "GIF not loaded, cannot set loop count");
        return;
    }
    gif_->loop_count = count;
}

uint32_t LvglGif::GetLoopDelay() const {
    return loop_delay_ms_;
}

void LvglGif::SetLoopDelay(uint32_t delay_ms) {
    loop_delay_ms_ = delay_ms;
    ESP_LOGD(TAG, "Loop delay set to %lu ms", delay_ms);
}

uint16_t LvglGif::width() const {
    if (!loaded_ || !gif_) {
        return 0;
    }
    return gif_->width;
}

uint16_t LvglGif::height() const {
    if (!loaded_ || !gif_) {
        return 0;
    }
    return gif_->height;
}

void LvglGif::SetFrameCallback(std::function<void()> callback) {
    frame_callback_ = callback;
}

void LvglGif::NextFrame() {
    if (!loaded_ || !gif_ || !playing_) {
        return;
    }

    // Check if we're in loop wait state (only for infinite loop GIFs with delay)
    if (loop_waiting_) {
        uint32_t wait_elapsed = lv_tick_elaps(loop_wait_start_);
        if (wait_elapsed < loop_delay_ms_) {
            // Still waiting for loop delay
            return;
        }
        // Loop delay completed, continue playing
        loop_waiting_ = false;
        ESP_LOGD(TAG, "Loop delay completed, continuing GIF");
    }

    // Check if enough time has passed for the next frame
    uint32_t elapsed = lv_tick_elaps(last_call_);
    if (elapsed < gif_->gce.delay * 10) {
        return;
    }

    last_call_ = lv_tick_get();

    // Save file position before getting next frame to detect loop
    uint32_t pos_before = gif_->f_rw_p;

    // Get next frame
    int has_next = gd_get_frame(gif_);
    if (has_next == 0) {
        // Animation truly finished (non-infinite loop)
        playing_ = false;
        if (timer_) {
            lv_timer_pause(timer_);
        }
        ESP_LOGD(TAG, "GIF animation completed");
        return;
    }

    // Detect loop by checking if file position jumped back (rewound to start)
    // This works for looping GIFs regardless of when loop_count is set
    if (loop_delay_ms_ > 0 && gif_->f_rw_p < pos_before) {
        // File position decreased, meaning GIF looped back to beginning
        // Start waiting before rendering this frame
        loop_waiting_ = true;
        loop_wait_start_ = lv_tick_get();
        ESP_LOGD(TAG, "GIF completed one cycle, waiting %lu ms before next loop", loop_delay_ms_);
        return;
    }

    // Render current frame
    if (gif_->canvas) {
        gd_render_frame(gif_, gif_->canvas);
        UpdateImageData();
        
        // Call frame callback if set
        if (frame_callback_) {
            frame_callback_();
        }
    }
}

void LvglGif::ApplyOpaqueBackground() {
    if (!force_opaque_background_ || !gif_ || gif_->canvas == nullptr) {
        return;
    }

    const uint32_t pixel_count = (uint32_t)gif_->width * gif_->height;
    uint8_t* pixel = gif_->canvas;
    for (uint32_t i = 0; i < pixel_count; ++i, pixel += 4) {
        const uint8_t alpha = pixel[3];
        if (alpha == 0xFF) {
            continue;
        }

        if (alpha == 0x00) {
            pixel[0] = background_b_;
            pixel[1] = background_g_;
            pixel[2] = background_r_;
        } else {
            const uint16_t inv_alpha = 255 - alpha;
            pixel[0] = (uint8_t)(((uint16_t)pixel[0] * alpha + (uint16_t)background_b_ * inv_alpha) / 255);
            pixel[1] = (uint8_t)(((uint16_t)pixel[1] * alpha + (uint16_t)background_g_ * inv_alpha) / 255);
            pixel[2] = (uint8_t)(((uint16_t)pixel[2] * alpha + (uint16_t)background_r_ * inv_alpha) / 255);
        }
        pixel[3] = 0xFF;
    }
}

void LvglGif::UpdateImageData() {
    ApplyOpaqueBackground();
    if (!output_rgb565_ || !gif_ || gif_->canvas == nullptr || rgb565_data_ == nullptr) {
        return;
    }

    const uint32_t pixel_count = (uint32_t)gif_->width * gif_->height;
    const uint8_t* src = gif_->canvas;
    uint16_t* dst = reinterpret_cast<uint16_t*>(rgb565_data_);
    for (uint32_t i = 0; i < pixel_count; ++i, src += 4) {
        const uint8_t b = src[0];
        const uint8_t g = src[1];
        const uint8_t r = src[2];
        dst[i] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
}

void LvglGif::Cleanup() {
    // Stop and delete timer
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }

    // Close GIF decoder
    if (gif_) {
        gd_close_gif(gif_);
        gif_ = nullptr;
    }
    if (rgb565_data_) {
        lv_free(rgb565_data_);
        rgb565_data_ = nullptr;
    }

    playing_ = false;
    loaded_ = false;
    
    // Clear image descriptor
    memset(&img_dsc_, 0, sizeof(img_dsc_));
}
