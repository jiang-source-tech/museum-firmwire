from pathlib import Path

PROTOCOL_H = Path("main/protocols/protocol.h")
PROTOCOL_CC = Path("main/protocols/protocol.cc")
WEBSOCKET = Path("main/protocols/websocket_protocol.cc")
MQTT = Path("main/protocols/mqtt_protocol.cc")
BOARD_H = Path("main/boards/common/board.h")
AUDIO_SERVICE_H = Path("main/audio/audio_service.h")
AUDIO_SERVICE_CC = Path("main/audio/audio_service.cc")
APPLICATION_H = Path("main/application.h")
APPLICATION_CC = Path("main/application.cc")
WAVESHARE_146 = Path(
    "main/boards/waveshare/esp32-s3-touch-lcd-1.46/esp32-s3-touch-lcd-1.46.cc"
)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def brace_block_at(source: str, marker_index: int, marker: str) -> str:
    stack = []
    for index, char in enumerate(source[:marker_index]):
        if char == "{":
            stack.append(index)
        elif char == "}":
            if not stack:
                raise AssertionError(f"unbalanced block before {marker!r}")
            stack.pop()
    if not stack:
        raise AssertionError(f"no enclosing block before {marker!r}")
    start = stack[-1]
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated block after {marker!r}")


def brace_block(source: str, marker: str) -> str:
    return brace_block_at(source, source.index(marker), marker)


def brace_blocks(source: str, marker: str) -> list[str]:
    blocks = []
    offset = 0
    while True:
        try:
            marker_index = source.index(marker, offset)
        except ValueError:
            return blocks
        blocks.append(brace_block_at(source, marker_index, marker))
        offset = marker_index + len(marker)


def test_protocol_exposes_correlated_tts_ack_sender():
    header = read(PROTOCOL_H)
    source = read(PROTOCOL_CC)
    assert "void SendTtsAck(const std::string& state," in header
    assert 'cJSON_AddStringToObject(root, "type", "tts");' in source
    assert 'cJSON_AddStringToObject(root, "session_id", session_id_.c_str());' in source
    assert 'cJSON_AddStringToObject(root, "sentence_id", sentence_id.c_str());' in source
    assert 'cJSON_AddStringToObject(root, "reason", reason.c_str());' in source


def test_only_production_websocket_advertises_reliable_tts_capabilities():
    websocket = read(WEBSOCKET)
    mqtt = read(MQTT)
    for capability in (
        'cJSON_AddBoolToObject(features, "tts_ready_ack", true);',
        'cJSON_AddBoolToObject(features, "tts_done_ack", true);',
        'cJSON_AddBoolToObject(features, "tts_preroll_buffer", true);',
        'cJSON_AddNumberToObject(features, "tts_preroll_capacity_ms", 5040);',
    ):
        assert capability in websocket
        assert capability not in mqtt


def test_board_exposes_default_audio_playback_preparation_hook():
    assert "virtual void PrepareForAudioPlayback() {}" in read(BOARD_H)


def test_waveshare_audio_preparation_stops_screensaver_work():
    source = read(WAVESHARE_146)
    start = source.index("void PrepareForAudioPlayback() override")
    end = source.index("virtual AudioCodec* GetAudioCodec() override", start)
    body = source[start:end]
    assert "power_save_timer_->WakeUp();" in body
    assert "display->HideLowPowerClockScreen();" in body
    assert "WifiBoard::SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);" in body


def test_waveshare_performance_level_also_wakes_power_save_timer():
    source = read(WAVESHARE_146)
    start = source.index("void SetPowerSaveLevel(PowerSaveLevel level) override")
    end = source.index("void PrepareForAudioPlayback() override", start)
    body = source[start:end]
    assert "if (level != PowerSaveLevel::LOW_POWER)" in body
    assert "power_save_timer_->WakeUp();" in body
    assert "WifiBoard::SetPowerSaveLevel(level);" in body


def test_tts_playback_session_is_compiled_into_firmware():
    cmake = Path("main/CMakeLists.txt").read_text(encoding="utf-8")
    assert "audio/tts_playback_session.cc" in cmake


