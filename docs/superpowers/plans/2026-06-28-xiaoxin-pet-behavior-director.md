# Xiaoxin Pet Behavior Director Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Xiaoxin's 1.46-inch Paopao pet feel less mechanical by caching service emotions during voice activity, playing them after the reply ends, and adding controlled idle micro-actions.

**Architecture:** Add a small pure-C `paopao_pet_behavior` director between display events and `paopao_pet_trigger`. The director owns pending service emotion, protected voice-state gating, anti-repeat idle variants, and idle timing; the existing trigger module keeps mapping trigger events to pet states and reaction playback.

**Tech Stack:** ESP-IDF C/C++, LVGL display integration, existing Paopao `trigger`, `mood`, `emotion`, and `state` modules, C `assert` unit tests, Python `pytest` path tests.

## Global Constraints

- Scope is only Waveshare ESP32-S3 Touch LCD 1.46 / Xiaoxin Paopao GIF behavior.
- Do not interrupt `speaking_fixed.gif` with service emotions.
- Do not add new GIF assets.
- Do not rewrite the whole `PaopaoPetDisplay` class.
- Strong animations such as failing, crying, anxiety, and stamp must not appear from ordinary idle randomness.
- Randomness must be bounded by cooldown, scene, and recent animation memory.
- Preserve local tap, hold, drag, and shake immediate feedback.

---

## File Structure

- Create `main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.h`
  - Public C interface for the behavior director context, voice-state input, service-emotion input, local interaction input, and tick output.
- Create `main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.c`
  - Pure scheduling logic: pending service trigger, idle micro-action selection, cooldowns, and anti-repeat memory.
- Create `tests/paopao_pet_behavior_test.c`
  - Native C assertions for the director without LVGL or ESP-IDF dependencies.
- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
  - Include the new director, initialize it, notify it from `SetStatus()`, `SetChatMessage()`, `SetEmotion()`, local touch/motion paths, and `RunRenderLoop()`.
- Modify `main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_trigger.c`
  - Remove the old fixed idle `REVIEW` tick path so idle micro-actions have one owner.
- Modify `main/CMakeLists.txt`
  - Add `paopao_pet_behavior.c` to the 1.46 board source list.
- Modify `tests/xiaoxin_pet_mood_integration_path_test.py`
  - Update path assertions so the display is expected to route service emotion through the behavior director.

---

### Task 1: Add Behavior Director Tests and Interface

**Files:**
- Create: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.h`
- Create: `tests/paopao_pet_behavior_test.c`

**Interfaces:**
- Consumes: `paopao_pet_trigger_event_t` and `paopao_pet_state_t` from `paopao_pet_trigger.h`.
- Produces:
  - `void paopao_pet_behavior_init(paopao_pet_behavior_context_t *ctx, uint32_t now_ms);`
  - `void paopao_pet_behavior_set_voice_state(paopao_pet_behavior_context_t *ctx, paopao_pet_behavior_voice_state_t voice_state, uint32_t now_ms);`
  - `paopao_pet_behavior_decision_t paopao_pet_behavior_handle_service_trigger(paopao_pet_behavior_context_t *ctx, paopao_pet_trigger_event_t trigger, uint32_t now_ms);`
  - `void paopao_pet_behavior_record_interaction(paopao_pet_behavior_context_t *ctx, uint32_t now_ms);`
  - `paopao_pet_behavior_decision_t paopao_pet_behavior_tick(paopao_pet_behavior_context_t *ctx, uint32_t now_ms);`

- [ ] **Step 1: Write the behavior director header**

Create `main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.h` with this exact initial API:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "paopao_pet_trigger.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  PAOPAO_PET_BEHAVIOR_VOICE_IDLE = 0,
  PAOPAO_PET_BEHAVIOR_VOICE_LISTENING,
  PAOPAO_PET_BEHAVIOR_VOICE_THINKING,
  PAOPAO_PET_BEHAVIOR_VOICE_SPEAKING,
  PAOPAO_PET_BEHAVIOR_VOICE_SLEEPING,
  PAOPAO_PET_BEHAVIOR_VOICE_FAILING
} paopao_pet_behavior_voice_state_t;

typedef struct {
  bool has_trigger;
  paopao_pet_trigger_event_t trigger;
} paopao_pet_behavior_decision_t;

typedef struct {
  paopao_pet_behavior_voice_state_t voice_state;
  paopao_pet_trigger_event_t pending_service_trigger;
  paopao_pet_state_t last_idle_variant_state;
  uint32_t last_idle_variant_ms;
  uint32_t next_idle_variant_ms;
  uint32_t last_interaction_ms;
  uint8_t idle_variant_index;
} paopao_pet_behavior_context_t;

void paopao_pet_behavior_init(paopao_pet_behavior_context_t *ctx, uint32_t now_ms);

void paopao_pet_behavior_set_voice_state(
  paopao_pet_behavior_context_t *ctx,
  paopao_pet_behavior_voice_state_t voice_state,
  uint32_t now_ms
);

paopao_pet_behavior_decision_t paopao_pet_behavior_handle_service_trigger(
  paopao_pet_behavior_context_t *ctx,
  paopao_pet_trigger_event_t trigger,
  uint32_t now_ms
);

void paopao_pet_behavior_record_interaction(
  paopao_pet_behavior_context_t *ctx,
  uint32_t now_ms
);

paopao_pet_behavior_decision_t paopao_pet_behavior_tick(
  paopao_pet_behavior_context_t *ctx,
  uint32_t now_ms
);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Write failing tests for pending service emotion**

Create `tests/paopao_pet_behavior_test.c` with these tests:

```c
#include <assert.h>
#include <stdio.h>

