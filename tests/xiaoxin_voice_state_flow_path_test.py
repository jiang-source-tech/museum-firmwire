import json
from pathlib import Path


DEVICE_STATE = Path("main/device_state.h")
STATE_MACHINE = Path("main/device_state_machine.cc")
APPLICATION = Path("main/application.cc")
APPLICATION_HEADER = Path("main/application.h")
LANG_CONFIG = Path("main/assets/lang_config.h")
EN_US_LANGUAGE = Path("main/assets/locales/en-US/language.json")
ZH_CN_LANGUAGE = Path("main/assets/locales/zh-CN/language.json")


def read_source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"function body not found: {signature}")


def switch_case(source: str, marker: str, next_marker: str) -> str:
    start = source.index(marker)
    end = source.index(next_marker, start)
    return source[start:end]


def branch_block(source: str, marker: str) -> str:
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"branch block not found: {marker}")


def test_thinking_state_is_declared_between_listening_and_speaking():
    source = read_source(DEVICE_STATE)

    assert "kDeviceStateListening" in source
    assert "kDeviceStateThinking" in source
    assert "kDeviceStateSpeaking" in source
    assert source.index("kDeviceStateListening") < source.index("kDeviceStateThinking")
    assert source.index("kDeviceStateThinking") < source.index("kDeviceStateSpeaking")


def test_state_machine_names_and_transitions_include_thinking():
    source = read_source(STATE_MACHINE)

    assert '"thinking"' in source

    listening_case = switch_case(source, "case kDeviceStateListening:", "case kDeviceStateThinking:")
    assert "to == kDeviceStateThinking" in listening_case
    assert "to == kDeviceStateSpeaking" in listening_case
    assert "to == kDeviceStateIdle" in listening_case

    thinking_case = switch_case(source, "case kDeviceStateThinking:", "case kDeviceStateSpeaking:")
    assert "to == kDeviceStateSpeaking" in thinking_case
    assert "to == kDeviceStateListening" in thinking_case
    assert "to == kDeviceStateIdle" in thinking_case


def test_lang_config_exposes_thinking_status_text():
    source = read_source(LANG_CONFIG)

    assert 'constexpr const char* THINKING = "思考中...";' in source


def test_language_sources_define_thinking_status_text():
    en_us = json.loads(read_source(EN_US_LANGUAGE))
    zh_cn = json.loads(read_source(ZH_CN_LANGUAGE))

    assert en_us["strings"]["THINKING"] == "Thinking..."
    assert zh_cn["strings"]["THINKING"] == "思考中..."


def test_application_renders_thinking_state_and_pauses_audio_processing():
    body = function_body(read_source(APPLICATION), "void Application::HandleStateChangedEvent()")

    thinking_case = switch_case(body, "case kDeviceStateThinking:", "case kDeviceStateSpeaking:")
    assert "display->SetStatus(Lang::Strings::THINKING);" in thinking_case
    assert 'display->SetEmotion("neutral");' in thinking_case
    assert "audio_service_.EnableVoiceProcessing(false);" in thinking_case
    assert "audio_service_.EnableWakeWordDetection(false);" in thinking_case


def test_stt_text_moves_listening_to_thinking_before_user_subtitle():
    body = function_body(read_source(APPLICATION), "bool Application::InitializeProtocol()")
    stt_section = switch_case(body, 'strcmp(type->valuestring, "stt") == 0', 'strcmp(type->valuestring, "llm") == 0')

    assert "if (GetDeviceState() == kDeviceStateListening && !IsSttThinkingSuppressed())" in stt_section
    assert "SetDeviceState(kDeviceStateThinking);" in stt_section
    assert 'display->SetChatMessage("user", message.c_str());' in stt_section
    assert stt_section.index("SetDeviceState(kDeviceStateThinking);") < stt_section.index('display->SetChatMessage("user", message.c_str());')


def test_tts_start_moves_thinking_to_speaking_and_tts_stop_leaves_speaking():
    body = function_body(read_source(APPLICATION), "bool Application::InitializeProtocol()")
    tts_section = switch_case(body, 'strcmp(type->valuestring, "tts") == 0', 'strcmp(type->valuestring, "stt") == 0')

    assert "if (!cJSON_IsString(state))" in tts_section
    assert "SetDeviceState(kDeviceStateSpeaking);" in tts_section
    assert "if (GetDeviceState() != kDeviceStateSpeaking) return;" in tts_section
    assert "SetDeviceState(kDeviceStateListening);" in tts_section
    assert "SetDeviceState(kDeviceStateIdle);" in tts_section