def test_audio_service_exposes_lossless_decode_admission():
    header = read(AUDIO_SERVICE_H)
    source = read(AUDIO_SERVICE_CC)
    assert "bool TryPushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket>& packet);" in header
    assert "audio_decode_queue_.push_back(std::move(packet));" in source
    assert "callbacks_.on_decode_queue_available" in source


def test_playback_drain_tracks_active_output_and_expected_speaker_time():
    header = read(AUDIO_SERVICE_H)
    source = read(AUDIO_SERVICE_CC)
    assert "bool WaitForPlaybackDrained(std::chrono::milliseconds timeout);" in header
    assert "bool audio_output_busy_ = false;" in header
    assert "last_output_expected_end_" in header
    assert "audio_output_sequence_" in header
    assert "audio_output_busy_ = true;" in source
    assert "audio_output_busy_ = false;" in source
    assert "last_output_expected_end_ =" in source


def test_playback_drain_tracks_decode_in_flight():
    header = read(AUDIO_SERVICE_H)
    source = read(AUDIO_SERVICE_CC)
    assert "bool audio_decode_busy_ = false;" in header
    assert "audio_decode_busy_ = true;" in source
    assert "audio_decode_busy_ = false;" in source
    assert "!audio_decode_busy_" in source


def test_audio_service_stop_state_is_atomic():
    header = read(AUDIO_SERVICE_H)
    assert "std::atomic<bool> service_stopped_{true};" in header


def test_playback_drain_uses_output_sequence_to_detect_state_aba():
    source = read(AUDIO_SERVICE_CC)
    assert "const auto observed_output_sequence = audio_output_sequence_;" in source
    assert "audio_output_sequence_ != observed_output_sequence" in source


def test_application_enters_preparing_before_scheduling_heavy_work():
    source = read(APPLICATION_CC)
    start = source.index("void Application::HandleReliableTtsStart")
    end = source.index("void Application::PrepareReliableTts", start)
    body = source[start:end]
    assert "tts_playback_session_.Start(sentence_id, explicit_return_state)" in body
    assert "Schedule(" in body
    assert body.index("tts_playback_session_.Start(sentence_id, explicit_return_state)") < body.index("Schedule(")


def test_incoming_audio_is_owned_by_ordered_tts_ingress():
    source = read(APPLICATION_CC)
    callback = source[source.index("protocol_->OnIncomingAudio"):source.index("protocol_->OnAudioChannelOpened")]
    assert "tts_playback_session_.Enqueue(std::move(packet))" in callback
    assert "GetDeviceState() == kDeviceStateSpeaking" not in callback
    assert "preroll_overflow" in callback


def test_ready_is_after_screen_wake_reset_and_pump_activation():
    source = read(APPLICATION_CC)
    start = source.index("void Application::PrepareReliableTts")
    end = source.index("void Application::HandleTtsAudioPump", start)
    body = source[start:end]
    for call in (
        "Board::GetInstance().PrepareForAudioPlayback();",
        "audio_service_.ResetDecoder();",
        "audio_service_.WaitForPlaybackDrained(std::chrono::milliseconds(500))",
        "tts_playback_session_.MarkPlaying(generation)",
        "HandleTtsAudioPump();",
        'protocol_->SendTtsAck("ready", sentence_id);',
    ):
        assert call in body
    assert body.index("PrepareForAudioPlayback") < body.index("ResetDecoder")
    assert body.index("ResetDecoder") < body.index("WaitForPlaybackDrained")
    assert body.index("WaitForPlaybackDrained") < body.index("MarkPlaying")
    assert body.index("MarkPlaying") < body.index("SendTtsAck")


def test_done_requires_ingress_and_audio_drain_before_ack():
    source = read(APPLICATION_CC)
    start = source.index("void Application::RunTtsDrain")
    end = source.index("void Application::FailReliableTts", start)
    body = source[start:end]
    assert "WaitForIngressEmpty" in body
    assert "WaitForPlaybackDrainResult" in body
    assert 'SendTtsAck("done", sentence_id)' in body
    assert body.index("WaitForIngressEmpty") < body.index("WaitForPlaybackDrainResult")
    assert body.index("WaitForPlaybackDrainResult") < body.index('SendTtsAck("done"')


