#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/display.h"
#include "display/lcd_display.h"
#include "display/lvgl_display/gif/lvgl_gif.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "power_save_timer.h"
#include "settings.h"
#include "time_sync_status.h"
#include "assets/lang_config.h"
#include "boot_diagnostics.h"
#include "runtime_health.h"

#include <esp_check.h>
#include <esp_app_desc.h>
#include <esp_console.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <font_awesome.h>
#include "i2c_device.h"
#include <driver/i2c_master.h>
#include <driver/ledc.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_spd2010.h>
#include <esp_lcd_touch.h>
#include <esp_lcd_touch_cst9217.h>
#include <esp_lcd_touch_spd2010.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include "esp_io_expander_tca9554.h"
#include <iot_button.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <time.h>

extern "C" {
#include "paopao_pet_emotion.h"
#include "paopao_pet_behavior.h"
#include "paopao_pet_mood.h"
#include "paopao_pet_gif_assets.h"
#include "paopao_pet_state.h"
#include "paopao_pet_trigger.h"
#include "xiaoxin_battery_state.h"
#include "xiaoxin_card_pager.h"
#include "xiaoxin_overview_model.h"
#include "xiaoxin_power_control.h"
#include "xiaoxin_low_power_clock_model.h"
#include "xiaoxin_settings_model.h"
#include "xiaoxin_system_overlay.h"
}

#define TAG "waveshare_lcd_1_46"

LV_FONT_DECLARE(font_puhui_basic_30_4);
LV_FONT_DECLARE(font_puhui_basic_20_4);

extern const uint8_t assets_images_idle_gif_start[] asm("_binary_idle_gif_start");
extern const uint8_t assets_images_idle_gif_end[] asm("_binary_idle_gif_end");
extern const uint8_t assets_images_working_gif_start[] asm("_binary_working_gif_start");
extern const uint8_t assets_images_working_gif_end[] asm("_binary_working_gif_end");
extern const uint8_t assets_images_speaking_fixed_gif_start[] asm("_binary_speaking_fixed_gif_start");
extern const uint8_t assets_images_speaking_fixed_gif_end[] asm("_binary_speaking_fixed_gif_end");
extern const uint8_t assets_images_thinking_gif_start[] asm("_binary_thinking_gif_start");
extern const uint8_t assets_images_thinking_gif_end[] asm("_binary_thinking_gif_end");
extern const uint8_t assets_images_waiting_gif_start[] asm("_binary_waiting_gif_start");
extern const uint8_t assets_images_waiting_gif_end[] asm("_binary_waiting_gif_end");
extern const uint8_t assets_images_done_gif_start[] asm("_binary_done_gif_start");
extern const uint8_t assets_images_done_gif_end[] asm("_binary_done_gif_end");
extern const uint8_t assets_images_sleeping_gif_start[] asm("_binary_sleeping_gif_start");
extern const uint8_t assets_images_sleeping_gif_end[] asm("_binary_sleeping_gif_end");
extern const uint8_t assets_images_jumping_gif_start[] asm("_binary_jumping_gif_start");
extern const uint8_t assets_images_jumping_gif_end[] asm("_binary_jumping_gif_end");
extern const uint8_t assets_images_failed_gif_start[] asm("_binary_failed_gif_start");
extern const uint8_t assets_images_failed_gif_end[] asm("_binary_failed_gif_end");
extern const uint8_t assets_images_giddy_gif_start[] asm("_binary_giddy_gif_start");
extern const uint8_t assets_images_giddy_gif_end[] asm("_binary_giddy_gif_end");
extern const uint8_t assets_images_review_gif_start[] asm("_binary_review_gif_start");
extern const uint8_t assets_images_review_gif_end[] asm("_binary_review_gif_end");
extern const uint8_t assets_images_happy_gif_start[] asm("_binary_happy_gif_start");
extern const uint8_t assets_images_happy_gif_end[] asm("_binary_happy_gif_end");
extern const uint8_t assets_images_boot_gif_start[] asm("_binary_boot_gif_start");
extern const uint8_t assets_images_boot_gif_end[] asm("_binary_boot_gif_end");
extern const uint8_t assets_images_waving_gif_start[] asm("_binary_waving_gif_start");
extern const uint8_t assets_images_waving_gif_end[] asm("_binary_waving_gif_end");

class CustomBoard;
static PowerSaveTimer* TargetPowerSaveTimer();
static void WakePowerSaveTimerFromTouch();
static void RequestSettingsWifiConfigFromSettingsPage();
static bool s_boot_on_battery = false;
extern const uint8_t assets_images_crying_gif_start[] asm("_binary_crying_gif_start");
extern const uint8_t assets_images_crying_gif_end[] asm("_binary_crying_gif_end");
extern const uint8_t assets_images_anxiety_gif_start[] asm("_binary_anxiety_gif_start");
extern const uint8_t assets_images_anxiety_gif_end[] asm("_binary_anxiety_gif_end");
extern const uint8_t assets_images_tired_gif_start[] asm("_binary_tired_gif_start");
extern const uint8_t assets_images_tired_gif_end[] asm("_binary_tired_gif_end");
extern const uint8_t assets_images_stamp_gif_start[] asm("_binary_stamp_gif_start");
extern const uint8_t assets_images_stamp_gif_end[] asm("_binary_stamp_gif_end");

static constexpr uint32_t k_touch_hold_ms = 1200;
static constexpr int16_t k_touch_drag_min_px = 42;
static constexpr uint32_t k_touch_motion_suppress_ms = 600;
static constexpr uint32_t k_touch_poll_ms = 16;
static constexpr uint32_t k_pet_render_task_stack_bytes = 12 * 1024;
static constexpr uint32_t k_power_off_release_poll_ms = 20;
static constexpr adc_channel_t k_battery_adc_channel = ADC_CHANNEL_7;
static constexpr int k_battery_voltage_divider = 3;
static constexpr int k_external_power_voltage_mv = 4500;
static constexpr uint64_t k_battery_monitor_interval_us = 2 * 1000 * 1000;
static constexpr uint64_t k_low_battery_shutdown_delay_us = 3 * 1000 * 1000;
static constexpr uint8_t k_battery_runtime_sample_count = 4;
static constexpr uint32_t k_motion_poll_ms = 50;
static constexpr uint32_t k_shake_cooldown_ms = 1800;
static constexpr int32_t k_shake_delta_threshold = 12000;
static constexpr uint8_t k_shake_required_samples = 3;
static constexpr bool k_ui_perf_trace_enabled = false;
static constexpr uint32_t k_ui_perf_log_interval_ms = 1000;
static constexpr uint16_t k_white_rgb565 = 0xFFFF;
static constexpr uint32_t k_pet_image_scale_base = LV_SCALE_NONE;
static constexpr uint16_t k_pet_target_visual_longest = 243;
static constexpr uint16_t k_card_snap_min_anim_ms = 150;
static constexpr uint16_t k_card_snap_max_anim_ms = 240;
static constexpr uint32_t k_boot_splash_duration_ms = 2400;
static constexpr uint32_t k_boot_splash_ready_hold_timeout_ms = 120000;
static constexpr uint32_t k_boot_splash_ui_ready_reveal_delay_ms = 5000;
static constexpr uint32_t k_boot_greeting_duration_ms = 2000;
static constexpr int k_boot_splash_brightness_percent = 100;
static constexpr uint8_t k_card_glass_count = XIAOXIN_CARD_NOTIFICATION_MAX;
static constexpr uint8_t k_notification_indicator_dot_count = XIAOXIN_CARD_NOTIFICATION_MAX;
static constexpr uint8_t k_overview_visible_row_count = 5;
static constexpr uint8_t k_settings_item_max_count = 6;
static constexpr int16_t k_settings_panel_w = 264;
static constexpr int16_t k_settings_panel_h = 288;
static constexpr int16_t k_settings_panel_radius = 28;
static constexpr int16_t k_settings_title_y = 22;
static constexpr int16_t k_settings_hint_bottom_offset = 18;
static constexpr int16_t k_settings_about_body_y = 92;
static constexpr int16_t k_settings_row_x = 22;
static constexpr int16_t k_settings_row_y = 60;
static constexpr int16_t k_settings_row_w = 220;
static constexpr int16_t k_settings_row_h = 34;
static constexpr int16_t k_settings_row_pitch = 38;
static constexpr int16_t k_settings_back_row_w = 156;
static constexpr int16_t k_settings_back_row_h = 36;
static constexpr int16_t k_settings_back_row_y = 240;
static constexpr int16_t k_settings_button_hit_slop_x = 32;
static constexpr int16_t k_settings_button_hit_slop_y = 22;
static constexpr int16_t k_settings_brightness_value_y = 76;
static constexpr int16_t k_settings_brightness_track_y = 140;
static constexpr int16_t k_settings_brightness_track_w = 190;
static constexpr int16_t k_settings_brightness_track_h = 12;
static constexpr int16_t k_settings_brightness_level_label_y = 158;
static constexpr int16_t k_settings_brightness_thumb_size = 24;
static constexpr int16_t k_settings_brightness_back_button_w = 132;
static constexpr int16_t k_settings_brightness_back_button_h = 38;
static constexpr int16_t k_settings_brightness_back_button_y = 180;
static constexpr uint32_t k_settings_panel_bg = 0x111827;
static constexpr uint32_t k_settings_panel_border = 0x4a9eff;
static constexpr uint32_t k_settings_text_primary = 0xe8eaed;
static constexpr uint32_t k_settings_text_secondary = 0x7d9cc6;
static constexpr uint32_t k_card_layer_bg_color = 0xe9edf3;
static constexpr lv_opa_t k_card_layer_bg_opa = static_cast<lv_opa_t>(18);
static constexpr uint32_t k_page_title_color = 0x111111;
static constexpr uint32_t k_card_bg_color = 0x17181d;
static constexpr uint32_t k_card_border_color = 0x5a5f6b;
static constexpr uint32_t k_card_shadow_color = 0x000000;
static constexpr lv_opa_t k_glass_bg_opa[k_card_glass_count] = {
    static_cast<lv_opa_t>(174),
    static_cast<lv_opa_t>(124),
    static_cast<lv_opa_t>(82),
    static_cast<lv_opa_t>(52),
    static_cast<lv_opa_t>(36),
    static_cast<lv_opa_t>(24)
};
static constexpr lv_opa_t k_glass_border_opa[k_card_glass_count] = {
    static_cast<lv_opa_t>(44),
    static_cast<lv_opa_t>(20),
    static_cast<lv_opa_t>(12),
    static_cast<lv_opa_t>(8),
    static_cast<lv_opa_t>(6),
    static_cast<lv_opa_t>(4)
};
static constexpr int16_t k_glass_radius = 34;
static constexpr int16_t k_glass_width = 268;
static constexpr int16_t k_glass_height = 150;
static constexpr int16_t k_glass_pad_v = 18;
static constexpr int16_t k_glass_pad_h = 22;
static constexpr int16_t k_glass_text_x = 24;
static constexpr int16_t k_glass_text_y = 82;
static constexpr int16_t k_glass_text_w = 218;
static constexpr int16_t k_glass_tag_x = 154;
static constexpr int16_t k_glass_tag_w = 38;
static constexpr int16_t k_glass_arrow_x = 200;
static constexpr int16_t k_notification_icon_size = 46;
static constexpr int16_t k_notification_indicator_dot_size = 5;
static constexpr int16_t k_dot_size = 8;
static constexpr uint32_t k_dot_color_urgent = 0xff5e5b;
static constexpr uint32_t k_dot_color_warning = 0xffb84d;
static constexpr uint32_t k_dot_color_info = 0x4fc3f7;
static constexpr int16_t k_overview_row_w = 270;
static constexpr int16_t k_overview_row_h = 58;
static constexpr uint32_t k_title_accent = 0x77c7ff;
static constexpr uint32_t k_text_primary = 0xe8eaed;
static constexpr uint32_t k_text_secondary = 0x7d9cc6;
static constexpr uint32_t k_text_dimmed = 0xa9b8ca;
static constexpr uint32_t k_tag_text = 0x9fd8ff;
static constexpr uint32_t k_tag_bg = 0x1d3654;
static constexpr uint32_t k_indicator_top = 0x2a4a6b;
static constexpr uint32_t k_indicator_home = 0x1e3350;
static constexpr uint32_t k_separator_color = 0x30597f;
static constexpr lv_opa_t k_separator_opa = static_cast<lv_opa_t>(46);
static constexpr uint32_t k_entry_fade_ms = 120;
static constexpr uint32_t k_entry_stagger_ms = 50;
static constexpr uint32_t k_notification_switch_anim_ms = 160;
static constexpr int16_t k_notification_dismiss_intent_px = 18;
static constexpr int16_t k_notification_dismiss_threshold_px = 72;
static constexpr uint32_t k_notification_dismiss_fly_ms = 160;
static constexpr uint32_t k_notification_dismiss_rebound_ms = 120;
static constexpr int16_t k_glass_y_start = 96;
static constexpr int16_t k_glass_stack_pitch = 15;
static constexpr int16_t k_notification_slide_pitch = 116;
static constexpr uint8_t k_low_power_wave_bar_count = 20;
static constexpr uint8_t k_low_power_wave_bar_min_level = 1;
static constexpr uint8_t k_low_power_wave_bar_max_level = 12;
static constexpr uint8_t k_low_power_wave_bar_step = 3;
static constexpr uint8_t k_low_power_left_gauge_point_count = 120;
static constexpr uint8_t k_low_power_left_gauge_tick_count = 40;
static constexpr uint64_t k_low_power_clock_timer_period_us = 50 * 1000;
static constexpr int16_t k_notification_clear_button_w = 104;
static constexpr int16_t k_notification_clear_button_h = 32;
static constexpr int16_t k_notification_clear_button_y = 46;
static constexpr int16_t k_notification_empty_panel_w = 164;
static constexpr int16_t k_notification_empty_panel_h = 52;
static constexpr int16_t k_notification_empty_panel_y = 176;
static constexpr uint32_t k_notification_empty_panel_bg = 0xffffff;
static constexpr lv_opa_t k_notification_empty_panel_opa = static_cast<lv_opa_t>(172);
static constexpr lv_opa_t k_notification_empty_panel_border_opa = static_cast<lv_opa_t>(34);
static constexpr int16_t k_overview_time_y = 26;
static constexpr int16_t k_overview_date_y = 49;
static constexpr int16_t k_overview_time_w = 260;
static constexpr int16_t k_overview_y_start = 78;
static constexpr int16_t k_overview_row_pitch = 58;
static constexpr uint8_t k_qmi8658_addr_primary = 0x6B;
static constexpr uint8_t k_qmi8658_addr_secondary = 0x6A;
static constexpr uint8_t k_qmi8658_reg_who_am_i = 0x00;
static constexpr uint8_t k_qmi8658_reg_ctrl1 = 0x02;
static constexpr uint8_t k_qmi8658_reg_ctrl2 = 0x03;
static constexpr uint8_t k_qmi8658_reg_ctrl7 = 0x08;
static constexpr uint8_t k_qmi8658_reg_accel_x_l = 0x35;

static bool NotificationIdContains(const char* id, const char* token) {
    return id != nullptr && token != nullptr && std::strstr(id, token) != nullptr;
}

static xiaoxin_notification_event_type_t NotificationTypeForEvent(const char* event_type) {
    if (NotificationIdContains(event_type, "course_reminder") || NotificationIdContains(event_type, "course")) {
        return XIAOXIN_NOTIFICATION_EVENT_REMINDER;
    }
    if (NotificationIdContains(event_type, "todo_reminder") || NotificationIdContains(event_type, "todo")) {
        return XIAOXIN_NOTIFICATION_EVENT_TODO_REMINDER;
    }
    if (NotificationIdContains(event_type, "network") || NotificationIdContains(event_type, "wifi")) {
        return XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED;
    }
    if (NotificationIdContains(event_type, "battery")) {
        return XIAOXIN_NOTIFICATION_EVENT_LOW_BATTERY;
    }
    if (NotificationIdContains(event_type, "system") || NotificationIdContains(event_type, "ota")) {
        return XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE;
    }
    return XIAOXIN_NOTIFICATION_EVENT_REMINDER;
}

class TouchReader {
public:
    virtual ~TouchReader() = default;
    virtual const char* Name() const = 0;
    virtual esp_err_t ReadPoint(uint16_t& x, uint16_t& y, bool& pressed) = 0;
};

class EspLcdTouchReader : public TouchReader {
public:
    explicit EspLcdTouchReader(esp_lcd_touch_handle_t touch) : touch_(touch) {}

    const char* Name() const override {
        return "esp_lcd_touch";
    }

    esp_err_t ReadPoint(uint16_t& x, uint16_t& y, bool& pressed) override {
        pressed = false;
        ESP_RETURN_ON_ERROR(esp_lcd_touch_read_data(touch_), TAG, "touch read failed");

        uint8_t count = 0;
        pressed = esp_lcd_touch_get_coordinates(touch_, &x, &y, nullptr, &count, 1) && count > 0;
        return ESP_OK;
    }

private:
    esp_lcd_touch_handle_t touch_ = nullptr;
};

class Spd2010DirectTouchReader : public TouchReader {
public:
    bool Initialize(i2c_master_bus_handle_t i2c_bus) {
        i2c_device_config_t device_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = ESP_LCD_TOUCH_IO_I2C_SPD2010_ADDRESS,
            .scl_speed_hz = 400 * 1000,
            .scl_wait_us = 0,
            .flags = {
                .disable_ack_check = 0,
            },
        };

        esp_err_t err = i2c_master_bus_add_device(i2c_bus, &device_cfg, &device_);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SPD2010 touch add device failed: %s", esp_err_to_name(err));
            return false;
        }

        uint8_t version[18] = {};
        err = ReadCommand(0x26, 0x00, version, sizeof(version));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SPD2010 touch version read failed: %s", esp_err_to_name(err));
            return false;
        }

        ESP_LOGI(TAG, "SPD2010 direct touch initialized");
        return true;
    }

    const char* Name() const override {
        return "SPD2010-direct";
    }

    esp_err_t ReadPoint(uint16_t& x, uint16_t& y, bool& pressed) override {
        pressed = false;

        uint8_t status[4] = {};
        ESP_RETURN_ON_ERROR(ReadCommand(0x20, 0x00, status, sizeof(status)), TAG, "Read SPD2010 status failed");

        const bool pt_exist = (status[0] & 0x01) != 0;
        const bool gesture = (status[0] & 0x02) != 0;
        const bool aux = (status[0] & 0x08) != 0;
        const bool cpu_run = (status[1] & 0x08) != 0;
        const bool tic_in_cpu = (status[1] & 0x20) != 0;
        const bool tic_in_bios = (status[1] & 0x40) != 0;
        const uint16_t read_len = (uint16_t)((uint16_t)status[3] << 8 | status[2]);

        if (tic_in_bios) {
            ESP_RETURN_ON_ERROR(WriteCommand4(0x02, 0x00, 0x01, 0x00), TAG, "Clear SPD2010 int failed");
            return WriteCommand4(0x04, 0x00, 0x01, 0x00);
        }
        if (tic_in_cpu) {
            ESP_RETURN_ON_ERROR(WriteCommand4(0x50, 0x00, 0x00, 0x00), TAG, "Set SPD2010 point mode failed");
            ESP_RETURN_ON_ERROR(WriteCommand4(0x46, 0x00, 0x00, 0x00), TAG, "Start SPD2010 touch failed");
            return WriteCommand4(0x02, 0x00, 0x01, 0x00);
        }
        if (cpu_run && read_len == 0) {
            return WriteCommand4(0x02, 0x00, 0x01, 0x00);
        }
        if (!(pt_exist || gesture)) {
            if (cpu_run && aux) {
                return WriteCommand4(0x02, 0x00, 0x01, 0x00);
            }
            return ESP_OK;
        }
        if (read_len < 10 || read_len > sizeof(report_)) {
            return ESP_OK;
        }

        ESP_RETURN_ON_ERROR(ReadCommand(0x00, 0x03, report_, read_len), TAG, "Read SPD2010 report failed");
        const uint8_t check_id = report_[4];
        if (check_id <= 0x0A && pt_exist) {
            const uint8_t weight = report_[8];
            if (weight != 0) {
                x = (uint16_t)(((report_[7] & 0xF0) << 4) | report_[5]);
                y = (uint16_t)(((report_[7] & 0x0F) << 8) | report_[6]);
                ApplyTransform(x, y);
                pressed = true;
            }
        }

        return ClearReportStatus();
    }

private:
    i2c_master_dev_handle_t device_ = nullptr;
    uint8_t report_[4 + (10 * 6)] = {};

    esp_err_t Write(const uint8_t* data, size_t length) {
        return i2c_master_transmit(device_, data, length, pdMS_TO_TICKS(100));
    }

    esp_err_t Receive(uint8_t* data, size_t length) {
        return i2c_master_receive(device_, data, length, pdMS_TO_TICKS(100));
    }

    esp_err_t WriteCommand4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
        const uint8_t data[4] = {a, b, c, d};
        esp_err_t err = Write(data, sizeof(data));
        esp_rom_delay_us(200);
        return err;
    }

    esp_err_t ReadCommand(uint8_t a, uint8_t b, uint8_t* data, size_t length) {
        const uint8_t cmd[2] = {a, b};
        ESP_RETURN_ON_ERROR(Write(cmd, sizeof(cmd)), TAG, "SPD2010 command write failed");
        esp_rom_delay_us(200);
        ESP_RETURN_ON_ERROR(Receive(data, length), TAG, "SPD2010 data receive failed");
        esp_rom_delay_us(200);
        return ESP_OK;
    }

    esp_err_t ClearReportStatus() {
        uint8_t hdp_status[8] = {};
        for (uint8_t i = 0; i < 3; i++) {
            ESP_RETURN_ON_ERROR(ReadCommand(0xFC, 0x02, hdp_status, sizeof(hdp_status)), TAG, "Read SPD2010 HDP status failed");
            if (hdp_status[5] == 0x82) {
                return WriteCommand4(0x02, 0x00, 0x01, 0x00);
            }
            if (hdp_status[5] != 0x00) {
                return ESP_OK;
            }

            const uint16_t remain_len = (uint16_t)((uint16_t)hdp_status[3] << 8 | hdp_status[2]);
            if (remain_len == 0 || remain_len > 32) {
                return ESP_OK;
            }
            uint8_t remain[32] = {};
            ESP_RETURN_ON_ERROR(ReadCommand(0x00, 0x03, remain, remain_len), TAG, "Read SPD2010 remain data failed");
        }

        return ESP_OK;
    }

    void ApplyTransform(uint16_t& x, uint16_t& y) {
        if (DISPLAY_SWAP_XY) {
            const uint16_t tmp = x;
            x = y;
            y = tmp;
        }
        if (DISPLAY_MIRROR_X) {
            x = (DISPLAY_WIDTH - 1) - x;
        }
        if (DISPLAY_MIRROR_Y) {
            y = (DISPLAY_HEIGHT - 1) - y;
        }
    }
};

struct PaopaoGifBinary {
    const uint8_t* start;
    const uint8_t* end;
};

struct GlassCard {
    lv_obj_t* container = nullptr;
    lv_obj_t* dot = nullptr;
    lv_obj_t* text_box = nullptr;
    lv_obj_t* icon_bg = nullptr;
    lv_obj_t* icon = nullptr;
    lv_obj_t* time = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* body = nullptr;
    lv_obj_t* tag = nullptr;
    lv_obj_t* arrow = nullptr;
    lv_obj_t* battery_meter = nullptr;
    lv_obj_t* battery_segments[4] = {};
    lv_obj_t* pager = nullptr;
    lv_obj_t* pager_total = nullptr;
    uint8_t visible_index = 0xff;
};

enum class NotificationGestureMode {
    None,
    VerticalScroll,
    DismissCard,
    ClearAllPress,
};

enum class SettingsView {
    List,
    Brightness,
    Wifi,
    About,
};

struct SettingsRow {
    lv_obj_t* container = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* value = nullptr;
    xiaoxin_settings_item_t item = XIAOXIN_SETTINGS_ITEM_ABOUT;
};

static PaopaoGifBinary PaopaoGifAssetForState(paopao_pet_state_t state) {
    switch (state) {
        case PAOPAO_PET_STATE_IDLE:
            return {assets_images_idle_gif_start, assets_images_idle_gif_end};
        case PAOPAO_PET_STATE_WORKING:
            return {assets_images_working_gif_start, assets_images_working_gif_end};
        case PAOPAO_PET_STATE_SPEAKING:
            return {assets_images_speaking_fixed_gif_start, assets_images_speaking_fixed_gif_end};
        case PAOPAO_PET_STATE_THINKING:
            return {assets_images_thinking_gif_start, assets_images_thinking_gif_end};
        case PAOPAO_PET_STATE_WAITING:
            return {assets_images_waiting_gif_start, assets_images_waiting_gif_end};
        case PAOPAO_PET_STATE_DONE:
            return {assets_images_done_gif_start, assets_images_done_gif_end};
        case PAOPAO_PET_STATE_SLEEPING:
            return {assets_images_sleeping_gif_start, assets_images_sleeping_gif_end};
        case PAOPAO_PET_STATE_JUMPING:
            return {assets_images_jumping_gif_start, assets_images_jumping_gif_end};
        case PAOPAO_PET_STATE_FAILING:
            return {assets_images_failed_gif_start, assets_images_failed_gif_end};
        case PAOPAO_PET_STATE_GIDDY:
            return {assets_images_giddy_gif_start, assets_images_giddy_gif_end};
        case PAOPAO_PET_STATE_REVIEW:
            return {assets_images_review_gif_start, assets_images_review_gif_end};
        case PAOPAO_PET_STATE_HAPPY:
            return {assets_images_happy_gif_start, assets_images_happy_gif_end};
        case PAOPAO_PET_STATE_CRYING:
            return {assets_images_crying_gif_start, assets_images_crying_gif_end};
        case PAOPAO_PET_STATE_ANXIETY:
            return {assets_images_anxiety_gif_start, assets_images_anxiety_gif_end};
        case PAOPAO_PET_STATE_TIRED:
            return {assets_images_tired_gif_start, assets_images_tired_gif_end};
        case PAOPAO_PET_STATE_STAMP:
            return {assets_images_stamp_gif_start, assets_images_stamp_gif_end};
        default:
            return {assets_images_idle_gif_start, assets_images_idle_gif_end};
    }
};

static uint16_t PaopaoGifVisualLongestForState(paopao_pet_state_t state) {
    switch (state) {
        case PAOPAO_PET_STATE_IDLE:
            return 162;
        case PAOPAO_PET_STATE_WORKING:
            return 165;
        case PAOPAO_PET_STATE_SPEAKING:
            return 182;
        case PAOPAO_PET_STATE_THINKING:
            return 159;
        case PAOPAO_PET_STATE_WAITING:
            return 151;
        case PAOPAO_PET_STATE_DONE:
            return 150;
        case PAOPAO_PET_STATE_SLEEPING:
            return 163;
        case PAOPAO_PET_STATE_JUMPING:
            return 125;
        case PAOPAO_PET_STATE_FAILING:
            return 112;
        case PAOPAO_PET_STATE_GIDDY:
            return 238;
        case PAOPAO_PET_STATE_REVIEW:
            return 126;
        case PAOPAO_PET_STATE_HAPPY:
            return 201;
        case PAOPAO_PET_STATE_CRYING:
            return 230;
        case PAOPAO_PET_STATE_ANXIETY:
            return 173;
        case PAOPAO_PET_STATE_TIRED:
            return 164;
        case PAOPAO_PET_STATE_STAMP:
            return 166;
        default:
            return k_pet_target_visual_longest;
    }
}

static uint32_t PaopaoImageScaleForVisualLongest(uint16_t visual_longest) {
    if (visual_longest == 0) {
        return k_pet_image_scale_base;
    }

    return (((uint32_t)k_pet_target_visual_longest * k_pet_image_scale_base) + visual_longest / 2) / visual_longest;
}

static uint32_t PaopaoImageScaleForVisualSize(paopao_pet_state_t state) {
    return PaopaoImageScaleForVisualLongest(PaopaoGifVisualLongestForState(state));
}

static const char* BatteryStateLabel(xiaoxin_battery_state_t state) {
    switch (state) {
        case XIAOXIN_BATTERY_STATE_NORMAL:
            return "normal";
        case XIAOXIN_BATTERY_STATE_LOW:
            return "low";
        case XIAOXIN_BATTERY_STATE_CRITICAL:
            return "critical";
        case XIAOXIN_BATTERY_STATE_UNKNOWN:
        default:
            return "unknown";
    }
}

static const char* BatteryPowerSourceLabel(xiaoxin_battery_power_source_t source) {
    switch (source) {
        case XIAOXIN_BATTERY_POWER_BATTERY:
            return "battery";
        case XIAOXIN_BATTERY_POWER_EXTERNAL:
            return "external";
        case XIAOXIN_BATTERY_POWER_UNKNOWN:
        default:
            return "unknown";
    }
}

class PaopaoPetDisplay : public SpiLcdDisplay {
public:
    PaopaoPetDisplay(
        esp_lcd_panel_io_handle_t panel_io,
        esp_lcd_panel_handle_t panel
    ) : SpiLcdDisplay(
            panel_io,
            panel,
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X,
            DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X,
            DISPLAY_MIRROR_Y,
            DISPLAY_SWAP_XY
        ) {
    }

    virtual ~PaopaoPetDisplay() {
        if (boot_splash_timer_ != nullptr) {
            esp_timer_stop(boot_splash_timer_);
            esp_timer_delete(boot_splash_timer_);
            boot_splash_timer_ = nullptr;
        }
        boot_splash_controller_.reset();
    }

    virtual void SetupUI() override {
        LcdDisplay::SetupUI();

        DisplayLockGuard lock(this);

        lv_obj_t* screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

        if (emoji_label_ != nullptr) {
            lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        }
        if (emoji_image_ != nullptr) {
            lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        }

        InitializePetFrameBuffer();
        if (pet_frame_buffer_ != nullptr) {
            pet_image_ = lv_image_create(screen);
            lv_image_set_src(pet_image_, &pet_frame_dsc_);
            lv_obj_align(pet_image_, LV_ALIGN_CENTER, 0, 0);
            lv_obj_move_to_index(pet_image_, 1);
        }
        InitializeCardPagerLayer();
        InitializeLowPowerClockLayerLocked();
        InitializeLowPowerClockRefreshTimer();
        RaiseOverlayObjects();
        lv_obj_invalidate(screen);

        const uint32_t now_ms = NowMs();
        paopao_pet_trigger_init(&trigger_, now_ms);
        paopao_pet_mood_init(&mood_, now_ms);
        paopao_pet_behavior_init(&behavior_, now_ms);
        xiaoxin_card_pager_init(&card_pager_, DISPLAY_HEIGHT);
        current_state_ = trigger_.displayed_state;
        PlayGifState(current_state_);
        ShowBootSplashLocked();
        ScheduleBootSplashRevealAfterUiReadyLocked();

        if (render_task_ == nullptr) {
            xTaskCreatePinnedToCore(
                RenderTask,
                "paopao_pet",
                k_pet_render_task_stack_bytes,
                this,
                3,
                &render_task_,
                1
            );
        }

    }

    virtual void CompleteBootSplash() override {
        DisplayLockGuard lock(this);
        const bool restore_boot_brightness = boot_splash_visible_;
        boot_splash_wait_for_ready_ = false;
        if (boot_splash_timer_ != nullptr) {
            esp_timer_stop(boot_splash_timer_);
        }
        HideBootSplashLocked();

        if (restore_boot_brightness) {
            auto backlight = Board::GetInstance().GetBacklight();
            if (backlight != nullptr) {
                backlight->RestoreBrightness();
            }
        }
    }

    virtual uint32_t ShowBootGreeting() override {
        DisplayLockGuard lock(this);
        const PaopaoGifBinary waving = {assets_images_waving_gif_start, assets_images_waving_gif_end};
        PlayGifBinaryLocked(
            waving,
            PaopaoImageScaleForVisualLongest(138),
            "waving.gif",
            138
        );
        return k_boot_greeting_duration_ms;
    }

