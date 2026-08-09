from pathlib import Path


SOURCE = Path("main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc")


def read_source() -> str:
    return SOURCE.read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace_start = source.index("{", start)
    depth = 0
    for index in range(brace_start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace_start : index + 1]
    raise AssertionError(f"function body not found for {signature}")


def test_board_includes_and_initializes_pet_mood():
    source = read_source()

    assert '#include "paopao_pet_mood.h"' in source
    assert '#include "paopao_pet_behavior.h"' in source
    assert "paopao_pet_mood_context_t mood_ = {};" in source
    assert "paopao_pet_behavior_context_t behavior_ = {};" in source
    assert "paopao_pet_mood_init(&mood_, now_ms);" in source
    assert "paopao_pet_behavior_init(&behavior_, now_ms);" in source


def test_service_emotion_routes_through_behavior_director():
    body = function_body(source=read_source(), signature="virtual void SetEmotion(const char* emotion) override")

    assert "paopao_pet_trigger_for_emotion(emotion)" in body
    assert "DisplayLockGuard lock(this);" in body
    assert "DispatchPetServiceEmotionLocked(event, NowMs());" in body
    assert "DispatchPetTrigger(event);" not in body


def test_render_loop_ticks_behavior_before_trigger_tick():
    body = function_body(source=read_source(), signature="void RunRenderLoop()")

    assert "DispatchPetBehaviorTickLocked(now_ms);" in body
    assert body.index("DispatchPetBehaviorTickLocked(now_ms);") < body.index("paopao_pet_trigger_tick(&trigger_, now_ms);")


def test_status_and_chat_events_update_mood_without_bypassing_trigger_state():
    source = read_source()
    status_body = function_body(source, "virtual void SetStatus(const char* status) override")
    chat_body = function_body(source, "virtual void SetChatMessage(const char* role, const char* content) override")

    assert "DispatchPetMoodEvent(PAOPAO_PET_MOOD_EVENT_VOICE_ERROR);" in status_body
    assert "PAOPAO_PET_MOOD_EVENT_CHAT_STARTED" in chat_body
    assert "PAOPAO_PET_MOOD_EVENT_ASSISTANT_REPLY" in chat_body
    assert "DispatchPetVoiceState(" in status_body
    assert "DispatchPetVoiceState(" in chat_body
    assert "SetPetBehaviorVoiceState(" not in status_body
    assert "SetPetBehaviorVoiceState(" not in chat_body


def test_busy_status_remains_idle_like_for_behavior_director():
    body = function_body(source=read_source(), signature="virtual void SetStatus(const char* status) override")

    busy_start = body.index("IsBusyStatus(status)")
    busy_section = body[busy_start : body.index("}", busy_start)]

    assert "PAOPAO_PET_TRIGGER_CONNECTING" in busy_section
    assert "PAOPAO_PET_BEHAVIOR_VOICE_IDLE" in busy_section
    assert "PAOPAO_PET_BEHAVIOR_VOICE_LISTENING" not in busy_section


def test_voice_state_helper_updates_trigger_and_behavior_under_one_lock():
    body = function_body(
        source=read_source(),
        signature="void DispatchPetVoiceState("
    )

    assert "DisplayLockGuard lock(this);" in body
    assert "DispatchPetVoiceStateLocked(" in body

    locked_body = function_body(
        source=read_source(),
        signature="void DispatchPetVoiceStateLocked("
    )

    assert "paopao_pet_trigger_dispatch(&trigger_, trigger_event, now_ms);" in locked_body
    assert "paopao_pet_behavior_set_voice_state(&behavior_, voice_state, now_ms);" in locked_body
    assert "ApplyPetStateIfChanged();" in locked_body


def test_service_emotion_helper_updates_mood_and_behavior_under_one_lock():
    locked_body = function_body(
        source=read_source(),
        signature="void DispatchPetServiceEmotionLocked("
    )

    assert "DispatchPetMoodEventLocked(PAOPAO_PET_MOOD_EVENT_SERVICE_EMOTION, service_trigger, now_ms);" in locked_body
    assert "const paopao_pet_behavior_decision_t decision =" in locked_body
    assert "paopao_pet_behavior_handle_service_trigger(&behavior_, service_trigger, now_ms);" in locked_body
    assert "DispatchPetBehaviorDecisionLocked(decision, now_ms);" in locked_body