def test_reliable_tts_captures_generation_return_state_and_uses_it_for_done_and_error():
    header = read(APPLICATION_H)
    source = read(APPLICATION_CC)
    start = source[source.index("void Application::HandleReliableTtsStart"):
                   source.index("void Application::HandleAudioChannelOpened")]
    drain = source[source.index("void Application::RunTtsDrain"):
                   source.index("void Application::FailReliableTts")]
    failure = source[source.index("void Application::FailReliableTts"):
                     source.index("void Application::SetDeviceStateIfTtsGenerationIdle")]

    assert "TtsReturnState ReliableTtsReturnStateForStart() const;" in header
    assert "ReliableTtsReturnStateForStart()" in start
    assert "tts_playback_session_.Start(sentence_id, explicit_return_state)" in start
    assert "completion.return_state" in drain
    assert "completion.return_state" in failure
    assert "listening_mode_ == kListeningModeManualStop" not in drain
    assert "kDeviceStateIdle);" not in failure


def test_notification_connecting_window_is_captured_as_idle_tts_origin():
    header = read(APPLICATION_H)
    source = read(APPLICATION_CC)
    wake = source[source.index("void Application::HandleNotificationWakeEvent"):
                  source.index("void Application::ContinueOpenNotificationChannel")]
    continuation = source[
        source.index("void Application::ContinueOpenNotificationChannel"):
        source.index("void Application::HandleStartListeningEvent")
    ]
    start = source[source.index("void Application::HandleReliableTtsStart"):
                   source.index("void Application::HandleAudioChannelOpened")]
    return_policy = source[
        source.index("TtsReturnState Application::ReliableTtsReturnStateForStart"):
        source.index("void Application::SetDeviceStateIfTtsGenerationIdle")
    ]

    assert "NotificationTtsOrigin notification_tts_origin_;" in header
    assert "NotificationTtsOrigin::Token notification_token" in wake
    assert "notification_tts_origin_.BeginOpenIntent()" in wake
    assert "notification_tts_origin_.ConsumeForTtsStart()" in start
    assert "notification_tts_origin_.ClearOpenIntent(notification_token)" in continuation
    close = source[
        source.index("protocol_->OnAudioChannelClosed"):
        source.index("protocol_->OnIncomingJson")
    ]
    assert "notification_tts_origin_.ClearOpenIntent" not in close
    assert wake.index("notification_tts_origin_.BeginOpenIntent()") < wake.index(
        "SetDeviceState(kDeviceStateConnecting);"
    )


def test_reliable_tts_supersession_inherits_return_state_without_explicit_origin():
    source = read(APPLICATION_CC)
    body = source[source.index("void Application::HandleReliableTtsStart"):
                  source.index("void Application::HandleAudioChannelOpened")]
    session = read(Path("main/audio/tts_playback_session.cc"))

    assert "std::optional<TtsReturnState> explicit_return_state" in body
    assert "std::nullopt" in body
    assert "explicit_return_state.has_value()" in session
    assert "return_state_ = *explicit_return_state" in session


def test_target_output_write_is_finite_and_propagates_typed_failure():
    codec_h = read(Path("main/audio/audio_codec.h"))
    codec_cc = read(Path("main/audio/audio_codec.cc"))
    no_audio = read(Path("main/audio/codecs/no_audio_codec.cc"))
    service = read(AUDIO_SERVICE_CC)
    failures = read(Path("main/audio/audio_playback_failure.h"))

    assert "virtual bool OutputData(std::vector<int16_t>& data);" in codec_h
    assert "bool AudioCodec::OutputData" in codec_cc
    write = no_audio[no_audio.index("int NoAudioCodec::Write"):
                     no_audio.index("int NoAudioCodec::Read")]
    assert "kNoAudioCodecWriteTimeout" in write
    assert "portMAX_DELAY" not in write
    assert "ESP_ERROR_CHECK(i2s_channel_write" not in write
    assert "bytes_written != expected_bytes" in write
    assert "ClassifyAudioOutputAttempt" in service
    assert "AudioPlaybackFailureReason::kOutputWriteTimeout" in failures
    assert "ReportPlaybackFailure" in service


