/**
 * @file config.h
 * @brief AI Pet S3 硬件引脚定义
 *
 * 硬件: ESP32-S3-DevKitC-1 N16R8
 * MIC:  INMP441 (I2S Simplex RX)
 * SPK:  MAX98357A (I2S Simplex TX)
 * 显示: ST7789 240×280 SPI
 * 触摸: TTP223 ×2 (GPIO)
 */
#pragma once

// ── 音频：麦克风 INMP441 (I2S RX, I2S_NUM_1) ──────────────────
#define AUDIO_INPUT_SAMPLE_RATE     16000
#define MIC_I2S_WS                  GPIO_NUM_4
#define MIC_I2S_SCK                 GPIO_NUM_5
#define MIC_I2S_DIN                 GPIO_NUM_6

// ── 音频：喇叭 MAX98357A (I2S TX, I2S_NUM_0) ──────────────────
#define AUDIO_OUTPUT_SAMPLE_RATE    24000
#define SPK_I2S_DOUT                GPIO_NUM_7
#define SPK_I2S_BCLK                GPIO_NUM_15
#define SPK_I2S_LRCK                GPIO_NUM_16

// ── 显示屏 ST7789 240×280 SPI ──────────────────────────────────
#define DISPLAY_WIDTH               240
#define DISPLAY_HEIGHT              280
#define DISPLAY_MOSI_PIN            GPIO_NUM_47
#define DISPLAY_CLK_PIN             GPIO_NUM_21
#define DISPLAY_DC_PIN              GPIO_NUM_40
#define DISPLAY_RST_PIN             GPIO_NUM_45
#define DISPLAY_CS_PIN              GPIO_NUM_41
#define DISPLAY_BACKLIGHT_PIN       GPIO_NUM_42
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_OFFSET_X            0
#define DISPLAY_OFFSET_Y            20
#define DISPLAY_MIRROR_X            false
#define DISPLAY_MIRROR_Y            false
#define DISPLAY_SWAP_XY             false
#define DISPLAY_INVERT_COLOR        true
#define DISPLAY_SPI_MODE            0

// ── 触摸 TTP223 ────────────────────────────────────────────────
#define TOUCH_HEAD_GPIO             GPIO_NUM_8   // 头顶触摸 → 开始对话
#define TOUCH_BACK_GPIO             GPIO_NUM_9   // 背部触摸 → 切换情绪/停止

// ── LED / Boot ────────────────────────────────────────────────
#define BUILTIN_LED_GPIO            GPIO_NUM_48
#define BOOT_BUTTON_GPIO            GPIO_NUM_0
