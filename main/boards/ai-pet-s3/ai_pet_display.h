#pragma once

#include "display/lcd_display.h"
#include <esp_timer.h>

/**
 * AI Pet full-screen expression display with idle animation
 *
 * Inherits SpiLcdDisplay. Overrides SetupUI() to expand emoji_box_ and
 * emoji_image_ to full screen so GIF expressions fill the entire 240x280
 * display instead of the small centered default.
 *
 * Idle animation: When the pet enters idle state (SetEmotion("neutral")),
 * after a 15s delay it begins cycling through calm expressions
 * (happy -> idle -> thinking) every ~20s to make the pet feel alive.
 * Any non-neutral SetEmotion call (e.g. from server emotion message)
 * immediately stops the idle animation.
 *
 * Reference: firmware/main/boards/otto-robot/otto_emoji_display.h
 */
class AiPetDisplay : public SpiLcdDisplay {
   public:
    AiPetDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                 int width, int height, int offset_x, int offset_y,
                 bool mirror_x, bool mirror_y, bool swap_xy);

    virtual ~AiPetDisplay();
    virtual void SetupUI() override;
    virtual void SetEmotion(const char* emotion) override;

   private:
    // 首次 neutral→idle 切换的延迟 (ms)
    static constexpr uint32_t kIdleDelayMs = 15000;
    // idle 表情轮换间隔 (ms)
    static constexpr uint32_t kIdleCycleMs = 20000;

    esp_timer_handle_t idle_anim_timer_ = nullptr;
    int idle_emotion_index_ = 0;
    bool idle_anim_active_ = false;

    void StartIdleDelayTimer();
    void StopIdleAnimation();
    static void IdleAnimTimerCallback(void* arg);
    void OnIdleAnimTimer();
};