def test_reliable_tts_dispatch_preserves_legacy_no_sentence_id_path():
    body = function_body(read_source(APPLICATION), "bool Application::InitializeProtocol()")
    tts_section = switch_case(body, 'strcmp(type->valuestring, "tts") == 0', 'strcmp(type->valuestring, "stt") == 0')

    assert "const bool has_sentence_id" in tts_section
    assert "HandleReliableTtsStart(sentence_id);" in tts_section
    assert "HandleReliableTtsStop(sentence_id);" in tts_section
    assert "if (has_sentence_id)" in tts_section
    assert "aborted_ = false;" in tts_section
    assert "SetDeviceState(kDeviceStateSpeaking);" in tts_section


def test_reliable_tts_pump_runs_before_scheduled_callbacks():
    body = function_body(read_source(APPLICATION), "void Application::Run()")

    assert "MAIN_EVENT_TTS_AUDIO_PUMP" in body
    assert "HandleTtsAudioPump();" in body
    assert body.index("HandleTtsAudioPump();") < body.index("if (bits & MAIN_EVENT_SCHEDULE)")


def test_speaking_state_does_not_reset_decoder_owned_by_reliable_tts():
    body = function_body(read_source(APPLICATION), "void Application::HandleStateChangedEvent()")
    speaking_case = switch_case(body, "case kDeviceStateSpeaking:", "case kDeviceStateWifiConfiguring:")

    assert "if (!tts_playback_session_.OwnsPlaybackPipeline())" in speaking_case
    guard = branch_block(speaking_case, "if (!tts_playback_session_.OwnsPlaybackPipeline())")
    assert "audio_service_.ResetDecoder();" in guard


def test_connection_and_user_abort_invalidate_reliable_tts_generation():
    source = read_source(APPLICATION)
    close_body = source[
        source.index("protocol_->OnAudioChannelClosed"):
        source.index("protocol_->OnIncomingJson")
    ]
    close_cleanup = function_body(source, "void Application::RunAudioChannelCloseCleanup")
    disconnect_body = function_body(source, "void Application::HandleNetworkDisconnectedEvent()")
    abort_body = function_body(source, "void Application::AbortSpeaking(AbortReason reason)")

    assert 'tts_playback_session_.AbortCurrent("connection_closed");' in close_body
    assert 'tts_playback_session_.AbortCurrent("connection_closed");' in disconnect_body
    assert 'tts_playback_session_.AbortCurrent("interrupted");' in abort_body
    assert "audio_service_.ResetDecoder();" in close_cleanup
    for body in (disconnect_body, abort_body):
        assert "audio_service_.ResetDecoder();" in body


def test_audio_channel_close_idle_is_guarded_by_connection_epoch_and_generation():
    header = read_source(APPLICATION_HEADER)
    source = read_source(APPLICATION)
    init_body = function_body(source, "bool Application::InitializeProtocol()")
    opened = switch_case(
        init_body,
        "protocol_->OnAudioChannelOpened",
        "protocol_->OnAudioChannelClosed",
    )
    closed = switch_case(
        init_body,
        "protocol_->OnAudioChannelClosed",
        "protocol_->OnIncomingJson",
    )
    opened_handler = function_body(source, "void Application::HandleAudioChannelOpened")
    cleanup = function_body(source, "void Application::RunAudioChannelCloseCleanup")

    assert "uint32_t tts_connection_epoch_ = 0;" in header
    assert '#include "audio/tts_ownership_gate.h"' in header
    assert "TtsOwnershipGate tts_ownership_gate_;" in header
    assert "bool audio_open_request_pending_ = false;" in header
    assert "HandleAudioChannelOpened(codec);" in opened
    assert "++tts_connection_epoch_;" in opened_handler
    assert "close_epoch = ++tts_connection_epoch_;" in closed
    assert "close_generation = tts_playback_session_.generation();" in closed
    assert "[this, close_epoch, close_generation]" in closed
    assert "RunAudioChannelCloseCleanup(close_epoch, close_generation);" in closed
    assert "tts_ownership_gate_.ReserveCleanup()" in cleanup
    assert "tts_ownership_gate_.ReleaseCleanup()" in cleanup
    reset_index = cleanup.index("audio_service_.ResetDecoder();")
    assert cleanup.index("tts_ownership_gate_.ReserveCleanup()") < reset_index
    assert reset_index < cleanup.index("tts_ownership_gate_.ReleaseCleanup()")
    assert "!audio_open_request_pending_" in cleanup
    assert cleanup.index("!audio_open_request_pending_") < cleanup.index(
        "state_machine_.CommitTransition(kDeviceStateIdle)"
    )