#include "../main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.h"

static void service_emotion_is_cached_while_speaking(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 1000);

    paopao_pet_behavior_set_voice_state(&ctx, PAOPAO_PET_BEHAVIOR_VOICE_SPEAKING, 1100);
    paopao_pet_behavior_decision_t during_speech =
        paopao_pet_behavior_handle_service_trigger(&ctx, PAOPAO_PET_TRIGGER_SERVICE_HAPPY, 1200);

    assert(!during_speech.has_trigger);
    assert(ctx.pending_service_trigger == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);

    paopao_pet_behavior_set_voice_state(&ctx, PAOPAO_PET_BEHAVIOR_VOICE_IDLE, 3000);
    paopao_pet_behavior_decision_t after_reply =
        paopao_pet_behavior_tick(&ctx, 3000);

    assert(after_reply.has_trigger);
    assert(after_reply.trigger == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);
    assert(ctx.pending_service_trigger == PAOPAO_PET_TRIGGER_NONE);
}

static void latest_service_emotion_wins_during_one_reply(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 1000);

    paopao_pet_behavior_set_voice_state(&ctx, PAOPAO_PET_BEHAVIOR_VOICE_THINKING, 1100);
    assert(!paopao_pet_behavior_handle_service_trigger(&ctx, PAOPAO_PET_TRIGGER_SERVICE_HAPPY, 1200).has_trigger);
    assert(!paopao_pet_behavior_handle_service_trigger(&ctx, PAOPAO_PET_TRIGGER_SERVICE_TIRED, 1300).has_trigger);

    paopao_pet_behavior_set_voice_state(&ctx, PAOPAO_PET_BEHAVIOR_VOICE_IDLE, 3000);
    paopao_pet_behavior_decision_t decision = paopao_pet_behavior_tick(&ctx, 3000);

    assert(decision.has_trigger);
    assert(decision.trigger == PAOPAO_PET_TRIGGER_SERVICE_TIRED);
}

static void neutral_service_emotion_is_ignored(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 1000);

    paopao_pet_behavior_set_voice_state(&ctx, PAOPAO_PET_BEHAVIOR_VOICE_SPEAKING, 1100);
    assert(!paopao_pet_behavior_handle_service_trigger(&ctx, PAOPAO_PET_TRIGGER_SERVICE_NEUTRAL, 1200).has_trigger);
    assert(ctx.pending_service_trigger == PAOPAO_PET_TRIGGER_NONE);
}

static void service_emotion_can_play_immediately_when_idle(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 1000);

    paopao_pet_behavior_decision_t decision =
        paopao_pet_behavior_handle_service_trigger(&ctx, PAOPAO_PET_TRIGGER_SERVICE_HAPPY, 1200);

    assert(decision.has_trigger);
    assert(decision.trigger == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);
    assert(ctx.pending_service_trigger == PAOPAO_PET_TRIGGER_NONE);
}

