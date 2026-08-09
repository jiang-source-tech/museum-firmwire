/**
 * @file ai_pet_s3_board.cc
 * @brief AI Pet S3 Board 实现
 *
 * 基于小智 WifiBoard 框架，对接我们的硬件：
 *   - INMP441 麦克风 (I2S Simplex, I2S_NUM_1)
 *   - MAX98357A 功放喇叭 (I2S Simplex, I2S_NUM_0)
 *   - ST7789 240×280 SPI 显示
 *   - TTP223 触摸传感器 ×2
 */
#include "wifi_board.h"
#include "audio_codec.h"
#include "codecs/no_audio_codec.h"
#include "ai_pet_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "system_reset.h"
#include "led/single_led.h"

#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#define TAG "AiPetS3Board"


class AiPetS3Board : public WifiBoard {
private:
    // ── 音频 ──────────────────────────────────────────────────
    NoAudioCodecSimplex* codec_ = nullptr;

    // ── 显示 ──────────────────────────────────────────────────
    AiPetDisplay* display_ = nullptr;

    // ── 触摸 / 按钮 ────────────────────────────────────────────
    Button head_touch_button_;
    Button back_touch_button_;
    Button boot_button_;

    // ────────────────────────────────────────────────────────
    void InitializeCodec() {
        // INMP441 麦克风 (I2S_NUM_1) + MAX98357A 功放 (I2S_NUM_0)
        codec_ = new NoAudioCodecSimplex(
            AUDIO_INPUT_SAMPLE_RATE,   // 16000 Hz
            AUDIO_OUTPUT_SAMPLE_RATE,  // 24000 Hz
            SPK_I2S_BCLK,             // GPIO15 - spk_bclk
            SPK_I2S_LRCK,             // GPIO16 - spk_ws
            SPK_I2S_DOUT,             // GPIO7  - spk_dout
            MIC_I2S_SCK,              // GPIO5  - mic_sck
            MIC_I2S_WS,               // GPIO4  - mic_ws
            MIC_I2S_DIN               // GPIO6  - mic_din
        );
    }

    void InitializeDisplay() {
        // ── SPI 总线初始化 ────────────────────────────────────
        spi_bus_config_t bus_cfg = {};
        bus_cfg.mosi_io_num     = DISPLAY_MOSI_PIN;
        bus_cfg.miso_io_num     = GPIO_NUM_NC;
        bus_cfg.sclk_io_num     = DISPLAY_CLK_PIN;
        bus_cfg.quadwp_io_num   = GPIO_NUM_NC;
        bus_cfg.quadhd_io_num   = GPIO_NUM_NC;
        bus_cfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

        // ── Panel IO (SPI 接口) ───────────────────────────────
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_io_spi_config_t io_cfg = {};
        io_cfg.cs_gpio_num       = DISPLAY_CS_PIN;
        io_cfg.dc_gpio_num       = DISPLAY_DC_PIN;
        io_cfg.pclk_hz           = 40 * 1000 * 1000;   // 40 MHz
        io_cfg.lcd_cmd_bits      = 8;
        io_cfg.lcd_param_bits    = 8;
        io_cfg.trans_queue_depth = 10;
        io_cfg.spi_mode          = DISPLAY_SPI_MODE;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &panel_io));

        // ── ST7789 Panel ─────────────────────────────────────
        esp_lcd_panel_handle_t panel = nullptr;
        esp_lcd_panel_dev_config_t panel_cfg = {};
        panel_cfg.reset_gpio_num = DISPLAY_RST_PIN;
        panel_cfg.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_cfg.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_cfg, &panel));

        // ── 初始化面板 ────────────────────────────────────────
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_set_gap(panel, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y);
        esp_lcd_panel_disp_on_off(panel, true);

        // ── 背光 ─────────────────────────────────────────────
        gpio_config_t bl_cfg = {};
        bl_cfg.pin_bit_mask = (1ULL << DISPLAY_BACKLIGHT_PIN);
        bl_cfg.mode         = GPIO_MODE_OUTPUT;
        gpio_config(&bl_cfg);
        // DISPLAY_BACKLIGHT_OUTPUT_INVERT = false → 高电平开灯
        gpio_set_level(DISPLAY_BACKLIGHT_PIN,
                       DISPLAY_BACKLIGHT_OUTPUT_INVERT ? 0 : 1);

        // ── 创建 AiPetDisplay 对象（全屏 GIF 表情支持） ────────
        display_ = new AiPetDisplay(
            panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY
        );
    }

    void InitializeButtons() {
        // 头顶触摸 → 开始/停止对话
        head_touch_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            app.ToggleChatState();
        });
        // 背部触摸 → 打断 / 停止
        back_touch_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            app.StopListening();
        });
        // Boot 按钮 → 调试用（同头部触摸）
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            app.ToggleChatState();
        });
    }

public:
    AiPetS3Board()
        : head_touch_button_(TOUCH_HEAD_GPIO, true),   // TTP223 触摸时高电平
          back_touch_button_(TOUCH_BACK_GPIO, true),   // TTP223 触摸时高电平
          boot_button_(BOOT_BUTTON_GPIO, false) {      // Boot 按钮接地，低电平

        ESP_LOGI(TAG, "Initializing AI Pet S3 Board...");
        InitializeCodec();
        InitializeDisplay();
        InitializeButtons();
        ESP_LOGI(TAG, "AI Pet S3 Board ready");
    }

    virtual ~AiPetS3Board() {
        delete codec_;
        delete display_;
    }

    // ── Board 接口 ────────────────────────────────────────────
    virtual AudioCodec* GetAudioCodec() override {
        return codec_;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }
};

// 注册 Board（小智宏）
DECLARE_BOARD(AiPetS3Board)
