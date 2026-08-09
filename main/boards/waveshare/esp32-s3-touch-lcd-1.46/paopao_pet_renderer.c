#include "paopao_pet_renderer.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "paopao_pet_board.h"
#include "paopao_pet_assets.h"

#define PAOPAO_PET_SCREEN_CHUNK_ROWS 20
#define PAOPAO_PET_BACKGROUND_RGB565 0x0923
#define PAOPAO_PET_RENDER_SCALE_NUMERATOR 150
#define PAOPAO_PET_RENDER_SCALE_DENOMINATOR 100
#define PAOPAO_PET_SCALE_ROUNDED(value) \
  (((value) * PAOPAO_PET_RENDER_SCALE_NUMERATOR + (PAOPAO_PET_RENDER_SCALE_DENOMINATOR / 2)) / \
   PAOPAO_PET_RENDER_SCALE_DENOMINATOR)
#define PAOPAO_PET_DRAW_WIDTH PAOPAO_PET_SCALE_ROUNDED(PAOPAO_PET_SPRITE_WIDTH)
#define PAOPAO_PET_DRAW_HEIGHT PAOPAO_PET_SCALE_ROUNDED(PAOPAO_PET_SPRITE_HEIGHT)

static const char *TAG = "paopao_renderer";
static paopao_pet_state_t s_current_state = PAOPAO_PET_STATE_IDLE;
static uint16_t s_current_frame = 0;
static uint32_t s_last_frame_ms = 0;
static uint16_t *s_screen_chunk = 0;
static int16_t s_pet_center_x = PAOPAO_PET_SCREEN_WIDTH / 2;
static int16_t s_pet_center_y = PAOPAO_PET_SCREEN_HEIGHT / 2;
static bool s_force_redraw = true;

static uint16_t swap_rgb565(uint16_t value) {
  return (uint16_t)((value << 8) | (value >> 8));
}