def test_audio_pipeline_failures_are_typed_generation_scoped_and_forbid_done():
    header = read(AUDIO_SERVICE_H)
    source = read(AUDIO_SERVICE_CC)
    application = read(APPLICATION_CC)

    assert '#include "audio_pipeline_epoch.h"' in header
    assert '#include "audio_playback_failure.h"' in header
    assert "AudioPipelineEpoch audio_pipeline_epoch_;" in header
    assert "AudioPlaybackFailureState playback_failures_;" in header
    assert "uint64_t pipeline_epoch" in read(PROTOCOL_H)
    assert "uint32_t playback_generation" in read(PROTOCOL_H)
    assert "audio_pipeline_epoch_.PublishIfCurrent" in source
    assert "audio_pipeline_epoch_.Reset" in source
    assert "AudioPlaybackFailureReason::kDecoderCreateFailed" in source
    assert "AudioPlaybackFailureReason::kDecodeFailed" in source
    assert "AudioPlaybackFailureReason::kResamplerCreateFailed" in source
    assert "WaitForPlaybackDrainResult" in header
    assert "WaitForPlaybackDrainResult(generation" in application
    assert "on_playback_failure" in application
    drain = application[application.index("void Application::RunTtsDrain"):
                        application.index("void Application::FailReliableTts")]
    assert "drain_result.failure" in drain
    assert "AudioPlaybackFailureReasonName" in drain
    assert drain.index("drain_result.failure") < drain.index('SendTtsAck("done"')


def test_reset_decoder_uses_epoch_barrier_without_tts_control_lock():
    source = read(AUDIO_SERVICE_CC)
    reset = source[source.index("void AudioService::ResetDecoder"):
                   source.index("void AudioService::CheckAndUpdateAudioPowerState")]
    output = source[source.index("void AudioService::AudioOutputTask"):
                    source.index("void AudioService::OpusCodecTask")]
    decode = source[source.index("void AudioService::OpusCodecTask"):
                    source.index("AudioPlaybackFailureReason AudioService::SetDecodeSampleRate")]

    assert "audio_pipeline_epoch_.Reset" in reset
    assert "audio_pipeline_epoch_.PublishIfCurrent" in output
    assert "audio_pipeline_epoch_.PublishIfCurrent" in decode
    assert "tts_control_mutex_" not in source
    assert Path("tests/audio_pipeline_epoch_test.cc").exists()


def test_legacy_audio_testing_route_is_admitted_into_current_pipeline_epoch():
    source = read(AUDIO_SERVICE_CC)
    body = source[source.index("void AudioService::EnableAudioTesting"):
                  source.index("void AudioService::EnableDeviceAec")]

    assert "audio_pipeline_epoch_.PublishCurrent" in body
    assert "packet->pipeline_epoch = epoch" in body
    assert body.index("audio_pipeline_epoch_.PublishCurrent") < body.index(
        "audio_decode_queue_ = std::move(audio_testing_queue_)"
    )


def test_xiaoxin_event_card_is_idempotent_by_delivery_id():
    source = read(APPLICATION_CC)
    start = source.index("void Application::HandleXiaoxinEvent")
    end = source.index("void Application::HandleXiaoxinOverviewUpdate", start)
    body = source[start:end]
    assert 'std::string notification_id = std::string("xiaoxin_event:") + delivery_id;' in body
    assert "event = std::move(event)" in body
    assert "event.c_str()" in body
    assert 'protocol_->SendXiaoxinAck(delivery_id, "device_received");' in body


def test_reliable_tts_control_transactions_do_not_span_blocking_or_external_work():
    header = read(APPLICATION_H)
    source = read(APPLICATION_CC)
    assert "std::mutex tts_control_mutex_;" in header
    blocks = brace_blocks(
        source,
        "std::lock_guard<std::mutex> control_lock(tts_control_mutex_);",
    )
    assert blocks
    for block in blocks:
        for forbidden in (
            "Board::GetInstance().PrepareForAudioPlayback()",
            "WaitForPlaybackDrained",
            "display->",
            "protocol_->SendTtsAck",
            "audio_service_.ResetDecoder()",
            "SetDeviceState(",
            "state_machine_.TransitionTo(",
        ):
            assert forbidden not in block