int main(void) {
    service_emotion_is_cached_while_speaking();
    latest_service_emotion_wins_during_one_reply();
    neutral_service_emotion_is_ignored();
    service_emotion_can_play_immediately_when_idle();
    puts("paopao_pet_behavior pending emotion tests passed");
    return 0;
}
```

- [ ] **Step 3: Run test to verify it fails**

Run:

```powershell
gcc -std=c11 -Wall -Wextra -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 tests/paopao_pet_behavior_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.c -o build/paopao_pet_behavior_test.exe
```

Expected: FAIL because `paopao_pet_behavior.c` does not exist yet.

- [ ] **Step 4: Commit**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.h tests/paopao_pet_behavior_test.c
git commit -m "test: define pet behavior director contract"
```

---

### Task 2: Implement Pending Service Emotion

**Files:**
- Create: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.c`
- Modify: `tests/paopao_pet_behavior_test.c`

**Interfaces:**
- Consumes: Task 1 header.
- Produces: pending service emotion behavior that can be used by display integration.

- [ ] **Step 1: Add minimal implementation**

Create `main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.c`:

```c
#include "paopao_pet_behavior.h"

#include <stddef.h>
#include <string.h>

static const uint32_t k_idle_variant_min_ms = 12000;

static paopao_pet_behavior_decision_t no_decision(void) {
  const paopao_pet_behavior_decision_t decision = {
    .has_trigger = false,
    .trigger = PAOPAO_PET_TRIGGER_NONE,
  };
  return decision;
}

static paopao_pet_behavior_decision_t trigger_decision(paopao_pet_trigger_event_t trigger) {
  const paopao_pet_behavior_decision_t decision = {
    .has_trigger = trigger != PAOPAO_PET_TRIGGER_NONE,
    .trigger = trigger,
  };
  return decision;
}

static bool is_protected_voice_state(paopao_pet_behavior_voice_state_t voice_state) {
  return voice_state == PAOPAO_PET_BEHAVIOR_VOICE_LISTENING ||
         voice_state == PAOPAO_PET_BEHAVIOR_VOICE_THINKING ||
         voice_state == PAOPAO_PET_BEHAVIOR_VOICE_SPEAKING ||
         voice_state == PAOPAO_PET_BEHAVIOR_VOICE_SLEEPING ||
         voice_state == PAOPAO_PET_BEHAVIOR_VOICE_FAILING;
}

static bool is_ignorable_service_trigger(paopao_pet_trigger_event_t trigger) {
  return trigger == PAOPAO_PET_TRIGGER_NONE ||
         trigger == PAOPAO_PET_TRIGGER_SERVICE_NEUTRAL ||
         trigger == PAOPAO_PET_TRIGGER_SERVICE_GIDDY;
}

static uint32_t next_idle_variant_time(uint32_t now_ms) {
  return now_ms + k_idle_variant_min_ms;
}

void paopao_pet_behavior_init(paopao_pet_behavior_context_t *ctx, uint32_t now_ms) {
  if (ctx == NULL) {
    return;
  }
  memset(ctx, 0, sizeof(*ctx));
  ctx->voice_state = PAOPAO_PET_BEHAVIOR_VOICE_IDLE;
  ctx->pending_service_trigger = PAOPAO_PET_TRIGGER_NONE;
  ctx->last_idle_variant_state = PAOPAO_PET_STATE_IDLE;
  ctx->last_interaction_ms = now_ms;
  ctx->next_idle_variant_ms = next_idle_variant_time(now_ms);
}

void paopao_pet_behavior_set_voice_state(
  paopao_pet_behavior_context_t *ctx,
  paopao_pet_behavior_voice_state_t voice_state,
  uint32_t now_ms
) {
  if (ctx == NULL) {
    return;
  }
  ctx->voice_state = voice_state;
  if (voice_state != PAOPAO_PET_BEHAVIOR_VOICE_IDLE) {
    ctx->last_interaction_ms = now_ms;
  }
}

paopao_pet_behavior_decision_t paopao_pet_behavior_handle_service_trigger(
  paopao_pet_behavior_context_t *ctx,
  paopao_pet_trigger_event_t trigger,
  uint32_t now_ms
) {
  (void)now_ms;
  if (ctx == NULL || is_ignorable_service_trigger(trigger)) {
    return no_decision();
  }
  if (is_protected_voice_state(ctx->voice_state)) {
    ctx->pending_service_trigger = trigger;
    return no_decision();
  }
  return trigger_decision(trigger);
}

void paopao_pet_behavior_record_interaction(
  paopao_pet_behavior_context_t *ctx,
  uint32_t now_ms
) {
  if (ctx == NULL) {
    return;
  }
  ctx->last_interaction_ms = now_ms;
  ctx->next_idle_variant_ms = next_idle_variant_time(now_ms);
}

