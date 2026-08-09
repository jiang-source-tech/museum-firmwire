#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "paopao_pet_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void paopao_pet_renderer_init(void);
void paopao_pet_play_state(paopao_pet_state_t state);
void paopao_pet_renderer_set_position(int16_t center_x, int16_t center_y);
bool paopao_pet_renderer_hit_test(uint16_t x, uint16_t y);
void paopao_pet_renderer_force_redraw(void);
void paopao_pet_renderer_tick(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

