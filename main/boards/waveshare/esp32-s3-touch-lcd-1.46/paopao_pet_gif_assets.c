#include "paopao_pet_gif_assets.h"

const char* paopao_pet_gif_asset_name(paopao_pet_state_t state) {
  switch (state) {
    case PAOPAO_PET_STATE_IDLE:
      return "idle.gif";
    case PAOPAO_PET_STATE_WORKING:
      return "working.gif";
    case PAOPAO_PET_STATE_SPEAKING:
      return "speaking_fixed.gif";
    case PAOPAO_PET_STATE_THINKING:
      return "thinking.gif";
    case PAOPAO_PET_STATE_WAITING:
      return "waiting.gif";
    case PAOPAO_PET_STATE_DONE:
      return "done.gif";
    case PAOPAO_PET_STATE_SLEEPING:
      return "sleeping.gif";
    case PAOPAO_PET_STATE_JUMPING:
      return "jumping.gif";
    case PAOPAO_PET_STATE_FAILING:
      return "failed.gif";
    case PAOPAO_PET_STATE_GIDDY:
      return "giddy.gif";
    case PAOPAO_PET_STATE_REVIEW:
      return "review.gif";
    case PAOPAO_PET_STATE_HAPPY:
      return "happy.gif";
    case PAOPAO_PET_STATE_CRYING:
      return "crying.gif";
    case PAOPAO_PET_STATE_ANXIETY:
      return "anxiety.gif";
    case PAOPAO_PET_STATE_TIRED:
      return "tired.gif";
    case PAOPAO_PET_STATE_STAMP:
      return "stamp.gif";
    default:
      return "idle.gif";
  }
}
