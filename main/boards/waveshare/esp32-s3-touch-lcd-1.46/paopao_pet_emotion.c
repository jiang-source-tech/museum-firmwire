#include "paopao_pet_emotion.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

static bool contains_ignore_case(const char* text, const char* needle) {
  if (text == NULL || needle == NULL || needle[0] == '\0') {
    return false;
  }

  const size_t needle_len = strlen(needle);
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    size_t i = 0;
    while (i < needle_len && cursor[i] != '\0' &&
           tolower((unsigned char)cursor[i]) == tolower((unsigned char)needle[i])) {
      ++i;
    }
    if (i == needle_len) {
      return true;
    }
  }
  return false;
}

paopao_pet_trigger_event_t paopao_pet_trigger_for_emotion(const char* emotion) {
  if (emotion == NULL || emotion[0] == '\0') {
    return PAOPAO_PET_TRIGGER_NONE;
  }

  if (contains_ignore_case(emotion, "neutral") ||
      contains_ignore_case(emotion, "calm") ||
      contains_ignore_case(emotion, "relaxed") ||
      contains_ignore_case(emotion, "microchip")) {
    return PAOPAO_PET_TRIGGER_SERVICE_NEUTRAL;
  }

  if (contains_ignore_case(emotion, "sleep")) {
    return PAOPAO_PET_TRIGGER_SERVICE_SLEEP;
  }

  if (contains_ignore_case(emotion, "cry") ||
      contains_ignore_case(emotion, "sad") ||
      contains_ignore_case(emotion, "unhappy") ||
      contains_ignore_case(emotion, "upset") ||
      contains_ignore_case(emotion, "lonely")) {
    return PAOPAO_PET_TRIGGER_SERVICE_CRYING;
  }

  if (contains_ignore_case(emotion, "happy") ||
      contains_ignore_case(emotion, "laugh") ||
      contains_ignore_case(emotion, "loving") ||
      contains_ignore_case(emotion, "excited") ||
      contains_ignore_case(emotion, "cool")) {
    return PAOPAO_PET_TRIGGER_SERVICE_HAPPY;
  }

  if (contains_ignore_case(emotion, "angry") ||
      contains_ignore_case(emotion, "annoyed") ||
      contains_ignore_case(emotion, "frustrated") ||
      contains_ignore_case(emotion, "mad") ||
      contains_ignore_case(emotion, "impatient")) {
    return PAOPAO_PET_TRIGGER_SERVICE_ANGRY;
  }

  if (contains_ignore_case(emotion, "anxious") ||
      contains_ignore_case(emotion, "worried") ||
      contains_ignore_case(emotion, "nervous") ||
      contains_ignore_case(emotion, "scared") ||
      contains_ignore_case(emotion, "afraid")) {
    return PAOPAO_PET_TRIGGER_SERVICE_ANXIOUS;
  }

  if (contains_ignore_case(emotion, "tired") ||
      contains_ignore_case(emotion, "weak") ||
      contains_ignore_case(emotion, "low_battery")) {
    return PAOPAO_PET_TRIGGER_SERVICE_TIRED;
  }

  if (contains_ignore_case(emotion, "think") ||
      contains_ignore_case(emotion, "confused") ||
      contains_ignore_case(emotion, "curious")) {
    return PAOPAO_PET_TRIGGER_SERVICE_THINKING;
  }

  if (contains_ignore_case(emotion, "error") ||
      contains_ignore_case(emotion, "fail") ||
      contains_ignore_case(emotion, "shock")) {
    return PAOPAO_PET_TRIGGER_SERVICE_FAILING;
  }

  return PAOPAO_PET_TRIGGER_NONE;
}