static int16_t clamp_int16(int16_t value, int16_t min_value, int16_t max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static void clamp_pet_center(void) {
  const int16_t min_x = PAOPAO_PET_DRAW_WIDTH / 2;
  const int16_t min_y = PAOPAO_PET_DRAW_HEIGHT / 2;
  const int16_t max_x = PAOPAO_PET_SCREEN_WIDTH - (PAOPAO_PET_DRAW_WIDTH / 2);
  const int16_t max_y = PAOPAO_PET_SCREEN_HEIGHT - (PAOPAO_PET_DRAW_HEIGHT / 2);

  s_pet_center_x = clamp_int16(s_pet_center_x, min_x, max_x);
  s_pet_center_y = clamp_int16(s_pet_center_y, min_y, max_y);
}

static void draw_frame(const paopao_pet_frame_t *frame) {
  if (frame == 0 || frame->pixels == 0) {
    return;
  }
  if (s_screen_chunk == 0) {
    return;
  }

  const uint16_t draw_x = (uint16_t)(s_pet_center_x - (PAOPAO_PET_DRAW_WIDTH / 2));
  const uint16_t draw_y = (uint16_t)(s_pet_center_y - (PAOPAO_PET_DRAW_HEIGHT / 2));

  for (uint16_t screen_y = 0; screen_y < PAOPAO_PET_SCREEN_HEIGHT; screen_y += PAOPAO_PET_SCREEN_CHUNK_ROWS) {
    const uint16_t rows =
      (screen_y + PAOPAO_PET_SCREEN_CHUNK_ROWS > PAOPAO_PET_SCREEN_HEIGHT)
        ? (uint16_t)(PAOPAO_PET_SCREEN_HEIGHT - screen_y)
        : PAOPAO_PET_SCREEN_CHUNK_ROWS;

    for (uint16_t row = 0; row < rows; row++) {
      const uint16_t y = screen_y + row;
      for (uint16_t x = 0; x < PAOPAO_PET_SCREEN_WIDTH; x++) {
        uint16_t pixel = PAOPAO_PET_BACKGROUND_RGB565;

        if (
          x >= draw_x &&
          x < draw_x + PAOPAO_PET_DRAW_WIDTH &&
          y >= draw_y &&
          y < draw_y + PAOPAO_PET_DRAW_HEIGHT
        ) {
          const uint16_t source_x =
            (uint16_t)(((uint32_t)(x - draw_x) * PAOPAO_PET_RENDER_SCALE_DENOMINATOR) /
                       PAOPAO_PET_RENDER_SCALE_NUMERATOR);
          const uint16_t source_y =
            (uint16_t)(((uint32_t)(y - draw_y) * PAOPAO_PET_RENDER_SCALE_DENOMINATOR) /
                       PAOPAO_PET_RENDER_SCALE_NUMERATOR);
          if (source_x < frame->width && source_y < frame->height) {
            pixel = frame->pixels[source_y * frame->width + source_x];
          }
        }

        s_screen_chunk[row * PAOPAO_PET_SCREEN_WIDTH + x] = swap_rgb565(pixel);
      }
    }

    const esp_err_t err = paopao_pet_board_draw_bitmap(
      0,
      screen_y,
      PAOPAO_PET_SCREEN_WIDTH,
      screen_y + rows,
      s_screen_chunk
    );
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "draw chunk failed: %s", esp_err_to_name(err));
      return;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void paopao_pet_renderer_init(void) {
  s_current_state = PAOPAO_PET_STATE_IDLE;
  s_current_frame = 0;
  s_last_frame_ms = 0;
  s_pet_center_x = PAOPAO_PET_SCREEN_WIDTH / 2;
  s_pet_center_y = PAOPAO_PET_SCREEN_HEIGHT / 2;
  s_force_redraw = true;

  const size_t chunk_pixels = PAOPAO_PET_SCREEN_WIDTH * PAOPAO_PET_SCREEN_CHUNK_ROWS;
  s_screen_chunk = heap_caps_malloc(chunk_pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
  if (s_screen_chunk == 0) {
    ESP_LOGE(TAG, "failed to allocate screen chunk buffer");
  }
}

void paopao_pet_play_state(paopao_pet_state_t state) {
  if (s_current_state == state) {
    return;
  }

  s_current_state = state;
  s_current_frame = 0;
  s_last_frame_ms = 0;
  s_force_redraw = true;
}

void paopao_pet_renderer_set_position(int16_t center_x, int16_t center_y) {
  const int16_t previous_x = s_pet_center_x;
  const int16_t previous_y = s_pet_center_y;

  s_pet_center_x = center_x;
  s_pet_center_y = center_y;
  clamp_pet_center();

  if (s_pet_center_x != previous_x || s_pet_center_y != previous_y) {
    s_force_redraw = true;
  }
}

bool paopao_pet_renderer_hit_test(uint16_t x, uint16_t y) {
  const uint16_t draw_x = (uint16_t)(s_pet_center_x - (PAOPAO_PET_DRAW_WIDTH / 2));
  const uint16_t draw_y = (uint16_t)(s_pet_center_y - (PAOPAO_PET_DRAW_HEIGHT / 2));

  return x >= draw_x &&
         x < draw_x + PAOPAO_PET_DRAW_WIDTH &&
         y >= draw_y &&
         y < draw_y + PAOPAO_PET_DRAW_HEIGHT;
}

void paopao_pet_renderer_force_redraw(void) {
  s_force_redraw = true;
}

void paopao_pet_renderer_tick(uint32_t now_ms) {
  const paopao_pet_animation_t *animation = paopao_pet_asset_for_state(s_current_state);
  if (animation == 0 || animation->frame_count == 0 || animation->fps == 0) {
    return;
  }

  const uint32_t frame_ms = 1000 / animation->fps;
  if (s_last_frame_ms == 0) {
    s_current_frame = 0;
    s_last_frame_ms = now_ms;
    draw_frame(&animation->frames[s_current_frame]);
    s_force_redraw = false;
    return;
  }

  const bool advance_frame = now_ms - s_last_frame_ms >= frame_ms;
  if (advance_frame || s_force_redraw) {
    if (advance_frame) {
      s_current_frame = (uint16_t)((s_current_frame + 1U) % animation->frame_count);
    }
    s_last_frame_ms = now_ms;
    draw_frame(&animation->frames[s_current_frame]);
    s_force_redraw = false;
  }
}