paopao_pet_behavior_decision_t paopao_pet_behavior_tick(
  paopao_pet_behavior_context_t *ctx,
  uint32_t now_ms
) {
  if (ctx == NULL) {
    return no_decision();
  }
  if (ctx->voice_state == PAOPAO_PET_BEHAVIOR_VOICE_IDLE &&
      ctx->pending_service_trigger != PAOPAO_PET_TRIGGER_NONE) {
    const paopao_pet_trigger_event_t pending = ctx->pending_service_trigger;
    ctx->pending_service_trigger = PAOPAO_PET_TRIGGER_NONE;
    ctx->last_interaction_ms = now_ms;
    ctx->next_idle_variant_ms = next_idle_variant_time(now_ms);
    return trigger_decision(pending);
  }
  return no_decision();
}
```

- [ ] **Step 2: Run behavior test**

Run:

```powershell
New-Item -ItemType Directory -Force build | Out-Null
gcc -std=c11 -Wall -Wextra -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 tests/paopao_pet_behavior_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.c -o build/paopao_pet_behavior_test.exe
.\build\paopao_pet_behavior_test.exe
```

Expected: PASS and prints `paopao_pet_behavior pending emotion tests passed`.

- [ ] **Step 3: Commit**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.c tests/paopao_pet_behavior_test.c
git commit -m "feat: cache service emotions during voice activity"
```

---

### Task 3: Add Idle Micro-Actions and Anti-Repetition

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.c`
- Modify: `tests/paopao_pet_behavior_test.c`

**Interfaces:**
- Consumes: `paopao_pet_behavior_tick()`.
- Produces: idle-only decisions for `SERVICE_THINKING`, `SERVICE_HAPPY`, `SERVICE_TIRED`, and a local `SERVICE_NEUTRAL`-free pool.

- [ ] **Step 1: Add failing idle tests**

Append these tests before `main()` in `tests/paopao_pet_behavior_test.c`, and call them from `main()`:

```c
static void idle_tick_chooses_light_micro_actions_without_repeating(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 0);

    paopao_pet_behavior_decision_t first = paopao_pet_behavior_tick(&ctx, 12000);
    assert(first.has_trigger);
    assert(first.trigger == PAOPAO_PET_TRIGGER_SERVICE_THINKING);

    paopao_pet_behavior_decision_t during_cooldown = paopao_pet_behavior_tick(&ctx, 13000);
    assert(!during_cooldown.has_trigger);

    paopao_pet_behavior_decision_t second = paopao_pet_behavior_tick(&ctx, 25000);
    assert(second.has_trigger);
    assert(second.trigger == PAOPAO_PET_TRIGGER_SERVICE_HAPPY);
    assert(second.trigger != first.trigger);
}

static void idle_tick_does_not_emit_strong_reactions(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 0);

    for (uint32_t now_ms = 12000; now_ms < 90000; now_ms += 13000) {
        paopao_pet_behavior_decision_t decision = paopao_pet_behavior_tick(&ctx, now_ms);
        if (!decision.has_trigger) {
            continue;
        }
        assert(decision.trigger != PAOPAO_PET_TRIGGER_SERVICE_FAILING);
        assert(decision.trigger != PAOPAO_PET_TRIGGER_SERVICE_CRYING);
        assert(decision.trigger != PAOPAO_PET_TRIGGER_SERVICE_ANXIOUS);
        assert(decision.trigger != PAOPAO_PET_TRIGGER_SERVICE_ANGRY);
    }
}

