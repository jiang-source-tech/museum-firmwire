#pragma once

#include <stdint.h>

#include "paopao_pet_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAOPAO_PET_SPRITE_WIDTH 256
#define PAOPAO_PET_SPRITE_HEIGHT 256
#define PAOPAO_PET_SCREEN_WIDTH 412
#define PAOPAO_PET_SCREEN_HEIGHT 412

typedef struct {
  const uint16_t *pixels;
  uint16_t width;
  uint16_t height;
} paopao_pet_frame_t;

typedef struct {
  const char *id;
  const paopao_pet_frame_t *frames;
  uint16_t frame_count;
  uint8_t fps;
} paopao_pet_animation_t;

const paopao_pet_animation_t *paopao_pet_asset_for_state(paopao_pet_state_t state);

#ifdef __cplusplus
}
#endif