    virtual void SetStatus(const char* status) override {
        if (status == nullptr) {
            return;
        }

        LcdDisplay::SetStatus(status);

        const bool status_error =
            StatusEquals(status, Lang::Strings::ERROR) ||
            Contains(status, "Error") || Contains(status, "error");

        if (StatusEquals(status, Lang::Strings::LISTENING) ||
            Contains(status, "Listening") || Contains(status, "listening")) {
            DispatchPetVoiceState(
                PAOPAO_PET_TRIGGER_LISTENING,
                PAOPAO_PET_BEHAVIOR_VOICE_LISTENING
            );
        } else if (StatusEquals(status, Lang::Strings::SPEAKING) ||
                   Contains(status, "Speaking") || Contains(status, "speaking")) {
            DispatchPetVoiceState(
                PAOPAO_PET_TRIGGER_SPEAKING,
                PAOPAO_PET_BEHAVIOR_VOICE_SPEAKING
            );
        } else if (StatusEquals(status, Lang::Strings::THINKING) ||
                   Contains(status, "Thinking") || Contains(status, "thinking")) {
            DispatchPetVoiceState(
                PAOPAO_PET_TRIGGER_THINKING,
                PAOPAO_PET_BEHAVIOR_VOICE_THINKING
            );
        } else if (StatusEquals(status, Lang::Strings::STANDBY) ||
                   Contains(status, "Standby") || Contains(status, "standby")) {
            DispatchPetVoiceState(
                PAOPAO_PET_TRIGGER_IDLE,
                PAOPAO_PET_BEHAVIOR_VOICE_IDLE
            );
        } else if (status_error) {
            DispatchPetMoodEvent(PAOPAO_PET_MOOD_EVENT_VOICE_ERROR);
        } else if (IsBusyStatus(status)) {
            DispatchPetVoiceState(
                PAOPAO_PET_TRIGGER_CONNECTING,
                PAOPAO_PET_BEHAVIOR_VOICE_IDLE
            );
        }
        {
            DisplayLockGuard lock(this);
            if (status_error) {
                AddVoiceFailureNotificationLocked(status);
            }
            RaiseOverlayObjects();
        }
    }

    virtual void SetEmotion(const char* emotion) override {
        const paopao_pet_trigger_event_t event = paopao_pet_trigger_for_emotion(emotion);
        if (event == PAOPAO_PET_TRIGGER_NONE) {
            return;
        }
        DisplayLockGuard lock(this);
        DispatchPetServiceEmotionLocked(event, NowMs());
    }

    virtual void SetChatMessage(const char* role, const char* content) override {
        if (role == nullptr || content == nullptr) {
            return;
        }
        if (content[0] == '\0') {
            LcdDisplay::SetChatMessage(role, content);
            {
                DisplayLockGuard lock(this);
                if (system_bars_hidden_for_card_) {
                    bottom_bar_was_hidden_for_card_ = true;
                }
                RaiseOverlayObjects();
            }
            return;
        }

        if (std::strcmp(role, "user") == 0) {
            DispatchPetVoiceState(
                PAOPAO_PET_TRIGGER_THINKING,
                PAOPAO_PET_BEHAVIOR_VOICE_THINKING,
                PAOPAO_PET_MOOD_EVENT_CHAT_STARTED
            );
        } else if (std::strcmp(role, "assistant") == 0) {
            if (Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking) {
                DispatchPetVoiceState(
                    PAOPAO_PET_TRIGGER_SPEAKING,
                    PAOPAO_PET_BEHAVIOR_VOICE_SPEAKING,
                    PAOPAO_PET_MOOD_EVENT_ASSISTANT_REPLY
                );
            } else {
                DispatchPetMoodEvent(PAOPAO_PET_MOOD_EVENT_ASSISTANT_REPLY);
            }
        }

        LcdDisplay::SetChatMessage(role, content);
        {
            DisplayLockGuard lock(this);
            if (system_bars_hidden_for_card_) {
                bottom_bar_was_hidden_for_card_ = IsHidden(bottom_bar_);
            }
            RaiseOverlayObjects();
        }
    }

    virtual void ClearChatMessages() override {
        LcdDisplay::ClearChatMessages();
        {
            DisplayLockGuard lock(this);
            if (system_bars_hidden_for_card_) {
                bottom_bar_was_hidden_for_card_ = true;
            }
            RaiseOverlayObjects();
        }
    }

    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override {
        // Persist transient display messages as notification cards because the
        // base-class status label is covered by the full-screen pet expression.
        // Ordinary notifications deliberately do not create an interrupting overlay.
        const char* body = (notification != nullptr) ? notification : "";
        uint32_t ttl = (duration_ms > 8000) ? (uint32_t)duration_ms : 8000u;
        DisplayLockGuard lock(this);
        const xiaoxin_notification_event_t event = {
            .type = XIAOXIN_NOTIFICATION_EVENT_REMINDER,
            .title = "小芯",
            .body = body,
            .tag = nullptr,
            .priority = 0,
            .ttl_ms = ttl,
        };
        UpsertNotificationEventLocked(event);
        RaiseOverlayObjects();
    }

    void ShowLowBatteryNotification() {
        DisplayLockGuard lock(this);
        const xiaoxin_notification_event_t event = {
            .type = XIAOXIN_NOTIFICATION_EVENT_LOW_BATTERY,
            .title = "低电量",
            .body = "电量低，请尽快充电",
            .tag = "电量",
            .priority = 2,
            .ttl_ms = 0,
        };
        UpsertNotificationEventLocked(event);
        RaiseOverlayObjects();
    }

    bool UpsertNotification(
        const char* id,
        const char* title,
        const char* body,
        const char* tag,
        uint32_t priority,
        uint32_t ttl_ms,
        const char* event_type = nullptr
    ) override {
        DisplayLockGuard lock(this);
        if (id == nullptr || id[0] == '\0' ||
            std::strlen(id) >= XIAOXIN_CARD_NOTIFICATION_ID_MAX) {
            return false;
        }
        if (strcmp(id, "ota_update") == 0) {
            const xiaoxin_notification_event_t event = {
                .type = XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE,
                .title = title,
                .body = body,
                .tag = tag,
                .priority = priority,
                .ttl_ms = ttl_ms,
            };
            return UpsertNotificationEventLocked(event);
        }
        if (std::strncmp(id, "xiaoxin_event:", std::strlen("xiaoxin_event:")) == 0) {
            const xiaoxin_notification_event_t event = {
                .id = id,
                .type = NotificationTypeForEvent(event_type),
                .title = title,
                .body = body,
                .tag = tag,
                .priority = priority,
                .ttl_ms = ttl_ms,
            };
            return UpsertNotificationEventLocked(event);
        }

        return false;
    }

    bool RemoveNotification(const char* id) override {
        DisplayLockGuard lock(this);
        if (id == nullptr) {
            return false;
        }
        if (strcmp(id, "ota_update") == 0) {
            RemoveNotificationEventLocked(XIAOXIN_NOTIFICATION_EVENT_OTA_UPDATE);
            return true;
        }

        return false;
    }

    virtual void UpdateStatusBar(bool update_all = false) override {
        LcdDisplay::UpdateStatusBar(update_all);
        {
            DisplayLockGuard lock(this);
            HideLegacyLowBatteryPopupLocked();
            SyncNetworkStatusLocked();
            SyncPetMoodNetworkStateLocked();
            RefreshOverviewPageIfVisible();
            RaiseOverlayObjects();
        }
    }

    virtual void SetPowerSaveMode(bool on) override {
        LvglDisplay::SetPowerSaveMode(on);
    }

    void DispatchPetTrigger(paopao_pet_trigger_event_t event) {
        DisplayLockGuard lock(this);
        DispatchPetTriggerLocked(event, NowMs());
    }

    void DispatchPetMoodEvent(
        paopao_pet_mood_event_t event,
        paopao_pet_trigger_event_t service_trigger = PAOPAO_PET_TRIGGER_NONE
    ) {
        DisplayLockGuard lock(this);
        DispatchPetMoodEventLocked(event, service_trigger, NowMs());
    }

    void DispatchPetBehaviorServiceTrigger(paopao_pet_trigger_event_t service_trigger) {
        DisplayLockGuard lock(this);
        const uint32_t now_ms = NowMs();
        SyncPetBehaviorVoiceStateFromBaseStateLocked(now_ms);
        const paopao_pet_behavior_decision_t decision =
            paopao_pet_behavior_handle_service_trigger(&behavior_, service_trigger, now_ms);
        DispatchPetBehaviorDecisionLocked(decision, now_ms);
    }

    void DispatchPetVoiceState(
        paopao_pet_trigger_event_t trigger_event,
        paopao_pet_behavior_voice_state_t voice_state,
        paopao_pet_mood_event_t mood_event = PAOPAO_PET_MOOD_EVENT_NONE
    ) {
        DisplayLockGuard lock(this);
        DispatchPetVoiceStateLocked(trigger_event, voice_state, mood_event, NowMs());
    }

    void DispatchLocalPetTrigger(
        paopao_pet_trigger_event_t trigger_event,
        paopao_pet_mood_event_t mood_event
    ) {
        DisplayLockGuard lock(this);
        DispatchLocalPetTriggerLocked(trigger_event, mood_event, NowMs());
    }

    void PlayLocalReaction(paopao_pet_state_t state, uint32_t duration_ms) {
        DisplayLockGuard lock(this);
        paopao_pet_trigger_play_reaction(&trigger_, state, duration_ms, NowMs());
        ApplyPetStateIfChanged();
    }

    void AttachTouch(TouchReader* touch) {
        DisplayLockGuard lock(this);
        touch_ = touch;
        ESP_LOGI(TAG, "Touch reader attached: %s", touch_ != nullptr ? touch_->Name() : "none");
    }

    bool IsSettingsOpen() {
        DisplayLockGuard lock(this);
        return settings_open_;
    }

