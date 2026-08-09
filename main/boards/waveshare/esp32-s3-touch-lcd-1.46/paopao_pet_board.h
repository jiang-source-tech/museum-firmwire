#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool touch_pressed;
  bool touch_tap;
  bool touch_hold;
  bool touch_has_position;
  uint16_t touch_x;
  uint16_t touch_y;
  bool shake_detected;
} paopao_pet_board_input_t;

esp_err_t paopao_pet_board_init(void);
esp_err_t paopao_pet_board_poll_inputs(uint32_t now_ms, paopao_pet_board_input_t *input);
esp_err_t paopao_pet_board_draw_bitmap(
  int x_start,
  int y_start,
  int x_end,
  int y_end,
  const uint16_t *pixels
);

#ifdef __cplusplus
}
#endif