def test_behavior_service_and_tick_sync_protected_voice_state_from_base_state():
    source = read_source()
    service_body = function_body(source, "void DispatchPetBehaviorServiceTrigger(")
    tick_body = function_body(source, "void DispatchPetBehaviorTickLocked(")

    assert "SyncPetBehaviorVoiceStateFromBaseStateLocked(now_ms);" in service_body
    assert "SyncPetBehaviorVoiceStateFromBaseStateLocked(now_ms);" in tick_body

    sync_body = function_body(
        source=source,
        signature="void SyncPetBehaviorVoiceStateFromBaseStateLocked("
    )

    assert "case PAOPAO_PET_STATE_SLEEPING:" in sync_body
    assert "PAOPAO_PET_BEHAVIOR_VOICE_SLEEPING" in sync_body
    assert "case PAOPAO_PET_STATE_FAILING:" in sync_body
    assert "PAOPAO_PET_BEHAVIOR_VOICE_FAILING" in sync_body


def test_local_interactions_reset_behavior_idle_timer():
    body = function_body(
        source=read_source(),
        signature="void DispatchLocalPetTriggerLocked("
    )

    assert "RecordPetBehaviorInteractionLocked(now_ms);" in body
    assert body.index("RecordPetBehaviorInteractionLocked(now_ms);") < body.index(
        "paopao_pet_trigger_dispatch(&trigger_, trigger_event, now_ms);"
    )


def test_cmake_wires_behavior_source_for_146_board():
    cmake = Path("main/CMakeLists.txt").read_text(encoding="utf-8")

    assert "paopao_pet_behavior.c" in cmake


def test_device_status_refresh_no_longer_syncs_mood_edges_from_battery_snapshot():
    body = function_body(source=read_source(), signature="virtual void UpdateStatusBar(bool update_all = false) override")

    assert "RefreshBatterySnapshotLocked();" not in body
    assert "SyncPetMoodDeviceStateLocked();" not in body


def test_low_battery_edge_notifies_and_dispatches_tired_mood():
    body = function_body(
        source=read_source(),
        signature="void HandleBatterySnapshot(const xiaoxin_battery_snapshot_t& snapshot)"
    )

    assert "if (snapshot.low_edge && display != nullptr)" in body
    low_edge_start = body.index("if (snapshot.low_edge && display != nullptr)")
    low_edge_block = function_body(
        source=body[low_edge_start:],
        signature="if (snapshot.low_edge && display != nullptr)"
    )
    critical_edge_block = function_body(
        source=body[body.index("if (snapshot.critical_edge)") :],
        signature="if (snapshot.critical_edge)"
    )

    assert "display->ShowLowBatteryNotification();" in low_edge_block
    assert "display->DispatchPetMoodEvent(PAOPAO_PET_MOOD_EVENT_BATTERY_LOW);" in low_edge_block
    assert "PAOPAO_PET_MOOD_EVENT_BATTERY_RECOVERED" not in body
    assert "DispatchPetMoodEvent(" not in critical_edge_block


def test_touch_and_motion_update_mood_but_boot_button_does_not():
    source = read_source()

    assert "DispatchLocalPetTriggerLocked(" in source
    assert "PAOPAO_PET_MOOD_EVENT_LOCAL_TAP" in source
    assert "PAOPAO_PET_MOOD_EVENT_LOCAL_HOLD" in source
    assert "PAOPAO_PET_MOOD_EVENT_LOCAL_DRAG" in source
    assert "DispatchLocalPetTrigger(PAOPAO_PET_TRIGGER_LOCAL_SHAKE, PAOPAO_PET_MOOD_EVENT_LOCAL_SHAKE);" in source

    boot_start = source.index("// Boot Button")
    power_start = source.index("// Power Button")
    boot_section = source[boot_start:power_start]

    assert "DispatchPetTrigger" not in boot_section
    assert "PAOPAO_PET_TRIGGER_LOCAL_TAP" not in boot_section
    assert "PAOPAO_PET_TRIGGER_LOCAL_HOLD" not in boot_section


def test_protected_state_helper_blocks_ordinary_mood_suggestions():
    body = function_body(
        source=read_source(),
        signature="bool ShouldDispatchMoodSuggestionLocked() const"
    )

    assert "switch (trigger_.base_state)" in body
    assert "case PAOPAO_PET_STATE_FAILING:" in body
    assert "case PAOPAO_PET_STATE_SLEEPING:" in body
    assert "case PAOPAO_PET_STATE_WAITING:" in body
    assert "case PAOPAO_PET_STATE_THINKING:" in body
    assert "case PAOPAO_PET_STATE_SPEAKING:" in body
    assert "return false;" in body
    assert "return true;" in body


def test_dispatch_pet_mood_input_updates_context_before_guarding_trigger_dispatch():
    body = function_body(source=read_source(), signature="void DispatchPetMoodInputLocked(const paopao_pet_mood_input_t& input, uint32_t now_ms)")

    assert "paopao_pet_mood_handle_event(&mood_, &input, now_ms);" in body
    assert "suggestion.has_trigger && ShouldDispatchMoodSuggestionLocked()" in body
    assert "paopao_pet_trigger_dispatch(&trigger_, suggestion.trigger, now_ms);" in body