static void local_interaction_resets_idle_micro_action_timer(void) {
    paopao_pet_behavior_context_t ctx;
    paopao_pet_behavior_init(&ctx, 0);

    paopao_pet_behavior_record_interaction(&ctx, 10000);
    assert(!paopao_pet_behavior_tick(&ctx, 11999).has_trigger);
    assert(!paopao_pet_behavior_tick(&ctx, 21999).has_trigger);
    assert(paopao_pet_behavior_tick(&ctx, 22000).has_trigger);
}
```

Update `main()`:

```c
int main(void) {
    service_emotion_is_cached_while_speaking();
    latest_service_emotion_wins_during_one_reply();
    neutral_service_emotion_is_ignored();
    service_emotion_can_play_immediately_when_idle();
    idle_tick_chooses_light_micro_actions_without_repeating();
    idle_tick_does_not_emit_strong_reactions();
    local_interaction_resets_idle_micro_action_timer();
    puts("paopao_pet_behavior tests passed");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
gcc -std=c11 -Wall -Wextra -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 tests/paopao_pet_behavior_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.c -o build/paopao_pet_behavior_test.exe
.\build\paopao_pet_behavior_test.exe
```

Expected: FAIL at `idle_tick_chooses_light_micro_actions_without_repeating`.

- [ ] **Step 3: Implement bounded idle micro-action selection**

In `paopao_pet_behavior.c`, add these helpers after `next_idle_variant_time()`:

```c
static bool time_reached(uint32_t now_ms, uint32_t target_ms) {
  return (int32_t)(now_ms - target_ms) >= 0;
}

static paopao_pet_trigger_event_t idle_variant_at(uint8_t index) {
  static const paopao_pet_trigger_event_t variants[] = {
    PAOPAO_PET_TRIGGER_SERVICE_THINKING,
    PAOPAO_PET_TRIGGER_SERVICE_HAPPY,
    PAOPAO_PET_TRIGGER_SERVICE_TIRED,
  };
  return variants[index % (uint8_t)(sizeof(variants) / sizeof(variants[0]))];
}
```

Then replace `paopao_pet_behavior_tick()` with:

```c
paopao_pet_behavior_decision_t paopao_pet_behavior_tick(
  paopao_pet_behavior_context_t *ctx,
  uint32_t now_ms
) {
  if (ctx == NULL) {
    return no_decision();
  }

  if (ctx->voice_state != PAOPAO_PET_BEHAVIOR_VOICE_IDLE) {
    return no_decision();
  }

  if (ctx->pending_service_trigger != PAOPAO_PET_TRIGGER_NONE) {
    const paopao_pet_trigger_event_t pending = ctx->pending_service_trigger;
    ctx->pending_service_trigger = PAOPAO_PET_TRIGGER_NONE;
    ctx->last_interaction_ms = now_ms;
    ctx->next_idle_variant_ms = next_idle_variant_time(now_ms);
    return trigger_decision(pending);
  }

  if (!time_reached(now_ms, ctx->next_idle_variant_ms)) {
    return no_decision();
  }

  const paopao_pet_trigger_event_t trigger = idle_variant_at(ctx->idle_variant_index);
  ctx->idle_variant_index++;
  ctx->last_idle_variant_ms = now_ms;
  ctx->next_idle_variant_ms = next_idle_variant_time(now_ms);
  return trigger_decision(trigger);
}
```

- [ ] **Step 4: Run behavior test**

Run:

```powershell
gcc -std=c11 -Wall -Wextra -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 tests/paopao_pet_behavior_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.c -o build/paopao_pet_behavior_test.exe
.\build\paopao_pet_behavior_test.exe
```

Expected: PASS and prints `paopao_pet_behavior tests passed`.

- [ ] **Step 5: Commit**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.c tests/paopao_pet_behavior_test.c
git commit -m "feat: add bounded idle pet micro-actions"
```

---

### Task 4: Move Fixed Idle Variant Out of Trigger

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_trigger.c`
- Modify: `tests/paopao_pet_trigger_test.c`

**Interfaces:**
- Consumes: behavior director owns idle micro-actions from Task 3.
- Produces: `paopao_pet_trigger_tick()` only expires reactions and handles sleep timeout.

- [ ] **Step 1: Update trigger test expectation**

Replace `idle_variant_uses_review_not_giddy()` in `tests/paopao_pet_trigger_test.c` with:

```c
static void trigger_tick_does_not_own_idle_micro_actions(void) {
    paopao_pet_trigger_context_t ctx;
    paopao_pet_trigger_init(&ctx, 0);

    assert(paopao_pet_trigger_tick(&ctx, 19999) == PAOPAO_PET_STATE_IDLE);
    assert(paopao_pet_trigger_tick(&ctx, 20000) == PAOPAO_PET_STATE_IDLE);
    assert(paopao_pet_trigger_tick(&ctx, 59999) == PAOPAO_PET_STATE_IDLE);
    assert(paopao_pet_trigger_tick(&ctx, 60000) == PAOPAO_PET_STATE_SLEEPING);
}
```

Update the call in `main()`:

```c
trigger_tick_does_not_own_idle_micro_actions();
```

- [ ] **Step 2: Run trigger test to verify it fails**

Run:

```powershell
gcc -std=c11 -Wall -Wextra -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 tests/paopao_pet_trigger_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_trigger.c -o build/paopao_pet_trigger_test.exe
.\build\paopao_pet_trigger_test.exe
```

Expected: FAIL because tick still emits `PAOPAO_PET_STATE_REVIEW` at the old idle variant time.

- [ ] **Step 3: Remove old fixed review idle branch**

In `paopao_pet_trigger.c`, delete the `else if` branch in `paopao_pet_trigger_tick()` that checks `time_reached(now_ms, ctx->next_idle_variant_ms)` and calls `paopao_pet_trigger_play_reaction(... PAOPAO_PET_STATE_REVIEW ...)`.

The resulting function body should be:

```c
paopao_pet_state_t paopao_pet_trigger_tick(paopao_pet_trigger_context_t *ctx, uint32_t now_ms) {
  if (ctx->reaction_active && time_reached(now_ms, ctx->reaction_until_ms)) {
    ctx->reaction_active = false;
  }

  if (ctx->base_state == PAOPAO_PET_STATE_IDLE &&
      now_ms - ctx->last_interaction_ms >= k_sleep_idle_timeout_ms) {
    set_base_state(ctx, PAOPAO_PET_STATE_SLEEPING, now_ms, true);
  }

  ctx->displayed_state = current_display_state(ctx);
  return ctx->displayed_state;
}
```

- [ ] **Step 4: Run trigger and behavior tests**

Run:

```powershell
gcc -std=c11 -Wall -Wextra -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 tests/paopao_pet_trigger_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_trigger.c -o build/paopao_pet_trigger_test.exe
.\build\paopao_pet_trigger_test.exe
gcc -std=c11 -Wall -Wextra -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 tests/paopao_pet_behavior_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.c -o build/paopao_pet_behavior_test.exe
.\build\paopao_pet_behavior_test.exe
```

Expected: both tests PASS.

- [ ] **Step 5: Commit**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_trigger.c tests/paopao_pet_trigger_test.c
git commit -m "refactor: move idle pet variants to behavior director"
```

---

### Task 5: Wire Behavior Director Into the 1.46 Display

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc`
- Modify: `main/CMakeLists.txt`
- Modify: `tests/xiaoxin_pet_mood_integration_path_test.py`

**Interfaces:**
- Consumes: `paopao_pet_behavior_*` APIs from Tasks 1-3.
- Produces: service emotions are routed through behavior decisions; render-loop ticks can dispatch pending emotion and idle micro-actions.

- [ ] **Step 1: Add failing path assertions**

In `tests/xiaoxin_pet_mood_integration_path_test.py`, update `test_board_includes_and_initializes_pet_mood()` to also require behavior:

```python
def test_board_includes_and_initializes_pet_mood():
    source = read_source()

    assert '#include "paopao_pet_mood.h"' in source
    assert '#include "paopao_pet_behavior.h"' in source
    assert "paopao_pet_mood_context_t mood_ = {};" in source
    assert "paopao_pet_behavior_context_t behavior_ = {};" in source
    assert "paopao_pet_mood_init(&mood_, now_ms);" in source
    assert "paopao_pet_behavior_init(&behavior_, now_ms);" in source
```

Replace `test_service_emotion_routes_through_mood_cooldown()` with:

```python
def test_service_emotion_routes_through_behavior_director():
    body = function_body(source=read_source(), signature="virtual void SetEmotion(const char* emotion) override")

    assert "paopao_pet_trigger_for_emotion(emotion)" in body
    assert "DispatchPetBehaviorServiceTrigger(event);" in body
    assert "DispatchPetMoodEvent(PAOPAO_PET_MOOD_EVENT_SERVICE_EMOTION, event);" in body
    assert "DispatchPetTrigger(event);" not in body
```

Add:

```python
def test_render_loop_ticks_behavior_before_trigger_tick():
    body = function_body(source=read_source(), signature="void RunRenderLoop()")

    assert "DispatchPetBehaviorTickLocked(now_ms);" in body
    assert body.index("DispatchPetBehaviorTickLocked(now_ms);") < body.index("paopao_pet_trigger_tick(&trigger_, now_ms);")
```

- [ ] **Step 2: Run path test to verify it fails**

Run:

```powershell
pytest -q tests/xiaoxin_pet_mood_integration_path_test.py
```

Expected: FAIL because the display does not include or initialize behavior yet.

- [ ] **Step 3: Include and initialize the behavior director**

In `esp32-s3-touch-lcd-1.46.cc`, add the include inside the existing `extern "C"` block:

```cpp
#include "paopao_pet_behavior.h"
```

Near the existing member field:

```cpp
paopao_pet_mood_context_t mood_ = {};
```

add:

```cpp
paopao_pet_behavior_context_t behavior_ = {};
```

Near the existing constructor/init code:

```cpp
paopao_pet_mood_init(&mood_, now_ms);
```

add:

```cpp
paopao_pet_behavior_init(&behavior_, now_ms);
```

- [ ] **Step 4: Add display-side behavior helper methods**

In `PaopaoPetDisplay`, add these methods near existing pet dispatch helpers:

```cpp
void DispatchPetBehaviorDecisionLocked(const paopao_pet_behavior_decision_t& decision, uint32_t now_ms) {
    if (!decision.has_trigger) {
        return;
    }
    paopao_pet_trigger_dispatch(&trigger_, decision.trigger, now_ms);
}

void DispatchPetBehaviorServiceTrigger(paopao_pet_trigger_event_t service_trigger) {
    DisplayLockGuard lock(this);
    const uint32_t now_ms = NowMs();
    const paopao_pet_behavior_decision_t decision =
        paopao_pet_behavior_handle_service_trigger(&behavior_, service_trigger, now_ms);
    DispatchPetBehaviorDecisionLocked(decision, now_ms);
}

void DispatchPetBehaviorTickLocked(uint32_t now_ms) {
    const paopao_pet_behavior_decision_t decision =
        paopao_pet_behavior_tick(&behavior_, now_ms);
    DispatchPetBehaviorDecisionLocked(decision, now_ms);
}

void SetPetBehaviorVoiceStateLocked(paopao_pet_behavior_voice_state_t voice_state, uint32_t now_ms) {
    paopao_pet_behavior_set_voice_state(&behavior_, voice_state, now_ms);
}

void RecordPetBehaviorInteractionLocked(uint32_t now_ms) {
    paopao_pet_behavior_record_interaction(&behavior_, now_ms);
}
```

- [ ] **Step 5: Route `SetEmotion()` through behavior**

Replace the body of `SetEmotion()` with:

```cpp
virtual void SetEmotion(const char* emotion) override {
    const paopao_pet_trigger_event_t event = paopao_pet_trigger_for_emotion(emotion);
    if (event == PAOPAO_PET_TRIGGER_NONE) {
        return;
    }

    DispatchPetMoodEvent(PAOPAO_PET_MOOD_EVENT_SERVICE_EMOTION, event);
    DispatchPetBehaviorServiceTrigger(event);
}
```

- [ ] **Step 6: Notify behavior from voice state changes**

In `SetStatus()`, immediately after each existing `DispatchPetTrigger(...)` call for voice state, add:

```cpp
SetPetBehaviorVoiceStateLocked(PAOPAO_PET_BEHAVIOR_VOICE_LISTENING, NowMs());
```

for listening/waiting states,

```cpp
SetPetBehaviorVoiceStateLocked(PAOPAO_PET_BEHAVIOR_VOICE_SPEAKING, NowMs());
```

for speaking,

```cpp
SetPetBehaviorVoiceStateLocked(PAOPAO_PET_BEHAVIOR_VOICE_THINKING, NowMs());
```

for thinking, and

```cpp
SetPetBehaviorVoiceStateLocked(PAOPAO_PET_BEHAVIOR_VOICE_IDLE, NowMs());
```

for idle.

If the method is outside a display lock at those points, wrap the behavior call in the same lock style used by nearby `DispatchPetTrigger()` helpers rather than directly mutating `behavior_`.

- [ ] **Step 7: Notify behavior from chat role changes**

In `SetChatMessage()`, add matching behavior voice notifications next to existing trigger dispatch:

```cpp
SetPetBehaviorVoiceStateLocked(PAOPAO_PET_BEHAVIOR_VOICE_THINKING, NowMs());
```

when the user message drives thinking, and:

```cpp
SetPetBehaviorVoiceStateLocked(PAOPAO_PET_BEHAVIOR_VOICE_SPEAKING, NowMs());
```

when the assistant message drives speaking.

- [ ] **Step 8: Record local interactions**

In `DispatchLocalPetTriggerLocked(...)`, after mood handling and before or after `paopao_pet_trigger_dispatch(&trigger_, trigger_event, now_ms);`, add:

```cpp
RecordPetBehaviorInteractionLocked(now_ms);
```

This resets the idle micro-action timer for tap, hold, drag, and shake.

- [ ] **Step 9: Tick behavior before trigger in render loop**

In `RunRenderLoop()`, inside the display lock and before:

```cpp
paopao_pet_trigger_tick(&trigger_, now_ms);
```

add:

```cpp
DispatchPetBehaviorTickLocked(now_ms);
```

- [ ] **Step 10: Add behavior source to CMake**

In `main/CMakeLists.txt`, under the 1.46 source list that already contains `paopao_pet_emotion.c` and `paopao_pet_mood.c`, add:

```cmake
${CMAKE_CURRENT_SOURCE_DIR}/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.c
```

- [ ] **Step 11: Run path and C tests**

Run:

```powershell
pytest -q tests/xiaoxin_pet_mood_integration_path_test.py
gcc -std=c11 -Wall -Wextra -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 tests/paopao_pet_behavior_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.c -o build/paopao_pet_behavior_test.exe
.\build\paopao_pet_behavior_test.exe
gcc -std=c11 -Wall -Wextra -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 tests/paopao_pet_trigger_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_trigger.c -o build/paopao_pet_trigger_test.exe
.\build\paopao_pet_trigger_test.exe
```

Expected: all PASS.

- [ ] **Step 12: Commit**

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc main/CMakeLists.txt tests/xiaoxin_pet_mood_integration_path_test.py
git commit -m "feat: wire pet behavior director into xiaoxin display"
```

---

### Task 6: Verification and Hardware Smoke Test

**Files:**
- Modify only if verification exposes a concrete defect.

**Interfaces:**
- Consumes: all previous tasks.
- Produces: verified behavior against regression tests and hardware-observable acceptance criteria.

- [ ] **Step 1: Run focused automated verification**

Run:

```powershell
pytest -q tests/xiaoxin_pet_mood_integration_path_test.py
gcc -std=c11 -Wall -Wextra -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 tests/paopao_pet_behavior_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_behavior.c -o build/paopao_pet_behavior_test.exe
.\build\paopao_pet_behavior_test.exe
gcc -std=c11 -Wall -Wextra -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 tests/paopao_pet_trigger_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_trigger.c -o build/paopao_pet_trigger_test.exe
.\build\paopao_pet_trigger_test.exe
gcc -std=c11 -Wall -Wextra -I main/boards/waveshare/esp32-s3-touch-lcd-1.46 tests/paopao_pet_emotion_test.c main/boards/waveshare/esp32-s3-touch-lcd-1.46/paopao_pet_emotion.c -o build/paopao_pet_emotion_test.exe
.\build\paopao_pet_emotion_test.exe
```

Expected: Python path tests PASS and each C test executable prints its success message.

- [ ] **Step 2: Build firmware for the target board**

Run the repository's normal ESP-IDF build command for the Waveshare ESP32-S3 Touch LCD 1.46 configuration:

```powershell
idf.py build
```

Expected: build succeeds with `paopao_pet_behavior.c` included in the component source list.

- [ ] **Step 3: Hardware smoke test service emotion**

On hardware, perform one voice reply where the WebSocket JSON includes:

```json
{"type":"llm","emotion":"happy"}
```

Expected visible sequence:

```text
thinking.gif or speaking_fixed.gif during the reply
happy.gif immediately after TTS stop / idle transition
idle.gif after the reaction duration
```

- [ ] **Step 4: Hardware smoke test idle life**

Leave the pet idle for 60 seconds without touch, shake, or voice interaction.

Expected: at least two light micro-actions appear from the allowed pool during the minute, and no `failed.gif`, `crying.gif`, `anxiety.gif`, or `stamp.gif` appears without an explicit event.

- [ ] **Step 5: Commit verification fixes**

If verification required code changes:

```powershell
git add main/boards/waveshare/esp32-s3-touch-lcd-1.46 tests main/CMakeLists.txt
git commit -m "fix: stabilize pet behavior director integration"
```

If verification did not require code changes, do not create an empty commit.

---

## Self-Review

- Spec coverage: pending service emotion, end-of-answer reaction, idle micro-actions, anti-repetition, strong-animation exclusion, local-interaction preservation, and 1.46-only scope are covered by Tasks 1-6.
- Open-ended wording scan: every code-changing step includes concrete code or an exact deletion target.
- Type consistency: all public names use the `paopao_pet_behavior_*` prefix and all trigger values are existing `paopao_pet_trigger_event_t` constants.
- Scope control: the plan keeps behavior scheduling outside `paopao_pet_trigger`, and does not change GIF assets, WebSocket protocol, TTS/STT, or LVGL rendering internals.