def test_cleanup_gate_nonblocking_defers_open_and_rejects_start_ownership():
    header = read_source(APPLICATION_HEADER)
    source = read_source(APPLICATION)

    assert "bool DeferUntilTtsCleanupComplete(std::function<void()>&& callback);" in header
    assert "void ScheduleAudioOpenRequest(std::function<void()>&& callback);" in header
    assert "void ConsumeAudioOpenRequest();" in header
    for signature in (
        "void Application::ContinueOpenAudioChannel(ListeningMode mode)",
        "void Application::ContinueWakeWordInvoke(const std::string& wake_word)",
    ):
        body = function_body(source, signature)
        assert "DeferUntilTtsCleanupComplete" in body
        assert "ConsumeAudioOpenRequest();" in body
        assert body.index("DeferUntilTtsCleanupComplete") < body.index(
            "ConsumeAudioOpenRequest();"
        )
        assert body.index("ConsumeAudioOpenRequest();") < body.index(
            "protocol_->OpenAudioChannel()"
        )

    assert source.count("ScheduleAudioOpenRequest(") >= 5
    schedule_helper = function_body(source, "void Application::ScheduleAudioOpenRequest")
    assert "audio_open_request_pending_ = true;" in schedule_helper
    assert "Schedule(std::move(callback));" in schedule_helper

    init_body = function_body(source, "bool Application::InitializeProtocol()")
    opened_callback = switch_case(
        init_body,
        "protocol_->OnAudioChannelOpened",
        "protocol_->OnAudioChannelClosed",
    )
    assert "HandleAudioChannelOpened(codec);" in opened_callback
    opened = function_body(source, "void Application::HandleAudioChannelOpened")
    assert "tts_ownership_gate_.DeferIfCleanupReserved" in opened
    assert "audio_open_request_pending_ = true;" in opened
    assert opened.index("tts_ownership_gate_.DeferIfCleanupReserved") < opened.index(
        "++tts_connection_epoch_"
    )

    start_body = function_body(source, "void Application::HandleReliableTtsStart")
    assert "tts_ownership_gate_.CanAcquireOwnership()" in start_body
    assert start_body.index("tts_ownership_gate_.CanAcquireOwnership()") < start_body.index(
        "tts_playback_session_.Start(sentence_id, explicit_return_state)"
    )


def test_tts_control_coordination_has_runtime_host_regressions():
    gate_source = Path("tests/tts_ownership_gate_test.cc").read_text(encoding="utf-8")
    state_source = Path("tests/device_state_transition_test.cc").read_text(encoding="utf-8")
    assert "gate.ReserveCleanup()" in gate_source
    assert "gate.DeferIfCleanupReserved" in gate_source
    assert "execution_order == std::vector<int>{1, 2}" in gate_source
    assert "state_machine.CommitTransition" in state_source
    assert "state_machine.PublishTransition" in state_source
    assert "control_mutex.try_lock()" in state_source


def test_idle_audio_ingress_requires_explicit_legacy_tts_ownership():
    header = read_source(APPLICATION_HEADER)
    source = read_source(APPLICATION)
    init_body = function_body(source, "bool Application::InitializeProtocol()")
    audio_callback = switch_case(
        init_body,
        "protocol_->OnIncomingAudio",
        "protocol_->OnAudioChannelOpened",
    )
    tts_section = switch_case(
        init_body,
        'strcmp(type->valuestring, "tts") == 0',
        'strcmp(type->valuestring, "stt") == 0',
    )

    assert "bool legacy_tts_active_ = false;" in header
    assert "if (!legacy_tts_active_)" in audio_callback
    assert "if (GetDeviceState() != kDeviceStateSpeaking)" in audio_callback
    assert audio_callback.index("if (!legacy_tts_active_)") < audio_callback.index(
        "if (GetDeviceState() != kDeviceStateSpeaking)"
    )
    assert audio_callback.index("if (GetDeviceState() != kDeviceStateSpeaking)") < audio_callback.index(
        "audio_service_.PushPacketToDecodeQueue(std::move(packet));"
    )
    assert "legacy_tts_active_ = true;" in tts_section
    assert "legacy_tts_active_ = false;" in tts_section
    assert tts_section.index("legacy_tts_active_ = true;") < tts_section.index("Schedule(")


def test_tts_sentence_start_arms_speaking_before_assistant_subtitle():
    body = function_body(read_source(APPLICATION), "bool Application::InitializeProtocol()")
    tts_section = switch_case(body, 'strcmp(type->valuestring, "tts") == 0', 'strcmp(type->valuestring, "stt") == 0')
    sentence_start = branch_block(tts_section, 'strcmp(state->valuestring, "sentence_start") == 0')

    assert "if (GetDeviceState() != kDeviceStateSpeaking)" in sentence_start
    assert "SetDeviceState(kDeviceStateSpeaking);" in sentence_start
    assert sentence_start.index("SetDeviceState(kDeviceStateSpeaking);") < sentence_start.index(
        'display->SetChatMessage("assistant", message.c_str());'
    )


