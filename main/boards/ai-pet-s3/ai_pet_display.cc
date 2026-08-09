/**
 * @file ai_pet_display.cc
 * @brief AI Pet full-screen GIF expression display with idle animation
 *
 * Overrides LcdDisplay::SetupUI() to reconfigure the LVGL UI layout
 * for full-screen animated expressions. The base class creates all
 * LVGL objects (emoji_box_, emoji_image_, top_bar_, bottom_bar_, etc.);
 * we then resize emoji_box_ + preview_image_ to fill the screen.
 *
 * Idle animation uses esp_timer (same pattern as LcdDisplay::preview_timer_)
 * to cycle through calm expressions when the pet is idle.
 *
 * Reference: firmware/main/boards/otto-robot/otto_emoji_display.cc:19-38
 */
#include "ai_pet_display.h"

#include <cstring>
#include <esp_log.h>

#define TAG "AiPetDisplay"

// 闲置状态下轮换的平静表情
// "happy" → happy.gif, "idle" → staticstate.gif, "thinking" → buxue.gif
// 参考: firmware/scripts/build_default_assets.py:229-236 (otto_gif_aliases)
static const char* kIdleEmotions[] = {"happy", "idle", "thinking"};
static constexpr int kIdleEmotionCount = 3;

AiPetDisplay::AiPetDisplay(esp_lcd_panel_io_handle_t panel_io,
                           esp_lcd_panel_handle_t panel,
                           int width, int height,
                           int offset_x, int offset_y,
                           bool mirror_x, bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
                    mirror_x, mirror_y, swap_xy) {
    // 创建闲置动画定时器 (与 LcdDisplay::preview_timer_ 同一模式)
    // 参考: firmware/main/display/lcd_display.cc:79-89
    esp_timer_create_args_t idle_timer_args = {
        .callback = IdleAnimTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "idle_anim",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&idle_timer_args, &idle_anim_timer_);
}

AiPetDisplay::~AiPetDisplay() {
    if (idle_anim_timer_) {
        esp_timer_stop(idle_anim_timer_);
        esp_timer_delete(idle_anim_timer_);
    }
}

void AiPetDisplay::SetupUI() {
    // Prevent duplicate calls - parent also checks, but early return here
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping");
        return;
    }

    // 1. Call parent to create all LVGL objects
    //    Creates: container_, emoji_box_(LV_SIZE_CONTENT), emoji_image_,
    //    emoji_label_, preview_image_(w/2 x h/2), top_bar_, status_bar_,
    //    bottom_bar_, chat_message_label_
    // Reference: lcd_display.cc #else branch (line 804+), used when
    //   CONFIG_USE_DEFAULT_MESSAGE_STYLE=y (AI Pet's config)
    SpiLcdDisplay::SetupUI();

    // 2. Reconfigure for full-screen expression display
    {
        DisplayLockGuard lock(this);

        // Expand emoji_box_ to full screen
        // Base class creates it as LV_SIZE_CONTENT, centered (lcd_display.cc:833-838)
        lv_obj_set_size(emoji_box_, width_, height_);
        lv_obj_set_style_pad_all(emoji_box_, 0, 0);
        lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, 0);

        // emoji_image_ stays as LV_SIZE_CONTENT (auto-sizes to GIF native resolution),
        // centered within the now-full-screen emoji_box_
        // Reference: lcd_display.cc:845-846
        lv_obj_center(emoji_image_);

        // Expand preview_image_ to full screen (same pattern as OttoEmojiDisplay)
        // Reference: otto_emoji_display.cc:33
        lv_obj_set_size(preview_image_, width_, height_);

        // Hide bottom chat bar for cleaner full-screen expression
        // TODO(Phase2-Expression): Restore bottom_bar_ with semi-transparent
        //   overlay when chat text display is redesigned for pet mode
        if (bottom_bar_ != nullptr) {
            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // 3. Set initial emotion (must be outside DisplayLockGuard scope
    //    because SetEmotion acquires the lock internally)
    // Reference: otto_emoji_display.cc:37
    SetEmotion("happy");

    ESP_LOGI(TAG, "Full-screen expression UI initialized (%dx%d)", width_, height_);
}

void AiPetDisplay::SetEmotion(const char* emotion) {
    if (strcmp(emotion, "neutral") == 0) {
        // 固件状态机在 idle/listening/connecting 时调用 SetEmotion("neutral")
        // 显示 happy 作为默认表情，同时启动闲置动画延迟定时器
        // 参考: firmware/main/application.cc:866-880 (三种状态都调用 neutral)
        SpiLcdDisplay::SetEmotion("happy");
        StartIdleDelayTimer();
        return;
    }

    // 非 neutral 表情（来自服务端 emotion 消息），停止闲置动画
    StopIdleAnimation();
    SpiLcdDisplay::SetEmotion(emotion);
}

void AiPetDisplay::StartIdleDelayTimer() {
    // 重置并启动延迟定时器
    // 每次收到 neutral 都重新计时，确保短暂的 connecting/listening 不会触发动画
    esp_timer_stop(idle_anim_timer_);
    idle_emotion_index_ = 0;
    idle_anim_active_ = false;
    ESP_ERROR_CHECK(esp_timer_start_once(idle_anim_timer_, kIdleDelayMs * 1000));
}

void AiPetDisplay::StopIdleAnimation() {
    if (idle_anim_timer_) {
        esp_timer_stop(idle_anim_timer_);
    }
    idle_anim_active_ = false;
    idle_emotion_index_ = 0;
}

void AiPetDisplay::IdleAnimTimerCallback(void* arg) {
    auto* self = static_cast<AiPetDisplay*>(arg);
    self->OnIdleAnimTimer();
}

void AiPetDisplay::OnIdleAnimTimer() {
    idle_anim_active_ = true;

    // 选择下一个平静表情
    const char* next_emotion = kIdleEmotions[idle_emotion_index_ % kIdleEmotionCount];
    idle_emotion_index_++;

    ESP_LOGI(TAG, "Idle animation: switching to '%s'", next_emotion);

    // 直接调用父类 SetEmotion，绕过 AiPetDisplay 的 neutral 映射逻辑
    SpiLcdDisplay::SetEmotion(next_emotion);

    // 调度下一次切换
    ESP_ERROR_CHECK(esp_timer_start_once(idle_anim_timer_, kIdleCycleMs * 1000));
}
