#pragma once

#include "../lvgl_image.h"
#include "gifdec.h"
#include <lvgl.h>
#include <memory>
#include <functional>

/**
 * C++ implementation of LVGL GIF widget
 * Provides GIF animation functionality using gifdec library
 */
class LvglGif {
public:
    explicit LvglGif(
        const lv_img_dsc_t* img_dsc,
        bool force_opaque_background = false,
        uint32_t background_rgb = 0xFFFFFF,
        bool output_rgb565 = false
    );
    virtual ~LvglGif();

    // LvglImage interface implementation
    virtual const lv_img_dsc_t* image_dsc() const;

    /**
     * Start/restart GIF animation
     */
    void Start();

    /**
     * Pause GIF animation
     */
    void Pause();

    /**
     * Resume GIF animation
     */
    void Resume();

    /**
     * Stop GIF animation and rewind to first frame
     */
    void Stop();

    /**
     * Check if GIF is currently playing
     */
    bool IsPlaying() const;

    /**
     * Check if GIF was loaded successfully
     */
    bool IsLoaded() const;

    /**
     * Get loop count
     */
    int32_t GetLoopCount() const;

    /**
     * Set loop count
     */
    void SetLoopCount(int32_t count);

    /**
     * Get loop delay in milliseconds (delay between loops)
     */
    uint32_t GetLoopDelay() const;

    /**
     * Set loop delay in milliseconds (delay between loops)
     * @param delay_ms Delay in milliseconds before starting next loop. 0 means no delay.
     */
    void SetLoopDelay(uint32_t delay_ms);

    /**
     * Get GIF dimensions
     */
    uint16_t width() const;
    uint16_t height() const;

    /**
     * Set frame update callback
     */
    void SetFrameCallback(std::function<void()> callback);

private:
    // GIF decoder instance
    gd_GIF* gif_;
    
    // LVGL image descriptor
    lv_img_dsc_t img_dsc_;
    uint8_t* rgb565_data_;

    // Optional opaque backing for displays that show artifacts around transparent GIF edges.
    bool force_opaque_background_;
    bool output_rgb565_;
    uint8_t background_r_;
    uint8_t background_g_;
    uint8_t background_b_;
    
    // Animation timer
    lv_timer_t* timer_;
    
    // Last frame update time
    uint32_t last_call_;
    
    // Animation state
    bool playing_;
    bool loaded_;
    
    // Loop delay configuration
    uint32_t loop_delay_ms_;      // Delay between loops in milliseconds
    bool loop_waiting_;           // Whether we're waiting for the next loop
    uint32_t loop_wait_start_;    // Timestamp when loop wait started
    
    // Frame update callback
    std::function<void()> frame_callback_;
    
    /**
     * Update to next frame
     */
    void NextFrame();

    void ApplyOpaqueBackground();
    void UpdateImageData();
    
    /**
     * Cleanup resources
     */
    void Cleanup();
};