def test_wake_word_listening_restart_arms_stt_thinking_suppression_before_send_start_listening():
    body = function_body(read_source(APPLICATION), "void Application::HandleWakeWordDetectedEvent()")
    listening_restart = switch_case(body, "if (state == kDeviceStateListening) {", "} else {")

    assert "SuppressSttThinkingFor(kSttThinkingSuppressionWindowUs);" in listening_restart
    assert "protocol_->SendStartListening(GetDefaultListeningMode());" in listening_restart
    assert listening_restart.index("SuppressSttThinkingFor(kSttThinkingSuppressionWindowUs);") < listening_restart.index(
        "protocol_->SendStartListening(GetDefaultListeningMode());"
    )


def test_vad_true_while_listening_clears_stt_thinking_suppression_before_led_refresh():
    body = function_body(read_source(APPLICATION), "void Application::Run()")
    vad_section = switch_case(body, "if (bits & MAIN_EVENT_VAD_CHANGE) {", "if (bits & MAIN_EVENT_SCHEDULE) {")

    assert "if (GetDeviceState() == kDeviceStateListening) {" in vad_section
    assert "if (audio_service_.IsVoiceDetected()) {" in vad_section
    assert "ClearSttThinkingSuppression();" in vad_section
    assert "led->OnStateChanged();" in vad_section
    assert vad_section.index("ClearSttThinkingSuppression();") < vad_section.index("led->OnStateChanged();")


def test_manual_start_listening_from_speaking_arms_suppression_before_listening_state():
    body = function_body(read_source(APPLICATION), "void Application::HandleStartListeningEvent()")
    speaking_branch = body[body.index("else if (state == kDeviceStateSpeaking) {") :]

    assert "AbortSpeaking(kAbortReasonNone);" in speaking_branch
    assert "SuppressSttThinkingFor(kSttThinkingSuppressionWindowUs);" in speaking_branch
    assert "SetListeningMode(kListeningModeManualStop);" in speaking_branch
    assert speaking_branch.index("SuppressSttThinkingFor(kSttThinkingSuppressionWindowUs);") < speaking_branch.index(
        "SetListeningMode(kListeningModeManualStop);"
    )


def test_toggle_chat_cancels_thinking_to_idle_like_active_listening():
    body = function_body(read_source(APPLICATION), "void Application::HandleToggleChatEvent()")
    cancel_branch = branch_block(body, "state == kDeviceStateListening || state == kDeviceStateThinking")

    assert "protocol_->CloseAudioChannel();" in cancel_branch
    assert "SetDeviceState(kDeviceStateIdle);" in cancel_branch


def test_stop_listening_cancels_thinking_to_idle():
    body = function_body(read_source(APPLICATION), "void Application::HandleStopListeningEvent()")
    cancel_branch = branch_block(body, "state == kDeviceStateListening || state == kDeviceStateThinking")

    assert "protocol_->SendStopListening();" in cancel_branch
    assert "SetDeviceState(kDeviceStateIdle);" in cancel_branch


def test_wake_word_speaking_restart_arms_suppression_before_listening_state():
    body = function_body(read_source(APPLICATION), "void Application::HandleWakeWordDetectedEvent()")
    speaking_start = body.index("} else {", body.index("if (state == kDeviceStateListening) {"))
    speaking_restart = body[speaking_start:]

    assert "play_popup_on_listening_ = true;" in speaking_restart
    assert "SuppressSttThinkingFor(kSttThinkingSuppressionWindowUs);" in speaking_restart
    assert "SetListeningMode(GetDefaultListeningMode());" in speaking_restart
    assert speaking_restart.index("SuppressSttThinkingFor(kSttThinkingSuppressionWindowUs);") < speaking_restart.index(
        "SetListeningMode(GetDefaultListeningMode());"
    )


def test_wake_word_thinking_restart_arms_suppression_before_listening_state():
    body = function_body(read_source(APPLICATION), "void Application::HandleWakeWordDetectedEvent()")

    assert "state == kDeviceStateSpeaking || state == kDeviceStateListening || state == kDeviceStateThinking" in body

    thinking_start = body.index("} else {", body.index("if (state == kDeviceStateListening) {"))
    thinking_restart = body[thinking_start:]

    assert "play_popup_on_listening_ = true;" in thinking_restart
    assert "SuppressSttThinkingFor(kSttThinkingSuppressionWindowUs);" in thinking_restart
    assert "SetListeningMode(GetDefaultListeningMode());" in thinking_restart
    assert thinking_restart.index("SuppressSttThinkingFor(kSttThinkingSuppressionWindowUs);") < thinking_restart.index(
        "SetListeningMode(GetDefaultListeningMode());"
    )