    void OpenSettingsOverlay() {
        DisplayLockGuard lock(this);
        if (settings_open_) {
            return;
        }
        EnsureSettingsOverlayLocked();
        settings_view_ = SettingsView::List;
        settings_open_ = true;
        RenderSettingsListLocked();
        lv_obj_remove_flag(settings_layer_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(settings_layer_);
        RaiseOverlayObjects();
    }

    void CloseSettingsOverlay() {
        DisplayLockGuard lock(this);
        CloseSettingsOverlayLocked();
    }

    void ShowLowPowerClockScreen() {
        DisplayLockGuard lock(this);
        low_power_clock_visible_ = true;
        low_power_clock_last_minute_ = 0xff;
        low_power_clock_last_second_ = 0xff;
        low_power_clock_animation_tick_ = 0;
        low_power_wave_random_state_ =
            0xA5A55A5AU ^ low_power_clock_animation_tick_ ^ (uint32_t)esp_timer_get_time();
        RefreshLowPowerClockScreenLocked(true);
        RefreshLowPowerClockAnimationLocked();
        if (low_power_clock_layer_ != nullptr) {
            lv_obj_remove_flag(low_power_clock_layer_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(low_power_clock_layer_);
        }
        StartLowPowerClockRefreshTimer();

        auto backlight = Board::GetInstance().GetBacklight();
        if (backlight != nullptr) {
            backlight->SetBrightness(low_power_clock_snapshot_.brightness_percent, false);
        }
    }

    void HideLowPowerClockScreen() {
        DisplayLockGuard lock(this);
        low_power_clock_visible_ = false;
        low_power_clock_last_minute_ = 0xff;
        low_power_clock_last_second_ = 0xff;
        if (low_power_clock_layer_ != nullptr) {
            lv_obj_add_flag(low_power_clock_layer_, LV_OBJ_FLAG_HIDDEN);
        }
        StopLowPowerClockRefreshTimer();

        auto backlight = Board::GetInstance().GetBacklight();
        if (backlight != nullptr) {
            backlight->RestoreBrightness();
        }
    }

    bool IsTouchMotionSuppressed(uint32_t now_ms) {
        DisplayLockGuard lock(this);
        return touch_pressed_ ||
               (touch_last_active_ms_ != 0 &&
                now_ms - touch_last_active_ms_ <= k_touch_motion_suppress_ms);
    }

    void OpenNotificationPageForDebug() {
        DisplayLockGuard lock(this);
        SetCardPagerPage(XIAOXIN_CARD_PAGE_NOTIFICATIONS);
    }

    void UpdateOverviewBatterySnapshot(const xiaoxin_battery_snapshot_t& snapshot) {
        DisplayLockGuard lock(this);
        overview_battery_snapshot_ = snapshot;
        overview_battery_has_snapshot_ = true;
        RefreshOverviewPageIfVisible();
    }

    void UpdateOverviewData(
        bool weather_configured,
        bool weather_available,
        const char* weather_summary,
        const char* weather_detail,
        bool course_configured,
        bool course_available_today,
        const char* course_title,
        const char* course_detail,
        bool todo_configured,
        uint8_t todo_count,
        const char* todo_detail,
        bool companion_available,
        uint8_t xiaoxin_age,
        const char* growth_summary
    ) override {
        DisplayLockGuard lock(this);
        overview_weather_configured_ = weather_configured;
        overview_weather_available_ = weather_available;
        overview_weather_summary_ = weather_summary != nullptr ? weather_summary : "";
        overview_weather_detail_ = weather_detail != nullptr ? weather_detail : "";
        overview_course_configured_ = course_configured;
        overview_course_available_today_ = course_available_today;
        overview_course_title_ = course_title != nullptr ? course_title : "";
        overview_course_detail_ = course_detail != nullptr ? course_detail : "";
        overview_todo_configured_ = todo_configured;
        overview_todo_count_ = todo_count;
        overview_todo_detail_ = todo_detail != nullptr ? todo_detail : "";
        overview_companion_available_ = companion_available;
        overview_xiaoxin_age_ = xiaoxin_age;
        overview_growth_summary_ = growth_summary != nullptr ? growth_summary : "";
        RefreshOverviewPageIfVisible();
    }

private:
    TaskHandle_t render_task_ = nullptr;
    esp_timer_handle_t low_power_clock_timer_ = nullptr;
    esp_timer_handle_t notification_maintenance_timer_ = nullptr;
    lv_obj_t* pet_image_ = nullptr;
    lv_obj_t* card_layer_ = nullptr;
    lv_obj_t* card_title_label_ = nullptr;
    lv_obj_t* overview_time_label_ = nullptr;
    lv_obj_t* overview_date_label_ = nullptr;
    lv_obj_t* pull_indicator_ = nullptr;
    lv_obj_t* home_indicator_ = nullptr;
    lv_obj_t* notification_clear_button_ = nullptr;
    lv_obj_t* notification_clear_label_ = nullptr;
    lv_obj_t* notification_empty_panel_ = nullptr;
    lv_obj_t* notification_empty_label_ = nullptr;
    lv_obj_t* notification_indicator_dots_[k_notification_indicator_dot_count] = {};
    lv_obj_t* settings_layer_ = nullptr;
    lv_obj_t* settings_panel_ = nullptr;
    lv_obj_t* settings_title_label_ = nullptr;
    lv_obj_t* settings_hint_label_ = nullptr;
    lv_obj_t* settings_back_row_ = nullptr;
    lv_obj_t* settings_back_label_ = nullptr;
    lv_obj_t* settings_brightness_value_label_ = nullptr;
    lv_obj_t* settings_brightness_track_ = nullptr;
    lv_obj_t* settings_brightness_fill_ = nullptr;
    lv_obj_t* settings_brightness_thumb_ = nullptr;
    lv_obj_t* settings_brightness_low_label_ = nullptr;
    lv_obj_t* settings_brightness_high_label_ = nullptr;
    lv_obj_t* settings_brightness_back_button_ = nullptr;
    lv_obj_t* settings_brightness_back_button_label_ = nullptr;
    lv_obj_t* low_power_clock_layer_ = nullptr;
    lv_obj_t* low_power_wave_bar_layer_ = nullptr;
    lv_obj_t* low_power_clock_outer_arc_ = nullptr;
    lv_obj_t* low_power_clock_inner_arc_ = nullptr;
    lv_obj_t* low_power_clock_top_dial_ = nullptr;
    lv_obj_t* low_power_clock_time_glow_label_ = nullptr;
    lv_obj_t* low_power_clock_time_label_ = nullptr;
    lv_obj_t* low_power_clock_date_label_ = nullptr;
    lv_obj_t* low_power_clock_sync_dot_ = nullptr;
    lv_obj_t* low_power_clock_sync_label_ = nullptr;
    lv_obj_t* low_power_clock_hint_label_ = nullptr;
    lv_obj_t* low_power_clock_brand_label_ = nullptr;
    lv_obj_t* low_power_clock_mode_label_ = nullptr;
    lv_obj_t* low_power_clock_title_label_ = nullptr;
    lv_obj_t* low_power_clock_micro_panel_ = nullptr;
    lv_obj_t* low_power_clock_micro_label_ = nullptr;
    lv_obj_t* low_power_clock_second_label_ = nullptr;
    lv_obj_t* low_power_clock_probe_label_ = nullptr;
    lv_obj_t* low_power_clock_left_red_dash_ = nullptr;
    lv_obj_t* low_power_clock_left_gray_panel_ = nullptr;
    lv_obj_t* low_power_clock_center_rule_ = nullptr;
    lv_obj_t* low_power_clock_center_stem_ = nullptr;
    lv_obj_t* low_power_clock_blue_top_block_ = nullptr;
    lv_obj_t* low_power_clock_blue_bottom_block_ = nullptr;
    SettingsRow settings_rows_[k_settings_item_max_count];
    xiaoxin_settings_item_t settings_items_[k_settings_item_max_count] = {};
    xiaoxin_low_power_clock_snapshot_t low_power_clock_snapshot_ = {};
    uint8_t low_power_wave_bar_levels_[k_low_power_wave_bar_count] = {};
    uint8_t low_power_wave_bar_target_levels_[k_low_power_wave_bar_count] = {};
    uint32_t low_power_wave_random_state_ = 0xA5A55A5AU;
    uint8_t low_power_clock_last_minute_ = 0xff;
    uint8_t low_power_clock_last_second_ = 0xff;
    uint32_t low_power_clock_animation_tick_ = 0;
    uint16_t low_power_clock_motion_angle_ = 0;
    uint8_t settings_item_count_ = 0;
    SettingsView settings_view_ = SettingsView::List;
    bool low_power_clock_visible_ = false;
    bool settings_open_ = false;
    bool settings_wifi_config_requested_ = false;
    uint8_t settings_brightness_value_ = 75;
    bool settings_brightness_dragging_ = false;
    bool settings_touch_action_consumed_ = false;
    uint8_t settings_brightness_applied_value_ = 0xff;
    bool settings_brightness_applied_permanent_ = false;
    bool power_save_timer_wake_requested_ = false;
    GlassCard glass_cards_[k_card_glass_count];
    lv_obj_t* overview_row_labels_[k_overview_visible_row_count] = {};
    xiaoxin_card_page_t rendered_card_page_ = XIAOXIN_CARD_PAGE_HOME;
    bool rendered_card_prepare_entry_ = false;
    bool card_page_rendered_ = false;
    lv_img_dsc_t pet_frame_dsc_ = {};
    uint16_t* pet_frame_buffer_ = nullptr;
    lv_img_dsc_t gif_source_dsc_ = {};
    std::unique_ptr<LvglGif> pet_gif_controller_ = nullptr;
    lv_obj_t* boot_splash_layer_ = nullptr;
    lv_obj_t* boot_splash_image_ = nullptr;
    lv_img_dsc_t boot_splash_source_dsc_ = {};
    std::unique_ptr<LvglGif> boot_splash_controller_ = nullptr;
    esp_timer_handle_t boot_splash_timer_ = nullptr;
    bool boot_splash_visible_ = false;
    bool boot_splash_wait_for_ready_ = false;
    TouchReader* touch_ = nullptr;
    xiaoxin_card_pager_t card_pager_ = {};
    xiaoxin_overview_snapshot_t overview_snapshot_ = {};
    xiaoxin_battery_snapshot_t overview_battery_snapshot_ = {};
    bool overview_battery_has_snapshot_ = false;
    bool overview_weather_configured_ = false;
    bool overview_weather_available_ = false;
    std::string overview_weather_summary_;
    std::string overview_weather_detail_;
    bool overview_course_configured_ = false;
    bool overview_course_available_today_ = false;
    std::string overview_course_title_;
    std::string overview_course_detail_;
    bool overview_todo_configured_ = false;
    uint8_t overview_todo_count_ = 0;
    std::string overview_todo_detail_;
    bool overview_companion_available_ = false;
    uint8_t overview_xiaoxin_age_ = 0;
    std::string overview_growth_summary_;
    paopao_pet_trigger_context_t trigger_;
    paopao_pet_mood_context_t mood_ = {};
    paopao_pet_behavior_context_t behavior_ = {};
    paopao_pet_state_t current_state_ = PAOPAO_PET_STATE_IDLE;
    bool touch_pressed_ = false;
    uint16_t touch_start_x_ = 0;
    uint16_t touch_start_y_ = 0;
    uint16_t touch_last_x_ = 0;
    uint16_t touch_last_y_ = 0;
    uint32_t touch_start_ms_ = 0;
    uint32_t touch_last_active_ms_ = 0;
    uint32_t touch_last_error_log_ms_ = 0;
    uint32_t touch_last_point_log_ms_ = 0;
    uint32_t ui_perf_last_log_ms_ = 0;
    uint32_t ui_perf_drag_calls_ = 0;
    uint32_t ui_perf_drag_total_us_ = 0;
    uint32_t ui_perf_drag_max_us_ = 0;
    uint32_t ui_perf_pet_copy_calls_ = 0;
    uint32_t ui_perf_pet_copy_total_us_ = 0;
    uint32_t ui_perf_pet_copy_max_us_ = 0;
    uint32_t ui_perf_touch_loop_calls_ = 0;
    uint32_t ui_perf_touch_loop_total_us_ = 0;
    uint32_t ui_perf_touch_loop_max_us_ = 0;
    bool pet_animation_paused_for_card_ = false;
    bool system_bars_hidden_for_card_ = false;
    bool top_bar_was_hidden_for_card_ = false;
    bool status_bar_was_hidden_for_card_ = false;
    bool bottom_bar_was_hidden_for_card_ = false;
    int16_t notification_scroll_y_ = 0;
    int16_t notification_drag_start_scroll_y_ = 0;
    bool notification_drag_visual_owns_cards_ = false;
    NotificationGestureMode notification_gesture_mode_ = NotificationGestureMode::None;
    int8_t notification_pressed_slot_ = -1;
    uint8_t notification_pressed_visible_index_ = 0xff;
    int16_t notification_card_drag_x_ = 0;
    bool notification_dismiss_animating_ = false;
    uint8_t notification_animating_visible_index_ = 0xff;
    bool network_notification_active_ = false;
    xiaoxin_system_overlay_network_state_t last_network_notification_state_ =
        XIAOXIN_SYSTEM_OVERLAY_NETWORK_CONNECTED;

    static uint32_t NowMs() {
        return (uint32_t)(esp_timer_get_time() / 1000ULL);
    }

    static bool Contains(const char* text, const char* needle) {
        return text != nullptr && needle != nullptr && std::strstr(text, needle) != nullptr;
    }

    static bool StatusEquals(const char* status, const char* expected) {
        return status != nullptr && expected != nullptr && std::strcmp(status, expected) == 0;
    }

    static constexpr lv_opa_t LowPowerClockOpaPercent(uint8_t percent) {
        return (lv_opa_t)((percent * 255U + 50U) / 100U);
    }

    static constexpr int16_t k_low_power_ref_lcd_w = 466;
    static constexpr int16_t k_low_power_ref_lcd_h = 466;
    static constexpr int16_t k_low_power_ref_sprite_x = 40;
    static constexpr int16_t k_low_power_ref_sprite_y = 120;
    static constexpr int16_t k_low_power_time_y_adjust = 20;
    static constexpr double k_low_power_ref_rad = 0.01745;

    static constexpr int16_t LowPowerRefX(int16_t sprite_x) {
        return (int16_t)(((k_low_power_ref_sprite_x + sprite_x) * DISPLAY_WIDTH + k_low_power_ref_lcd_w / 2) / k_low_power_ref_lcd_w);
    }

    static constexpr int16_t LowPowerRefY(int16_t sprite_y) {
        return (int16_t)(((k_low_power_ref_sprite_y + sprite_y) * DISPLAY_HEIGHT + k_low_power_ref_lcd_h / 2) / k_low_power_ref_lcd_h);
    }

    static constexpr int16_t LowPowerRefLen(int16_t value) {
        return (int16_t)((value * DISPLAY_WIDTH + k_low_power_ref_lcd_w / 2) / k_low_power_ref_lcd_w);
    }

    static void ConfigureLowPowerTimeLabel(
        lv_obj_t* label,
        const lv_font_t* font,
        uint32_t color_hex,
        lv_opa_t opa,
        int16_t letter_space,
        int16_t scale,
        int16_t transform_width,
        int16_t transform_height
    ) {
        if (label == nullptr) {
            return;
        }

        lv_obj_set_style_text_color(label, lv_color_hex(color_hex), 0);
        lv_obj_set_style_text_opa(label, opa, 0);
        if (font != nullptr) {
            lv_obj_set_style_text_font(label, font, 0);
        }
        lv_obj_set_style_text_letter_space(label, letter_space, 0);
        lv_obj_set_style_transform_scale(label, scale, 0);
        lv_obj_set_style_transform_width(label, transform_width, 0);
        lv_obj_set_style_transform_height(label, transform_height, 0);
    }

    static void RefreshLowPowerTimeLabelLocked(
        lv_obj_t* label,
        const char* text,
        lv_align_t align,
        int16_t x_offset,
        int16_t y_offset
    ) {
        if (label == nullptr) {
            return;
        }

        lv_label_set_text(label, text);
        lv_obj_update_layout(label);
        lv_obj_set_style_transform_pivot_x(label, lv_obj_get_width(label) / 2, 0);
        lv_obj_set_style_transform_pivot_y(label, lv_obj_get_height(label) / 2, 0);
        lv_obj_align(label, align, x_offset, y_offset);
    }

    static lv_obj_t* CreateLowPowerBlock(
        lv_obj_t* parent,
        int16_t w,
        int16_t h,
        uint32_t color_hex,
        lv_opa_t opa
    ) {
        lv_obj_t* obj = lv_obj_create(parent);
        if (obj == nullptr) {
            return nullptr;
        }
        lv_obj_remove_style_all(obj);
        lv_obj_set_size(obj, w, h);
        lv_obj_set_style_bg_color(obj, lv_color_hex(color_hex), 0);
        lv_obj_set_style_bg_opa(obj, opa, 0);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        return obj;
    }

    uint32_t NextLowPowerWaveRandomLocked() {
        uint32_t state = low_power_wave_random_state_;
        if (state == 0) {
            state = 0xA5A55A5AU ^ low_power_clock_animation_tick_;
        }
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        low_power_wave_random_state_ = state;
        return state;
    }

    void UpdateLowPowerWaveBarsLocked() {
        if (low_power_wave_bar_layer_ == nullptr) {
            return;
        }
        for (uint8_t i = 0; i < k_low_power_wave_bar_count; ++i) {
            const uint8_t target = (uint8_t)(
                k_low_power_wave_bar_min_level +
                (NextLowPowerWaveRandomLocked() % k_low_power_wave_bar_max_level)
            );
            low_power_wave_bar_target_levels_[i] = target;

            uint8_t level = low_power_wave_bar_levels_[i];
            if (level < target) {
                level = (uint8_t)(level + std::min<uint8_t>(k_low_power_wave_bar_step, (uint8_t)(target - level)));
            } else if (level > target) {
                level = (uint8_t)(level - std::min<uint8_t>(k_low_power_wave_bar_step, (uint8_t)(level - target)));
            }
            low_power_wave_bar_levels_[i] = level;
        }
    }

    void DrawLowPowerLeftGaugeLocked(lv_layer_t* layer, uint16_t angle) {
        if (layer == nullptr) {
            return;
        }

        lv_draw_rect_dsc_t pixel_dsc;
        lv_draw_rect_dsc_init(&pixel_dsc);
        pixel_dsc.bg_color = lv_color_hex(0x707070);
        pixel_dsc.bg_opa = LowPowerClockOpaPercent(78);

        lv_draw_line_dsc_t tick_dsc;
        lv_draw_line_dsc_init(&tick_dsc);
        tick_dsc.opa = LowPowerClockOpaPercent(88);
        tick_dsc.round_start = 1;
        tick_dsc.round_end = 1;

        for (uint8_t i = 0; i < k_low_power_left_gauge_point_count; ++i) {
            const uint16_t a = (uint16_t)((angle + i * 3U) % 360U);
            const int16_t x_ref = (int16_t)std::lround(118.0 * std::cos(k_low_power_ref_rad * a)) - 2;
            const int16_t y_ref = (int16_t)std::lround(118.0 * std::sin(k_low_power_ref_rad * a)) + 120;

            lv_area_t pixel_area;
            pixel_area.x1 = LowPowerRefX(x_ref);
            pixel_area.y1 = LowPowerRefY(y_ref);
            pixel_area.x2 = pixel_area.x1;
            pixel_area.y2 = pixel_area.y1;
            lv_draw_rect(layer, &pixel_dsc, &pixel_area);

            if ((i % 3U) != 0U) {
                continue;
            }

            int16_t len_ref = 6;
            int16_t width = LowPowerRefLen(2);
            tick_dsc.color = lv_color_hex(0x787878);
            if ((i % 12U) == 0U) {
                len_ref = 30;
                width = LowPowerRefLen(4);
                tick_dsc.color = lv_color_hex(0xA0A0A0);
            } else if ((i % 6U) == 0U) {
                len_ref = 18;
                width = LowPowerRefLen(3);
                tick_dsc.color = lv_color_hex(0x8C8C8C);
            }

            tick_dsc.width = width;
            lv_point_precise_set(&tick_dsc.p1, LowPowerRefX(x_ref), LowPowerRefY(y_ref));
            lv_point_precise_set(&tick_dsc.p2, LowPowerRefX(x_ref - len_ref), LowPowerRefY(y_ref));
            lv_draw_line(layer, &tick_dsc);
        }
    }

    void DrawLowPowerWaveBarsLocked(lv_layer_t* layer) {
        if (layer == nullptr) {
            return;
        }

        lv_draw_rect_dsc_t bar_dsc;
        lv_draw_rect_dsc_init(&bar_dsc);
        bar_dsc.bg_color = lv_color_hex(0xA9C7E8);
        bar_dsc.bg_opa = LV_OPA_COVER;
        bar_dsc.radius = 1;

        for (uint8_t i = 0; i < k_low_power_wave_bar_count; ++i) {
            const uint8_t level = low_power_wave_bar_levels_[i];
            for (uint8_t j = 0; j < level; ++j) {
                const int16_t x = LowPowerRefX((int16_t)(190 + i * 6U));
                const int16_t y = LowPowerRefY((int16_t)(90 - j * 4U));
                lv_area_t bar_area;
                bar_area.x1 = x;
                bar_area.y1 = y;
                bar_area.x2 = (int16_t)(x + LowPowerRefLen(4) - 1);
                bar_area.y2 = (int16_t)(y + LowPowerRefLen(3) - 1);
                lv_draw_rect(layer, &bar_dsc, &bar_area);
            }
        }
    }

    void DrawLowPowerWaveMotionLocked(lv_event_t* e) {
        if (e == nullptr) {
            return;
        }
        lv_layer_t* layer = lv_event_get_layer(e);
        DrawLowPowerLeftGaugeLocked(layer, low_power_clock_motion_angle_);
        DrawLowPowerWaveBarsLocked(layer);
    }

    static void LowPowerWaveMotionDrawEvent(lv_event_t* e) {
        if (e == nullptr || lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) {
            return;
        }
        auto* self = static_cast<PaopaoPetDisplay*>(lv_event_get_user_data(e));
        if (self != nullptr) {
            self->DrawLowPowerWaveMotionLocked(e);
        }
    }

    void InitializeLowPowerWaveBarsLocked() {
        if (low_power_clock_layer_ == nullptr || low_power_wave_bar_layer_ != nullptr) {
            return;
        }

        low_power_wave_bar_layer_ = lv_obj_create(low_power_clock_layer_);
        if (low_power_wave_bar_layer_ == nullptr) {
            return;
        }
        lv_obj_remove_style_all(low_power_wave_bar_layer_);
        lv_obj_set_size(low_power_wave_bar_layer_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lv_obj_align(low_power_wave_bar_layer_, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_clear_flag(low_power_wave_bar_layer_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(low_power_wave_bar_layer_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(low_power_wave_bar_layer_, LowPowerWaveMotionDrawEvent, LV_EVENT_DRAW_MAIN, this);

        for (uint8_t i = 0; i < k_low_power_wave_bar_count; ++i) {
            low_power_wave_bar_levels_[i] = (uint8_t)(1U + (i * 5U) % k_low_power_wave_bar_max_level);
            low_power_wave_bar_target_levels_[i] = low_power_wave_bar_levels_[i];
        }
        UpdateLowPowerWaveBarsLocked();
    }

    void InitializeLowPowerWaveReferenceLabelsLocked(const lv_font_t* hint_font) {
        static constexpr const char* k_runtime_letters[] = {"X", "i", "a", "o", "X", "i", "n"};
        for (uint8_t i = 0; i < 7; ++i) {
            lv_obj_t* cell = CreateLowPowerBlock(low_power_clock_layer_, LowPowerRefLen(26), LowPowerRefLen(26), 0x30363D, LowPowerClockOpaPercent(82));
            if (cell == nullptr) {
                continue;
            }
            lv_obj_set_style_radius(cell, LowPowerRefLen(3), 0);
            lv_obj_align(cell, LV_ALIGN_TOP_LEFT, LowPowerRefX(186 + i * 30), LowPowerRefY(2));

            lv_obj_t* letter = lv_label_create(cell);
            lv_obj_set_style_text_color(letter, lv_color_hex(0xC9D1D9), 0);
            lv_obj_set_style_text_opa(letter, LV_OPA_COVER, 0);
            if (hint_font != nullptr) {
                lv_obj_set_style_text_font(letter, hint_font, 0);
            }
            lv_label_set_text(letter, k_runtime_letters[i]);
            lv_obj_center(letter);
            if (i == 0) {
                low_power_clock_mode_label_ = letter;
            }
        }

        low_power_clock_title_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_title_label_, lv_color_hex(0xB8C7D1), 0);
        lv_obj_set_style_text_opa(low_power_clock_title_label_, LV_OPA_90, 0);
        if (hint_font != nullptr) {
            lv_obj_set_style_text_font(low_power_clock_title_label_, hint_font, 0);
        }
        lv_label_set_text(low_power_clock_title_label_, "HZCU TIME");
        lv_obj_align(low_power_clock_title_label_, LV_ALIGN_TOP_LEFT, LowPowerRefX(190), LowPowerRefY(104));

        low_power_clock_probe_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_probe_label_, lv_color_hex(0x66707A), 0);
        lv_obj_set_style_text_opa(low_power_clock_probe_label_, LowPowerClockOpaPercent(32), 0);
        if (hint_font != nullptr) {
            lv_obj_set_style_text_font(low_power_clock_probe_label_, hint_font, 0);
        }
        lv_label_set_text(low_power_clock_probe_label_, "CAN YOU READ THIS");
        lv_obj_align(low_power_clock_probe_label_, LV_ALIGN_TOP_LEFT, LowPowerRefX(346), LowPowerRefY(128));
    }

    void UpdateLowPowerSecondGaugeLocked(const char* second_text, int second) {
        (void)second;
        if (low_power_clock_second_label_ != nullptr) {
            lv_label_set_text(low_power_clock_second_label_, second_text != nullptr ? second_text : "--");
        }
    }

    void InitializeLowPowerSecondGaugeLocked(const lv_font_t* hint_font) {
        low_power_clock_second_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_second_label_, lv_color_hex(0xF6FAFF), 0);
        lv_obj_set_style_text_opa(low_power_clock_second_label_, LV_OPA_COVER, 0);
        if (hint_font != nullptr) {
            lv_obj_set_style_text_font(low_power_clock_second_label_, hint_font, 0);
        }
        lv_label_set_text(low_power_clock_second_label_, "00");
        lv_obj_align(low_power_clock_second_label_, LV_ALIGN_TOP_LEFT, LowPowerRefX(24), LowPowerRefY(124));
    }

    void InitializeLowPowerWaveReferenceBlocksLocked(const lv_font_t* hint_font) {
        low_power_clock_left_red_dash_ = CreateLowPowerBlock(low_power_clock_layer_, LowPowerRefLen(24), LowPowerRefLen(4), 0xD04141, LV_OPA_COVER);
        if (low_power_clock_left_red_dash_ != nullptr) {
            lv_obj_align(low_power_clock_left_red_dash_, LV_ALIGN_TOP_LEFT, LowPowerRefX(54), LowPowerRefY(120));
        }

        low_power_clock_left_gray_panel_ = CreateLowPowerBlock(
            low_power_clock_layer_,
            LowPowerRefLen(72),
            LowPowerRefLen(30),
            0x30363D,
            LowPowerClockOpaPercent(82)
        );
        if (low_power_clock_left_gray_panel_ != nullptr) {
            lv_obj_align(low_power_clock_left_gray_panel_, LV_ALIGN_TOP_LEFT, LowPowerRefX(0), LowPowerRefY(145));
            low_power_clock_micro_panel_ = low_power_clock_left_gray_panel_;
            low_power_clock_micro_label_ = lv_label_create(low_power_clock_left_gray_panel_);
            if (low_power_clock_micro_label_ != nullptr) {
                lv_obj_set_style_text_color(low_power_clock_micro_label_, lv_color_hex(0xC9D1D9), 0);
                lv_obj_set_style_text_opa(low_power_clock_micro_label_, LV_OPA_COVER, 0);
                if (hint_font != nullptr) {
                    lv_obj_set_style_text_font(low_power_clock_micro_label_, hint_font, 0);
                }
                lv_label_set_text(low_power_clock_micro_label_, "second");
                lv_obj_center(low_power_clock_micro_label_);
            }
        }

        low_power_clock_center_rule_ = CreateLowPowerBlock(
            low_power_clock_layer_,
            LowPowerRefLen(120),
            LowPowerRefLen(3),
            0x57606A,
            LowPowerClockOpaPercent(62)
        );
        if (low_power_clock_center_rule_ != nullptr) {
            lv_obj_align(low_power_clock_center_rule_, LV_ALIGN_TOP_LEFT, LowPowerRefX(180), LowPowerRefY(136));
        }

        low_power_clock_center_stem_ = CreateLowPowerBlock(
            low_power_clock_layer_,
            LowPowerRefLen(3),
            LowPowerRefLen(34),
            0x57606A,
            LowPowerClockOpaPercent(62)
        );
        if (low_power_clock_center_stem_ != nullptr) {
            lv_obj_align(low_power_clock_center_stem_, LV_ALIGN_TOP_LEFT, LowPowerRefX(186), LowPowerRefY(130));
        }

        low_power_clock_blue_top_block_ = CreateLowPowerBlock(
            low_power_clock_layer_,
            LowPowerRefLen(40),
            LowPowerRefLen(135),
            0x01052A,
            LowPowerClockOpaPercent(88)
        );
        if (low_power_clock_blue_top_block_ != nullptr) {
            lv_obj_align(low_power_clock_blue_top_block_, LV_ALIGN_TOP_LEFT, LowPowerRefX(136), LowPowerRefY(0));
        }

        low_power_clock_blue_bottom_block_ = CreateLowPowerBlock(
            low_power_clock_layer_,
            LowPowerRefLen(40),
            LowPowerRefLen(16),
            0x01052A,
            LowPowerClockOpaPercent(88)
        );
        if (low_power_clock_blue_bottom_block_ != nullptr) {
            lv_obj_align(low_power_clock_blue_bottom_block_, LV_ALIGN_TOP_LEFT, LowPowerRefX(136), LowPowerRefY(224));
        }

        low_power_clock_top_dial_ = lv_arc_create(low_power_clock_layer_);
        if (low_power_clock_top_dial_ == nullptr) {
            return;
        }
        lv_obj_set_size(low_power_clock_top_dial_, LowPowerRefLen(56), LowPowerRefLen(56));
        lv_obj_align(low_power_clock_top_dial_, LV_ALIGN_TOP_LEFT, LowPowerRefX(372 - 28), LowPowerRefY(76 - 28));
        lv_arc_set_bg_angles(low_power_clock_top_dial_, 0, 360);
        lv_arc_set_angles(low_power_clock_top_dial_, 0, 96);
        lv_obj_remove_style(low_power_clock_top_dial_, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(low_power_clock_top_dial_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(low_power_clock_top_dial_, 8, LV_PART_MAIN);
        lv_obj_set_style_arc_width(low_power_clock_top_dial_, 8, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(low_power_clock_top_dial_, lv_color_hex(0x01052A), LV_PART_MAIN);
        lv_obj_set_style_arc_color(low_power_clock_top_dial_, lv_color_hex(0x26D9FF), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(low_power_clock_top_dial_, LowPowerClockOpaPercent(72), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(low_power_clock_top_dial_, LV_OPA_COVER, LV_PART_INDICATOR);
    }

    xiaoxin_low_power_clock_state_t BuildLowPowerClockState() {
        xiaoxin_low_power_clock_state_t state = {};
        time_t now = 0;
        time(&now);

        struct tm timeinfo = {};
        if (now > 24 * 60 * 60 &&
            localtime_r(&now, &timeinfo) != nullptr &&
            timeinfo.tm_year >= 120) {
            state.time_valid = true;
            state.hour = timeinfo.tm_hour;
            state.minute = timeinfo.tm_min;
            state.second = timeinfo.tm_sec;
            state.month = timeinfo.tm_mon + 1;
            state.day = timeinfo.tm_mday;
            state.weekday = timeinfo.tm_wday;
        }

        const TimeSyncStatus sync_status = GetTimeSyncStatus();
        if (sync_status == TimeSyncStatus::Synced) {
            state.sync_state = XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCED;
        } else if (sync_status == TimeSyncStatus::Syncing) {
            state.sync_state = XIAOXIN_LOW_POWER_CLOCK_SYNC_SYNCING;
        } else {
            state.sync_state = XIAOXIN_LOW_POWER_CLOCK_SYNC_IDLE;
        }
        return state;
    }

    void RefreshLowPowerClockScreenLocked(bool force) {
        if (low_power_clock_layer_ == nullptr) {
            return;
        }

        const xiaoxin_low_power_clock_state_t state = BuildLowPowerClockState();
        const uint8_t current_minute = state.time_valid ? (uint8_t)state.minute : 0xff;
        const uint8_t current_second = state.time_valid ? (uint8_t)state.second : 0xff;
        if (!force &&
            !xiaoxin_low_power_clock_should_refresh(low_power_clock_last_minute_, current_minute, low_power_clock_last_second_, current_second)) {
            return;
        }

        low_power_clock_last_minute_ = current_minute;
        low_power_clock_last_second_ = current_second;
        xiaoxin_low_power_clock_model_build(&state, &low_power_clock_snapshot_);

        RefreshLowPowerTimeLabelLocked(
            low_power_clock_time_glow_label_,
            low_power_clock_snapshot_.time_text,
            LV_ALIGN_TOP_LEFT,
            LowPowerRefX(196),
            LowPowerRefY(150) + k_low_power_time_y_adjust
        );
        RefreshLowPowerTimeLabelLocked(
            low_power_clock_time_label_,
            low_power_clock_snapshot_.time_text,
            LV_ALIGN_TOP_LEFT,
            LowPowerRefX(196),
            LowPowerRefY(150) + k_low_power_time_y_adjust
        );
        lv_label_set_text(low_power_clock_date_label_, low_power_clock_snapshot_.date_text);
        lv_label_set_text(low_power_clock_sync_label_, low_power_clock_snapshot_.sync_text);
        lv_obj_set_style_bg_color(low_power_clock_sync_dot_, lv_color_hex(low_power_clock_snapshot_.sync_color_hex), 0);
        lv_label_set_text(low_power_clock_hint_label_, low_power_clock_snapshot_.hint_text);
        UpdateLowPowerSecondGaugeLocked(low_power_clock_snapshot_.second_text, state.second);
    }

    void RefreshLowPowerClockAnimationLocked() {
        if (low_power_clock_inner_arc_ == nullptr || low_power_clock_outer_arc_ == nullptr) {
            return;
        }

        const uint16_t start = xiaoxin_low_power_clock_animation_phase(low_power_clock_animation_tick_++);
        low_power_clock_motion_angle_ = start;
        UpdateLowPowerWaveBarsLocked();
        if (low_power_wave_bar_layer_ != nullptr) {
            lv_obj_invalidate(low_power_wave_bar_layer_);
        }
        lv_arc_set_rotation(low_power_clock_inner_arc_, start);
        if (low_power_clock_top_dial_ != nullptr) {
            lv_arc_set_rotation(low_power_clock_top_dial_, (start + 42) % 360);
        }

        if (low_power_clock_sync_dot_ != nullptr) {
            const lv_opa_t dot_opa = (low_power_clock_animation_tick_ % 2U) == 0U ? LV_OPA_COVER : LV_OPA_60;
            lv_obj_set_style_opa(low_power_clock_sync_dot_, dot_opa, 0);
        }
    }

    void RefreshLowPowerClockScreenFromTimer() {
        DisplayLockGuard lock(this);
        if (!low_power_clock_visible_) {
            return;
        }

        RefreshLowPowerClockAnimationLocked();
        RefreshLowPowerClockScreenLocked(false);
    }

    void InitializeLowPowerClockRefreshTimer() {
        if (low_power_clock_timer_ != nullptr) {
            return;
        }

        const esp_timer_create_args_t low_power_clock_timer_args = {
            .callback = [](void* arg) {
                static_cast<PaopaoPetDisplay*>(arg)->RefreshLowPowerClockScreenFromTimer();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "low_power_clock",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&low_power_clock_timer_args, &low_power_clock_timer_));
    }

    void StartLowPowerClockRefreshTimer() {
        if (low_power_clock_timer_ != nullptr && !esp_timer_is_active(low_power_clock_timer_)) {
            ESP_ERROR_CHECK(esp_timer_start_periodic(low_power_clock_timer_, k_low_power_clock_timer_period_us));
        }
    }

    void StopLowPowerClockRefreshTimer() {
        if (low_power_clock_timer_ != nullptr && esp_timer_is_active(low_power_clock_timer_)) {
            ESP_ERROR_CHECK(esp_timer_stop(low_power_clock_timer_));
        }
    }

    void RefreshNotificationPageIfVisibleLocked() {
        if (xiaoxin_card_pager_current_page(&card_pager_) != XIAOXIN_CARD_PAGE_NOTIFICATIONS) {
            return;
        }
        RenderNotificationPageAfterDataChange();
    }

    static void NotificationMaintenanceTimerCallback(void* arg) {
        auto* self = static_cast<PaopaoPetDisplay*>(arg);
        if (self != nullptr) {
            self->RefreshNotificationsFromTimer();
        }
    }

    void RefreshNotificationsFromTimer() {
        DisplayLockGuard lock(this);
        const uint8_t removed = xiaoxin_card_pager_notification_expire(&card_pager_, NowMs());
        if (removed > 0) {
            notification_scroll_y_ = ClampNotificationScrollY(
                notification_scroll_y_,
                xiaoxin_card_pager_notification_count(&card_pager_)
            );
            RefreshNotificationPageIfVisibleLocked();
        }
        SyncNotificationMaintenanceTimerLocked();
    }

    void EnsureNotificationMaintenanceTimer() {
        if (notification_maintenance_timer_ != nullptr) {
            return;
        }

        const esp_timer_create_args_t notification_maintenance_timer_args = {
            .callback = &NotificationMaintenanceTimerCallback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "xiaoxin_notify",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&notification_maintenance_timer_args, &notification_maintenance_timer_));
    }

    void StartNotificationMaintenanceTimer() {
        EnsureNotificationMaintenanceTimer();
        if (notification_maintenance_timer_ != nullptr &&
            !esp_timer_is_active(notification_maintenance_timer_)) {
            ESP_ERROR_CHECK(esp_timer_start_periodic(notification_maintenance_timer_, 250 * 1000));
        }
    }

    void StopNotificationMaintenanceTimer() {
        if (notification_maintenance_timer_ != nullptr &&
            esp_timer_is_active(notification_maintenance_timer_)) {
            ESP_ERROR_CHECK(esp_timer_stop(notification_maintenance_timer_));
        }
    }

    void SyncNotificationMaintenanceTimerLocked() {
        if (xiaoxin_card_pager_notification_has_pending_expiry(&card_pager_)) {
            StartNotificationMaintenanceTimer();
        } else {
            StopNotificationMaintenanceTimer();
        }
    }

    bool UpsertNotificationEventLocked(const xiaoxin_notification_event_t& event) {
        const bool upserted =
            xiaoxin_card_pager_notification_upsert_event_at(
                &card_pager_, &event, NowMs());
        if (upserted) {
            notification_scroll_y_ = ClampNotificationScrollY(
                notification_scroll_y_,
                xiaoxin_card_pager_notification_count(&card_pager_)
            );
            SyncNotificationMaintenanceTimerLocked();
            RefreshNotificationPageIfVisibleLocked();
        }
        return upserted;
    }

    void RemoveNotificationEventLocked(xiaoxin_notification_event_type_t type) {
        if (xiaoxin_card_pager_notification_remove_event(&card_pager_, type)) {
            notification_scroll_y_ = ClampNotificationScrollY(
                notification_scroll_y_,
                xiaoxin_card_pager_notification_count(&card_pager_)
            );
            SyncNotificationMaintenanceTimerLocked();
            RefreshNotificationPageIfVisibleLocked();
        }
    }

    void SyncNetworkNotificationLocked(xiaoxin_system_overlay_network_state_t state) {
        const bool disconnected = state != XIAOXIN_SYSTEM_OVERLAY_NETWORK_CONNECTED;
        if (!disconnected) {
            if (network_notification_active_) {
                RemoveNotificationEventLocked(XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED);
            }
            network_notification_active_ = false;
            last_network_notification_state_ = state;
            return;
        }

        if (network_notification_active_ &&
            last_network_notification_state_ == state) {
            return;
        }

        const char* body = state == XIAOXIN_SYSTEM_OVERLAY_NETWORK_CONFIGURING
            ? "正在配网或等待连接"
            : "WiFi 已断开，正在重新连接";
        const xiaoxin_notification_event_t event = {
            .type = XIAOXIN_NOTIFICATION_EVENT_WIFI_DISCONNECTED,
            .title = nullptr,
            .body = body,
            .tag = nullptr,
            .priority = 0,
            .ttl_ms = 0,
        };
        UpsertNotificationEventLocked(event);
        network_notification_active_ = true;
        last_network_notification_state_ = state;
    }

    void AddVoiceFailureNotificationLocked(const char* status) {
        const xiaoxin_notification_event_t event = {
            .type = XIAOXIN_NOTIFICATION_EVENT_VOICE_RECOGNITION_FAILED,
            .title = nullptr,
            .body = status != nullptr ? status : "没听清，请再说一次",
            .tag = nullptr,
            .priority = 0,
            .ttl_ms = 8000,
        };
        UpsertNotificationEventLocked(event);
    }

    static bool PointInObj(lv_obj_t* obj, uint16_t x, uint16_t y) {
        if (obj == nullptr || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
            return false;
        }
        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);
        return x >= coords.x1 && x <= coords.x2 && y >= coords.y1 && y <= coords.y2;
    }

    static bool PointInObjWithSlop(
        lv_obj_t* obj,
        uint16_t x,
        uint16_t y,
        int16_t slop_x,
        int16_t slop_y
    ) {
        if (obj == nullptr || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
            return false;
        }
        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);
        coords.x1 -= slop_x;
        coords.x2 += slop_x;
        coords.y1 -= slop_y;
        coords.y2 += slop_y;
        return x >= coords.x1 && x <= coords.x2 && y >= coords.y1 && y <= coords.y2;
    }

    static PaopaoPetDisplay* SettingsEventDisplay(lv_event_t* e) {
        return e != nullptr ? static_cast<PaopaoPetDisplay*>(lv_event_get_user_data(e)) : nullptr;
    }

    static void SettingsBackRowClicked(lv_event_t* e) {
        auto* self = SettingsEventDisplay(e);
        if (self != nullptr) {
            self->CloseSettingsOverlayLocked();
        }
    }

    static void SettingsBrightnessBackClicked(lv_event_t* e) {
        auto* self = SettingsEventDisplay(e);
        if (self != nullptr) {
            self->RenderSettingsListLocked();
        }
    }

    static void SettingsRowClicked(lv_event_t* e) {
        auto* self = SettingsEventDisplay(e);
        lv_obj_t* target = e != nullptr ? (lv_obj_t*)lv_event_get_target(e) : nullptr;
        if (self == nullptr || target == nullptr) {
            return;
        }
        for (uint8_t i = 0; i < self->settings_item_count_; ++i) {
            if (self->settings_rows_[i].container == target) {
                self->OpenSettingsItemLocked(self->settings_rows_[i].item);
                return;
            }
        }
    }

    void HandleSettingsBrightnessSliderEvent(lv_event_t* e) {
        const lv_event_code_t code = lv_event_get_code(e);
        if (code != LV_EVENT_PRESSED &&
            code != LV_EVENT_PRESSING &&
            code != LV_EVENT_RELEASED &&
            code != LV_EVENT_PRESS_LOST) {
            return;
        }

        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            settings_brightness_dragging_ = false;
            ApplySettingsBrightness(settings_brightness_value_, true);
            return;
        }

        lv_point_t point = {};
        lv_indev_t* indev = lv_indev_active();
        if (indev == nullptr) {
            return;
        }
        lv_indev_get_point(indev, &point);

        lv_area_t coords = {};
        if (settings_brightness_track_ != nullptr) {
            lv_obj_get_coords(settings_brightness_track_, &coords);
        }
        settings_brightness_dragging_ = true;
        settings_brightness_value_ = xiaoxin_settings_brightness_from_x(
            (int)point.x,
            (int)coords.x1,
            k_settings_brightness_track_w
        );
        UpdateSettingsBrightnessSliderLocked(settings_brightness_value_);
        ApplySettingsBrightness(settings_brightness_value_, false);
    }

    static void SettingsBrightnessSliderEvent(lv_event_t* e) {
        auto* self = SettingsEventDisplay(e);
        if (self != nullptr) {
            self->HandleSettingsBrightnessSliderEvent(e);
        }
    }

    bool ConsumeSettingsWifiConfigRequestLocked() {
        const bool requested = settings_wifi_config_requested_;
        settings_wifi_config_requested_ = false;
        return requested;
    }

    bool ConsumePowerSaveTimerWakeRequestLocked() {
        const bool requested = power_save_timer_wake_requested_;
        power_save_timer_wake_requested_ = false;
        return requested;
    }

    void CloseSettingsOverlayLocked() {
        if (!settings_open_) {
            return;
        }
        settings_open_ = false;
        settings_view_ = SettingsView::List;
        settings_touch_action_consumed_ = false;
        AddFlagIfCreated(settings_layer_, LV_OBJ_FLAG_HIDDEN);
        RaiseOverlayObjects();
    }

    xiaoxin_settings_caps_t SettingsCaps() const;

    void EnsureSettingsOverlayLocked() {
        if (settings_layer_ != nullptr) {
            return;
        }
        lv_obj_t* screen = lv_screen_active();
        settings_layer_ = lv_obj_create(screen);
        lv_obj_remove_style_all(settings_layer_);
        lv_obj_set_size(settings_layer_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lv_obj_set_style_bg_color(settings_layer_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(settings_layer_, static_cast<lv_opa_t>(118), 0);
        lv_obj_clear_flag(settings_layer_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(settings_layer_, LV_OBJ_FLAG_HIDDEN);

        settings_panel_ = lv_obj_create(settings_layer_);
        lv_obj_remove_style_all(settings_panel_);
        lv_obj_set_size(settings_panel_, k_settings_panel_w, k_settings_panel_h);
        lv_obj_set_style_radius(settings_panel_, k_settings_panel_radius, 0);
        lv_obj_set_style_bg_color(settings_panel_, lv_color_hex(k_settings_panel_bg), 0);
        lv_obj_set_style_bg_opa(settings_panel_, static_cast<lv_opa_t>(220), 0);
        lv_obj_set_style_border_width(settings_panel_, 1, 0);
        lv_obj_set_style_border_color(settings_panel_, lv_color_hex(k_settings_panel_border), 0);
        lv_obj_set_style_border_opa(settings_panel_, LV_OPA_70, 0);
        lv_obj_clear_flag(settings_panel_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(settings_panel_, LV_ALIGN_CENTER, 0, 0);

        settings_title_label_ = lv_label_create(settings_panel_);
        lv_obj_set_style_text_color(settings_title_label_, lv_color_hex(k_settings_text_primary), 0);
        lv_obj_align(settings_title_label_, LV_ALIGN_TOP_MID, 0, k_settings_title_y);

        settings_hint_label_ = lv_label_create(settings_panel_);
        lv_obj_set_style_text_color(settings_hint_label_, lv_color_hex(k_settings_text_secondary), 0);
        lv_obj_align(settings_hint_label_, LV_ALIGN_BOTTOM_MID, 0, -k_settings_hint_bottom_offset);

        settings_brightness_value_label_ = lv_label_create(settings_panel_);
        lv_label_set_text(settings_brightness_value_label_, "75%");
        lv_obj_set_style_text_color(settings_brightness_value_label_, lv_color_hex(k_settings_text_primary), 0);
        lv_obj_align(settings_brightness_value_label_, LV_ALIGN_TOP_MID, 0, k_settings_brightness_value_y);
        lv_obj_add_flag(settings_brightness_value_label_, LV_OBJ_FLAG_HIDDEN);

        settings_brightness_track_ = lv_obj_create(settings_panel_);
        lv_obj_remove_style_all(settings_brightness_track_);
        lv_obj_set_size(settings_brightness_track_, k_settings_brightness_track_w, k_settings_brightness_track_h);
        lv_obj_set_style_radius(settings_brightness_track_, k_settings_brightness_track_h / 2, 0);
        lv_obj_set_style_bg_color(settings_brightness_track_, lv_color_hex(0x26364f), 0);
        lv_obj_set_style_bg_opa(settings_brightness_track_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(settings_brightness_track_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(settings_brightness_track_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(settings_brightness_track_, SettingsBrightnessSliderEvent, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(settings_brightness_track_, SettingsBrightnessSliderEvent, LV_EVENT_PRESSING, this);
        lv_obj_add_event_cb(settings_brightness_track_, SettingsBrightnessSliderEvent, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(settings_brightness_track_, SettingsBrightnessSliderEvent, LV_EVENT_PRESS_LOST, this);
        lv_obj_align(settings_brightness_track_, LV_ALIGN_TOP_MID, 0, k_settings_brightness_track_y);
        lv_obj_add_flag(settings_brightness_track_, LV_OBJ_FLAG_HIDDEN);

        settings_brightness_fill_ = lv_obj_create(settings_brightness_track_);
        lv_obj_remove_style_all(settings_brightness_fill_);
        lv_obj_set_size(settings_brightness_fill_, 1, k_settings_brightness_track_h);
        lv_obj_set_style_radius(settings_brightness_fill_, k_settings_brightness_track_h / 2, 0);
        lv_obj_set_style_bg_color(settings_brightness_fill_, lv_color_hex(k_settings_panel_border), 0);
        lv_obj_set_style_bg_opa(settings_brightness_fill_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(settings_brightness_fill_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(settings_brightness_fill_, LV_ALIGN_LEFT_MID, 0, 0);

        settings_brightness_thumb_ = lv_obj_create(settings_panel_);
        lv_obj_remove_style_all(settings_brightness_thumb_);
        lv_obj_set_size(settings_brightness_thumb_, k_settings_brightness_thumb_size, k_settings_brightness_thumb_size);
        lv_obj_set_style_radius(settings_brightness_thumb_, k_settings_brightness_thumb_size / 2, 0);
        lv_obj_set_style_bg_color(settings_brightness_thumb_, lv_color_hex(k_settings_panel_border), 0);
        lv_obj_set_style_bg_opa(settings_brightness_thumb_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(settings_brightness_thumb_, 2, 0);
        lv_obj_set_style_border_color(settings_brightness_thumb_, lv_color_hex(0xeaf4ff), 0);
        lv_obj_set_style_border_opa(settings_brightness_thumb_, LV_OPA_80, 0);
        lv_obj_clear_flag(settings_brightness_thumb_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(settings_brightness_thumb_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(settings_brightness_thumb_, SettingsBrightnessSliderEvent, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(settings_brightness_thumb_, SettingsBrightnessSliderEvent, LV_EVENT_PRESSING, this);
        lv_obj_add_event_cb(settings_brightness_thumb_, SettingsBrightnessSliderEvent, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(settings_brightness_thumb_, SettingsBrightnessSliderEvent, LV_EVENT_PRESS_LOST, this);
        lv_obj_add_flag(settings_brightness_thumb_, LV_OBJ_FLAG_HIDDEN);

        settings_brightness_low_label_ = lv_label_create(settings_panel_);
        lv_label_set_text(settings_brightness_low_label_, "低");
        lv_obj_set_style_text_color(settings_brightness_low_label_, lv_color_hex(k_settings_text_secondary), 0);
        lv_obj_align(
            settings_brightness_low_label_,
            LV_ALIGN_TOP_LEFT,
            (k_settings_panel_w - k_settings_brightness_track_w) / 2,
            k_settings_brightness_level_label_y
        );
        lv_obj_add_flag(settings_brightness_low_label_, LV_OBJ_FLAG_HIDDEN);

        settings_brightness_high_label_ = lv_label_create(settings_panel_);
        lv_label_set_text(settings_brightness_high_label_, "高");
        lv_obj_set_style_text_color(settings_brightness_high_label_, lv_color_hex(k_settings_text_secondary), 0);
        lv_obj_align(
            settings_brightness_high_label_,
            LV_ALIGN_TOP_RIGHT,
            -((k_settings_panel_w - k_settings_brightness_track_w) / 2),
            k_settings_brightness_level_label_y
        );
        lv_obj_add_flag(settings_brightness_high_label_, LV_OBJ_FLAG_HIDDEN);

        settings_brightness_back_button_ = lv_obj_create(settings_panel_);
        lv_obj_remove_style_all(settings_brightness_back_button_);
        lv_obj_set_size(
            settings_brightness_back_button_,
            k_settings_brightness_back_button_w,
            k_settings_brightness_back_button_h
        );
        lv_obj_set_style_radius(settings_brightness_back_button_, k_settings_brightness_back_button_h / 2, 0);
        lv_obj_set_style_bg_color(settings_brightness_back_button_, lv_color_hex(0x0e1a2b), 0);
        lv_obj_set_style_bg_opa(settings_brightness_back_button_, static_cast<lv_opa_t>(118), 0);
        lv_obj_set_style_border_width(settings_brightness_back_button_, 1, 0);
        lv_obj_set_style_border_color(settings_brightness_back_button_, lv_color_hex(k_settings_panel_border), 0);
        lv_obj_set_style_border_opa(settings_brightness_back_button_, LV_OPA_70, 0);
        lv_obj_clear_flag(settings_brightness_back_button_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(settings_brightness_back_button_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(settings_brightness_back_button_, SettingsBrightnessBackClicked, LV_EVENT_CLICKED, this);
        lv_obj_align(settings_brightness_back_button_, LV_ALIGN_TOP_MID, 0, k_settings_brightness_back_button_y);
        lv_obj_add_flag(settings_brightness_back_button_, LV_OBJ_FLAG_HIDDEN);

        settings_brightness_back_button_label_ = lv_label_create(settings_brightness_back_button_);
        lv_label_set_text(settings_brightness_back_button_label_, "返回");
        lv_obj_set_style_text_color(settings_brightness_back_button_label_, lv_color_hex(k_settings_text_primary), 0);
        lv_obj_add_flag(settings_brightness_back_button_label_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(
            settings_brightness_back_button_label_,
            SettingsBrightnessBackClicked,
            LV_EVENT_CLICKED,
            this
        );
        lv_obj_align(settings_brightness_back_button_label_, LV_ALIGN_CENTER, 0, 0);

        for (uint8_t i = 0; i < k_settings_item_max_count; ++i) {
            SettingsRow& row = settings_rows_[i];
            row.container = lv_obj_create(settings_panel_);
            lv_obj_remove_style_all(row.container);
            lv_obj_set_size(row.container, k_settings_row_w, k_settings_row_h);
            lv_obj_set_style_radius(row.container, 14, 0);
            lv_obj_set_style_bg_color(row.container, lv_color_hex(0x1d3654), 0);
            lv_obj_set_style_bg_opa(row.container, static_cast<lv_opa_t>(122), 0);
            lv_obj_clear_flag(row.container, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(row.container, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row.container, SettingsRowClicked, LV_EVENT_CLICKED, this);
            lv_obj_align(row.container, LV_ALIGN_TOP_LEFT, k_settings_row_x, k_settings_row_y + i * k_settings_row_pitch);

            row.title = lv_label_create(row.container);
            lv_obj_set_style_text_color(row.title, lv_color_hex(k_settings_text_primary), 0);
            lv_obj_align(row.title, LV_ALIGN_LEFT_MID, 14, 0);

            row.value = lv_label_create(row.container);
            lv_obj_set_style_text_color(row.value, lv_color_hex(k_settings_text_secondary), 0);
            lv_obj_align(row.value, LV_ALIGN_RIGHT_MID, -14, 0);
        }

        settings_back_row_ = lv_obj_create(settings_panel_);
        lv_obj_remove_style_all(settings_back_row_);
        lv_obj_set_size(settings_back_row_, k_settings_back_row_w, k_settings_back_row_h);
        lv_obj_set_style_radius(settings_back_row_, 13, 0);
        lv_obj_set_style_bg_color(settings_back_row_, lv_color_hex(0x0e1a2b), 0);
        lv_obj_set_style_bg_opa(settings_back_row_, static_cast<lv_opa_t>(72), 0);
        lv_obj_set_style_border_width(settings_back_row_, 1, 0);
        lv_obj_set_style_border_color(settings_back_row_, lv_color_hex(k_settings_text_secondary), 0);
        lv_obj_set_style_border_opa(settings_back_row_, LV_OPA_50, 0);
        lv_obj_clear_flag(settings_back_row_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(settings_back_row_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(settings_back_row_, SettingsBackRowClicked, LV_EVENT_CLICKED, this);
        lv_obj_align(settings_back_row_, LV_ALIGN_TOP_MID, 0, k_settings_back_row_y);

        settings_back_label_ = lv_label_create(settings_back_row_);
        lv_label_set_text(settings_back_label_, "退出设置");
        lv_obj_set_style_text_color(settings_back_label_, lv_color_hex(k_settings_text_secondary), 0);
        lv_obj_add_flag(settings_back_label_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(settings_back_label_, SettingsBackRowClicked, LV_EVENT_CLICKED, this);
        lv_obj_align(settings_back_label_, LV_ALIGN_CENTER, 0, 0);
    }

    void HideSettingsRowsLocked() {
        for (uint8_t i = 0; i < k_settings_item_max_count; ++i) {
            AddFlagIfCreated(settings_rows_[i].container, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void HideSettingsBrightnessSliderLocked() {
        AddFlagIfCreated(settings_brightness_value_label_, LV_OBJ_FLAG_HIDDEN);
        AddFlagIfCreated(settings_brightness_track_, LV_OBJ_FLAG_HIDDEN);
        AddFlagIfCreated(settings_brightness_thumb_, LV_OBJ_FLAG_HIDDEN);
        AddFlagIfCreated(settings_brightness_low_label_, LV_OBJ_FLAG_HIDDEN);
        AddFlagIfCreated(settings_brightness_high_label_, LV_OBJ_FLAG_HIDDEN);
        AddFlagIfCreated(settings_brightness_back_button_, LV_OBJ_FLAG_HIDDEN);
        settings_brightness_dragging_ = false;
    }

    void ShowSettingsBrightnessSliderLocked() {
        RemoveFlagIfCreated(settings_brightness_value_label_, LV_OBJ_FLAG_HIDDEN);
        RemoveFlagIfCreated(settings_brightness_track_, LV_OBJ_FLAG_HIDDEN);
        RemoveFlagIfCreated(settings_brightness_fill_, LV_OBJ_FLAG_HIDDEN);
        RemoveFlagIfCreated(settings_brightness_thumb_, LV_OBJ_FLAG_HIDDEN);
        RemoveFlagIfCreated(settings_brightness_low_label_, LV_OBJ_FLAG_HIDDEN);
        RemoveFlagIfCreated(settings_brightness_high_label_, LV_OBJ_FLAG_HIDDEN);
        RemoveFlagIfCreated(settings_brightness_back_button_, LV_OBJ_FLAG_HIDDEN);
    }

    void UpdateSettingsBrightnessSliderLocked(uint8_t brightness) {
        const uint8_t clamped = std::max<uint8_t>((uint8_t)10, xiaoxin_settings_clamp_percent(brightness));
        settings_brightness_value_ = clamped;
        if (settings_brightness_value_label_ == nullptr ||
            settings_brightness_fill_ == nullptr ||
            settings_brightness_thumb_ == nullptr) {
            return;
        }

        char text[8] = {};
        std::snprintf(text, sizeof(text), "%u%%", (unsigned)settings_brightness_value_);
        lv_label_set_text(settings_brightness_value_label_, text);

        const int16_t fill_w = (int16_t)std::max<int32_t>(
            1,
            ((int32_t)k_settings_brightness_track_w * (settings_brightness_value_ - 10) + 45) / 90
        );
        lv_obj_set_width(settings_brightness_fill_, fill_w);
        lv_obj_align(settings_brightness_fill_, LV_ALIGN_LEFT_MID, 0, 0);

        const int16_t track_left = (int16_t)((k_settings_panel_w - k_settings_brightness_track_w) / 2);
        const int16_t thumb_x = (int16_t)(track_left + fill_w - k_settings_brightness_thumb_size / 2);
        const int16_t thumb_y = (int16_t)(k_settings_brightness_track_y -
            (k_settings_brightness_thumb_size - k_settings_brightness_track_h) / 2);
        lv_obj_align(settings_brightness_thumb_, LV_ALIGN_TOP_LEFT, thumb_x, thumb_y);
    }

    bool SettingsBrightnessSliderContains(uint16_t x, uint16_t y) const {
        if (settings_brightness_track_ == nullptr ||
            lv_obj_has_flag(settings_brightness_track_, LV_OBJ_FLAG_HIDDEN)) {
            return false;
        }
        lv_area_t coords;
        lv_obj_get_coords(settings_brightness_track_, &coords);
        coords.x1 -= 18;
        coords.x2 += 18;
        coords.y1 -= 24;
        coords.y2 += 24;
        return x >= coords.x1 && x <= coords.x2 && y >= coords.y1 && y <= coords.y2;
    }

    void SetSettingsBackRowVisibleLocked(bool visible) {
        if (settings_back_row_ == nullptr) {
            return;
        }
        if (visible) {
            lv_obj_remove_flag(settings_back_row_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(settings_back_row_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void AlignSettingsHintBottomLocked() {
        if (settings_hint_label_ == nullptr) {
            return;
        }
        lv_obj_align(settings_hint_label_, LV_ALIGN_BOTTOM_MID, 0, -k_settings_hint_bottom_offset);
    }

    bool SettingsPowerSaveEnabled() const {
        Settings settings("wifi", false);
        return settings.GetBool("sleep_mode", true);
    }

    void ApplySettingsDefaultRowValueStyleLocked(SettingsRow& row) {
        if (row.value == nullptr) {
            return;
        }
        lv_label_set_text(row.value, "›");
        lv_obj_set_style_text_color(row.value, lv_color_hex(k_settings_text_secondary), 0);
        lv_obj_set_style_bg_opa(row.value, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(row.value, 0, 0);
        lv_obj_set_style_pad_left(row.value, 0, 0);
        lv_obj_set_style_pad_right(row.value, 0, 0);
        lv_obj_set_style_pad_top(row.value, 0, 0);
        lv_obj_set_style_pad_bottom(row.value, 0, 0);
    }

    void ApplySettingsPowerSaveRowStyleLocked(SettingsRow& row, bool power_save_enabled) {
        if (row.value == nullptr) {
            return;
        }
        lv_obj_set_style_text_color(
            row.value,
            lv_color_hex(power_save_enabled ? 0xfff4cc : k_settings_text_secondary),
            0
        );
        lv_obj_set_style_bg_color(
            row.value,
            lv_color_hex(power_save_enabled ? 0x70521b : 0x26364f),
            0
        );
        lv_obj_set_style_bg_opa(row.value, static_cast<lv_opa_t>(188), 0);
        lv_obj_set_style_radius(row.value, 10, 0);
        lv_obj_set_style_pad_left(row.value, 7, 0);
        lv_obj_set_style_pad_right(row.value, 7, 0);
        lv_obj_set_style_pad_top(row.value, 2, 0);
        lv_obj_set_style_pad_bottom(row.value, 2, 0);
    }

    void RenderSettingsListLocked() {
        EnsureSettingsOverlayLocked();
        settings_view_ = SettingsView::List;
        lv_label_set_text(settings_title_label_, "设置");
        AlignSettingsHintBottomLocked();
        lv_label_set_text(settings_hint_label_, "");
        SetSettingsBackRowVisibleLocked(true);
        settings_item_count_ = xiaoxin_settings_visible_items(SettingsCaps(), settings_items_, k_settings_item_max_count);
        HideSettingsRowsLocked();
        HideSettingsBrightnessSliderLocked();
        for (uint8_t i = 0; i < settings_item_count_; ++i) {
            SettingsRow& row = settings_rows_[i];
            row.item = settings_items_[i];
            lv_label_set_text(row.title, xiaoxin_settings_item_title(row.item));
            if (row.item == XIAOXIN_SETTINGS_ITEM_POWER_SAVE) {
                const bool power_save_enabled = SettingsPowerSaveEnabled();
                lv_label_set_text(row.value, xiaoxin_settings_power_save_value_label(power_save_enabled));
                ApplySettingsPowerSaveRowStyleLocked(row, power_save_enabled);
            } else {
                ApplySettingsDefaultRowValueStyleLocked(row);
            }
            lv_obj_remove_flag(row.container, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void ApplySettingsBrightness(uint8_t brightness, bool permanent) {
        const uint8_t clamped = xiaoxin_settings_clamp_percent(brightness);
        if (settings_brightness_applied_value_ == clamped &&
            settings_brightness_applied_permanent_ == permanent) {
            return;
        }
        settings_brightness_applied_value_ = clamped;
        settings_brightness_applied_permanent_ = permanent;

        auto backlight = Board::GetInstance().GetBacklight();
        if (backlight != nullptr) {
            backlight->SetBrightness(clamped, permanent);
        }
    }

    void RenderSettingsBrightnessPage();

    void RenderSettingsWifiPage();

    void ToggleSettingsPowerSaveLocked() {
        const bool enabled = !SettingsPowerSaveEnabled();
        {
            Settings settings("wifi", true);
            settings.SetBool("sleep_mode", enabled);
        }

        PowerSaveTimer* power_save_timer = TargetPowerSaveTimer();
        if (power_save_timer != nullptr) {
            power_save_timer->SetEnabled(enabled);
        }
    }

    void RenderSettingsAboutPage() {
        settings_view_ = SettingsView::About;
        EnsureSettingsOverlayLocked();
        HideSettingsRowsLocked();
        HideSettingsBrightnessSliderLocked();
        SetSettingsBackRowVisibleLocked(false);
        const esp_app_desc_t* app_desc = esp_app_get_description();
        xiaoxin_runtime_health_snapshot_t snapshot = {};
        RuntimeHealthReadSnapshot(&snapshot);
        char current_duration[24] = {};
        char last_duration[24] = {};
        xiaoxin_runtime_health_format_duration(
            current_duration,
            sizeof(current_duration),
            snapshot.current_runtime_sec
        );
        xiaoxin_runtime_health_format_duration(
            last_duration,
            sizeof(last_duration),
            snapshot.last_runtime_sec
        );
        const char* reset_label = xiaoxin_runtime_health_reset_label(snapshot.last_reset_kind);
        char text[192] = {};
        std::snprintf(
            text,
            sizeof(text),
            "小芯 D151\n固件 %s\n本次 %s\n上次 %s\n重启 %s\n欠压 %lu次",
            app_desc != nullptr ? app_desc->version : "-",
            current_duration,
            last_duration,
            reset_label,
            (unsigned long)snapshot.brownout_count
        );
        lv_label_set_text(settings_title_label_, "关于");
        lv_obj_align(settings_hint_label_, LV_ALIGN_TOP_MID, 0, k_settings_about_body_y);
        lv_label_set_text(settings_hint_label_, text);
    }

    void OpenSettingsItemLocked(xiaoxin_settings_item_t item) {
        switch (item) {
            case XIAOXIN_SETTINGS_ITEM_BRIGHTNESS:
                RenderSettingsBrightnessPage();
                break;
            case XIAOXIN_SETTINGS_ITEM_WIFI:
                RenderSettingsWifiPage();
                break;
            case XIAOXIN_SETTINGS_ITEM_POWER_SAVE:
                ToggleSettingsPowerSaveLocked();
                RenderSettingsListLocked();
                break;
            case XIAOXIN_SETTINGS_ITEM_ABOUT:
                RenderSettingsAboutPage();
                break;
            default:
                RenderSettingsListLocked();
                break;
        }
    }

    void HandleSettingsTouch(uint16_t x, uint16_t y, bool pressed) {
        if (!pressed) {
            if (settings_view_ == SettingsView::Brightness && settings_brightness_dragging_) {
                settings_brightness_dragging_ = false;
                ApplySettingsBrightness(settings_brightness_value_, true);
            }
            settings_touch_action_consumed_ = false;
            return;
        }

        if (!settings_touch_action_consumed_) {
            if (settings_view_ == SettingsView::List) {
                if (PointInObjWithSlop(
                        settings_back_row_,
                        x,
                        y,
                        k_settings_button_hit_slop_x,
                        k_settings_button_hit_slop_y
                    )) {
                    settings_touch_action_consumed_ = true;
                    CloseSettingsOverlayLocked();
                    return;
                }
                if (!touch_pressed_) {
                    for (uint8_t i = 0; i < settings_item_count_; ++i) {
                        if (PointInObj(settings_rows_[i].container, x, y)) {
                            settings_touch_action_consumed_ = true;
                            OpenSettingsItemLocked(settings_rows_[i].item);
                            return;
                        }
                    }
                }
                return;
            }

            if (settings_view_ == SettingsView::Brightness &&
                PointInObjWithSlop(
                    settings_brightness_back_button_,
                    x,
                    y,
                    k_settings_button_hit_slop_x,
                    k_settings_button_hit_slop_y
                )) {
                settings_touch_action_consumed_ = true;
                RenderSettingsListLocked();
                return;
            }

            if (!touch_pressed_ && settings_view_ == SettingsView::About) {
                settings_touch_action_consumed_ = true;
                RenderSettingsListLocked();
                return;
            }
        }

        if (settings_view_ == SettingsView::Brightness &&
            (settings_brightness_dragging_ || SettingsBrightnessSliderContains(x, y))) {
            lv_area_t coords = {};
            if (settings_brightness_track_ != nullptr) {
                lv_obj_get_coords(settings_brightness_track_, &coords);
            }
            settings_brightness_dragging_ = true;
            settings_brightness_value_ = xiaoxin_settings_brightness_from_x(
                (int)x,
                (int)coords.x1,
                k_settings_brightness_track_w
            );
            UpdateSettingsBrightnessSliderLocked(settings_brightness_value_);
            ApplySettingsBrightness(settings_brightness_value_, false);
        }
    }

    int8_t NotificationCardSlotAtPoint(uint16_t x, uint16_t y) const {
        const uint8_t total = xiaoxin_card_pager_notification_count(&card_pager_);
        const uint8_t active_slot = NotificationIndexForScroll(notification_scroll_y_, total);

        if (active_slot < k_card_glass_count &&
            PointInObj(glass_cards_[active_slot].container, x, y)) {
            return (int8_t)active_slot;
        }

        for (int8_t slot = (int8_t)k_card_glass_count - 1; slot >= 0; --slot) {
            if ((uint8_t)slot == active_slot) {
                continue;
            }
            if (PointInObj(glass_cards_[slot].container, x, y)) {
                return slot;
            }
        }
        return -1;
    }

    bool NotificationClearButtonContains(uint16_t x, uint16_t y) const {
        return PointInObj(notification_clear_button_, x, y);
    }

    void AddUiPerfSample(uint32_t& calls, uint32_t& total_us, uint32_t& max_us, uint32_t elapsed_us) {
        if (!k_ui_perf_trace_enabled) {
            return;
        }
        calls++;
        total_us += elapsed_us;
        if (elapsed_us > max_us) {
            max_us = elapsed_us;
        }
    }

    void LogUiPerfSummary(uint32_t now_ms) {
        if (!k_ui_perf_trace_enabled) {
            return;
        }
        if (ui_perf_last_log_ms_ != 0 && now_ms - ui_perf_last_log_ms_ < k_ui_perf_log_interval_ms) {
            return;
        }
        ui_perf_last_log_ms_ = now_ms;

        const uint32_t drag_avg = ui_perf_drag_calls_ == 0 ? 0 : ui_perf_drag_total_us_ / ui_perf_drag_calls_;
        const uint32_t pet_avg = ui_perf_pet_copy_calls_ == 0 ? 0 : ui_perf_pet_copy_total_us_ / ui_perf_pet_copy_calls_;
        const uint32_t touch_avg = ui_perf_touch_loop_calls_ == 0 ? 0 : ui_perf_touch_loop_total_us_ / ui_perf_touch_loop_calls_;

        ESP_LOGI(
            TAG,
            "[UI-PERF] drag avg=%uus max=%uus calls=%u pet_copy avg=%uus max=%uus calls=%u touch_loop avg=%uus max=%uus calls=%u",
            (unsigned)drag_avg,
            (unsigned)ui_perf_drag_max_us_,
            (unsigned)ui_perf_drag_calls_,
            (unsigned)pet_avg,
            (unsigned)ui_perf_pet_copy_max_us_,
            (unsigned)ui_perf_pet_copy_calls_,
            (unsigned)touch_avg,
            (unsigned)ui_perf_touch_loop_max_us_,
            (unsigned)ui_perf_touch_loop_calls_
        );

        ui_perf_drag_calls_ = 0;
        ui_perf_drag_total_us_ = 0;
        ui_perf_drag_max_us_ = 0;
        ui_perf_pet_copy_calls_ = 0;
        ui_perf_pet_copy_total_us_ = 0;
        ui_perf_pet_copy_max_us_ = 0;
        ui_perf_touch_loop_calls_ = 0;
        ui_perf_touch_loop_total_us_ = 0;
        ui_perf_touch_loop_max_us_ = 0;
    }

    static bool IsBusyStatus(const char* status) {
        return StatusEquals(status, Lang::Strings::CONNECTING) ||
               StatusEquals(status, Lang::Strings::REGISTERING_NETWORK) ||
               StatusEquals(status, Lang::Strings::DETECTING_MODULE) ||
               StatusEquals(status, Lang::Strings::LOADING_PROTOCOL) ||
               StatusEquals(status, Lang::Strings::CHECKING_NEW_VERSION) ||
               StatusEquals(status, Lang::Strings::UPGRADING) ||
               StatusEquals(status, Lang::Strings::ACTIVATION) ||
               StatusEquals(status, Lang::Strings::WIFI_CONFIG_MODE) ||
               Contains(status, "Connecting") ||
               Contains(status, "connecting") ||
               Contains(status, "Loading") ||
               Contains(status, "loading") ||
               Contains(status, "Upgrading") ||
               Contains(status, "upgrading");
    }

    void ApplyPetStateIfChanged() {
        if (current_state_ == trigger_.displayed_state) {
            return;
        }
        if (IsCardLayerVisible()) {
            return;
        }
        current_state_ = trigger_.displayed_state;
        PlayGifState(current_state_);
    }

    void InitializePetFrameBuffer() {
        if (pet_frame_buffer_ == nullptr) {
            const size_t frame_bytes = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
            pet_frame_buffer_ = static_cast<uint16_t*>(
                heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
            );
            if (pet_frame_buffer_ == nullptr) {
                pet_frame_buffer_ = static_cast<uint16_t*>(heap_caps_malloc(frame_bytes, MALLOC_CAP_8BIT));
            }
            if (pet_frame_buffer_ == nullptr) {
                ESP_LOGE(TAG, "Failed to allocate Paopao full-screen frame buffer");
                return;
            }
        }

        std::fill_n(pet_frame_buffer_, DISPLAY_WIDTH * DISPLAY_HEIGHT, k_white_rgb565);
        pet_frame_dsc_ = {};
        pet_frame_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        pet_frame_dsc_.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
        pet_frame_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        pet_frame_dsc_.header.w = DISPLAY_WIDTH;
        pet_frame_dsc_.header.h = DISPLAY_HEIGHT;
        pet_frame_dsc_.header.stride = DISPLAY_WIDTH * sizeof(uint16_t);
        pet_frame_dsc_.data_size = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        pet_frame_dsc_.data = reinterpret_cast<const uint8_t*>(pet_frame_buffer_);
    }

    void ShowBootSplashLocked() {
        const size_t gif_size = assets_images_boot_gif_end - assets_images_boot_gif_start;
        if (gif_size == 0) {
            return;
        }

        boot_splash_wait_for_ready_ = s_boot_on_battery;

        lv_obj_t* screen = lv_screen_active();
        boot_splash_layer_ = lv_obj_create(screen);
        lv_obj_set_size(boot_splash_layer_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lv_obj_set_style_radius(boot_splash_layer_, 0, 0);
        lv_obj_set_style_bg_color(boot_splash_layer_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(boot_splash_layer_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(boot_splash_layer_, 0, 0);
        lv_obj_set_style_pad_all(boot_splash_layer_, 0, 0);
        lv_obj_set_scrollbar_mode(boot_splash_layer_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(boot_splash_layer_, LV_OBJ_FLAG_SCROLLABLE);

        boot_splash_image_ = lv_image_create(boot_splash_layer_);
        lv_obj_center(boot_splash_image_);

        boot_splash_source_dsc_ = {};
        boot_splash_source_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        boot_splash_source_dsc_.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
        boot_splash_source_dsc_.data_size = gif_size;
        boot_splash_source_dsc_.data = assets_images_boot_gif_start;

        boot_splash_controller_ = std::make_unique<LvglGif>(&boot_splash_source_dsc_, true, 0x000000, true);
        if (boot_splash_controller_ == nullptr || !boot_splash_controller_->IsLoaded()) {
            ESP_LOGW(TAG, "Boot splash GIF failed to load");
            HideBootSplashLocked();
            return;
        }

        boot_splash_controller_->SetFrameCallback([this]() {
            if (boot_splash_image_ != nullptr && boot_splash_controller_ != nullptr) {
                lv_image_set_src(boot_splash_image_, boot_splash_controller_->image_dsc());
            }
        });
        lv_image_set_src(boot_splash_image_, boot_splash_controller_->image_dsc());
        boot_splash_controller_->Start();
        lv_obj_move_foreground(boot_splash_layer_);
        boot_splash_visible_ = true;

        if (boot_splash_timer_ == nullptr) {
            const esp_timer_create_args_t boot_splash_timer_args = {
                .callback = BootSplashTimerCallback,
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "boot_splash",
                .skip_unhandled_events = true,
            };
            esp_err_t err = esp_timer_create(&boot_splash_timer_args, &boot_splash_timer_);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Boot splash timer create failed: %s", esp_err_to_name(err));
                HideBootSplashLocked();
                return;
            }
        }

        esp_timer_stop(boot_splash_timer_);
        const uint32_t splash_duration_ms = boot_splash_wait_for_ready_
            ? k_boot_splash_ready_hold_timeout_ms
            : k_boot_splash_duration_ms;
        ESP_ERROR_CHECK(esp_timer_start_once(boot_splash_timer_, splash_duration_ms * 1000ULL));
    }

    void ScheduleBootSplashRevealAfterUiReadyLocked() {
        if (!boot_splash_visible_) {
            return;
        }
        if (boot_splash_wait_for_ready_) {
            return;
        }
        if (boot_splash_timer_ == nullptr) {
            HideBootSplashLocked();
            return;
        }

        boot_splash_wait_for_ready_ = false;
        esp_timer_stop(boot_splash_timer_);
        esp_err_t err = esp_timer_start_once(boot_splash_timer_, k_boot_splash_ui_ready_reveal_delay_ms * 1000ULL);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Boot splash UI-ready reveal timer start failed: %s", esp_err_to_name(err));
            HideBootSplashLocked();
        }
    }

    void HideBootSplashLocked() {
        boot_splash_visible_ = false;
        boot_splash_wait_for_ready_ = false;
        boot_splash_controller_.reset();
        if (boot_splash_layer_ != nullptr) {
            lv_obj_del(boot_splash_layer_);
            boot_splash_layer_ = nullptr;
            boot_splash_image_ = nullptr;
        }
        RaiseOverlayObjects();
    }

    void HideBootSplashFromTimer() {
        DisplayLockGuard lock(this);
        if (!boot_splash_visible_) {
            return;
        }
        if (boot_splash_wait_for_ready_) {
            boot_splash_wait_for_ready_ = false;
        }
        HideBootSplashLocked();
    }

    static void BootSplashTimerCallback(void* arg) {
        static_cast<PaopaoPetDisplay*>(arg)->HideBootSplashFromTimer();
    }

    static bool IsHidden(lv_obj_t* obj) {
        return obj != nullptr && lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

    static void RestoreHiddenFlag(lv_obj_t* obj, bool hidden) {
        if (obj == nullptr) {
            return;
        }
        if (hidden) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void HideLegacyLowBatteryPopupLocked() {
        if (low_battery_popup_ != nullptr) {
            lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    bool IsCardLayerVisible() const {
        return card_layer_ != nullptr && !lv_obj_has_flag(card_layer_, LV_OBJ_FLAG_HIDDEN);
    }

    void ApplyPetAnimationForCardPager() {
        if (pet_gif_controller_ == nullptr) {
            pet_animation_paused_for_card_ = false;
            return;
        }

        if (IsCardLayerVisible()) {
            if (pet_gif_controller_->IsPlaying()) {
                pet_gif_controller_->Pause();
            }
            pet_animation_paused_for_card_ = true;
            return;
        }

        if (pet_animation_paused_for_card_) {
            pet_gif_controller_->Resume();
            pet_animation_paused_for_card_ = false;
        }
    }

    void ApplySystemBarsForCardPager() {
        const bool card_visible = IsCardLayerVisible();
        if (card_visible) {
            return;
        }

        if (!system_bars_hidden_for_card_) {
            return;
        }

        RestoreHiddenFlag(top_bar_, top_bar_was_hidden_for_card_);
        RestoreHiddenFlag(status_bar_, status_bar_was_hidden_for_card_);
        RestoreHiddenFlag(bottom_bar_, bottom_bar_was_hidden_for_card_);
        system_bars_hidden_for_card_ = false;
    }

    void RaiseBootSplashLayerLocked() {
        if (boot_splash_visible_ && boot_splash_layer_ != nullptr) {
            lv_obj_move_foreground(boot_splash_layer_);
        }
    }

    void RaiseOverlayObjects() {
        HideLegacyLowBatteryPopupLocked();
        ApplySystemBarsForCardPager();
        if (IsCardLayerVisible()) {
            lv_obj_move_foreground(card_layer_);
            if (low_power_clock_visible_ && low_power_clock_layer_ != nullptr) {
                lv_obj_move_foreground(low_power_clock_layer_);
            }
            RaiseBootSplashLayerLocked();
            return;
        }

        if (top_bar_ != nullptr) {
            lv_obj_move_foreground(top_bar_);
        }
        if (status_bar_ != nullptr) {
            lv_obj_move_foreground(status_bar_);
        }
        if (bottom_bar_ != nullptr) {
            lv_obj_move_foreground(bottom_bar_);
        }
        if (settings_open_ && settings_layer_ != nullptr) {
            lv_obj_move_foreground(settings_layer_);
        }
        if (low_power_clock_visible_ && low_power_clock_layer_ != nullptr) {
            lv_obj_move_foreground(low_power_clock_layer_);
        }
        RaiseBootSplashLayerLocked();
    }

    void InitializeLowPowerClockLayerLocked() {
        lv_obj_t* screen = lv_screen_active();
        auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        const lv_font_t* clock_font = &font_puhui_basic_30_4;
        const lv_font_t* date_font = &font_puhui_basic_20_4;
        const lv_font_t* hint_font = lvgl_theme != nullptr && lvgl_theme->text_font() != nullptr
            ? lvgl_theme->text_font()->font()
            : nullptr;

        low_power_clock_layer_ = lv_obj_create(screen);
        lv_obj_remove_style_all(low_power_clock_layer_);
        lv_obj_set_size(low_power_clock_layer_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lv_obj_set_style_bg_color(low_power_clock_layer_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(low_power_clock_layer_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(low_power_clock_layer_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(low_power_clock_layer_, LV_OBJ_FLAG_HIDDEN);

        InitializeLowPowerWaveReferenceBlocksLocked(hint_font);
        InitializeLowPowerSecondGaugeLocked(hint_font);
        InitializeLowPowerWaveBarsLocked();

        low_power_clock_outer_arc_ = lv_arc_create(low_power_clock_layer_);
        lv_obj_set_size(low_power_clock_outer_arc_, 1, 1);
        lv_obj_center(low_power_clock_outer_arc_);
        lv_arc_set_bg_angles(low_power_clock_outer_arc_, 0, 360);
        lv_arc_set_angles(low_power_clock_outer_arc_, 0, 0);
        lv_obj_remove_style(low_power_clock_outer_arc_, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(low_power_clock_outer_arc_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_opa(low_power_clock_outer_arc_, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(low_power_clock_outer_arc_, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_add_flag(low_power_clock_outer_arc_, LV_OBJ_FLAG_HIDDEN);

        low_power_clock_inner_arc_ = lv_arc_create(low_power_clock_layer_);
        lv_obj_set_size(low_power_clock_inner_arc_, 1, 1);
        lv_obj_center(low_power_clock_inner_arc_);
        lv_arc_set_bg_angles(low_power_clock_inner_arc_, 0, 360);
        lv_arc_set_angles(low_power_clock_inner_arc_, 0, XIAOXIN_LOW_POWER_CLOCK_ARC_SPAN_DEGREES);
        lv_obj_remove_style(low_power_clock_inner_arc_, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(low_power_clock_inner_arc_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(low_power_clock_inner_arc_, 3, LV_PART_MAIN);
        lv_obj_set_style_arc_width(low_power_clock_inner_arc_, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(low_power_clock_inner_arc_, lv_color_hex(0x050A0C), LV_PART_MAIN);
        lv_obj_set_style_arc_color(low_power_clock_inner_arc_, lv_color_hex(0x1B4965), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(low_power_clock_inner_arc_, LV_OPA_20, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(low_power_clock_inner_arc_, LowPowerClockOpaPercent(74), LV_PART_INDICATOR);
        lv_obj_add_flag(low_power_clock_inner_arc_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_arc_rounded(low_power_clock_inner_arc_, true, LV_PART_INDICATOR);

        low_power_clock_time_glow_label_ = lv_label_create(low_power_clock_layer_);
        ConfigureLowPowerTimeLabel(low_power_clock_time_glow_label_, clock_font, 0x75DFFF, LowPowerClockOpaPercent(24), 1, 560, 150, 28);
        lv_obj_align(low_power_clock_time_glow_label_, LV_ALIGN_TOP_LEFT, LowPowerRefX(196), LowPowerRefY(150) + k_low_power_time_y_adjust);

        low_power_clock_time_label_ = lv_label_create(low_power_clock_layer_);
        ConfigureLowPowerTimeLabel(low_power_clock_time_label_, clock_font, 0xF6FAFF, LV_OPA_COVER, 1, 532, 142, 24);
        lv_obj_align(low_power_clock_time_label_, LV_ALIGN_TOP_LEFT, LowPowerRefX(196), LowPowerRefY(150) + k_low_power_time_y_adjust);

        low_power_clock_date_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_date_label_, lv_color_hex(0x75AFC0), 0);
        lv_obj_set_style_text_opa(low_power_clock_date_label_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_font(low_power_clock_date_label_, date_font, 0);
        lv_obj_align(low_power_clock_date_label_, LV_ALIGN_TOP_LEFT, 84, 44);
        lv_obj_add_flag(low_power_clock_date_label_, LV_OBJ_FLAG_HIDDEN);

        low_power_clock_sync_dot_ = lv_obj_create(low_power_clock_layer_);
        lv_obj_remove_style_all(low_power_clock_sync_dot_);
        lv_obj_set_size(low_power_clock_sync_dot_, 6, 6);
        lv_obj_set_style_radius(low_power_clock_sync_dot_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(low_power_clock_sync_dot_, LV_OPA_COVER, 0);
        lv_obj_align(low_power_clock_sync_dot_, LV_ALIGN_BOTTOM_RIGHT, -80, -40);
        lv_obj_add_flag(low_power_clock_sync_dot_, LV_OBJ_FLAG_HIDDEN);

        low_power_clock_sync_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_sync_label_, lv_color_hex(0x75AFC0), 0);
        lv_obj_set_style_text_opa(low_power_clock_sync_label_, LV_OPA_80, 0);
        if (hint_font != nullptr) {
            lv_obj_set_style_text_font(low_power_clock_sync_label_, hint_font, 0);
        }
        lv_obj_align(low_power_clock_sync_label_, LV_ALIGN_BOTTOM_RIGHT, -38, -36);
        lv_obj_add_flag(low_power_clock_sync_label_, LV_OBJ_FLAG_HIDDEN);

        low_power_clock_hint_label_ = lv_label_create(low_power_clock_layer_);
        lv_obj_set_style_text_color(low_power_clock_hint_label_, lv_color_hex(0x75AFC0), 0);
        lv_obj_set_style_text_opa(low_power_clock_hint_label_, LV_OPA_90, 0);
        if (hint_font != nullptr) {
            lv_obj_set_style_text_font(low_power_clock_hint_label_, hint_font, 0);
        }
        lv_label_set_text(low_power_clock_hint_label_, "POWER \xE5\x94\xA4\xE9\x86\x92");
        lv_obj_align(low_power_clock_hint_label_, LV_ALIGN_BOTTOM_MID, 0, -34);
        lv_obj_add_flag(low_power_clock_hint_label_, LV_OBJ_FLAG_HIDDEN);
        InitializeLowPowerWaveReferenceLabelsLocked(hint_font);
    }

    void InitializeCardPagerLayer() {
        lv_obj_t* screen = lv_screen_active();

        if (network_label_ != nullptr) {
            lv_obj_add_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
        }
        if (battery_label_ != nullptr) {
            lv_obj_add_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
        }

        auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        SyncNetworkStatusLocked();

        card_layer_ = lv_obj_create(screen);
        lv_obj_remove_style_all(card_layer_);
        lv_obj_set_size(card_layer_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(card_layer_, lv_color_hex(k_card_layer_bg_color), 0);
        lv_obj_set_style_bg_opa(card_layer_, k_card_layer_bg_opa, 0);
        lv_obj_set_style_pad_all(card_layer_, 0, 0);
        lv_obj_set_scrollbar_mode(card_layer_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_align(card_layer_, LV_ALIGN_CENTER, 0, 0);

        pull_indicator_ = lv_obj_create(card_layer_);
        lv_obj_remove_style_all(pull_indicator_);
        lv_obj_set_size(pull_indicator_, 34, 3);
        lv_obj_set_style_bg_color(pull_indicator_, lv_color_hex(k_indicator_top), 0);
        lv_obj_set_style_bg_opa(pull_indicator_, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(pull_indicator_, 2, 0);
        lv_obj_align(pull_indicator_, LV_ALIGN_TOP_MID, 0, 24);

        home_indicator_ = lv_obj_create(card_layer_);
        lv_obj_remove_style_all(home_indicator_);
        lv_obj_set_size(home_indicator_, 56, 3);
        lv_obj_set_style_bg_color(home_indicator_, lv_color_hex(k_indicator_home), 0);
        lv_obj_set_style_bg_opa(home_indicator_, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(home_indicator_, 2, 0);
        lv_obj_align(home_indicator_, LV_ALIGN_BOTTOM_MID, 0, -8);

        card_title_label_ = lv_label_create(card_layer_);
        lv_obj_set_width(card_title_label_, 260);
        lv_obj_set_style_text_align(card_title_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(card_title_label_, lv_color_hex(k_page_title_color), 0);
        lv_obj_set_style_text_opa(card_title_label_, LV_OPA_COVER, 0);
        lv_label_set_text(card_title_label_, "");
        lv_obj_add_flag(card_title_label_, LV_OBJ_FLAG_HIDDEN);

        overview_time_label_ = lv_label_create(card_layer_);
        lv_obj_set_width(overview_time_label_, k_overview_time_w);
        lv_obj_set_style_text_align(overview_time_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(overview_time_label_, lv_color_hex(k_page_title_color), 0);
        lv_obj_set_style_text_opa(overview_time_label_, LV_OPA_COVER, 0);
        lv_label_set_text(overview_time_label_, "");
        lv_obj_align(overview_time_label_, LV_ALIGN_TOP_MID, 0, k_overview_time_y);
        lv_obj_add_flag(overview_time_label_, LV_OBJ_FLAG_HIDDEN);

        overview_date_label_ = lv_label_create(card_layer_);
        lv_obj_set_width(overview_date_label_, k_overview_time_w);
        lv_obj_set_style_text_align(overview_date_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(overview_date_label_, lv_color_hex(k_text_dimmed), 0);
        lv_obj_set_style_text_opa(overview_date_label_, LV_OPA_COVER, 0);
        lv_label_set_text(overview_date_label_, "");
        lv_obj_align(overview_date_label_, LV_ALIGN_TOP_MID, 0, k_overview_date_y);
        lv_obj_add_flag(overview_date_label_, LV_OBJ_FLAG_HIDDEN);

        notification_clear_button_ = lv_obj_create(card_layer_);
        lv_obj_remove_style_all(notification_clear_button_);
        lv_obj_set_size(notification_clear_button_, k_notification_clear_button_w, k_notification_clear_button_h);
        lv_obj_set_style_radius(notification_clear_button_, 16, 0);
        lv_obj_set_style_bg_color(notification_clear_button_, lv_color_hex(k_tag_bg), 0);
        lv_obj_set_style_bg_opa(notification_clear_button_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(notification_clear_button_, lv_color_hex(k_title_accent), 0);
        lv_obj_set_style_border_opa(notification_clear_button_, LV_OPA_70, 0);
        lv_obj_set_style_border_width(notification_clear_button_, 1, 0);
        lv_obj_align(notification_clear_button_, LV_ALIGN_TOP_MID, 0, k_notification_clear_button_y);
        lv_obj_add_flag(notification_clear_button_, LV_OBJ_FLAG_HIDDEN);

        notification_clear_label_ = lv_label_create(notification_clear_button_);
        lv_obj_set_style_text_color(notification_clear_label_, lv_color_hex(k_text_primary), 0);
        lv_label_set_text(notification_clear_label_, "全部清理");
        lv_obj_center(notification_clear_label_);

        notification_empty_panel_ = lv_obj_create(card_layer_);
        lv_obj_remove_style_all(notification_empty_panel_);
        lv_obj_set_size(notification_empty_panel_, k_notification_empty_panel_w, k_notification_empty_panel_h);
        lv_obj_set_style_radius(notification_empty_panel_, 20, 0);
        lv_obj_set_style_bg_color(notification_empty_panel_, lv_color_hex(k_notification_empty_panel_bg), 0);
        lv_obj_set_style_bg_opa(notification_empty_panel_, k_notification_empty_panel_opa, 0);
        lv_obj_set_style_border_color(notification_empty_panel_, lv_color_hex(k_page_title_color), 0);
        lv_obj_set_style_border_opa(notification_empty_panel_, k_notification_empty_panel_border_opa, 0);
        lv_obj_set_style_border_width(notification_empty_panel_, 1, 0);
        lv_obj_clear_flag(notification_empty_panel_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(notification_empty_panel_, LV_ALIGN_TOP_MID, 0, k_notification_empty_panel_y);
        lv_obj_add_flag(notification_empty_panel_, LV_OBJ_FLAG_HIDDEN);

        notification_empty_label_ = lv_label_create(notification_empty_panel_);
        lv_obj_set_width(notification_empty_label_, k_notification_empty_panel_w - 24);
        lv_obj_set_style_text_align(notification_empty_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(notification_empty_label_, lv_color_hex(k_page_title_color), 0);
        lv_obj_set_style_text_opa(notification_empty_label_, LV_OPA_COVER, 0);
        lv_label_set_text(notification_empty_label_, "暂无通知");
        lv_obj_center(notification_empty_label_);
        lv_obj_add_flag(notification_empty_label_, LV_OBJ_FLAG_HIDDEN);

        for (uint8_t i = 0; i < k_card_glass_count; ++i) {
            GlassCard& card = glass_cards_[i];

            card.container = lv_obj_create(card_layer_);
            lv_obj_remove_style_all(card.container);
            lv_obj_set_size(card.container, k_glass_width, k_glass_height);
            lv_obj_set_style_radius(card.container, k_glass_radius, 0);
            lv_obj_set_style_bg_color(card.container, lv_color_hex(k_card_bg_color), 0);
            lv_obj_set_style_bg_opa(card.container, k_glass_bg_opa[i], 0);
            lv_obj_set_style_border_color(card.container, lv_color_hex(k_card_border_color), 0);
            lv_obj_set_style_border_opa(card.container, k_glass_border_opa[i], 0);
            lv_obj_set_style_border_width(card.container, 1, 0);
            lv_obj_set_style_pad_top(card.container, k_glass_pad_v, 0);
            lv_obj_set_style_pad_bottom(card.container, k_glass_pad_v, 0);
            lv_obj_set_style_pad_left(card.container, k_glass_pad_h, 0);
            lv_obj_set_style_pad_right(card.container, k_glass_pad_h, 0);
            lv_obj_set_style_layout(card.container, LV_LAYOUT_NONE, 0);
            lv_obj_align(card.container, LV_ALIGN_TOP_MID, 0, k_glass_y_start + (int32_t)i * k_glass_stack_pitch);
            lv_obj_add_flag(card.container, LV_OBJ_FLAG_HIDDEN);

            card.icon_bg = lv_obj_create(card.container);
            lv_obj_remove_style_all(card.icon_bg);
            lv_obj_set_size(card.icon_bg, k_notification_icon_size, k_notification_icon_size);
            lv_obj_set_style_radius(card.icon_bg, 15, 0);
            lv_obj_set_style_bg_color(card.icon_bg, lv_color_hex(0x2a2a30), 0);
            lv_obj_set_style_bg_opa(card.icon_bg, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(card.icon_bg, lv_color_hex(0xff8b93), 0);
            lv_obj_set_style_border_opa(card.icon_bg, LV_OPA_50, 0);
            lv_obj_set_style_border_width(card.icon_bg, 1, 0);
            lv_obj_set_style_shadow_color(card.icon_bg, lv_color_hex(0xff5e5b), 0);
            lv_obj_set_style_shadow_width(card.icon_bg, 14, 0);
            lv_obj_set_style_shadow_opa(card.icon_bg, static_cast<lv_opa_t>(90), 0);
            lv_obj_align(card.icon_bg, LV_ALIGN_TOP_LEFT, 26, 22);

            card.icon = lv_label_create(card.icon_bg);
            lv_obj_set_style_text_color(card.icon, lv_color_hex(k_text_primary), 0);
            lv_obj_set_style_text_align(card.icon, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_text(card.icon, "!");
            lv_obj_center(card.icon);

            card.time = lv_label_create(card.container);
            lv_obj_set_width(card.time, 54);
            lv_obj_set_style_text_align(card.time, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_style_text_color(card.time, lv_color_hex(k_text_dimmed), 0);
            lv_label_set_long_mode(card.time, LV_LABEL_LONG_MODE_DOTS);
            lv_label_set_text(card.time, "\xE7\x8E\xB0\xE5\x9C\xA8");
            lv_obj_align(card.time, LV_ALIGN_TOP_RIGHT, -28, 29);

            card.text_box = lv_obj_create(card.container);
            lv_obj_remove_style_all(card.text_box);
            lv_obj_set_size(card.text_box, k_glass_text_w, 52);
            lv_obj_set_flex_flow(card.text_box, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(card.text_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            lv_obj_set_style_pad_row(card.text_box, 4, 0);
            lv_obj_set_pos(card.text_box, k_glass_text_x, k_glass_text_y);

            card.title = lv_label_create(card.text_box);
            lv_obj_set_width(card.title, k_glass_text_w);
            lv_obj_set_style_text_color(card.title, lv_color_hex(k_text_primary), 0);
            lv_label_set_long_mode(card.title, LV_LABEL_LONG_MODE_DOTS);
            lv_label_set_text(card.title, "");

            card.body = lv_label_create(card.text_box);
            lv_obj_set_width(card.body, k_glass_text_w);
            lv_obj_set_style_text_color(card.body, lv_color_hex(0xd8d8de), 0);
            lv_label_set_long_mode(card.body, LV_LABEL_LONG_MODE_DOTS);
            lv_label_set_text(card.body, "");

            if (i == 0) {
                for (uint8_t dot = 0; dot < k_notification_indicator_dot_count; ++dot) {
                    notification_indicator_dots_[dot] = lv_obj_create(card_layer_);
                    lv_obj_remove_style_all(notification_indicator_dots_[dot]);
                    lv_obj_set_size(
                        notification_indicator_dots_[dot],
                        k_notification_indicator_dot_size,
                        k_notification_indicator_dot_size
                    );
                    lv_obj_set_style_radius(notification_indicator_dots_[dot], LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(notification_indicator_dots_[dot], lv_color_hex(k_text_dimmed), 0);
                    lv_obj_set_style_bg_opa(notification_indicator_dots_[dot], LV_OPA_40, 0);
                    lv_obj_align(
                        notification_indicator_dots_[dot],
                        LV_ALIGN_BOTTOM_MID,
                        ((int16_t)dot - 1) * 12,
                        -35
                    );
                    lv_obj_add_flag(notification_indicator_dots_[dot], LV_OBJ_FLAG_HIDDEN);
                }
            }

        }

        for (uint8_t i = 0; i < k_overview_visible_row_count; ++i) {
            overview_row_labels_[i] = lv_label_create(card_layer_);
            lv_obj_remove_style_all(overview_row_labels_[i]);
            lv_obj_set_size(overview_row_labels_[i], k_overview_row_w, k_overview_row_h);
            lv_obj_set_style_radius(overview_row_labels_[i], 18, 0);
            lv_obj_set_style_bg_color(overview_row_labels_[i], lv_color_hex(k_card_bg_color), 0);
            lv_obj_set_style_bg_opa(overview_row_labels_[i], static_cast<lv_opa_t>(196), 0);
            lv_obj_set_style_border_color(overview_row_labels_[i], lv_color_hex(k_card_border_color), 0);
            lv_obj_set_style_border_opa(overview_row_labels_[i], LV_OPA_30, 0);
            lv_obj_set_style_border_width(overview_row_labels_[i], 1, 0);
            lv_obj_set_style_shadow_color(overview_row_labels_[i], lv_color_hex(k_card_shadow_color), 0);
            lv_obj_set_style_shadow_width(overview_row_labels_[i], 10, 0);
            lv_obj_set_style_shadow_opa(overview_row_labels_[i], LV_OPA_20, 0);
            lv_obj_set_style_shadow_offset_y(overview_row_labels_[i], 3, 0);
            lv_obj_set_style_pad_top(overview_row_labels_[i], 6, 0);
            lv_obj_set_style_pad_bottom(overview_row_labels_[i], 6, 0);
            lv_obj_set_style_pad_left(overview_row_labels_[i], 14, 0);
            lv_obj_set_style_pad_right(overview_row_labels_[i], 14, 0);
            lv_obj_set_style_text_color(overview_row_labels_[i], lv_color_hex(k_text_primary), 0);
            lv_obj_set_style_text_opa(overview_row_labels_[i], LV_OPA_COVER, 0);
            lv_label_set_long_mode(overview_row_labels_[i], LV_LABEL_LONG_MODE_DOTS);
            lv_label_set_text(overview_row_labels_[i], "");
            lv_obj_clear_flag(overview_row_labels_[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_align(
                overview_row_labels_[i],
                LV_ALIGN_TOP_MID,
                0,
                k_overview_y_start + (int32_t)i * k_overview_row_pitch
            );
            lv_obj_add_flag(overview_row_labels_[i], LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_add_flag(card_layer_, LV_OBJ_FLAG_HIDDEN);
    }

    static void CardLayerSetY(void* obj, int32_t y) {
        lv_obj_set_y(static_cast<lv_obj_t*>(obj), y);
    }

    static int16_t ClampCardY(int32_t value, int16_t min_value, int16_t max_value) {
        return (int16_t)std::min<int32_t>(std::max<int32_t>(value, min_value), max_value);
    }

    static int16_t HiddenCardLayerY(xiaoxin_card_page_t page, int16_t screen_height) {
        if (page == XIAOXIN_CARD_PAGE_NOTIFICATIONS) {
            return (int16_t)-screen_height;
        }
        if (page == XIAOXIN_CARD_PAGE_OVERVIEW) {
            return screen_height;
        }
        return 0;
    }

    static int16_t DragCardLayerY(
        xiaoxin_card_page_t current,
        xiaoxin_card_page_t target,
        int16_t offset,
        int16_t screen_height,
        int16_t max_drag_px
    ) {
        const int32_t drag_limit = std::max<int16_t>(max_drag_px, 1);
        const int32_t abs_offset = std::min<int32_t>(std::abs(offset), drag_limit);
        const int32_t travel = (int32_t)screen_height;
        const int32_t revealed = (travel * abs_offset) / drag_limit;

        if (current == XIAOXIN_CARD_PAGE_HOME) {
            if (target == XIAOXIN_CARD_PAGE_NOTIFICATIONS) {
                return ClampCardY(-travel + revealed, -screen_height, 0);
            }
            if (target == XIAOXIN_CARD_PAGE_OVERVIEW) {
                return ClampCardY(travel - revealed, 0, screen_height);
            }
            return 0;
        }

        if (current == XIAOXIN_CARD_PAGE_NOTIFICATIONS) {
            return target == XIAOXIN_CARD_PAGE_HOME
                ? ClampCardY(-revealed, -screen_height, 0)
                : 0;
        }

        if (current == XIAOXIN_CARD_PAGE_OVERVIEW) {
            return target == XIAOXIN_CARD_PAGE_HOME
                ? ClampCardY(revealed, 0, screen_height)
                : 0;
        }

        return 0;
    }

    static uint32_t CardReleaseDurationMs(int16_t from_y, int16_t to_y, int16_t screen_height) {
        const int32_t distance = std::abs((int32_t)to_y - (int32_t)from_y);
        const int32_t denominator = std::max<int16_t>(screen_height, 1);
        const int32_t span = (int32_t)k_card_snap_max_anim_ms - (int32_t)k_card_snap_min_anim_ms;
        const int32_t scaled = (distance * span) / denominator;
        return (uint32_t)std::min<int32_t>(
            k_card_snap_max_anim_ms,
            std::max<int32_t>(k_card_snap_min_anim_ms, k_card_snap_min_anim_ms + scaled)
        );
    }

    static lv_opa_t DragCardLayerOpacity(
        xiaoxin_card_page_t current,
        int16_t offset,
        int16_t threshold_px
    ) {
        if (current != XIAOXIN_CARD_PAGE_HOME) {
            return LV_OPA_COVER;
        }

        const int32_t threshold = std::max<int16_t>(threshold_px, 1);
        const int32_t progress = std::min<int32_t>((std::abs(offset) * 255) / threshold, 255);
        return (lv_opa_t)std::min<int32_t>(70 + progress, 255);
    }

    static void AddFlagIfCreated(lv_obj_t* obj, lv_obj_flag_t flag) {
        if (obj != nullptr) {
            lv_obj_add_flag(obj, flag);
        }
    }

    static void RemoveFlagIfCreated(lv_obj_t* obj, lv_obj_flag_t flag) {
        if (obj != nullptr) {
            lv_obj_remove_flag(obj, flag);
        }
    }

    void UpdateNotificationIndicatorDots(uint8_t index, uint8_t total, bool prepare_entry_animation) {
        const uint8_t visible = std::min<uint8_t>(total, k_notification_indicator_dot_count);
        if (visible <= 1) {
            for (uint8_t i = 0; i < k_notification_indicator_dot_count; ++i) {
                AddFlagIfCreated(notification_indicator_dots_[i], LV_OBJ_FLAG_HIDDEN);
            }
            return;
        }

        const uint8_t active = std::min<uint8_t>(index, (uint8_t)(visible - 1));
        const int16_t gap = 12;
        const int16_t center_offset = (int16_t)((visible - 1) * gap / 2);
        for (uint8_t i = 0; i < k_notification_indicator_dot_count; ++i) {
            lv_obj_t* dot = notification_indicator_dots_[i];
            if (dot == nullptr) {
                continue;
            }
            if (i >= visible) {
                lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
                continue;
            }

            const bool selected = i == active;
            lv_obj_set_style_bg_color(dot, lv_color_hex(selected ? k_title_accent : k_text_dimmed), 0);
            lv_obj_set_style_bg_opa(dot, selected ? LV_OPA_COVER : LV_OPA_60, 0);
            lv_obj_set_style_shadow_color(dot, lv_color_hex(k_title_accent), 0);
            lv_obj_set_style_shadow_width(dot, selected ? 8 : 0, 0);
            lv_obj_set_style_shadow_opa(dot, selected ? LV_OPA_30 : LV_OPA_TRANSP, 0);
            lv_obj_set_style_opa(
                dot,
                prepare_entry_animation
                    ? static_cast<lv_opa_t>(LV_OPA_0)
                    : static_cast<lv_opa_t>(LV_OPA_COVER),
                0
            );
            lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, (int16_t)(i * gap) - center_offset, -35);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(dot);
        }
    }

    static uint32_t DotColorForPriority(uint32_t priority) {
        if (priority <= 1) {
            return k_dot_color_urgent;
        }
        if (priority == 2) {
            return k_dot_color_warning;
        }
        return k_dot_color_info;
    }

    static int16_t NotificationMinScrollY(uint8_t total) {
        if (total <= 1) {
            return 0;
        }
        return (int16_t)-((int16_t)(total - 1) * k_notification_slide_pitch);
    }

    static int16_t ClampNotificationScrollY(int16_t scroll_y, uint8_t total) {
        const int16_t min_scroll = NotificationMinScrollY(total);
        return std::max<int16_t>(min_scroll, std::min<int16_t>(0, scroll_y));
    }

    static int16_t NotificationScrollDisplayY(int16_t raw_scroll_y, uint8_t total) {
        const int16_t min_scroll = NotificationMinScrollY(total);
        if (raw_scroll_y > 0) {
            return (int16_t)(raw_scroll_y / 3);
        }
        if (raw_scroll_y < min_scroll) {
            return (int16_t)(min_scroll + (raw_scroll_y - min_scroll) / 3);
        }
        return raw_scroll_y;
    }

    static uint8_t NotificationIndexForScroll(int16_t scroll_y, uint8_t total) {
        if (total == 0) {
            return 0;
        }
        const int16_t clamped = ClampNotificationScrollY(scroll_y, total);
        const int16_t distance = (int16_t)-clamped;
        uint8_t index = (uint8_t)((distance + k_notification_slide_pitch / 2) / k_notification_slide_pitch);
        return std::min<uint8_t>(index, (uint8_t)(total - 1));
    }

    void PopulateNotificationCard(GlassCard& card, const xiaoxin_card_item_t* item) {
        if (card.container == nullptr || item == nullptr) {
            return;
        }

        lv_obj_set_size(card.container, k_glass_width, k_glass_height);
        lv_obj_set_style_radius(card.container, k_glass_radius, 0);
        lv_obj_set_style_border_color(card.container, lv_color_hex(k_card_border_color), 0);
        lv_obj_set_style_border_width(card.container, 1, 0);
        lv_obj_set_style_pad_top(card.container, k_glass_pad_v, 0);
        lv_obj_set_style_pad_bottom(card.container, k_glass_pad_v, 0);
        lv_obj_set_style_pad_left(card.container, k_glass_pad_h, 0);
        lv_obj_set_style_pad_right(card.container, k_glass_pad_h, 0);

        if (card.icon_bg != nullptr) {
            lv_obj_set_style_bg_color(card.icon_bg, lv_color_hex(DotColorForPriority(item->priority)), 0);
        }
        if (card.icon != nullptr) {
            lv_label_set_text(card.icon, "!");
        }
        if (card.time != nullptr) {
            lv_label_set_text(card.time, "\xE7\x8E\xB0\xE5\x9C\xA8");
        }
        if (card.title != nullptr) {
            lv_label_set_text(card.title, item->title);
        }
        if (card.body != nullptr) {
            lv_label_set_text(card.body, item->body);
        }

        lv_obj_remove_flag(card.container, LV_OBJ_FLAG_HIDDEN);
    }

    lv_opa_t NotificationCardOpacityForSlot(uint8_t slot, int16_t scroll_y) const {
        const int16_t y_offset = (int16_t)((int16_t)slot * k_notification_slide_pitch + scroll_y);
        const int16_t distance_from_center = std::min<int16_t>(
            (int16_t)std::abs(y_offset),
            (int16_t)(k_notification_slide_pitch * 2)
        );
        const int32_t opacity = LV_OPA_COVER - ((int32_t)distance_from_center * 90) / k_notification_slide_pitch;
        return (lv_opa_t)std::max<int32_t>(LV_OPA_60, std::min<int32_t>(LV_OPA_COVER, opacity));
    }

    void ApplyNotificationScrollVisual(
        int16_t scroll_y,
        bool prepare_entry_animation = false,
        bool lightweight_drag = false
    ) {
        const int64_t perf_start_us = k_ui_perf_trace_enabled ? esp_timer_get_time() : 0;
        const uint8_t total = xiaoxin_card_pager_notification_count(&card_pager_);
        const uint8_t active_index = NotificationIndexForScroll(scroll_y, total);

        if (lightweight_drag) {
            if (!notification_drag_visual_owns_cards_) {
                for (uint8_t slot = 0; slot < k_card_glass_count; ++slot) {
                    GlassCard& card = glass_cards_[slot];
                    if (card.container == nullptr || lv_obj_has_flag(card.container, LV_OBJ_FLAG_HIDDEN)) {
                        continue;
                    }
                    lv_anim_delete(card.container, nullptr);
                }
                notification_drag_visual_owns_cards_ = true;
            }
        } else {
            notification_drag_visual_owns_cards_ = false;
        }

        for (uint8_t slot = 0; slot < k_card_glass_count; ++slot) {
            GlassCard& card = glass_cards_[slot];
            if (card.container == nullptr || lv_obj_has_flag(card.container, LV_OBJ_FLAG_HIDDEN)) {
                continue;
            }

            const int16_t y_offset = (int16_t)((int16_t)slot * k_notification_slide_pitch + scroll_y);
            const bool near_center = slot == active_index;
            const lv_opa_t target_opa = prepare_entry_animation
                ? static_cast<lv_opa_t>(LV_OPA_0)
                : NotificationCardOpacityForSlot(slot, scroll_y);

            lv_obj_align(card.container, LV_ALIGN_TOP_MID, 0, k_glass_y_start + y_offset);
            lv_obj_set_style_opa(card.container, target_opa, 0);
            if (!lightweight_drag) {
                lv_obj_set_style_bg_opa(card.container, near_center ? static_cast<lv_opa_t>(174) : static_cast<lv_opa_t>(82), 0);
                lv_obj_set_style_border_opa(card.container, near_center ? static_cast<lv_opa_t>(44) : static_cast<lv_opa_t>(18), 0);
            }
        }

        if (!lightweight_drag) {
            UpdateNotificationIndicatorDots(active_index, total, prepare_entry_animation);
        }
        if (!lightweight_drag) {
            for (uint8_t slot = 0; slot < k_card_glass_count; ++slot) {
                if (slot != active_index && glass_cards_[slot].container != nullptr) {
                    lv_obj_move_foreground(glass_cards_[slot].container);
                }
            }
            if (active_index < k_card_glass_count &&
                glass_cards_[active_index].container != nullptr &&
                !lv_obj_has_flag(glass_cards_[active_index].container, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_move_foreground(glass_cards_[active_index].container);
            }
            for (uint8_t i = 0; i < k_notification_indicator_dot_count; ++i) {
                lv_obj_t* dot = notification_indicator_dots_[i];
                if (dot != nullptr && !lv_obj_has_flag(dot, LV_OBJ_FLAG_HIDDEN)) {
                    lv_obj_move_foreground(dot);
                }
            }
        }
        if (k_ui_perf_trace_enabled) {
            AddUiPerfSample(
                ui_perf_drag_calls_,
                ui_perf_drag_total_us_,
                ui_perf_drag_max_us_,
                (uint32_t)(esp_timer_get_time() - perf_start_us)
            );
        }
    }

    void RenderNotificationCards(const xiaoxin_card_item_t* /*items*/, uint8_t count, bool prepare_entry_animation) {
        notification_scroll_y_ = ClampNotificationScrollY(notification_scroll_y_, count);
        for (uint8_t slot = 0; slot < k_card_glass_count; ++slot) {
            GlassCard& card = glass_cards_[slot];
            if (card.container == nullptr) {
                continue;
            }

            const xiaoxin_card_item_t* item = xiaoxin_card_pager_notification_at(&card_pager_, slot);
            if (item == nullptr || slot >= count) {
                card.visible_index = 0xff;
                lv_obj_add_flag(card.container, LV_OBJ_FLAG_HIDDEN);
                continue;
            }

            card.visible_index = slot;
            PopulateNotificationCard(card, item);
        }

        const bool empty = xiaoxin_card_pager_notification_empty(&card_pager_);
        if (empty) {
            AddFlagIfCreated(notification_clear_button_, LV_OBJ_FLAG_HIDDEN);
            RemoveFlagIfCreated(notification_empty_panel_, LV_OBJ_FLAG_HIDDEN);
            RemoveFlagIfCreated(notification_empty_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            RemoveFlagIfCreated(notification_clear_button_, LV_OBJ_FLAG_HIDDEN);
            if (notification_clear_button_ != nullptr) {
                lv_obj_align(notification_clear_button_, LV_ALIGN_TOP_MID, 0, k_notification_clear_button_y);
                lv_obj_move_foreground(notification_clear_button_);
            }
            AddFlagIfCreated(notification_empty_panel_, LV_OBJ_FLAG_HIDDEN);
            AddFlagIfCreated(notification_empty_label_, LV_OBJ_FLAG_HIDDEN);
        }

        ApplyNotificationScrollVisual(notification_scroll_y_, prepare_entry_animation);
    }

    static void NotificationScrollSetY(void* obj, int32_t scroll_y) {
        auto* self = static_cast<PaopaoPetDisplay*>(obj);
        if (self != nullptr) {
            self->ApplyNotificationScrollVisual((int16_t)scroll_y, false, true);
        }
    }

    static void NotificationDismissSetX(void* obj, int32_t x) {
        lv_obj_set_x(static_cast<lv_obj_t*>(obj), (lv_coord_t)x);
    }

    static void NotificationDismissSetOpacity(void* obj, int32_t opa) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), (lv_opa_t)opa, 0);
    }

    static void NotificationDismissAnimationCompleted(lv_anim_t* anim) {
        auto* self = static_cast<PaopaoPetDisplay*>(lv_anim_get_user_data(anim));
        if (self == nullptr) {
            return;
        }

        const uint8_t visible_index = self->notification_animating_visible_index_;
        self->notification_dismiss_animating_ = false;
        self->notification_animating_visible_index_ = 0xff;
        self->notification_card_drag_x_ = 0;
        self->notification_pressed_slot_ = -1;
        self->notification_pressed_visible_index_ = 0xff;

        if (visible_index != 0xff) {
            xiaoxin_card_pager_notification_dismiss(&self->card_pager_, visible_index);
        }
        self->SyncNotificationMaintenanceTimerLocked();
        self->notification_scroll_y_ = ClampNotificationScrollY(
            self->notification_scroll_y_,
            xiaoxin_card_pager_notification_count(&self->card_pager_)
        );
        self->RenderNotificationPageAfterDataChange();
    }

    static void NotificationDismissReboundCompleted(lv_anim_t* anim) {
        auto* self = static_cast<PaopaoPetDisplay*>(lv_anim_get_user_data(anim));
        if (self == nullptr) {
            return;
        }

        self->notification_dismiss_animating_ = false;
        self->notification_animating_visible_index_ = 0xff;
        self->notification_card_drag_x_ = 0;
        self->notification_pressed_slot_ = -1;
        self->notification_pressed_visible_index_ = 0xff;
        self->ApplyNotificationScrollVisual(self->notification_scroll_y_);
        self->RaiseOverlayObjects();
    }

    void AnimateNotificationDismiss(int8_t slot, uint8_t visible_index) {
        if (slot < 0 || slot >= (int8_t)k_card_glass_count) {
            return;
        }
        GlassCard& card = glass_cards_[slot];
        if (card.container == nullptr) {
            return;
        }

        notification_dismiss_animating_ = true;
        notification_animating_visible_index_ = visible_index;
        notification_drag_visual_owns_cards_ = false;

        lv_anim_delete(card.container, NotificationDismissSetX);
        lv_anim_delete(card.container, NotificationDismissSetOpacity);

        const int32_t start_x = notification_card_drag_x_;
        const int32_t start_opa = std::max<int32_t>(
            LV_OPA_30,
            LV_OPA_COVER + ((int32_t)notification_card_drag_x_ * LV_OPA_COVER) / DISPLAY_WIDTH
        );

        lv_anim_t x_anim;
        lv_anim_init(&x_anim);
        lv_anim_set_var(&x_anim, card.container);
        lv_anim_set_values(&x_anim, start_x, -DISPLAY_WIDTH);
        lv_anim_set_time(&x_anim, k_notification_dismiss_fly_ms);
        lv_anim_set_path_cb(&x_anim, lv_anim_path_ease_in);
        lv_anim_set_exec_cb(&x_anim, NotificationDismissSetX);
        lv_anim_set_completed_cb(&x_anim, NotificationDismissAnimationCompleted);
        lv_anim_set_user_data(&x_anim, this);
        lv_anim_start(&x_anim);

        lv_anim_t opa_anim;
        lv_anim_init(&opa_anim);
        lv_anim_set_var(&opa_anim, card.container);
        lv_anim_set_values(&opa_anim, start_opa, LV_OPA_0);
        lv_anim_set_time(&opa_anim, k_notification_dismiss_fly_ms);
        lv_anim_set_path_cb(&opa_anim, lv_anim_path_ease_in);
        lv_anim_set_exec_cb(&opa_anim, NotificationDismissSetOpacity);
        lv_anim_start(&opa_anim);
    }

    void AnimateNotificationDismissRebound(int8_t slot) {
        if (slot < 0 || slot >= (int8_t)k_card_glass_count) {
            return;
        }
        GlassCard& card = glass_cards_[slot];
        if (card.container == nullptr) {
            return;
        }

        notification_dismiss_animating_ = true;
        notification_animating_visible_index_ = 0xff;
        notification_drag_visual_owns_cards_ = false;

        lv_anim_delete(card.container, NotificationDismissSetX);
        lv_anim_delete(card.container, NotificationDismissSetOpacity);

        const int32_t start_x = notification_card_drag_x_;
        const int32_t start_opa = std::max<int32_t>(
            LV_OPA_30,
            LV_OPA_COVER + ((int32_t)notification_card_drag_x_ * LV_OPA_COVER) / DISPLAY_WIDTH
        );

        lv_anim_t x_anim;
        lv_anim_init(&x_anim);
        lv_anim_set_var(&x_anim, card.container);
        lv_anim_set_values(&x_anim, start_x, 0);
        lv_anim_set_time(&x_anim, k_notification_dismiss_rebound_ms);
        lv_anim_set_path_cb(&x_anim, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&x_anim, NotificationDismissSetX);
        lv_anim_set_completed_cb(&x_anim, NotificationDismissReboundCompleted);
        lv_anim_set_user_data(&x_anim, this);
        lv_anim_start(&x_anim);

        lv_anim_t opa_anim;
        lv_anim_init(&opa_anim);
        lv_anim_set_var(&opa_anim, card.container);
        lv_anim_set_values(&opa_anim, start_opa, NotificationCardOpacityForSlot((uint8_t)slot, notification_scroll_y_));
        lv_anim_set_time(&opa_anim, k_notification_dismiss_rebound_ms);
        lv_anim_set_path_cb(&opa_anim, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&opa_anim, NotificationDismissSetOpacity);
        lv_anim_start(&opa_anim);
    }

    static void NotificationScrollAnimationCompleted(lv_anim_t* anim) {
        auto* self = static_cast<PaopaoPetDisplay*>(lv_anim_get_user_data(anim));
        if (self == nullptr) {
            return;
        }
        self->ApplyNotificationScrollVisual(self->notification_scroll_y_);
        self->RaiseOverlayObjects();
    }

    void AnimateNotificationScroll(int16_t start_scroll_y, int16_t target_scroll_y) {
        notification_scroll_y_ = target_scroll_y;
        lv_anim_delete(this, NotificationScrollSetY);
        ApplyNotificationScrollVisual(start_scroll_y);

        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, this);
        lv_anim_set_values(&anim, start_scroll_y, target_scroll_y);
        lv_anim_set_time(&anim, k_notification_switch_anim_ms);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&anim, NotificationScrollSetY);
        lv_anim_set_completed_cb(&anim, NotificationScrollAnimationCompleted);
        lv_anim_set_user_data(&anim, this);
        lv_anim_start(&anim);
    }

    static void PopulateOverviewTime(xiaoxin_overview_state_t& state) {
        time_t now = 0;
        time(&now);

        struct tm timeinfo = {};
        if (now > 24 * 60 * 60 &&
            localtime_r(&now, &timeinfo) != nullptr &&
            timeinfo.tm_year >= 120) {
            state.time_valid = true;
            state.hour = timeinfo.tm_hour;
            state.minute = timeinfo.tm_min;
            state.month = timeinfo.tm_mon + 1;
            state.day = timeinfo.tm_mday;
            state.weekday = (uint8_t)timeinfo.tm_wday;
        }
    }

    xiaoxin_overview_state_t BuildOverviewState() {
        xiaoxin_overview_state_t state = {};
        PopulateOverviewTime(state);

        state.network_connected = SystemOverlayNetworkState() == XIAOXIN_SYSTEM_OVERLAY_NETWORK_CONNECTED;
        state.battery_power_source = XIAOXIN_BATTERY_POWER_UNKNOWN;
        state.battery_known = false;
        if (overview_battery_has_snapshot_) {
            state.battery_state = overview_battery_snapshot_.state;
            state.battery_power_source = overview_battery_snapshot_.power_source;
            state.battery_percent = overview_battery_snapshot_.display_percent;
            state.battery_level = overview_battery_snapshot_.display_level;
            state.battery_known = overview_battery_snapshot_.percent_reliable;
        }

        state.weather_configured = overview_weather_configured_;
        state.weather_available = overview_weather_available_;
        state.weather_summary = overview_weather_summary_.empty() ? nullptr : overview_weather_summary_.c_str();
        state.weather_detail = overview_weather_detail_.empty() ? nullptr : overview_weather_detail_.c_str();

        state.course_configured = overview_course_configured_;
        state.course_available_today = overview_course_available_today_;
        state.course_title = overview_course_title_.empty() ? nullptr : overview_course_title_.c_str();
        state.course_detail = overview_course_detail_.empty() ? nullptr : overview_course_detail_.c_str();

        state.todo_configured = overview_todo_configured_;
        state.todo_count = overview_todo_count_;
        state.todo_detail = overview_todo_detail_.empty() ? nullptr : overview_todo_detail_.c_str();

        state.companion_available = overview_companion_available_;
        state.xiaoxin_age = overview_xiaoxin_age_;
        state.growth_summary = overview_growth_summary_.empty() ? nullptr : overview_growth_summary_.c_str();

        return state;
    }

    xiaoxin_system_overlay_network_state_t SystemOverlayNetworkState() const {
        if (Application::GetInstance().GetDeviceState() == kDeviceStateWifiConfiguring) {
            return XIAOXIN_SYSTEM_OVERLAY_NETWORK_CONFIGURING;
        }
        if (network_icon_ == nullptr || std::strcmp(network_icon_, FONT_AWESOME_WIFI_SLASH) == 0) {
            return XIAOXIN_SYSTEM_OVERLAY_NETWORK_DISCONNECTED;
        }
        return XIAOXIN_SYSTEM_OVERLAY_NETWORK_CONNECTED;
    }

    void SyncNetworkStatusLocked() {
        const auto network_state = SystemOverlayNetworkState();
        SyncNetworkNotificationLocked(network_state);
    }

    void SyncPetMoodNetworkStateLocked() {
        const uint32_t now_ms = NowMs();
        const bool wifi_connected =
            SystemOverlayNetworkState() == XIAOXIN_SYSTEM_OVERLAY_NETWORK_CONNECTED;
        if (wifi_connected != mood_.wifi_connected) {
            DispatchPetMoodEventLocked(
                wifi_connected
                    ? PAOPAO_PET_MOOD_EVENT_WIFI_CONNECTED
                    : PAOPAO_PET_MOOD_EVENT_WIFI_DISCONNECTED,
                PAOPAO_PET_TRIGGER_NONE,
                now_ms
            );
        }
    }

    void ApplyCardEntryAnimation(xiaoxin_card_page_t page) {
        if (page == XIAOXIN_CARD_PAGE_NOTIFICATIONS) {
            for (uint8_t i = 0; i < k_card_glass_count; ++i) {
                GlassCard& card = glass_cards_[i];
                if (card.container == nullptr || lv_obj_has_flag(card.container, LV_OBJ_FLAG_HIDDEN)) {
                    continue;
                }

                lv_anim_t anim;
                lv_anim_init(&anim);
                lv_anim_set_var(&anim, card.container);
                lv_anim_set_values(&anim, LV_OPA_0, LV_OPA_COVER);
                lv_anim_set_time(&anim, k_entry_fade_ms);
                lv_anim_set_delay(&anim, (uint32_t)i * k_entry_stagger_ms);
                lv_anim_set_exec_cb(&anim, [](void* obj, int32_t opacity) {
                    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), (lv_opa_t)opacity, 0);
                });
                lv_anim_start(&anim);
            }
            return;
        }

        if (page == XIAOXIN_CARD_PAGE_OVERVIEW) {
            for (uint8_t i = 0; i < k_overview_visible_row_count; ++i) {
                if (overview_row_labels_[i] == nullptr ||
                    lv_obj_has_flag(overview_row_labels_[i], LV_OBJ_FLAG_HIDDEN)) {
                    continue;
                }

                lv_anim_t anim;
                lv_anim_init(&anim);
                lv_anim_set_var(&anim, overview_row_labels_[i]);
                lv_anim_set_values(&anim, LV_OPA_0, LV_OPA_COVER);
                lv_anim_set_time(&anim, k_entry_fade_ms);
                lv_anim_set_delay(&anim, (uint32_t)i * k_entry_stagger_ms);
                lv_anim_set_exec_cb(&anim, [](void* obj, int32_t opacity) {
                    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), (lv_opa_t)opacity, 0);
                });
                lv_anim_start(&anim);
            }
        }
    }

    static void CardLayerAnimationCompleted(lv_anim_t* anim) {
        auto* self = static_cast<PaopaoPetDisplay*>(lv_anim_get_user_data(anim));
        if (self == nullptr || self->card_layer_ == nullptr) {
            return;
        }
        lv_obj_set_y(self->card_layer_, 0);
        lv_obj_set_style_opa(self->card_layer_, LV_OPA_COVER, 0);
        if (xiaoxin_card_pager_current_page(&self->card_pager_) == XIAOXIN_CARD_PAGE_HOME) {
            lv_obj_add_flag(self->card_layer_, LV_OBJ_FLAG_HIDDEN);
        } else if (xiaoxin_card_pager_animation(&self->card_pager_) == XIAOXIN_CARD_ANIMATION_SNAP) {
            self->ApplyCardEntryAnimation(xiaoxin_card_pager_current_page(&self->card_pager_));
        }
        self->ApplyPetAnimationForCardPager();
        self->RaiseOverlayObjects();
    }

    static void CardLayerDragAnimationCompleted(lv_anim_t* anim) {
        auto* self = static_cast<PaopaoPetDisplay*>(lv_anim_get_user_data(anim));
        if (self == nullptr || self->card_layer_ == nullptr) {
            return;
        }
        lv_obj_set_style_opa(self->card_layer_, LV_OPA_COVER, 0);
        if (xiaoxin_card_pager_current_page(&self->card_pager_) == XIAOXIN_CARD_PAGE_HOME) {
            lv_obj_add_flag(self->card_layer_, LV_OBJ_FLAG_HIDDEN);
        }
        self->ApplyPetAnimationForCardPager();
        self->RaiseOverlayObjects();
    }

    void RenderCardPage(xiaoxin_card_page_t page, bool prepare_entry_animation = false) {
        if (card_layer_ == nullptr) {
            return;
        }

        notification_drag_visual_owns_cards_ = false;
        rendered_card_page_ = page;
        card_page_rendered_ = true;

        for (uint8_t i = 0; i < k_card_glass_count; ++i) {
            AddFlagIfCreated(glass_cards_[i].container, LV_OBJ_FLAG_HIDDEN);
            AddFlagIfCreated(glass_cards_[i].pager, LV_OBJ_FLAG_HIDDEN);
        }
        for (uint8_t i = 0; i < k_notification_indicator_dot_count; ++i) {
            AddFlagIfCreated(notification_indicator_dots_[i], LV_OBJ_FLAG_HIDDEN);
        }
        for (uint8_t i = 0; i < k_overview_visible_row_count; ++i) {
            AddFlagIfCreated(overview_row_labels_[i], LV_OBJ_FLAG_HIDDEN);
        }
        AddFlagIfCreated(card_title_label_, LV_OBJ_FLAG_HIDDEN);
        AddFlagIfCreated(overview_time_label_, LV_OBJ_FLAG_HIDDEN);
        AddFlagIfCreated(overview_date_label_, LV_OBJ_FLAG_HIDDEN);
        AddFlagIfCreated(pull_indicator_, LV_OBJ_FLAG_HIDDEN);
        AddFlagIfCreated(home_indicator_, LV_OBJ_FLAG_HIDDEN);
        AddFlagIfCreated(notification_clear_button_, LV_OBJ_FLAG_HIDDEN);
        AddFlagIfCreated(notification_empty_panel_, LV_OBJ_FLAG_HIDDEN);
        AddFlagIfCreated(notification_empty_label_, LV_OBJ_FLAG_HIDDEN);

        if (page == XIAOXIN_CARD_PAGE_HOME) {
            lv_obj_add_flag(card_layer_, LV_OBJ_FLAG_HIDDEN);
            rendered_card_page_ = XIAOXIN_CARD_PAGE_HOME;
            rendered_card_prepare_entry_ = false;
            ApplyPetAnimationForCardPager();
            RaiseOverlayObjects();
            return;
        }

        lv_obj_remove_flag(card_layer_, LV_OBJ_FLAG_HIDDEN);
        ApplyPetAnimationForCardPager();

        if (page == XIAOXIN_CARD_PAGE_NOTIFICATIONS) {
            RemoveFlagIfCreated(pull_indicator_, LV_OBJ_FLAG_HIDDEN);
            RemoveFlagIfCreated(home_indicator_, LV_OBJ_FLAG_HIDDEN);
            if (home_indicator_ != nullptr) {
                lv_obj_align(home_indicator_, LV_ALIGN_BOTTOM_MID, 0, -8);
            }

            RenderNotificationCards(
                nullptr,
                xiaoxin_card_pager_notification_count(&card_pager_),
                prepare_entry_animation
            );
        } else if (page == XIAOXIN_CARD_PAGE_OVERVIEW) {
            xiaoxin_overview_state_t overview_state = BuildOverviewState();
            xiaoxin_overview_model_build(&overview_state, &overview_snapshot_);

            if (overview_time_label_ != nullptr) {
                lv_label_set_text(overview_time_label_, overview_snapshot_.time_text);
                lv_obj_align(overview_time_label_, LV_ALIGN_TOP_MID, 0, k_overview_time_y);
                lv_obj_remove_flag(overview_time_label_, LV_OBJ_FLAG_HIDDEN);
            }
            if (overview_date_label_ != nullptr) {
                lv_label_set_text(overview_date_label_, overview_snapshot_.date_text);
                lv_obj_align(overview_date_label_, LV_ALIGN_TOP_MID, 0, k_overview_date_y);
                lv_obj_remove_flag(overview_date_label_, LV_OBJ_FLAG_HIDDEN);
            }
            RemoveFlagIfCreated(home_indicator_, LV_OBJ_FLAG_HIDDEN);
            if (home_indicator_ != nullptr) {
                lv_obj_align(home_indicator_, LV_ALIGN_TOP_MID, 0, 8);
            }

            const xiaoxin_card_item_t* items = overview_snapshot_.items;
            const uint8_t count = overview_snapshot_.item_count;
            const uint8_t visible = std::min<uint8_t>(count, k_overview_visible_row_count);
            for (uint8_t i = 0; i < visible && items != nullptr; ++i) {
                if (overview_row_labels_[i] == nullptr) {
                    continue;
                }
                char row_text[160] = {};
                std::snprintf(
                    row_text,
                    sizeof(row_text),
                    "%s  %s\n%s",
                    items[i].title != nullptr ? items[i].title : "",
                    items[i].body != nullptr ? items[i].body : "",
                    items[i].detail != nullptr ? items[i].detail : ""
                );
                lv_label_set_text(overview_row_labels_[i], row_text);
                lv_obj_set_style_opa(
                    overview_row_labels_[i],
                    prepare_entry_animation
                        ? static_cast<lv_opa_t>(LV_OPA_0)
                        : static_cast<lv_opa_t>(LV_OPA_COVER),
                    0
                );
                lv_obj_remove_flag(overview_row_labels_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }

        RaiseOverlayObjects();
        rendered_card_page_ = page;
        rendered_card_prepare_entry_ = prepare_entry_animation;
    }

    void RefreshOverviewPageIfVisible() {
        if (rendered_card_page_ != XIAOXIN_CARD_PAGE_OVERVIEW ||
            card_layer_ == nullptr ||
            lv_obj_has_flag(card_layer_, LV_OBJ_FLAG_HIDDEN)) {
            return;
        }

        card_page_rendered_ = false;
        RenderCardPage(XIAOXIN_CARD_PAGE_OVERVIEW, false);
    }

    void EnsureCardPageRendered(xiaoxin_card_page_t page, bool prepare_entry_animation = false) {
        const bool visible_page_is_hidden =
            page != XIAOXIN_CARD_PAGE_HOME &&
            card_layer_ != nullptr &&
            lv_obj_has_flag(card_layer_, LV_OBJ_FLAG_HIDDEN);
        const bool home_page_is_visible =
            page == XIAOXIN_CARD_PAGE_HOME &&
            card_layer_ != nullptr &&
            !lv_obj_has_flag(card_layer_, LV_OBJ_FLAG_HIDDEN);
        if (card_page_rendered_ &&
            rendered_card_page_ == page &&
            rendered_card_prepare_entry_ == prepare_entry_animation &&
            !home_page_is_visible &&
            !visible_page_is_hidden) {
            return;
        }

        RenderCardPage(page, prepare_entry_animation);
    }

    void RenderNotificationPageAfterDataChange() {
        card_page_rendered_ = false;
        RenderCardPage(XIAOXIN_CARD_PAGE_NOTIFICATIONS, false);
    }

    void ApplyCardPagerVisual() {
        if (card_layer_ == nullptr) {
            return;
        }

        const xiaoxin_card_page_t current = xiaoxin_card_pager_current_page(&card_pager_);
        const xiaoxin_card_page_t visual_page = xiaoxin_card_pager_visual_page(&card_pager_);

        EnsureCardPageRendered(visual_page, false);
        if (visual_page == XIAOXIN_CARD_PAGE_HOME) {
            return;
        }

        const int16_t offset = xiaoxin_card_pager_offset_y(&card_pager_);
        const xiaoxin_card_page_t target = xiaoxin_card_pager_target_page(&card_pager_);
        lv_anim_delete(card_layer_, CardLayerSetY);
        lv_obj_set_y(card_layer_, DragCardLayerY(
            current,
            target,
            offset,
            card_pager_.screen_height,
            card_pager_.max_drag_px
        ));
        lv_obj_set_style_opa(
            card_layer_,
            xiaoxin_card_pager_is_dragging(&card_pager_)
                ? DragCardLayerOpacity(current, offset, card_pager_.threshold_px)
                : static_cast<lv_opa_t>(LV_OPA_COVER),
            0
        );
        ApplyPetAnimationForCardPager();
    }

    void AnimateCardPagerRelease(
        xiaoxin_card_page_t visual_page,
        int16_t from_y,
        int16_t to_y,
        bool from_drag
    ) {
        if (card_layer_ == nullptr) {
            return;
        }
        notification_drag_visual_owns_cards_ = false;
        if (visual_page != XIAOXIN_CARD_PAGE_NOTIFICATIONS || from_drag) {
            EnsureCardPageRendered(visual_page, false);
        }
        if (visual_page == XIAOXIN_CARD_PAGE_HOME) {
            return;
        }

        lv_anim_delete(card_layer_, CardLayerSetY);
        lv_obj_set_y(card_layer_, from_y);
        lv_obj_set_style_opa(card_layer_, LV_OPA_COVER, 0);

        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, card_layer_);
        lv_anim_set_values(&anim, from_y, to_y);
        lv_anim_set_time(
            &anim,
            (!from_drag && visual_page == XIAOXIN_CARD_PAGE_NOTIFICATIONS)
                ? k_notification_switch_anim_ms
                : CardReleaseDurationMs(from_y, to_y, card_pager_.screen_height)
        );
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&anim, CardLayerSetY);
        lv_anim_set_completed_cb(&anim, from_drag ? CardLayerDragAnimationCompleted : CardLayerAnimationCompleted);
        lv_anim_set_user_data(&anim, this);
        lv_anim_start(&anim);
    }

    void SetCardPagerPage(xiaoxin_card_page_t page) {
        notification_drag_visual_owns_cards_ = false;
        card_pager_.current_page = page;
        card_pager_.target_page = page;
        card_pager_.animation = XIAOXIN_CARD_ANIMATION_SNAP;
        card_pager_.offset_y = 0;
        card_pager_.pressed = false;
        card_pager_.dragging = false;
        ApplyCardPagerVisual();
        ApplyPetAnimationForCardPager();
    }

    void CopyPetFrameToScreen(const lv_img_dsc_t* frame, uint32_t image_scale) {
        if (pet_frame_buffer_ == nullptr || frame == nullptr || frame->data == nullptr) {
            return;
        }

        const int64_t perf_start_us = k_ui_perf_trace_enabled ? esp_timer_get_time() : 0;

        std::fill_n(pet_frame_buffer_, DISPLAY_WIDTH * DISPLAY_HEIGHT, k_white_rgb565);

        const uint16_t src_w = frame->header.w;
        const uint16_t src_h = frame->header.h;
        if (src_w == 0 || src_h == 0 || image_scale == 0) {
            return;
        }

        uint32_t draw_w = ((uint32_t)src_w * image_scale + 128) / 256;
        uint32_t draw_h = ((uint32_t)src_h * image_scale + 128) / 256;
        if (draw_w == 0 || draw_h == 0) {
            return;
        }

        const int32_t dst_start_x = ((int32_t)DISPLAY_WIDTH - (int32_t)draw_w) / 2;
        const int32_t dst_start_y = ((int32_t)DISPLAY_HEIGHT - (int32_t)draw_h) / 2;
        const uint16_t* src = reinterpret_cast<const uint16_t*>(frame->data);
        const size_t src_stride_pixels = frame->header.stride / sizeof(uint16_t);

        for (uint32_t dy = 0; dy < draw_h; ++dy) {
            const int32_t screen_y = dst_start_y + (int32_t)dy;
            if (screen_y < 0 || screen_y >= DISPLAY_HEIGHT) {
                continue;
            }
            const uint32_t sy = (dy * 256) / image_scale;
            if (sy >= src_h) {
                continue;
            }
            for (uint32_t dx = 0; dx < draw_w; ++dx) {
                const int32_t screen_x = dst_start_x + (int32_t)dx;
                if (screen_x < 0 || screen_x >= DISPLAY_WIDTH) {
                    continue;
                }
                const uint32_t sx = (dx * 256) / image_scale;
                if (sx >= src_w) {
                    continue;
                }
                pet_frame_buffer_[screen_y * DISPLAY_WIDTH + screen_x] =
                    src[sy * src_stride_pixels + sx];
            }
        }
        if (k_ui_perf_trace_enabled) {
            AddUiPerfSample(
                ui_perf_pet_copy_calls_,
                ui_perf_pet_copy_total_us_,
                ui_perf_pet_copy_max_us_,
                (uint32_t)(esp_timer_get_time() - perf_start_us)
            );
        }
    }

    void PlayGifState(paopao_pet_state_t state) {
        const PaopaoGifBinary asset = PaopaoGifAssetForState(state);
        PlayGifBinaryLocked(
            asset,
            PaopaoImageScaleForVisualSize(state),
            paopao_pet_gif_asset_name(state),
            PaopaoGifVisualLongestForState(state)
        );
    }

    void PlayGifBinaryLocked(
        PaopaoGifBinary asset,
        uint32_t image_scale,
        const char* asset_name,
        uint16_t visual_longest
    ) {
        if (pet_image_ == nullptr) {
            return;
        }

        if (pet_gif_controller_) {
            pet_gif_controller_->Stop();
            pet_gif_controller_.reset();
        }

        const size_t gif_size = (size_t)(asset.end - asset.start);
        gif_source_dsc_ = {};
        gif_source_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        gif_source_dsc_.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
        gif_source_dsc_.header.w = 0;
        gif_source_dsc_.header.h = 0;
        gif_source_dsc_.header.stride = 0;
        gif_source_dsc_.data_size = gif_size;
        gif_source_dsc_.data = asset.start;

        pet_gif_controller_ = std::make_unique<LvglGif>(&gif_source_dsc_, true, 0xFFFFFF, true);
        if (!pet_gif_controller_->IsLoaded()) {
            ESP_LOGE(
                TAG,
                "Failed to load Paopao GIF asset=%s size=%u",
                asset_name,
                (unsigned)gif_size
            );
            pet_gif_controller_.reset();
            return;
        }

        pet_gif_controller_->SetFrameCallback([this, image_scale]() {
            CopyPetFrameToScreen(pet_gif_controller_->image_dsc(), image_scale);
            lv_image_set_src(pet_image_, &pet_frame_dsc_);
            lv_obj_invalidate(pet_image_);
        });
        CopyPetFrameToScreen(pet_gif_controller_->image_dsc(), image_scale);
        lv_image_set_src(pet_image_, &pet_frame_dsc_);
        lv_image_set_scale(pet_image_, 256);
        lv_obj_align(pet_image_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_invalidate(pet_image_);
        pet_gif_controller_->Start();
        ApplyPetAnimationForCardPager();
        ESP_LOGI(
            TAG,
            "Playing Paopao GIF asset=%s size=%u scale=%u visual=%u",
            asset_name,
            (unsigned)gif_size,
            (unsigned)image_scale,
            (unsigned)visual_longest
        );
    }

    void DispatchPetTriggerLocked(paopao_pet_trigger_event_t event, uint32_t now_ms) {
        paopao_pet_trigger_dispatch(&trigger_, event, now_ms);
        ApplyPetStateIfChanged();
    }

    bool ShouldDispatchMoodSuggestionLocked() const {
        switch (trigger_.base_state) {
            case PAOPAO_PET_STATE_FAILING:
            case PAOPAO_PET_STATE_SLEEPING:
            case PAOPAO_PET_STATE_WAITING:
            case PAOPAO_PET_STATE_THINKING:
            case PAOPAO_PET_STATE_SPEAKING:
                return false;
            default:
                return true;
        }
    }

    void DispatchPetMoodInputLocked(const paopao_pet_mood_input_t& input, uint32_t now_ms) {
        const paopao_pet_mood_suggestion_t suggestion =
            paopao_pet_mood_handle_event(&mood_, &input, now_ms);
        if (suggestion.has_trigger && ShouldDispatchMoodSuggestionLocked()) {
            paopao_pet_trigger_dispatch(&trigger_, suggestion.trigger, now_ms);
            ApplyPetStateIfChanged();
        }
    }

    void DispatchPetMoodEventLocked(
        paopao_pet_mood_event_t event,
        paopao_pet_trigger_event_t service_trigger,
        uint32_t now_ms
    ) {
        const paopao_pet_mood_input_t input = {
            .event = event,
            .service_trigger = service_trigger,
        };
        DispatchPetMoodInputLocked(input, now_ms);
    }

    void DispatchPetBehaviorDecisionLocked(const paopao_pet_behavior_decision_t& decision, uint32_t now_ms) {
        if (!decision.has_trigger) {
            return;
        }
        paopao_pet_trigger_dispatch(&trigger_, decision.trigger, now_ms);
        ApplyPetStateIfChanged();
    }

    void DispatchPetServiceEmotionLocked(paopao_pet_trigger_event_t service_trigger, uint32_t now_ms) {
        DispatchPetMoodEventLocked(PAOPAO_PET_MOOD_EVENT_SERVICE_EMOTION, service_trigger, now_ms);
        SyncPetBehaviorVoiceStateFromBaseStateLocked(now_ms);
        const paopao_pet_behavior_decision_t decision =
            paopao_pet_behavior_handle_service_trigger(&behavior_, service_trigger, now_ms);
        DispatchPetBehaviorDecisionLocked(decision, now_ms);
    }

    void DispatchPetBehaviorTickLocked(uint32_t now_ms) {
        SyncPetBehaviorVoiceStateFromBaseStateLocked(now_ms);
        const paopao_pet_behavior_decision_t decision =
            paopao_pet_behavior_tick(&behavior_, now_ms);
        DispatchPetBehaviorDecisionLocked(decision, now_ms);
    }

    void DispatchPetVoiceStateLocked(
        paopao_pet_trigger_event_t trigger_event,
        paopao_pet_behavior_voice_state_t voice_state,
        paopao_pet_mood_event_t mood_event,
        uint32_t now_ms
    ) {
        if (mood_event != PAOPAO_PET_MOOD_EVENT_NONE) {
            DispatchPetMoodEventLocked(mood_event, PAOPAO_PET_TRIGGER_NONE, now_ms);
        }
        paopao_pet_trigger_dispatch(&trigger_, trigger_event, now_ms);
        paopao_pet_behavior_set_voice_state(&behavior_, voice_state, now_ms);
        ApplyPetStateIfChanged();
    }

    void SetPetBehaviorVoiceStateLocked(paopao_pet_behavior_voice_state_t voice_state, uint32_t now_ms) {
        paopao_pet_behavior_set_voice_state(&behavior_, voice_state, now_ms);
    }

    void SyncPetBehaviorVoiceStateFromBaseStateLocked(uint32_t now_ms) {
        switch (trigger_.base_state) {
            case PAOPAO_PET_STATE_WAITING:
                paopao_pet_behavior_set_voice_state(&behavior_, PAOPAO_PET_BEHAVIOR_VOICE_LISTENING, now_ms);
                break;
            case PAOPAO_PET_STATE_THINKING:
                paopao_pet_behavior_set_voice_state(&behavior_, PAOPAO_PET_BEHAVIOR_VOICE_THINKING, now_ms);
                break;
            case PAOPAO_PET_STATE_SPEAKING:
                paopao_pet_behavior_set_voice_state(&behavior_, PAOPAO_PET_BEHAVIOR_VOICE_SPEAKING, now_ms);
                break;
            case PAOPAO_PET_STATE_SLEEPING:
                paopao_pet_behavior_set_voice_state(&behavior_, PAOPAO_PET_BEHAVIOR_VOICE_SLEEPING, now_ms);
                break;
            case PAOPAO_PET_STATE_FAILING:
                paopao_pet_behavior_set_voice_state(&behavior_, PAOPAO_PET_BEHAVIOR_VOICE_FAILING, now_ms);
                break;
            default:
                paopao_pet_behavior_set_voice_state(&behavior_, PAOPAO_PET_BEHAVIOR_VOICE_IDLE, now_ms);
                break;
        }
    }

    void RecordPetBehaviorInteractionLocked(uint32_t now_ms) {
        paopao_pet_behavior_record_interaction(&behavior_, now_ms);
    }

    void DispatchLocalPetTriggerLocked(
        paopao_pet_trigger_event_t trigger_event,
        paopao_pet_mood_event_t mood_event,
        uint32_t now_ms
    ) {
        DispatchPetMoodEventLocked(mood_event, PAOPAO_PET_TRIGGER_NONE, now_ms);
        RecordPetBehaviorInteractionLocked(now_ms);
        paopao_pet_trigger_dispatch(&trigger_, trigger_event, now_ms);
        ApplyPetStateIfChanged();
    }

    void HandleTouchRelease(uint32_t now_ms) {
        const bool was_card_dragging = xiaoxin_card_pager_is_dragging(&card_pager_);
        const xiaoxin_card_page_t release_current_page = xiaoxin_card_pager_current_page(&card_pager_);
        const xiaoxin_card_page_t release_target_page = xiaoxin_card_pager_target_page(&card_pager_);
        const int16_t release_offset = xiaoxin_card_pager_offset_y(&card_pager_);
        const xiaoxin_card_page_t release_visual_page =
            release_target_page == XIAOXIN_CARD_PAGE_HOME
                ? release_current_page
                : release_target_page;
        const int16_t release_y = DragCardLayerY(
            release_current_page,
            release_target_page,
            release_offset,
            card_pager_.screen_height,
            card_pager_.max_drag_px
        );
        if (card_pager_.pressed) {
            xiaoxin_card_pager_release(&card_pager_);
        }

        if (release_current_page == XIAOXIN_CARD_PAGE_NOTIFICATIONS && !was_card_dragging) {
            const int16_t dx = (int16_t)touch_last_x_ - (int16_t)touch_start_x_;
            const int16_t dy = (int16_t)touch_last_y_ - (int16_t)touch_start_y_;
            const int16_t abs_dx = (int16_t)std::abs(dx);
            const int16_t abs_dy = (int16_t)std::abs(dy);

            if (notification_gesture_mode_ == NotificationGestureMode::DismissCard &&
                notification_pressed_slot_ >= 0) {
                if (dx <= -k_notification_dismiss_threshold_px) {
                    AnimateNotificationDismiss(notification_pressed_slot_, notification_pressed_visible_index_);
                } else {
                    AnimateNotificationDismissRebound(notification_pressed_slot_);
                }
                notification_gesture_mode_ = NotificationGestureMode::None;
                return;
            }

            if (notification_gesture_mode_ == NotificationGestureMode::ClearAllPress &&
                abs_dx < 8 && abs_dy < 8) {
                xiaoxin_card_pager_notification_clear_all(&card_pager_);
                SyncNotificationMaintenanceTimerLocked();
                notification_scroll_y_ = 0;
                RenderNotificationPageAfterDataChange();
                notification_gesture_mode_ = NotificationGestureMode::None;
                notification_pressed_slot_ = -1;
                notification_pressed_visible_index_ = 0xff;
                notification_card_drag_x_ = 0;
                return;
            }

            const uint8_t total = xiaoxin_card_pager_notification_count(&card_pager_);

            if (abs_dy >= k_touch_drag_min_px && abs_dy > abs_dx) {
                notification_gesture_mode_ = NotificationGestureMode::VerticalScroll;
                const int16_t raw_scroll = (int16_t)(notification_drag_start_scroll_y_ + dy);
                const int16_t display_scroll = NotificationScrollDisplayY(raw_scroll, total);
                const int16_t target_scroll = ClampNotificationScrollY(raw_scroll, total);
                card_pager_.notification_index = NotificationIndexForScroll(target_scroll, total);
                AnimateNotificationScroll(display_scroll, target_scroll);
                return;
            }
            if (abs_dy > 4 && abs_dy > abs_dx) {
                notification_gesture_mode_ = NotificationGestureMode::VerticalScroll;
                const int16_t raw_scroll = (int16_t)(notification_drag_start_scroll_y_ + dy);
                const int16_t display_scroll = NotificationScrollDisplayY(raw_scroll, total);
                const int16_t target_scroll = ClampNotificationScrollY(raw_scroll, total);
                card_pager_.notification_index = NotificationIndexForScroll(target_scroll, total);
                AnimateNotificationScroll(display_scroll, target_scroll);
                return;
            }
            notification_gesture_mode_ = NotificationGestureMode::None;
            notification_pressed_slot_ = -1;
            notification_pressed_visible_index_ = 0xff;
            notification_card_drag_x_ = 0;
            return;
        }

        if (was_card_dragging) {
            const int16_t target_y =
                xiaoxin_card_pager_current_page(&card_pager_) == XIAOXIN_CARD_PAGE_HOME
                    ? HiddenCardLayerY(release_visual_page, card_pager_.screen_height)
                    : 0;
            AnimateCardPagerRelease(release_visual_page, release_y, target_y, true);
            ESP_LOGI(TAG, "Card pager page=%s animation=%d",
                xiaoxin_card_page_name(xiaoxin_card_pager_current_page(&card_pager_)),
                (int)xiaoxin_card_pager_animation(&card_pager_));
            return;
        }

        const int16_t dx = (int16_t)touch_last_x_ - (int16_t)touch_start_x_;
        const int16_t abs_dx = (int16_t)std::abs(dx);
        const int16_t abs_dy = (int16_t)std::abs((int16_t)touch_last_y_ - (int16_t)touch_start_y_);
        if (!xiaoxin_card_pager_allows_pet_interaction(&card_pager_)) {
            if (now_ms - touch_start_ms_ >= k_touch_hold_ms) {
                SetCardPagerPage(XIAOXIN_CARD_PAGE_HOME);
            }
            return;
        }

        if (abs_dx >= k_touch_drag_min_px && abs_dx > abs_dy) {
            DispatchLocalPetTriggerLocked(
                dx < 0 ? PAOPAO_PET_TRIGGER_LOCAL_DRAG_LEFT : PAOPAO_PET_TRIGGER_LOCAL_DRAG_RIGHT,
                PAOPAO_PET_MOOD_EVENT_LOCAL_DRAG,
                now_ms
            );
        } else if (now_ms - touch_start_ms_ >= k_touch_hold_ms) {
            DispatchLocalPetTriggerLocked(
                PAOPAO_PET_TRIGGER_LOCAL_HOLD,
                PAOPAO_PET_MOOD_EVENT_LOCAL_HOLD,
                now_ms
            );
        } else {
            DispatchLocalPetTriggerLocked(
                PAOPAO_PET_TRIGGER_LOCAL_TAP,
                PAOPAO_PET_MOOD_EVENT_LOCAL_TAP,
                now_ms
            );
        }
    }

    void PollTouch(uint32_t now_ms) {
        if (touch_ == nullptr) {
            return;
        }

        bool pressed = false;
        uint16_t x = 0;
        uint16_t y = 0;
        const esp_err_t touch_err = touch_->ReadPoint(x, y, pressed);
        if (touch_err != ESP_OK) {
            if (settings_open_ && touch_pressed_) {
                HandleSettingsTouch(touch_last_x_, touch_last_y_, false);
                power_save_timer_wake_requested_ = true;
                touch_pressed_ = false;
            }
            if (touch_pressed_) {
                power_save_timer_wake_requested_ = true;
                HandleTouchRelease(now_ms);
                touch_last_active_ms_ = now_ms;
                touch_pressed_ = false;
            }
            if (now_ms - touch_last_error_log_ms_ >= 1000) {
                touch_last_error_log_ms_ = now_ms;
                ESP_LOGW(TAG, "Touch read failed: %s", esp_err_to_name(touch_err));
            }
            return;
        }

        if (pressed || touch_pressed_) {
            power_save_timer_wake_requested_ = true;
        }

        if (settings_open_) {
            if (pressed) {
                touch_last_x_ = x;
                touch_last_y_ = y;
                touch_last_active_ms_ = now_ms;
            }
            HandleSettingsTouch(
                pressed ? x : touch_last_x_,
                pressed ? y : touch_last_y_,
                pressed
            );
            touch_pressed_ = pressed;
            return;
        }

        if (pressed) {
            if (!touch_pressed_) {
                ESP_LOGI(TAG, "Touch point x=%u y=%u", x, y);
            }
            touch_last_x_ = x;
            touch_last_y_ = y;
            touch_last_active_ms_ = now_ms;
            if (!touch_pressed_) {
                touch_start_x_ = x;
                touch_start_y_ = y;
                touch_start_ms_ = now_ms;
                if (card_layer_ != nullptr) {
                    lv_anim_delete(card_layer_, CardLayerSetY);
                }
                notification_drag_start_scroll_y_ = notification_scroll_y_;
                notification_gesture_mode_ = NotificationGestureMode::None;
                notification_pressed_slot_ = -1;
                notification_pressed_visible_index_ = 0xff;
                notification_card_drag_x_ = 0;
                if (xiaoxin_card_pager_current_page(&card_pager_) == XIAOXIN_CARD_PAGE_NOTIFICATIONS) {
                    notification_pressed_slot_ = NotificationCardSlotAtPoint(x, y);
                    notification_pressed_visible_index_ =
                        notification_pressed_slot_ >= 0 ? glass_cards_[notification_pressed_slot_].visible_index : 0xff;
                    if (NotificationClearButtonContains(x, y)) {
                        notification_gesture_mode_ = NotificationGestureMode::ClearAllPress;
                    }
                }
                xiaoxin_card_pager_press(&card_pager_, (int16_t)x, (int16_t)y);
            } else {
                if (xiaoxin_card_pager_current_page(&card_pager_) == XIAOXIN_CARD_PAGE_NOTIFICATIONS) {
                    const int16_t dx = (int16_t)x - (int16_t)touch_start_x_;
                    const int16_t dy = (int16_t)y - (int16_t)touch_start_y_;
                    const int16_t abs_dx = (int16_t)std::abs(dx);
                    const int16_t abs_dy = (int16_t)std::abs(dy);
                    const uint8_t total = xiaoxin_card_pager_notification_count(&card_pager_);
                    const bool at_notification_bottom =
                        notification_scroll_y_ <= NotificationMinScrollY(total) + 1;

                    if (!notification_dismiss_animating_ &&
                        notification_gesture_mode_ == NotificationGestureMode::None &&
                        notification_pressed_slot_ >= 0 &&
                        notification_pressed_visible_index_ != 0xff &&
                        dx < 0 &&
                        abs_dx >= k_notification_dismiss_intent_px &&
                        ((int32_t)abs_dx * 4) > ((int32_t)abs_dy * 5)) {
                        notification_gesture_mode_ = NotificationGestureMode::DismissCard;
                    }

                    if (notification_gesture_mode_ == NotificationGestureMode::DismissCard) {
                        notification_card_drag_x_ = std::max<int16_t>((int16_t)-DISPLAY_WIDTH, dx);
                        lv_obj_set_x(glass_cards_[notification_pressed_slot_].container, notification_card_drag_x_);
                        const int32_t opa = LV_OPA_COVER + ((int32_t)notification_card_drag_x_ * LV_OPA_COVER) / DISPLAY_WIDTH;
                        lv_obj_set_style_opa(
                            glass_cards_[notification_pressed_slot_].container,
                            (lv_opa_t)std::max<int32_t>(LV_OPA_30, opa),
                            0
                        );
                        return;
                    }

                    if (abs_dy > abs_dx) {
                        notification_gesture_mode_ = NotificationGestureMode::VerticalScroll;
                        if (dy < 0 && at_notification_bottom) {
                            xiaoxin_card_pager_drag(&card_pager_, (int16_t)x, (int16_t)y);
                            if (xiaoxin_card_pager_is_dragging(&card_pager_)) {
                                ApplyCardPagerVisual();
                            }
                        } else {
                            const int16_t raw_scroll = (int16_t)(notification_drag_start_scroll_y_ + dy);
                            ApplyNotificationScrollVisual(NotificationScrollDisplayY(raw_scroll, total), false, true);
                        }
                        return;
                    }
                }

                xiaoxin_card_pager_drag(&card_pager_, (int16_t)x, (int16_t)y);
                if (xiaoxin_card_pager_is_dragging(&card_pager_)) {
                    ApplyCardPagerVisual();
                }
            }
        } else if (touch_pressed_) {
            HandleTouchRelease(now_ms);
            touch_last_active_ms_ = now_ms;
        }

        touch_pressed_ = pressed;
    }

    void RunRenderLoop() {
        while (true) {
            const int64_t perf_start_us = k_ui_perf_trace_enabled ? esp_timer_get_time() : 0;
            bool request_settings_wifi_config = false;
            bool wake_power_save_timer = false;
            {
                DisplayLockGuard lock(this);
                const uint32_t now_ms = NowMs();
                PollTouch(now_ms);
                DispatchPetBehaviorTickLocked(now_ms);
                paopao_pet_trigger_tick(&trigger_, now_ms);
                ApplyPetStateIfChanged();
                LogUiPerfSummary(now_ms);
                request_settings_wifi_config = ConsumeSettingsWifiConfigRequestLocked();
                wake_power_save_timer = ConsumePowerSaveTimerWakeRequestLocked();
            }
            if (wake_power_save_timer) {
                WakePowerSaveTimerFromTouch();
            }
            if (request_settings_wifi_config) {
                RequestSettingsWifiConfigFromSettingsPage();
            }
            if (k_ui_perf_trace_enabled) {
                AddUiPerfSample(
                    ui_perf_touch_loop_calls_,
                    ui_perf_touch_loop_total_us_,
                    ui_perf_touch_loop_max_us_,
                    (uint32_t)(esp_timer_get_time() - perf_start_us)
                );
            }
            vTaskDelay(pdMS_TO_TICKS(k_touch_poll_ms));
        }
    }

    static void RenderTask(void* arg) {
        static_cast<PaopaoPetDisplay*>(arg)->RunRenderLoop();
    }
};

enum class TouchControllerType {
    Spd2010,
    Cst9217,
};

class Qmi8658Motion {
public:
    bool Initialize(i2c_master_bus_handle_t i2c_bus) {
        uint8_t address = k_qmi8658_addr_primary;
        esp_err_t err = i2c_master_probe(i2c_bus, address, pdMS_TO_TICKS(100));
        if (err != ESP_OK) {
            address = k_qmi8658_addr_secondary;
            err = i2c_master_probe(i2c_bus, address, pdMS_TO_TICKS(100));
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "QMI8658 not found");
            return false;
        }

        i2c_device_config_t device_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = address,
            .scl_speed_hz = 400 * 1000,
            .scl_wait_us = 0,
            .flags = {
                .disable_ack_check = 0,
            },
        };
        err = i2c_master_bus_add_device(i2c_bus, &device_cfg, &device_);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "QMI8658 add device failed: %s", esp_err_to_name(err));
            return false;
        }

        uint8_t chip_id = 0;
        if (ReadReg(k_qmi8658_reg_who_am_i, &chip_id) != ESP_OK) {
            ESP_LOGW(TAG, "QMI8658 WHO_AM_I read failed");
            return false;
        }

        WriteReg(k_qmi8658_reg_ctrl1, 0x60);
        WriteReg(k_qmi8658_reg_ctrl2, 0x23);
        WriteReg(k_qmi8658_reg_ctrl7, 0x01);

        ESP_LOGI(TAG, "QMI8658 initialized at 0x%02x, chip_id=0x%02x", address, chip_id);
        return true;
    }

    bool ReadAcceleration(int16_t& x, int16_t& y, int16_t& z) {
        uint8_t data[6] = {};
        if (ReadRegs(k_qmi8658_reg_accel_x_l, data, sizeof(data)) != ESP_OK) {
            return false;
        }

        x = (int16_t)((uint16_t)data[1] << 8 | data[0]);
        y = (int16_t)((uint16_t)data[3] << 8 | data[2]);
        z = (int16_t)((uint16_t)data[5] << 8 | data[4]);
        return true;
    }

private:
    i2c_master_dev_handle_t device_ = nullptr;

    esp_err_t WriteReg(uint8_t reg, uint8_t value) {
        uint8_t buffer[2] = {reg, value};
        return i2c_master_transmit(device_, buffer, sizeof(buffer), pdMS_TO_TICKS(100));
    }

    esp_err_t ReadReg(uint8_t reg, uint8_t* value) {
        return i2c_master_transmit_receive(device_, &reg, 1, value, 1, pdMS_TO_TICKS(100));
    }

    esp_err_t ReadRegs(uint8_t reg, uint8_t* buffer, size_t length) {
        return i2c_master_transmit_receive(device_, &reg, 1, buffer, length, pdMS_TO_TICKS(100));
    }
};

class CustomBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    esp_io_expander_handle_t io_expander = NULL;
    Display* display_ = nullptr;
    esp_lcd_panel_io_handle_t touch_io_handle_ = nullptr;
    esp_lcd_touch_handle_t touch_handle_ = nullptr;
    TouchReader* touch_reader_ = nullptr;
    Spd2010DirectTouchReader spd2010_touch_reader_;
    Qmi8658Motion motion_;
    TaskHandle_t motion_task_ = nullptr;
    TaskHandle_t power_off_task_ = nullptr;
    TaskHandle_t debug_console_task_ = nullptr;
    PowerSaveTimer* power_save_timer_ = nullptr;
    xiaoxin_power_control_t power_control_ = {};
    adc_oneshot_unit_handle_t battery_adc_handle_ = nullptr;
    adc_cali_handle_t battery_adc_cali_handle_ = nullptr;
    bool battery_adc_initialized_ = false;
    bool battery_adc_available_ = false;
    xiaoxin_battery_context_t battery_context_ = {};
    xiaoxin_battery_snapshot_t battery_snapshot_ = {};
    bool battery_context_initialized_ = false;
    bool battery_has_snapshot_ = false;
    int last_battery_voltage_mv_ = 0;
    uint32_t last_battery_sample_ms_ = 0;
    esp_timer_handle_t battery_monitor_timer_ = nullptr;
    esp_timer_handle_t low_battery_shutdown_timer_ = nullptr;
    bool low_battery_shutdown_pending_ = false;
    bool low_battery_shutdown_startup_stage_ = false;
    button_handle_t boot_btn, pwr_btn;
    button_driver_t* boot_btn_driver_ = nullptr;
    button_driver_t* pwr_btn_driver_ = nullptr;
    bool boot_long_press_handled_ = false;
    esp_timer_handle_t boot_poll_timer_ = nullptr;
    esp_console_repl_t* debug_console_repl_ = nullptr;
    int64_t boot_press_started_us_ = 0;
    bool boot_poll_pressed_ = false;
    bool pwr_ignore_until_release_ = false;
    bool on_battery_ = false;
    bool startup_low_battery_protection_ = false;
    static CustomBoard* instance_;

    void InitializePowerHoldEarly() {
        xiaoxin_power_control_init(&power_control_);
        gpio_reset_pin(PWR_BUTTON_GPIO);
        gpio_set_direction(PWR_BUTTON_GPIO, GPIO_MODE_INPUT);
        gpio_set_pull_mode(PWR_BUTTON_GPIO, GPIO_PULLUP_ONLY);
        pwr_ignore_until_release_ = gpio_get_level(PWR_BUTTON_GPIO) == 0;
        gpio_reset_pin(PWR_Control_PIN);
        gpio_set_direction(PWR_Control_PIN, GPIO_MODE_OUTPUT);
        gpio_set_level(PWR_Control_PIN, 1);
        // Allow power rail to stabilize after latch, especially on battery
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    bool ReadPwrButtonPressedForRuntime() {
        const bool pressed = gpio_get_level(PWR_BUTTON_GPIO) == 0;
        if (pwr_ignore_until_release_) {
            if (!pressed) {
                pwr_ignore_until_release_ = false;
                ESP_LOGI(TAG, "PWR startup hold released; runtime power button enabled");
            }
            return false;
        }

        return pressed;
    }

    void DetectPowerSourceEarly() {
        InitializeBatteryAdc();
        if (!battery_adc_available_) {
            on_battery_ = true;
            ESP_LOGI(TAG, "[BOOT] Battery ADC unavailable; assuming battery power");
            return;
        }

        int voltage_sum = 0;
        uint8_t sample_count = 0;
        for (uint8_t i = 0; i < 3; ++i) {
            int raw_value = 0;
            int pin_voltage_mv = 0;
            if (adc_oneshot_read(battery_adc_handle_, k_battery_adc_channel, &raw_value) != ESP_OK) {
                continue;
            }
            if (adc_cali_raw_to_voltage(battery_adc_cali_handle_, raw_value, &pin_voltage_mv) != ESP_OK) {
                continue;
            }
            voltage_sum += pin_voltage_mv * k_battery_voltage_divider;
            sample_count++;
        }

        const int voltage_mv = sample_count > 0 ? voltage_sum / sample_count : 0;
        // Li-ion battery max ~4200mV; USB is ~5000mV. Use 4500mV as threshold.
        on_battery_ = voltage_mv <= k_external_power_voltage_mv;
        ESP_LOGI(TAG, "[BOOT] Early power detection: %dmV → %s",
            voltage_mv,
            on_battery_ ? "battery" : "USB/external");
    }

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = I2C_SDA_IO,
            .scl_io_num = I2C_SCL_IO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }
    
    void InitializeTca9554(void) {
        esp_err_t ret = esp_io_expander_new_i2c_tca9554(i2c_bus_, I2C_ADDRESS, &io_expander);
        if(ret != ESP_OK)
            ESP_LOGE(TAG, "TCA9554 create returned error");        

        // uint32_t input_level_mask = 0;
        // ret = esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1, IO_EXPANDER_INPUT);               // 设置引脚 EXIO0 和 EXIO1 模式为输入 
        // ret = esp_io_expander_get_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1, &input_level_mask);             // 获取引脚 EXIO0 和 EXIO1 的电平状态,存放在 input_level_mask 中

        // ret = esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_2 | IO_EXPANDER_PIN_NUM_3, IO_EXPANDER_OUTPUT);              // 设置引脚 EXIO2 和 EXIO3 模式为输出
        // ret = esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_2 | IO_EXPANDER_PIN_NUM_3, 1);                             // 将引脚电平设置为 1
        // ret = esp_io_expander_print_state(io_expander);                                                                             // 打印引脚状态

        ret = esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1, IO_EXPANDER_OUTPUT);                 // 设置引脚 EXIO0 和 EXIO1 模式为输出
        ESP_ERROR_CHECK(ret);
        ret = esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1, 1);                                // 复位 LCD 与 TouchPad
        ESP_ERROR_CHECK(ret);
        vTaskDelay(pdMS_TO_TICKS(300));
        ret = esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1, 0);                                // 复位 LCD 与 TouchPad
        ESP_ERROR_CHECK(ret);
        vTaskDelay(pdMS_TO_TICKS(300));
        ret = esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1, 1);                                // 复位 LCD 与 TouchPad
        ESP_ERROR_CHECK(ret);
    }

    void InitializeSpi() {
        ESP_LOGI(TAG, "Initialize QSPI bus");

        const spi_bus_config_t bus_config = TAIJIPI_SPD2010_PANEL_BUS_QSPI_CONFIG(QSPI_PIN_NUM_LCD_PCLK,
                                                                        QSPI_PIN_NUM_LCD_DATA0,
                                                                        QSPI_PIN_NUM_LCD_DATA1,
                                                                        QSPI_PIN_NUM_LCD_DATA2,
                                                                        QSPI_PIN_NUM_LCD_DATA3,
                                                                        QSPI_LCD_H_RES * 80 * sizeof(uint16_t));
        ESP_ERROR_CHECK(spi_bus_initialize(QSPI_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));
    }

    void InitializeSpd2010Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGI(TAG, "Install panel IO");
        
        const esp_lcd_panel_io_spi_config_t io_config = SPD2010_PANEL_IO_QSPI_CONFIG(QSPI_PIN_NUM_LCD_CS, NULL, NULL);
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)QSPI_LCD_HOST, &io_config, &panel_io));

        ESP_LOGI(TAG, "Install SPD2010 panel driver");
        
        spd2010_vendor_config_t vendor_config = {
            .flags = {
                .use_qspi_interface = 1,
            },
        };
        const esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = QSPI_PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,     // Implemented by LCD command `36h`
            .bits_per_pixel = QSPI_LCD_BIT_PER_PIXEL,    // Implemented by LCD command `3Ah` (16/18)
            .vendor_config = &vendor_config,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_spd2010(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_disp_on_off(panel, true);
        if (DISPLAY_SWAP_XY) {
            esp_lcd_panel_swap_xy(panel, true);
        }
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        display_ = new PaopaoPetDisplay(panel_io, panel);
    }

    bool TryInitializeTouchController(TouchControllerType type, const esp_lcd_touch_config_t& touch_cfg) {
        const uint8_t address = type == TouchControllerType::Spd2010
            ? ESP_LCD_TOUCH_IO_I2C_SPD2010_ADDRESS
            : ESP_LCD_TOUCH_IO_I2C_CST9217_ADDRESS;
        const char* name = type == TouchControllerType::Spd2010 ? "SPD2010" : "CST9217";

        esp_err_t err = i2c_master_probe(i2c_bus_, address, pdMS_TO_TICKS(100));
        if (err != ESP_OK) {
            ESP_LOGI(TAG, "Touch controller %s not found at 0x%02x: %s", name, address, esp_err_to_name(err));
            return false;
        }

        if (type == TouchControllerType::Spd2010) {
            if (!spd2010_touch_reader_.Initialize(i2c_bus_)) {
                return false;
            }
            touch_reader_ = &spd2010_touch_reader_;
            static_cast<PaopaoPetDisplay*>(display_)->AttachTouch(touch_reader_);
            ESP_LOGI(TAG, "Touch controller initialized: %s at 0x%02x", touch_reader_->Name(), address);
            return true;
        }

        esp_lcd_panel_io_i2c_config_t touch_io_config = {};
        touch_io_config = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
        touch_io_config.scl_speed_hz = 400 * 1000;

        esp_lcd_panel_io_handle_t touch_io_handle = nullptr;
        err = esp_lcd_new_panel_io_i2c(i2c_bus_, &touch_io_config, &touch_io_handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Touch panel IO init failed for %s: %s", name, esp_err_to_name(err));
            return false;
        }

        esp_lcd_touch_handle_t touch_handle = nullptr;
        err = esp_lcd_touch_new_i2c_cst9217(touch_io_handle, &touch_cfg, &touch_handle);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Touch controller %s init failed: %s", name, esp_err_to_name(err));
            esp_lcd_panel_io_del(touch_io_handle);
            return false;
        }

        touch_io_handle_ = touch_io_handle;
        touch_handle_ = touch_handle;
        touch_reader_ = new EspLcdTouchReader(touch_handle_);
        static_cast<PaopaoPetDisplay*>(display_)->AttachTouch(touch_reader_);
        ESP_LOGI(TAG, "Touch controller initialized: %s at 0x%02x", name, address);
        return true;
    }

    void InitializeTouch() {
        esp_lcd_touch_config_t touch_cfg = {
            .x_max = DISPLAY_WIDTH - 1,
            .y_max = DISPLAY_HEIGHT - 1,
            .rst_gpio_num = TP_PIN_NUM_RST,
            .int_gpio_num = TP_PIN_NUM_INT,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = DISPLAY_SWAP_XY,
                .mirror_x = DISPLAY_MIRROR_X,
                .mirror_y = DISPLAY_MIRROR_Y,
            },
        };

        if (TryInitializeTouchController(TouchControllerType::Spd2010, touch_cfg)) {
            return;
        }
        if (TryInitializeTouchController(TouchControllerType::Cst9217, touch_cfg)) {
            return;
        }

        ESP_LOGW(TAG, "No supported touch controller found");
    }

    void InitializeMotion() {
        if (!motion_.Initialize(i2c_bus_)) {
            return;
        }

        xTaskCreatePinnedToCore(
            MotionTask,
            "paopao_motion",
            3072,
            this,
            3,
            &motion_task_,
            1
        );
    }

    void InitializeBatteryAdc() {
        if (battery_adc_initialized_) {
            return;
        }
        battery_adc_initialized_ = true;

        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1,
        };
        esp_err_t err = adc_oneshot_new_unit(&init_config, &battery_adc_handle_);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Battery ADC unit init failed: %s", esp_err_to_name(err));
            return;
        }

        adc_oneshot_chan_cfg_t channel_config = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        err = adc_oneshot_config_channel(battery_adc_handle_, k_battery_adc_channel, &channel_config);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Battery ADC channel init failed: %s", esp_err_to_name(err));
            return;
        }

        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        err = adc_cali_create_scheme_curve_fitting(&cali_config, &battery_adc_cali_handle_);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Battery ADC calibration init failed: %s", esp_err_to_name(err));
            return;
        }

        battery_adc_available_ = true;
        ESP_LOGI(TAG, "Battery ADC initialized on GPIO8 / ADC1 channel 7");
    }

    bool ReadBatteryVoltageMv(int* voltage_mv) {
        if (voltage_mv == nullptr) {
            return false;
        }
        if (!battery_adc_available_) {
            return false;
        }

        int voltage_sum = 0;
        uint8_t sample_count = 0;
        for (uint8_t i = 0; i < k_battery_runtime_sample_count; ++i) {
            int raw_value = 0;
            int pin_voltage_mv = 0;
            if (adc_oneshot_read(battery_adc_handle_, k_battery_adc_channel, &raw_value) != ESP_OK) {
                continue;
            }
            if (adc_cali_raw_to_voltage(battery_adc_cali_handle_, raw_value, &pin_voltage_mv) != ESP_OK) {
                continue;
            }
            voltage_sum += pin_voltage_mv * k_battery_voltage_divider;
            sample_count++;
        }

        if (sample_count == 0) {
            return false;
        }

        *voltage_mv = voltage_sum / sample_count;
        return true;
    }

    uint32_t BatteryNowMs() const {
        return (uint32_t)(esp_timer_get_time() / 1000);
    }

    xiaoxin_battery_power_hint_t CurrentBatteryPowerHint(int voltage_mv) const {
        if (voltage_mv > k_external_power_voltage_mv) {
            return XIAOXIN_BATTERY_POWER_HINT_EXTERNAL;
        }
        if (on_battery_) {
            return XIAOXIN_BATTERY_POWER_HINT_BATTERY;
        }
        return XIAOXIN_BATTERY_POWER_HINT_UNKNOWN;
    }

    void StartBatteryMonitor() {
        if (battery_monitor_timer_ != nullptr) {
            return;
        }

        const uint32_t now_ms = BatteryNowMs();
        xiaoxin_battery_state_init(&battery_context_, now_ms);
        battery_snapshot_ = xiaoxin_battery_state_snapshot(&battery_context_);
        battery_context_initialized_ = true;
        battery_has_snapshot_ = true;

        const esp_timer_create_args_t battery_timer_args = {
            .callback = [](void* arg) {
                static_cast<CustomBoard*>(arg)->RefreshBatteryStateFromTimer();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "battery_monitor",
            .skip_unhandled_events = true,
        };

        esp_err_t err = esp_timer_create(&battery_timer_args, &battery_monitor_timer_);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Battery monitor timer create failed: %s", esp_err_to_name(err));
            return;
        }

        RefreshBatteryStateFromTimer();
        err = esp_timer_start_periodic(battery_monitor_timer_, k_battery_monitor_interval_us);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Battery monitor timer start failed: %s", esp_err_to_name(err));
        }
    }

    void RefreshBatteryStateFromTimer() {
        if (!battery_context_initialized_) {
            xiaoxin_battery_state_init(&battery_context_, BatteryNowMs());
            battery_context_initialized_ = true;
        }

        int voltage_mv = 0;
        const bool sample_valid = ReadBatteryVoltageMv(&voltage_mv);
        const uint32_t now_ms = BatteryNowMs();
        if (sample_valid) {
            last_battery_voltage_mv_ = voltage_mv;
            last_battery_sample_ms_ = now_ms;
        }

        battery_snapshot_ = xiaoxin_battery_state_update(
            &battery_context_,
            voltage_mv,
            sample_valid,
            sample_valid ? CurrentBatteryPowerHint(voltage_mv) : XIAOXIN_BATTERY_POWER_HINT_UNKNOWN,
            XIAOXIN_BATTERY_LOAD_IDLE,
            now_ms
        );
        battery_has_snapshot_ = true;
        HandleBatterySnapshot(battery_snapshot_);
    }

    void HandleBatterySnapshot(const xiaoxin_battery_snapshot_t& snapshot) {
        CancelLowBatteryShutdownIfRecovered(snapshot);

        auto* display = static_cast<PaopaoPetDisplay*>(display_);
        if (display != nullptr) {
            display->UpdateOverviewBatterySnapshot(snapshot);
        }

        if (snapshot.low_edge && display != nullptr) {
            display->ShowLowBatteryNotification();
            display->DispatchPetMoodEvent(PAOPAO_PET_MOOD_EVENT_BATTERY_LOW);
        }

        if (snapshot.critical_edge) {
            BeginLowBatteryShutdown(false);
        }
    }

    void CancelLowBatteryShutdownIfRecovered(const xiaoxin_battery_snapshot_t& snapshot) {
        if (!low_battery_shutdown_pending_) {
            return;
        }

        if (low_battery_shutdown_startup_stage_) {
            return;
        }

        const bool external_power = snapshot.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL;
        const bool recovered = snapshot.recovered_edge;
        if (!external_power && !recovered) {
            return;
        }

        if (low_battery_shutdown_timer_ != nullptr) {
            esp_timer_stop(low_battery_shutdown_timer_);
        }
        low_battery_shutdown_pending_ = false;
        low_battery_shutdown_startup_stage_ = false;
        ESP_LOGI(TAG, "Low battery shutdown canceled: external=%d recovered=%d", external_power ? 1 : 0, recovered ? 1 : 0);
        if (display_ != nullptr) {
            display_->ShowNotification("已接入电源", 2000);
        }
    }

    void BeginLowBatteryShutdown(bool startup_stage) {
        if (low_battery_shutdown_pending_ || xiaoxin_power_control_shutdown_requested(&power_control_)) {
            return;
        }

        low_battery_shutdown_pending_ = true;
        low_battery_shutdown_startup_stage_ = startup_stage;
        if (display_ != nullptr) {
            display_->ShowNotification("电量不足，即将关机", 3000);
        }

        if (low_battery_shutdown_timer_ == nullptr) {
            const esp_timer_create_args_t shutdown_timer_args = {
                .callback = [](void* arg) {
                    static_cast<CustomBoard*>(arg)->FinishLowBatteryShutdown();
                },
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "low_battery_off",
                .skip_unhandled_events = true,
            };
            esp_err_t err = esp_timer_create(&shutdown_timer_args, &low_battery_shutdown_timer_);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Low battery shutdown timer create failed: %s", esp_err_to_name(err));
                FinishLowBatteryShutdown();
                return;
            }
        } else {
            esp_timer_stop(low_battery_shutdown_timer_);
        }

        esp_err_t err = esp_timer_start_once(low_battery_shutdown_timer_, k_low_battery_shutdown_delay_us);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Low battery shutdown timer start failed: %s", esp_err_to_name(err));
            FinishLowBatteryShutdown();
        }
    }

    void HandleStartupLowBatteryProtection() {
        if (!startup_low_battery_protection_) {
            return;
        }

        ESP_LOGW(TAG, "Runtime health recommends startup low battery protection");
        if (display_ != nullptr) {
            display_->ShowNotification("电量不足，请充电后再启动", 3000);
        }
        BeginLowBatteryShutdown(true);
    }

    void FinishLowBatteryShutdown() {
        if (!low_battery_shutdown_pending_) {
            return;
        }

        if (!low_battery_shutdown_startup_stage_) {
            int voltage_mv = 0;
            const bool sample_valid = ReadBatteryVoltageMv(&voltage_mv);
            const uint32_t now_ms = BatteryNowMs();
            if (sample_valid) {
                last_battery_voltage_mv_ = voltage_mv;
                last_battery_sample_ms_ = now_ms;
            }
            battery_snapshot_ = xiaoxin_battery_state_update(
                &battery_context_,
                voltage_mv,
                sample_valid,
                sample_valid ? CurrentBatteryPowerHint(voltage_mv) : XIAOXIN_BATTERY_POWER_HINT_UNKNOWN,
                XIAOXIN_BATTERY_LOAD_IDLE,
                now_ms
            );
            battery_has_snapshot_ = true;
            CancelLowBatteryShutdownIfRecovered(battery_snapshot_);
            if (!low_battery_shutdown_pending_) {
                return;
            }
        }

        low_battery_shutdown_pending_ = false;
        RuntimeHealthRecordLowBatteryShutdown(
            last_battery_voltage_mv_,
            low_battery_shutdown_startup_stage_
        );
        RuntimeHealthForceCheckpoint();
        xiaoxin_power_control_request_shutdown(&power_control_);
        GetBacklight()->SetBrightness(0);
        gpio_set_level(PWR_Control_PIN, xiaoxin_power_control_power_hold(&power_control_));
        ESP_LOGI(TAG, "Low battery shutdown: power hold released (startup=%d voltage=%dmV)",
            low_battery_shutdown_startup_stage_ ? 1 : 0,
            last_battery_voltage_mv_);

        if (power_off_task_ == nullptr) {
            xTaskCreatePinnedToCore(
                PowerOffTask,
                "pwr_off",
                3072,
                this,
                4,
                &power_off_task_,
                1
            );
        }
    }

    void RunMotionLoop() {
        int16_t last_x = 0;
        int16_t last_y = 0;
        int16_t last_z = 0;
        bool has_last = false;
        uint32_t last_shake_ms = 0;
        uint8_t shake_sample_count = 0;

        while (true) {
            int16_t x = 0;
            int16_t y = 0;
            int16_t z = 0;
            const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
            if (motion_.ReadAcceleration(x, y, z)) {
                if (has_last) {
                    const int32_t delta =
                        std::abs((int32_t)x - last_x) +
                        std::abs((int32_t)y - last_y) +
                        std::abs((int32_t)z - last_z);

                    const bool touch_suppressed =
                        static_cast<PaopaoPetDisplay*>(display_)->IsTouchMotionSuppressed(now_ms);
                    if (touch_suppressed) {
                        shake_sample_count = 0;
                    } else if (delta >= k_shake_delta_threshold) {
                        if (shake_sample_count < k_shake_required_samples) {
                            shake_sample_count++;
                        }
                    } else {
                        shake_sample_count = 0;
                    }

                    if (shake_sample_count >= k_shake_required_samples &&
                        now_ms - last_shake_ms >= k_shake_cooldown_ms) {
                        last_shake_ms = now_ms;
                        shake_sample_count = 0;
                        static_cast<PaopaoPetDisplay*>(display_)->DispatchLocalPetTrigger(PAOPAO_PET_TRIGGER_LOCAL_SHAKE, PAOPAO_PET_MOOD_EVENT_LOCAL_SHAKE);
                    }
                }

                last_x = x;
                last_y = y;
                last_z = z;
                has_last = true;
            }

            vTaskDelay(pdMS_TO_TICKS(k_motion_poll_ms));
        }
    }

    static void MotionTask(void* arg) {
        static_cast<CustomBoard*>(arg)->RunMotionLoop();
    }

    void WaitForPowerButtonReleaseAndSleep() {
        ESP_LOGI(TAG, "Waiting for PWR release before soft power-off sleep");
        while (!gpio_get_level(PWR_BUTTON_GPIO)) {
            vTaskDelay(pdMS_TO_TICKS(k_power_off_release_poll_ms));
        }

        vTaskDelay(pdMS_TO_TICKS(120));
        ESP_LOGI(TAG, "Entering deep sleep; PWR button will wake the board when USB is still powering it");
        esp_err_t err = esp_sleep_enable_ext0_wakeup(PWR_BUTTON_GPIO, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "PWR deep-sleep wake setup failed: %s", esp_err_to_name(err));
            power_off_task_ = nullptr;
            vTaskDelete(nullptr);
            return;
        }
        esp_deep_sleep_start();
    }

    void RequestPowerOff() {
        if (xiaoxin_power_control_shutdown_requested(&power_control_)) {
            return;
        }

        xiaoxin_power_control_request_shutdown(&power_control_);
        GetBacklight()->SetBrightness(0);
        RuntimeHealthForceCheckpoint();
        gpio_set_level(PWR_Control_PIN, xiaoxin_power_control_power_hold(&power_control_));
        ESP_LOGI(TAG, "PWR long press: power hold released");

        if (power_off_task_ == nullptr) {
            xTaskCreatePinnedToCore(
                PowerOffTask,
                "pwr_off",
                3072,
                this,
                4,
                &power_off_task_,
                1
            );
        }
    }

    static void PowerOffTask(void* arg) {
        static_cast<CustomBoard*>(arg)->WaitForPowerButtonReleaseAndSleep();
    }

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, -1);
        power_save_timer_->OnEnterSleepMode([this]() {
            auto* display = static_cast<PaopaoPetDisplay*>(display_);
            if (display != nullptr) {
                display->ShowLowPowerClockScreen();
            }
        });
        power_save_timer_->OnExitSleepMode([this]() {
            auto* display = static_cast<PaopaoPetDisplay*>(display_);
            if (display != nullptr) {
                display->HideLowPowerClockScreen();
            }
        });
        power_save_timer_->SetEnabled(true);
    }
 
    void InitializeButtonsCustom() {
        xiaoxin_power_control_init(&power_control_);
        gpio_reset_pin(BOOT_BUTTON_GPIO);                                     
        gpio_set_direction(BOOT_BUTTON_GPIO, GPIO_MODE_INPUT);   
        gpio_set_pull_mode(BOOT_BUTTON_GPIO, GPIO_PULLUP_ONLY);
        gpio_reset_pin(PWR_BUTTON_GPIO);                                     
        gpio_set_direction(PWR_BUTTON_GPIO, GPIO_MODE_INPUT);   
        gpio_set_pull_mode(PWR_BUTTON_GPIO, GPIO_PULLUP_ONLY);
        gpio_set_level(PWR_Control_PIN, xiaoxin_power_control_power_hold(&power_control_));
    }

    void OpenSettingsOverlayFromBootButton() {
        ESP_LOGI(TAG, "BOOT long press: opening settings overlay");
        auto* display = static_cast<PaopaoPetDisplay*>(display_);
        if (display != nullptr) {
            ESP_LOGI(TAG, "Opening BOOT settings overlay");
            display->OpenSettingsOverlay();
        }
    }

    void HandleBootLongPress() {
        if (boot_long_press_handled_) {
            return;
        }
        boot_long_press_handled_ = true;
        WakePowerSaveTimer();
        OpenSettingsOverlayFromBootButton();
    }

    void PollBootButtonFallback() {
        const bool pressed = gpio_get_level(BOOT_BUTTON_GPIO) == 0;
        const int64_t now_us = esp_timer_get_time();
        if (pressed) {
            if (!boot_poll_pressed_) {
                boot_poll_pressed_ = true;
                boot_press_started_us_ = now_us;
                boot_long_press_handled_ = false;
                ESP_LOGI(TAG, "BOOT poll press down");
            }
            if (!boot_long_press_handled_ &&
                boot_press_started_us_ > 0 &&
                now_us - boot_press_started_us_ >= 2000000) {
                ESP_LOGI(TAG, "BOOT poll long press fallback");
                HandleBootLongPress();
            }
            return;
        }

        if (boot_poll_pressed_) {
            boot_poll_pressed_ = false;
            boot_press_started_us_ = 0;
            boot_long_press_handled_ = false;
            ESP_LOGI(TAG, "BOOT poll press up");
        }
    }

    void InitializeBootButtonPollingFallback() {
        if (boot_poll_timer_ != nullptr) {
            return;
        }
        const esp_timer_create_args_t boot_poll_timer_args = {
            .callback = [](void* arg) {
                static_cast<CustomBoard*>(arg)->PollBootButtonFallback();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "boot_poll",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&boot_poll_timer_args, &boot_poll_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(boot_poll_timer_, 50 * 1000));
        ESP_LOGI(TAG, "BOOT polling fallback started: gpio=%d level=%d", (int)BOOT_BUTTON_GPIO, gpio_get_level(BOOT_BUTTON_GPIO));
    }

    void InitializeButtons() {
        instance_ = this;
        InitializeButtonsCustom();

        // Boot Button
        button_config_t boot_btn_config = {
            .long_press_time = 2000,
            .short_press_time = 0
        };
        boot_btn_driver_ = (button_driver_t*)calloc(1, sizeof(button_driver_t));
        boot_btn_driver_->enable_power_save = false;
        boot_btn_driver_->get_key_level = [](button_driver_t *button_driver) -> uint8_t {
            return !gpio_get_level(BOOT_BUTTON_GPIO);
        };
        ESP_ERROR_CHECK(iot_button_create(&boot_btn_config, boot_btn_driver_, &boot_btn));
        iot_button_register_cb(boot_btn, BUTTON_PRESS_DOWN, nullptr, [](void* button_handle, void* usr_data) {
            auto self = static_cast<CustomBoard*>(usr_data);
            self->boot_long_press_handled_ = false;
            ESP_LOGI(TAG, "BOOT press down");
        }, this);
        iot_button_register_cb(boot_btn, BUTTON_PRESS_UP, nullptr, [](void* button_handle, void* usr_data) {
            auto self = static_cast<CustomBoard*>(usr_data);
            self->boot_long_press_handled_ = false;
            ESP_LOGI(TAG, "BOOT press up");
        }, this);
        iot_button_register_cb(boot_btn, BUTTON_SINGLE_CLICK, nullptr, [](void* button_handle, void* usr_data) {
            auto self = static_cast<CustomBoard*>(usr_data);
            self->WakePowerSaveTimer();
            auto* display = static_cast<PaopaoPetDisplay*>(self->display_);
            if (display != nullptr && display->IsSettingsOpen()) {
                display->CloseSettingsOverlay();
                return;
            }
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                self->EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        }, this);
        iot_button_register_cb(boot_btn, BUTTON_LONG_PRESS_START, nullptr, [](void* button_handle, void* usr_data) {
            auto self = static_cast<CustomBoard*>(usr_data);
            self->HandleBootLongPress();
        }, this);
        iot_button_register_cb(boot_btn, BUTTON_LONG_PRESS_HOLD, nullptr, [](void* button_handle, void* usr_data) {
            auto self = static_cast<CustomBoard*>(usr_data);
            self->HandleBootLongPress();
        }, this);
        InitializeBootButtonPollingFallback();

        // Power Button
        button_config_t pwr_btn_config = {
            .long_press_time = 5000,
            .short_press_time = 0
        };
        pwr_btn_driver_ = (button_driver_t*)calloc(1, sizeof(button_driver_t));
        pwr_btn_driver_->enable_power_save = false;
        pwr_btn_driver_->get_key_level = [](button_driver_t *button_driver) -> uint8_t {
            auto self = CustomBoard::Instance();
            return self != nullptr && self->ReadPwrButtonPressedForRuntime();
        };
        ESP_ERROR_CHECK(iot_button_create(&pwr_btn_config, pwr_btn_driver_, &pwr_btn));
        iot_button_register_cb(pwr_btn, BUTTON_SINGLE_CLICK, nullptr, [](void* button_handle, void* usr_data) {
            auto self = static_cast<CustomBoard*>(usr_data);
            self->WakePowerSaveTimer();
            // 短按无处理
        }, this);
        iot_button_register_cb(pwr_btn, BUTTON_LONG_PRESS_START, nullptr, [](void* button_handle, void* usr_data) {
            auto self = static_cast<CustomBoard*>(usr_data);
            self->WakePowerSaveTimer();
            self->RequestPowerOff();
        }, this);
    }

    void InitializeDebugConsole() {
        if (debug_console_repl_ != nullptr) {
            return;
        }

        const esp_console_cmd_t notify_test_cmd = {
            .command = "notify_test",
            .help = "show a test notification and open the notification page",
            .hint = nullptr,
            .func = nullptr,
            .argtable = nullptr,
            .func_w_context = [](void* context, int argc, char** argv) -> int {
                (void)argc;
                (void)argv;
                auto* self = static_cast<CustomBoard*>(context);
                auto* display = static_cast<PaopaoPetDisplay*>(self->display_);
                if (display == nullptr) {
                    printf("notify_test: display is not ready\n");
                    return 1;
                }

                display->UpsertNotification(
                    "ota_update",
                    "Notify test",
                    "Serial debug notification",
                    "Debug",
                    4,
                    0
                );
                display->OpenNotificationPageForDebug();
                printf("notify_test: opened notification page\n");
                return 0;
            },
            .context = this,
        };
        ESP_ERROR_CHECK(esp_console_cmd_register(&notify_test_cmd));

        const esp_console_cmd_t boot_diag_cmd = {
            .command = "boot_diag",
            .help = "print previous boot and current boot diagnostics",
            .hint = nullptr,
            .func = nullptr,
            .argtable = nullptr,
            .func_w_context = [](void* context, int argc, char** argv) -> int {
                (void)context;
                (void)argc;
                (void)argv;

                char trace[BOOT_DIAGNOSTICS_TRACE_MAX] = {};
                bool on_battery = false;
                if (BootDiagnosticsReadPrevious(trace, sizeof(trace), &on_battery)) {
                    printf("previous boot (%s): %s\n", on_battery ? "battery" : "usb", trace);
                } else {
                    printf("previous boot: <empty>\n");
                }

                trace[0] = '\0';
                on_battery = false;
                if (BootDiagnosticsReadCurrent(trace, sizeof(trace), &on_battery)) {
                    printf("current boot (%s): %s\n", on_battery ? "battery" : "usb", trace);
                } else {
                    printf("current boot: <empty>\n");
                }
                return 0;
            },
            .context = this,
        };
        ESP_ERROR_CHECK(esp_console_cmd_register(&boot_diag_cmd));

        const esp_console_cmd_t battery_cmd = {
            .command = "battery",
            .help = "Print battery monitor state",
            .hint = nullptr,
            .func = [](int argc, char** argv) -> int {
                (void)argc;
                (void)argv;
                auto* self = CustomBoard::Instance();
                if (self == nullptr) {
                    printf("battery: board is not ready\n");
                    return 0;
                }

                const uint32_t now_ms = self->BatteryNowMs();
                const uint32_t age_ms = self->last_battery_sample_ms_ == 0
                    ? 0
                    : now_ms - self->last_battery_sample_ms_;
                printf(
                    "battery: voltage=%dmV age=%lums state=%s source=%s percent=%d level=%u reliable=%d shutdown_pending=%d\n",
                    self->last_battery_voltage_mv_,
                    (unsigned long)age_ms,
                    BatteryStateLabel(self->battery_snapshot_.state),
                    BatteryPowerSourceLabel(self->battery_snapshot_.power_source),
                    self->battery_snapshot_.display_percent,
                    (unsigned)self->battery_snapshot_.display_level,
                    self->battery_snapshot_.percent_reliable ? 1 : 0,
                    self->low_battery_shutdown_pending_ ? 1 : 0
                );
                return 0;
            },
        };
        ESP_ERROR_CHECK(esp_console_cmd_register(&battery_cmd));

        const esp_console_cmd_t runtime_health_cmd = {
            .command = "runtime_health",
            .help = "print runtime health counters",
            .hint = nullptr,
            .func = nullptr,
            .argtable = nullptr,
            .func_w_context = [](void* context, int argc, char** argv) -> int {
                (void)context;
                (void)argc;
                (void)argv;

                xiaoxin_runtime_health_snapshot_t snapshot = {};
                RuntimeHealthReadSnapshot(&snapshot);
                char current_duration[24] = {};
                char last_duration[24] = {};
                char max_duration[24] = {};
                xiaoxin_runtime_health_format_duration(
                    current_duration,
                    sizeof(current_duration),
                    snapshot.current_runtime_sec
                );
                xiaoxin_runtime_health_format_duration(
                    last_duration,
                    sizeof(last_duration),
                    snapshot.last_runtime_sec
                );
                xiaoxin_runtime_health_format_duration(
                    max_duration,
                    sizeof(max_duration),
                    snapshot.max_runtime_sec
                );
                printf(
                    "runtime: current=%s last=%s max=%s reset=%s brownout=%lu short_streak=%lu battery=%d low_shutdowns=%lu low_mv=%lu low_stage=%s\n",
                    current_duration,
                    last_duration,
                    max_duration,
                    xiaoxin_runtime_health_reset_label(snapshot.last_reset_kind),
                    (unsigned long)snapshot.brownout_count,
                    (unsigned long)snapshot.short_run_streak,
                    snapshot.current_on_battery ? 1 : 0,
                    (unsigned long)snapshot.low_battery_shutdown_count,
                    (unsigned long)snapshot.last_low_battery_shutdown_voltage_mv,
                    snapshot.last_low_battery_shutdown_startup_stage ? "startup" : "runtime"
                );
                return 0;
            },
            .context = this,
        };
        ESP_ERROR_CHECK(esp_console_cmd_register(&runtime_health_cmd));

        esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
        repl_config.max_cmdline_length = 128;
        repl_config.prompt = "xiaoxin>";

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
        esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
        esp_err_t repl_err = esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &debug_console_repl_);
#elif CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
        esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
        esp_err_t repl_err = esp_console_new_repl_uart(&hw_config, &repl_config, &debug_console_repl_);
#else
        ESP_LOGW(TAG, "Debug console not started: no interactive console device configured.");
        return;
#endif
        if (repl_err != ESP_OK) {
            debug_console_repl_ = nullptr;
            ESP_LOGW(TAG, "Debug console REPL unavailable: %s", esp_err_to_name(repl_err));
            return;
        }

        esp_err_t start_err = esp_console_start_repl(debug_console_repl_);
        if (start_err != ESP_OK) {
            debug_console_repl_ = nullptr;
            ESP_LOGW(TAG, "Debug console start failed: %s", esp_err_to_name(start_err));
            return;
        }

        ESP_LOGI(TAG, "Debug console started. Use `notify_test` to open notification page.");
    }

    void ScheduleDeferredDebugConsole() {
        if (debug_console_task_ != nullptr || debug_console_repl_ != nullptr) {
            return;
        }

        xTaskCreate([](void* arg) {
            auto* self = static_cast<CustomBoard*>(arg);
            vTaskDelay(pdMS_TO_TICKS(2000));
            self->InitializeDebugConsole();
            self->debug_console_task_ = nullptr;
            vTaskDelete(nullptr);
        }, "debug_console", 4096, this, 1, &debug_console_task_);
    }

public:
    CustomBoard() {
        ESP_LOGI(TAG, "[BOOT] Stage 1/11: Power hold latch");
        InitializePowerHoldEarly();

        ESP_LOGI(TAG, "[BOOT] Stage 2/11: Early power source detection");
        DetectPowerSourceEarly();
        RuntimeHealthStart(on_battery_);
        startup_low_battery_protection_ = on_battery_ && RuntimeHealthProtectionRecommended();
        BootDiagnosticsStart(on_battery_);
        BootDiagnosticsMark("board_power_source_detected");
        s_boot_on_battery = on_battery_;

        ESP_LOGI(TAG, "[BOOT] Stage 3/11: I2C bus");
        BootDiagnosticsMark("board_i2c_start");
        InitializeI2c();

        ESP_LOGI(TAG, "[BOOT] Stage 4/11: TCA9554 IO expander + LCD reset");
        BootDiagnosticsMark("board_io_expander_start");
        InitializeTca9554();

        ESP_LOGI(TAG, "[BOOT] Stage 5/11: QSPI bus");
        BootDiagnosticsMark("board_qspi_start");
        InitializeSpi();

        ESP_LOGI(TAG, "[BOOT] Stage 6/11: SPD2010 display panel");
        BootDiagnosticsMark("board_display_start");
        InitializeSpd2010Display();

        ESP_LOGI(TAG, "[BOOT] Stage 7/11: Backlight restore (%s)", on_battery_ ? "battery-splash" : "normal");
        BootDiagnosticsMark("board_backlight_ready");
        if (on_battery_) {
            GetBacklight()->SetBrightness(k_boot_splash_brightness_percent, false);
            vTaskDelay(pdMS_TO_TICKS(200));
        } else {
            GetBacklight()->RestoreBrightness();
        }
        StartBatteryMonitor();
        HandleStartupLowBatteryProtection();
        if (startup_low_battery_protection_) {
            BootDiagnosticsMark("board_startup_low_battery_protection");
            BootDiagnosticsFlush();
            ESP_LOGW(TAG, "[BOOT] Startup low battery protection active; skipping full board init");
            return;
        }

        ESP_LOGI(TAG, "[BOOT] Stage 8/11: Touch controller");
        BootDiagnosticsMark("board_touch_start");
        InitializeTouch();
        StabilizeBatteryBootStage();

        ESP_LOGI(TAG, "[BOOT] Stage 9/11: Buttons");
        BootDiagnosticsMark("board_buttons_start");
        InitializeButtons();
        StabilizeBatteryBootStage();

        ESP_LOGI(TAG, "[BOOT] Stage 10/11: Power save timer");
        BootDiagnosticsMark("board_power_save_timer_start");
        InitializePowerSaveTimer();
        StabilizeBatteryBootStage();

        if (on_battery_) {
            ESP_LOGI(TAG, "[BOOT] Stage 11/11: Deferring IMU init (battery power)");
            BootDiagnosticsMark("board_imu_deferred");
            ScheduleDeferredMotionInit();
            StabilizeBatteryBootStage();
        } else {
            ESP_LOGI(TAG, "[BOOT] Stage 11/11: IMU motion sensor");
            BootDiagnosticsMark("board_imu_start");
            InitializeMotion();
        }

        if (on_battery_) {
            ScheduleDeferredDebugConsole();
        } else {
            InitializeDebugConsole();
        }
        BootDiagnosticsMark("board_constructor_done");
        BootDiagnosticsFlush();
        ESP_LOGI(TAG, "[BOOT] Constructor complete (on_battery=%d)", on_battery_ ? 1 : 0);
    }

    void StabilizeBatteryBootStage() {
        if (on_battery_) {
            vTaskDelay(pdMS_TO_TICKS(150));
        }
    }

    void ScheduleDeferredMotionInit() {
        if (motion_task_ != nullptr) {
            return;
        }
        const esp_timer_create_args_t deferred_motion_args = {
            .callback = [](void* arg) {
                auto* self = static_cast<CustomBoard*>(arg);
                ESP_LOGI(TAG, "[BOOT] Deferred IMU initialization starting");
                BootDiagnosticsMark("board_imu_deferred_start");
                self->InitializeMotion();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "defer_motion",
            .skip_unhandled_events = true,
        };
        esp_timer_handle_t deferred_motion_timer = nullptr;
        esp_err_t err = esp_timer_create(&deferred_motion_args, &deferred_motion_timer);
        if (err == ESP_OK) {
            // Start IMU initialization 3 seconds after boot, when power is stable
            esp_timer_start_once(deferred_motion_timer, 3 * 1000 * 1000);
        } else {
            ESP_LOGW(TAG, "Failed to create deferred IMU timer: %s", esp_err_to_name(err));
            // Fall back to immediate init
            InitializeMotion();
        }
    }

    static CustomBoard* Instance() {
        return instance_;
    }

    bool HasPowerSaveScheduler() const {
        return power_save_timer_ != nullptr;
    }

    bool OnBattery() const {
        return on_battery_;
    }

    PowerSaveTimer* PowerSaveTimerForSettings() const {
        return power_save_timer_;
    }

    void WakePowerSaveTimer() {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
    }

    void RequestSettingsWifiConfig() {
        auto* display = static_cast<PaopaoPetDisplay*>(display_);
        if (display != nullptr) {
            display->CloseSettingsOverlay();
        }
        EnterWifiConfigMode();
    }

    void StartNetwork() override {
        if (startup_low_battery_protection_) {
            ESP_LOGW(TAG, "Skipping network startup during low battery protection");
            return;
        }

        if (on_battery_) {
            vTaskDelay(pdMS_TO_TICKS(300));
        }

        WifiBoard::StartNetwork();
    }

    bool ShouldSkipApplicationStartup() override {
        return startup_low_battery_protection_;
    }

    void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            if (power_save_timer_ != nullptr) {
                power_save_timer_->WakeUp();
            }
        }
        WifiBoard::SetPowerSaveLevel(level);
    }

    void PrepareForAudioPlayback() override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        auto* display = static_cast<PaopaoPetDisplay*>(display_);
        if (display != nullptr) {
            display->HideLowPowerClockScreen();
        }
        WifiBoard::SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, I2S_STD_SLOT_LEFT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN, I2S_STD_SLOT_RIGHT); // I2S_STD_SLOT_LEFT / I2S_STD_SLOT_RIGHT / I2S_STD_SLOT_BOTH
        static bool output_volume_configured = []() {
            audio_codec.SetOutputVolume(100);
            audio_codec.SetOutputBoost(AUDIO_OUTPUT_BOOST);
            return true;
        }();
        (void)output_volume_configured;

        return &audio_codec;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        charging = false;
        discharging = false;

        if (!battery_has_snapshot_ || !battery_snapshot_.percent_reliable) {
            return false;
        }

        level = battery_snapshot_.display_percent;
        charging = battery_snapshot_.power_source == XIAOXIN_BATTERY_POWER_EXTERNAL;
        discharging = battery_snapshot_.power_source == XIAOXIN_BATTERY_POWER_BATTERY;
        return true;
    }

    bool GetBatteryDisplayLevel(int& level) override {
        if (!battery_has_snapshot_) {
            return false;
        }

        level = battery_snapshot_.display_level;
        return true;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
    
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

xiaoxin_settings_caps_t PaopaoPetDisplay::SettingsCaps() const {
    PowerSaveTimer* power_save_timer_ = TargetPowerSaveTimer();
    const xiaoxin_settings_caps_t caps = {
        .has_audio_output = false,
        .has_vibration = false,
        .has_power_save_scheduler = power_save_timer_ != nullptr,
    };
    return caps;
}

void PaopaoPetDisplay::RenderSettingsBrightnessPage() {
    settings_view_ = SettingsView::Brightness;
    EnsureSettingsOverlayLocked();
    HideSettingsRowsLocked();
    SetSettingsBackRowVisibleLocked(false);
    lv_label_set_text(settings_title_label_, "亮度");
    AlignSettingsHintBottomLocked();
    lv_label_set_text(settings_hint_label_, "");
    ShowSettingsBrightnessSliderLocked();

    uint8_t brightness = 75;
    auto backlight = Board::GetInstance().GetBacklight();
    if (backlight != nullptr) {
        brightness = backlight->brightness();
    }
    if (brightness < 10) {
        brightness = 10;
    }
    UpdateSettingsBrightnessSliderLocked(brightness);
}

void PaopaoPetDisplay::RenderSettingsWifiPage() {
    settings_view_ = SettingsView::Wifi;
    EnsureSettingsOverlayLocked();
    HideSettingsRowsLocked();
    HideSettingsBrightnessSliderLocked();
    SetSettingsBackRowVisibleLocked(false);
    lv_label_set_text(settings_title_label_, "Wi-Fi");
    lv_label_set_text(settings_hint_label_, "重新配网");
    AlignSettingsHintBottomLocked();
    settings_wifi_config_requested_ = true;
    CloseSettingsOverlayLocked();
}

static PowerSaveTimer* TargetPowerSaveTimer() {
    return CustomBoard::Instance() != nullptr
        ? CustomBoard::Instance()->PowerSaveTimerForSettings()
        : nullptr;
}

static void WakePowerSaveTimerFromTouch() {
    PowerSaveTimer* power_save_timer = TargetPowerSaveTimer();
    if (power_save_timer != nullptr) {
        power_save_timer->WakeUp();
    }
}

static void RequestSettingsWifiConfigFromSettingsPage() {
    if (CustomBoard::Instance() != nullptr) {
        CustomBoard::Instance()->RequestSettingsWifiConfig();
    }
}

DECLARE_BOARD(CustomBoard);

CustomBoard* CustomBoard::instance_ = nullptr;