def test_drain_task_creation_failure_releases_context_and_fails_same_generation():
    source = read(APPLICATION_CC)
    start = source.index("void Application::HandleReliableTtsStop")
    end = source.index("void Application::RunTtsDrain", start)
    body = source[start:end]
    failure = body[body.index("if (created != pdPASS)") :]
    assert "delete context;" in failure
    assert "FailReliableTts(generation, sentence_id, \"drain_task_create_failed\")" in failure
    assert failure.index("delete context;") < failure.index("Schedule(")


def test_incoming_audio_routing_is_serialized_against_start_and_abort():
    source = read(APPLICATION_CC)
    callback = source[
        source.index("protocol_->OnIncomingAudio"):
        source.index("protocol_->OnAudioChannelOpened")
    ]
    assert "std::lock_guard<std::mutex> control_lock(tts_control_mutex_);" in callback
    assert callback.index("control_lock(tts_control_mutex_)") < callback.index(
        "tts_playback_session_.phase()"
    )
    assert callback.index("control_lock(tts_control_mutex_)") < callback.index(
        "tts_playback_session_.Enqueue(std::move(packet))"
    )


def test_stop_during_preparing_is_deferred_until_ready_transition_finishes():
    source = read(APPLICATION_CC)
    start = source.index("void Application::HandleReliableTtsStop")
    end = source.index("void Application::RunTtsDrain", start)
    body = source[start:end]
    assert "tts_playback_session_.phase() == TtsPlaybackPhase::kPreparing" in body
    assert "tts_playback_session_.IsCurrent(generation, sentence_id)" in body
    assert "HandleReliableTtsStop(sentence_id);" in body
    assert body.index("TtsPlaybackPhase::kPreparing") < body.index("Schedule(")
    assert body.index("if (defer_until_prepared)") < body.index("Schedule(")
    assert body.index("Schedule(") < body.index(
        "if (!draining_started)", body.index("Schedule(")
    )


def test_done_and_error_device_state_side_effects_use_generation_guarded_short_transaction():
    header = read(APPLICATION_H)
    source = read(APPLICATION_CC)
    assert "void SetDeviceStateIfTtsGenerationIdle(uint32_t generation, DeviceState state);" in header

    guard_start = source.index("void Application::SetDeviceStateIfTtsGenerationIdle")
    guard_end = source.index("void Application::ShowActivationCode", guard_start)
    guard_body = source[guard_start:guard_end]
    assert "std::lock_guard<std::mutex> control_lock(tts_control_mutex_);" in guard_body
    assert "tts_playback_session_.generation() != generation" in guard_body
    assert "tts_playback_session_.phase() != TtsPlaybackPhase::kIdle" in guard_body
    commit_block = brace_block(guard_body, "state_machine_.CommitTransition(state)")
    assert "state_machine_.CommitTransition(state)" in commit_block
    assert "state_machine_.PublishTransition(transition)" not in commit_block
    assert "SetDeviceState(" not in commit_block
    assert guard_body.index("state_machine_.CommitTransition(state)") < guard_body.index(
        "state_machine_.PublishTransition(transition)"
    )

    drain_start = source.index("void Application::RunTtsDrain")
    drain_end = source.index("void Application::FailReliableTts", drain_start)
    drain_body = source[drain_start:drain_end]
    done_control = brace_block(
        drain_body,
        'tts_playback_session_.Complete(generation, "done", "")',
    )
    assert 'tts_playback_session_.Complete(generation, "done", "")' in done_control
    assert 'protocol_->SendTtsAck("done", sentence_id)' not in done_control
    assert "SetDeviceState" not in done_control
    assert drain_body.index('protocol_->SendTtsAck("done", sentence_id)') < drain_body.index(
        "SetDeviceStateIfTtsGenerationIdle(generation, final_state);"
    )

    fail_start = drain_end
    fail_end = guard_start
    fail_body = source[fail_start:fail_end]
    error_control = brace_block(
        fail_body,
        "tts_playback_session_.Fail(generation, reason)",
    )
    assert "tts_playback_session_.Fail(generation, reason)" in error_control
    assert 'protocol_->SendTtsAck("error", sentence_id, reason)' not in error_control
    assert "SetDeviceState" not in error_control
    assert fail_body.index('protocol_->SendTtsAck("error", sentence_id, reason)') < fail_body.index(
        "SetDeviceStateIfTtsGenerationIdle(generation, final_state);"
    )
