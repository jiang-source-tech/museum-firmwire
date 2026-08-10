#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "settings.h"
#include "boot_diagnostics.h"
#include "runtime_health.h"
#include "museum_state.h"
#include "ota_release_audit.h"

#include <cstring>
#include <string>
#include <utility>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>

#define TAG "Application"

static constexpr int64_t kSttThinkingSuppressionWindowUs = 800 * 1000;

Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

bool Application::SetDeviceState(DeviceState state) {
    return state_machine_.TransitionTo(state);
}

void Application::Initialize() {
    auto& board = Board::GetInstance();
    if (board.ShouldSkipApplicationStartup()) {
        BootDiagnosticsMark("app_startup_low_battery_protection");
        BootDiagnosticsFlush();
        return;
    }

    BootDiagnosticsMark("app_initialize_start");
    OtaReleaseAuditReconcileRunningImage();
    const bool pending_ota_verification = Ota::IsRunningPartitionPendingVerification();
    ota_pending_verification_ = pending_ota_verification;
    ota_pending_validation_policy_.Arm(
        pending_ota_verification,
        static_cast<uint64_t>(esp_timer_get_time() / 1000000));
    if (pending_ota_verification) {
        ESP_LOGW(TAG, "Running a pending OTA image; waiting for health confirmation");
    }
    SetDeviceState(kDeviceStateStarting);

    // Setup the display
    auto display = board.GetDisplay();
    display->SetupUI();
    BootDiagnosticsMark("app_ui_ready");
    display->SetStatus("Boot: UI");
    display->UpdateStatusBar(true);
    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    // Setup the audio service
    BootDiagnosticsMark("app_audio_start");
    ESP_LOGI(TAG, "Boot: Audio");
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();
    display->CompleteBootSplash();
    const uint32_t boot_greeting_ms = display->ShowBootGreeting();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_decode_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_TTS_AUDIO_PUMP);
    };
    callbacks.on_playback_failure =
        [this](uint32_t generation, AudioPlaybackFailureReason reason) {
            std::string sentence_id;
            {
                std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
                if (tts_playback_session_.generation() != generation ||
                    tts_playback_session_.phase() == TtsPlaybackPhase::kIdle) {
                    return;
                }
                sentence_id = tts_playback_session_.sentence_id();
            }
            if (sentence_id.empty()) return;
            Schedule([this, generation, sentence_id, reason]() {
                FailReliableTts(
                    generation,
                    sentence_id,
                    AudioPlaybackFailureReasonName(reason));
            });
        };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // Add state change listeners
    state_machine_.AddStateChangeListener([this](DeviceState old_state, DeviceState new_state) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_STATE_CHANGED);
    });

    // Start the clock timer to update the status bar
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    // The first validation evaluation happens from the main event loop after
    // this local UI/audio/clock startup path has completed.  OTA endpoint and
    // transport reachability remain diagnostics, not rollback conditions.
    ota_local_runtime_ready_ = true;

    // Add MCP common tools (only once during initialization)
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    // Set network event callback for UI updates and network state handling
    board.SetNetworkEventCallback([this](NetworkEvent event, const std::string& data) {
        auto display = Board::GetInstance().GetDisplay();
        
        switch (event) {
            case NetworkEvent::Scanning:
                display->SetStatus(Lang::Strings::SCANNING_WIFI);
                display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::Connecting: {
                display->SetStatus(Lang::Strings::CONNECTING);
                if (data.empty()) {
                    // Cellular network - registering without carrier info yet
                    display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                } else {
                    // WiFi or cellular with carrier info
                    std::string msg = Lang::Strings::CONNECT_TO;
                    msg += data;
                    msg += "...";
                    display->ShowNotification(msg.c_str(), 30000);
                }
                break;
            }
            case NetworkEvent::Connected: {
                std::string msg = Lang::Strings::CONNECTED_TO;
                msg += data;
                display->ShowNotification(msg.c_str(), 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_CONNECTED);
                break;
            }
            case NetworkEvent::Disconnected:
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::WifiConfigModeEnter:
                // WiFi config mode enter is handled by WifiBoard internally
                break;
            case NetworkEvent::WifiConfigModeExit:
                // WiFi config mode exit is handled by WifiBoard internally
                break;
            // Cellular modem specific events
            case NetworkEvent::ModemDetecting:
                BootDiagnosticsMark("modem_detecting");
                display->SetStatus(Lang::Strings::DETECTING_MODULE);
                break;
            case NetworkEvent::ModemErrorNoSim:
                BootDiagnosticsMark("modem_error_no_sim");
                Alert(Lang::Strings::ERROR, Lang::Strings::PIN_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_PIN);
                break;
            case NetworkEvent::ModemErrorRegDenied:
                BootDiagnosticsMark("modem_error_reg_denied");
                Alert(Lang::Strings::ERROR, Lang::Strings::REG_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_REG);
                break;
            case NetworkEvent::ModemErrorInitFailed:
                BootDiagnosticsMark("modem_error_init_failed");
                Alert(Lang::Strings::ERROR, Lang::Strings::MODEM_INIT_ERROR, "triangle_exclamation", Lang::Sounds::OGG_EXCLAMATION);
                break;
            case NetworkEvent::ModemErrorTimeout:
                BootDiagnosticsMark("modem_error_timeout");
                display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                break;
        }
    });

    // Start network asynchronously
    if (boot_greeting_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(boot_greeting_ms));
    }
    BootDiagnosticsMark("app_network_start");
    display->SetStatus("Boot: Wi-Fi");
    display->UpdateStatusBar(true);
    display->SetStatus(Lang::Strings::SCANNING_WIFI);
    display->UpdateStatusBar(true);
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);
}

void Application::Run() {
    // Set the priority of the main task to 10
    vTaskPrioritySet(nullptr, 10);

    const EventBits_t ALL_EVENTS = 
        MAIN_EVENT_SCHEDULE |
        MAIN_EVENT_SEND_AUDIO |
        MAIN_EVENT_WAKE_WORD_DETECTED |
        MAIN_EVENT_VAD_CHANGE |
        MAIN_EVENT_CLOCK_TICK |
        MAIN_EVENT_ERROR |
        MAIN_EVENT_NETWORK_CONNECTED |
        MAIN_EVENT_NETWORK_DISCONNECTED |
        MAIN_EVENT_TOGGLE_CHAT |
        MAIN_EVENT_START_LISTENING |
        MAIN_EVENT_STOP_LISTENING |
        MAIN_EVENT_ACTIVATION_DONE |
        MAIN_EVENT_STATE_CHANGED |
        MAIN_EVENT_TTS_AUDIO_PUMP;

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, ALL_EVENTS, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            error_message_visible_ = true;
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_NETWORK_CONNECTED) {
            HandleNetworkConnectedEvent();
        }

        if (bits & MAIN_EVENT_NETWORK_DISCONNECTED) {
            HandleNetworkDisconnectedEvent();
        }

        if (bits & MAIN_EVENT_ACTIVATION_DONE) {
            HandleActivationDoneEvent();
        }

        if (bits & MAIN_EVENT_STATE_CHANGED) {
            HandleStateChangedEvent();
        }

        if (bits & MAIN_EVENT_TOGGLE_CHAT) {
            HandleToggleChatEvent();
        }

        if (bits & MAIN_EVENT_START_LISTENING) {
            HandleStartListeningEvent();
        }

        if (bits & MAIN_EVENT_STOP_LISTENING) {
            HandleStopListeningEvent();
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            HandleWakeWordDetectedEvent();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (GetDeviceState() == kDeviceStateListening) {
                if (audio_service_.IsVoiceDetected()) {
                    ClearSttThinkingSuppression();
                }
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_TTS_AUDIO_PUMP) {
            HandleTtsAudioPump();
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            MaybeCompletePendingOtaValidation();
            MaybeSchedulePeriodicOtaCheck();
            RuntimeHealthMaybeCheckpoint();
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
        
            // Print debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

void Application::MaybeCompletePendingOtaValidation() {
    if (!ota_pending_validation_policy_.IsArmed()) {
        return;
    }

    // OTA and transport reachability are diagnostics. They cannot prove that
    // a local image is bad, and rolling back would not repair an outage.
    const auto action = ota_pending_validation_policy_.Evaluate(
        static_cast<uint64_t>(esp_timer_get_time() / 1000000),
        ota_local_runtime_ready_.load());
    if (action == OtaValidationAction::kWait) {
        return;
    }

    if (action == OtaValidationAction::kConfirm) {
        ESP_LOGI(TAG, "Pending OTA image passed the health window; confirming it");
        if (Ota::MarkCurrentVersionValid()) {
            ota_pending_verification_ = false;
            ota_pending_validation_policy_.Disarm();
            BootDiagnosticsMark("ota_health_confirmed");
            return;
        }

        BootDiagnosticsMarkError("ota_health_confirmation_failed", ESP_FAIL);
        ESP_LOGE(TAG, "Unable to confirm pending OTA image; rolling it back");
    } else if (action == OtaValidationAction::kRollback) {
        BootDiagnosticsMarkError("ota_health_gate_expired", ESP_ERR_TIMEOUT);
        ESP_LOGE(TAG, "Pending OTA image did not become healthy before the rollback deadline");
    } else {
        return;
    }

    Ota::MarkCurrentVersionInvalidAndReboot();
}

void Application::MaybeSchedulePeriodicOtaCheck() {
    if (periodic_ota_check_interval_seconds_ == 0) {
        periodic_ota_check_interval_seconds_ = GetPeriodicOtaCheckIntervalSeconds();
    }
    if (!periodic_ota_check_pending_ &&
        !periodic_ota_check_scheduled_ &&
        periodic_ota_check_elapsed_seconds_ < periodic_ota_check_interval_seconds_) {
        ++periodic_ota_check_elapsed_seconds_;
    }
    if (!periodic_ota_check_pending_ &&
        !periodic_ota_check_scheduled_ &&
        periodic_ota_check_elapsed_seconds_ == periodic_ota_check_interval_seconds_) {
        periodic_ota_check_pending_ = true;
    }
    if (!periodic_ota_check_pending_ || periodic_ota_check_scheduled_) {
        return;
    }

    const char* defer_reason = nullptr;
    if (!CanRunPeriodicOtaCheck(&defer_reason)) {
        if (!periodic_ota_check_deferred_) {
            ESP_LOGI(TAG, "Periodic OTA check deferred: %s", defer_reason);
            periodic_ota_check_deferred_ = true;
        }
        return;
    }

    periodic_ota_check_pending_ = false;
    periodic_ota_check_deferred_ = false;
    periodic_ota_check_scheduled_ = true;
    Schedule([this]() {
        RunPeriodicOtaCheck();
    });
}

uint32_t Application::GetPeriodicOtaCheckIntervalSeconds() const {
    const uint32_t max_jitter = CONFIG_OTA_PERIODIC_CHECK_JITTER_SECONDS;
    if (max_jitter == 0) {
        return CONFIG_OTA_PERIODIC_CHECK_INTERVAL_SECONDS;
    }

    uint32_t hash = 2166136261u;
    for (unsigned char character : SystemInfo::GetMacAddress()) {
        hash ^= character;
        hash *= 16777619u;
    }
    return CONFIG_OTA_PERIODIC_CHECK_INTERVAL_SECONDS + hash % (max_jitter + 1);
}

bool Application::CanRunPeriodicOtaCheck(const char** defer_reason) {
    if (!network_connected_) {
        *defer_reason = "network_unavailable";
        return false;
    }
    if (!CanRunOtaInstallation(defer_reason)) {
        return false;
    }
    if (activation_task_handle_ != nullptr) {
        *defer_reason = "activation_running";
        return false;
    }
    if (GetDeviceState() != kDeviceStateIdle) {
        *defer_reason = "device_not_idle";
        return false;
    }
    if (ota_ != nullptr) {
        *defer_reason = "ota_offer_in_use";
        return false;
    }

    std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
    if (tts_playback_session_.phase() != TtsPlaybackPhase::kIdle ||
        legacy_tts_active_ ||
        !tts_ownership_gate_.CanAcquireOwnership() ||
        audio_open_request_pending_) {
        *defer_reason = "tts_or_audio_cleanup_active";
        return false;
    }
    return true;
}

bool Application::CanRunOtaInstallation(const char** defer_reason) {
    int battery_percent = 0;
    bool charging = false;
    bool discharging = false;
    if (!Board::GetInstance().GetBatteryLevel(
            battery_percent, charging, discharging)) {
        *defer_reason = "battery_state_unknown";
        return false;
    }
    (void)discharging;
    if (!charging && battery_percent < CONFIG_OTA_MIN_BATTERY_PERCENT) {
        *defer_reason = "battery_below_ota_threshold";
        return false;
    }
    return true;
}

bool Application::CanRunStartupOtaInstallation(const char** defer_reason) {
    if (!network_connected_) {
        *defer_reason = "network_unavailable";
        return false;
    }
    if (!CanRunOtaInstallation(defer_reason)) {
        return false;
    }

    std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
    if (tts_playback_session_.phase() != TtsPlaybackPhase::kIdle ||
        legacy_tts_active_ ||
        !tts_ownership_gate_.CanAcquireOwnership() ||
        audio_open_request_pending_) {
        *defer_reason = "tts_or_audio_cleanup_active";
        return false;
    }
    return true;
}

void Application::RunPeriodicOtaCheck() {
    periodic_ota_check_scheduled_ = false;
    const char* defer_reason = nullptr;
    if (!CanRunPeriodicOtaCheck(&defer_reason)) {
        periodic_ota_check_pending_ = true;
        periodic_ota_check_deferred_ = false;
        ESP_LOGI(TAG, "Periodic OTA check deferred: %s", defer_reason);
        return;
    }

    periodic_ota_check_elapsed_seconds_ = 0;
    auto periodic_ota = std::make_unique<Ota>();
    BootDiagnosticsMark("periodic_ota_check_start");
    ESP_LOGI(TAG, "Periodic OTA check started");
    const esp_err_t err = periodic_ota->CheckVersion();
    if (err != ESP_OK) {
        BootDiagnosticsMarkError("periodic_ota_check_failed", err);
        ESP_LOGW(TAG, "Periodic OTA check failed: %s", esp_err_to_name(err));
        return;
    }

    BootDiagnosticsMark("periodic_ota_check_done");
    if (!periodic_ota->HasNewVersion()) {
        ESP_LOGI(TAG, "Periodic OTA check found no new version");
        return;
    }

    if (!CanRunPeriodicOtaCheck(&defer_reason)) {
        periodic_ota_check_pending_ = true;
        periodic_ota_check_deferred_ = false;
        ESP_LOGI(TAG, "Periodic OTA check deferred after finding update: %s", defer_reason);
        return;
    }

    ESP_LOGI(TAG, "Periodic OTA check found version %s", periodic_ota->GetFirmwareVersion().c_str());
    ota_ = std::move(periodic_ota);
    if (UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())) {
        return;
    }

    ota_.reset();
    SetDeviceState(kDeviceStateIdle);
    BootDiagnosticsMarkError("periodic_ota_upgrade_failed", ESP_FAIL);
    ESP_LOGW(TAG, "Periodic OTA upgrade failed; returning to idle");
}

void Application::HandleNetworkConnectedEvent() {
    ESP_LOGI(TAG, "Network connected");
    network_connected_ = true;
    auto state = GetDeviceState();

    if (state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring) {
        // Network is ready, start activation
        SetDeviceState(kDeviceStateActivating);
        StartActivationTask();
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleNetworkDisconnectedEvent() {
    network_connected_ = false;
    ota_transport_probe_succeeded_ = false;
    {
        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
        tts_playback_session_.AbortCurrent("connection_closed");
        legacy_tts_active_ = false;
    }
    audio_service_.ResetDecoder();

    // Close current conversation when network disconnected
    auto state = GetDeviceState();
    if (state == kDeviceStateConnecting || state == kDeviceStateListening || state == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "Closing audio channel due to network disconnection");
        protocol_->CloseAudioChannel();
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleActivationDoneEvent() {
    ESP_LOGI(TAG, "Activation done");

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota_->HasServerTime();

    auto display = Board::GetInstance().GetDisplay();
    display->CompleteBootSplash();
    std::string message = std::string(Lang::Strings::VERSION) + ota_->GetCurrentVersion();
    display->ShowNotification(message.c_str());
    display->SetChatMessage("system", "");

    if (ota_pending_verification_.load()) {
        ESP_LOGI(TAG, "Pending OTA image will be confirmed after local runtime health is stable");
    }

    // Release OTA object after activation is complete
    ota_.reset();
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);

    Schedule([this]() {
        // Play the success sound to indicate the device is ready
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    });

}

void Application::StartActivationTask() {
    if (activation_task_handle_ != nullptr) {
        ESP_LOGW(TAG, "Activation task already running");
        activation_restart_pending_ = true;
        return;
    }

    activation_abort_requested_ = false;
    xTaskCreate([](void* arg) {
        Application* app = static_cast<Application*>(arg);
        app->ActivationTask();
        app->activation_task_handle_ = nullptr;
        if (app->activation_restart_pending_) {
            app->activation_restart_pending_ = false;
            app->StartActivationTask();
        }
        vTaskDelete(NULL);
    }, "activation", 4096 * 2, this, 2, &activation_task_handle_);
}

void Application::ActivationTask() {
    BootDiagnosticsMark("activation_task_start");
    ota_check_succeeded_ = false;
    ota_transport_connected_ = false;
    ota_transport_probe_succeeded_ = false;
    // Create OTA object for activation process
    ota_ = std::make_unique<Ota>();

    // Check for new assets version
    CheckAssetsVersion();
    if (activation_abort_requested_) {
        return;
    }

    // Check for new firmware version
    CheckNewVersion();
    if (activation_abort_requested_) {
        return;
    }
    if (!ota_check_succeeded_) {
        BootDiagnosticsMarkError("activation_ota_check_not_ready", ESP_FAIL);
        ESP_LOGW(TAG, "Activation continuing without a successful OTA check");
    }

    const bool protocol_started = InitializeProtocol();
    if (!protocol_started) {
        BootDiagnosticsMarkError("activation_protocol_not_ready", ESP_FAIL);
        ESP_LOGE(TAG, "Protocol startup failed; activation will continue without OTA confirmation");
    }

    if (ota_pending_verification_.load()) {
        if (!ota_check_succeeded_) {
            ESP_LOGW(TAG, "Pending OTA transport diagnostic skipped because OTA check did not succeed");
        } else if (!protocol_started) {
            ESP_LOGW(TAG, "Pending OTA transport diagnostic skipped because protocol startup failed");
        } else {
            if (!ProbeOtaTransportHealth()) {
                ESP_LOGW(TAG, "Pending OTA transport diagnostic failed; local health still controls rollback");
            }
        }
    }
    if (activation_abort_requested_) {
        return;
    }

    // Signal completion to main loop
    xEventGroupSetBits(event_group_, MAIN_EVENT_ACTIVATION_DONE);
}

void Application::CheckAssetsVersion() {
    // Only allow CheckAssetsVersion to be called once
    if (assets_version_checked_) {
        return;
    }
    assets_version_checked_ = true;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [this, display](int progress, size_t speed) -> void {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            Schedule([display, message = std::string(buffer)]() {
                display->SetChatMessage("system", message.c_str());
            });
        });

        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            SetDeviceState(kDeviceStateActivating);
            return;
        }
    }

    // Apply assets
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion() {
    constexpr int kMaxStartupOtaCheckAttempts = 2;
    constexpr int kStartupOtaRetryDelaySeconds = 5;
    int retry_count = 0;

    ota_check_succeeded_ = false;
    auto& board = Board::GetInstance();
    while (true) {
        if (activation_abort_requested_) {
            return;
        }
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        BootDiagnosticsMark("ota_check_start");
        ota_check_succeeded_ = false;
        esp_err_t err = ota_->CheckVersion();
        if (err != ESP_OK) {
            BootDiagnosticsMarkError("ota_check_failed", err);
            retry_count++;
            if (retry_count >= kMaxStartupOtaCheckAttempts) {
                ESP_LOGW(TAG, "Startup OTA check unavailable after %d attempts; will retry periodically", retry_count);
                return;
            }

            char error_message[128];
            snprintf(error_message, sizeof(error_message), "code=%d, url=%s", err, ota_->GetCheckVersionUrl().c_str());
            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED,
                     kStartupOtaRetryDelaySeconds, error_message);
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)",
                     kStartupOtaRetryDelaySeconds, retry_count, kMaxStartupOtaCheckAttempts);
            for (int i = 0; i < kStartupOtaRetryDelaySeconds; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (GetDeviceState() == kDeviceStateIdle || activation_abort_requested_) {
                    break;
                }
            }
            continue;
        }
        BootDiagnosticsMark("ota_check_done");
        ota_check_succeeded_ = true;
        retry_count = 0;

        if (ota_->HasNewVersion()) {
            display->UpsertNotification("ota_update", "OTA 更新", "发现新版本", "系统", 4, 0);
            const char* defer_reason = nullptr;
            if (!CanRunStartupOtaInstallation(&defer_reason)) {
                BootDiagnosticsMark("ota_upgrade_deferred");
                ESP_LOGI(TAG, "Startup OTA installation deferred: %s", defer_reason);
                Schedule([this]() {
                    periodic_ota_check_pending_ = true;
                    periodic_ota_check_deferred_ = false;
                });
            } else if (UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())) {
                return; // This line will never be reached after reboot
            } else {
                BootDiagnosticsMarkError("ota_upgrade_failed", ESP_FAIL);
                ESP_LOGE(TAG, "OTA upgrade failed; continuing activation on the current firmware");
            }
        }

        if (!ota_->HasActivationCode() && !ota_->HasActivationChallenge()) {
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_->HasActivationCode()) {
            display->CompleteBootSplash();
            ShowActivationCode(ota_->GetActivationCode(), ota_->GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            BootDiagnosticsMark("activation_poll_start");
            esp_err_t err = ota_->Activate();
            if (err == ESP_OK) {
                BootDiagnosticsMark("activation_poll_done");
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                BootDiagnosticsMarkError("activation_poll_failed", err);
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                BootDiagnosticsMarkError("activation_poll_failed", err);
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (GetDeviceState() == kDeviceStateIdle || activation_abort_requested_) {
                break;
            }
        }
    }
}

bool Application::ProbeOtaTransportHealth() {
    if (protocol_ == nullptr) {
        return false;
    }

    // This is deliberately a short, real transport handshake for a pending OTA
    // image. It does not start recording or enqueue audio; it opens the normal
    // server channel, waits for its hello, then closes it again.
    ESP_LOGI(TAG, "Starting pending-OTA transport health probe");
    const bool opened = protocol_->OpenAudioChannel();
    const bool transport_connected = ota_transport_connected_.load();
    const bool channel_open = protocol_->IsAudioChannelOpened();
    const bool healthy = opened && transport_connected && channel_open;
    if (channel_open) {
        protocol_->CloseAudioChannel(false);
    }

    ota_transport_probe_succeeded_ = healthy;
    if (healthy) {
        BootDiagnosticsMark("ota_transport_probe_done");
        ESP_LOGI(TAG, "Pending-OTA transport health probe succeeded");
    } else {
        BootDiagnosticsMarkError("ota_transport_probe_failed", ESP_FAIL);
        ESP_LOGE(TAG, "Pending-OTA transport health probe failed");
    }
    return healthy;
}

bool Application::InitializeProtocol() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto codec = board.GetAudioCodec();

    BootDiagnosticsMark("protocol_initialize_start");
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    if (ota_->HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota_->HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        ota_transport_connected_ = true;
        DismissAlert();
    });

    protocol_->OnDisconnected([this]() {
        ota_transport_connected_ = false;
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
        if (tts_playback_session_.phase() == TtsPlaybackPhase::kIdle) {
            if (!legacy_tts_active_) {
                return;
            }
            if (GetDeviceState() != kDeviceStateSpeaking) {
                return;
            }
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
            return;
        }

        const auto result = tts_playback_session_.Enqueue(std::move(packet));
        if (result == TtsIngressResult::kAccepted) {
            xEventGroupSetBits(event_group_, MAIN_EVENT_TTS_AUDIO_PUMP);
            return;
        }
        if (result == TtsIngressResult::kOverflow) {
            const uint32_t generation = tts_playback_session_.generation();
            const std::string sentence_id = tts_playback_session_.sentence_id();
            Schedule([this, generation, sentence_id]() {
                FailReliableTts(generation, sentence_id, "preroll_overflow");
            });
        }
    });
    
    protocol_->OnAudioChannelOpened([this, codec]() {
        HandleAudioChannelOpened(codec);
    });
    
    protocol_->OnAudioChannelClosed([this, &board]() {
        uint32_t close_epoch;
        uint32_t close_generation;
        {
            std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
            close_epoch = ++tts_connection_epoch_;
            tts_playback_session_.AbortCurrent("connection_closed");
            close_generation = tts_playback_session_.generation();
            legacy_tts_active_ = false;
        }
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        Schedule([this, close_epoch, close_generation]() {
            RunAudioChannelCloseCleanup(close_epoch, close_generation);
        });
    });
    
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        // 先判空再比较，避免缺失/非字符串的 type 字段导致解引用崩溃。
        if (!cJSON_IsString(type)) {
            ESP_LOGW(TAG, "Incoming message has no valid 'type' field");
            return;
        }
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (!cJSON_IsString(state)) {
                ESP_LOGW(TAG, "TTS message has no valid 'state' field");
                return;
            }
            auto sentence = cJSON_GetObjectItem(root, "sentence_id");
            const bool has_sentence_id =
                cJSON_IsString(sentence) && sentence->valuestring[0] != '\0';
            const std::string sentence_id =
                has_sentence_id ? sentence->valuestring : "";
            if (strcmp(state->valuestring, "start") == 0) {
                if (has_sentence_id) {
                    HandleReliableTtsStart(sentence_id);
                } else {
                    {
                        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
                        legacy_tts_active_ = true;
                    }
                    Schedule([this]() {
                        aborted_ = false;
                        SetDeviceState(kDeviceStateSpeaking);
                    });
                }
            } else if (strcmp(state->valuestring, "stop") == 0) {
                if (has_sentence_id) {
                    HandleReliableTtsStop(sentence_id);
                } else {
                    {
                        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
                        legacy_tts_active_ = false;
                    }
                    Schedule([this]() {
                        if (GetDeviceState() != kDeviceStateSpeaking) return;
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    });
                }
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([this, display, message = std::string(text->valuestring)]() {
                        if (GetDeviceState() != kDeviceStateSpeaking) {
                            SetDeviceState(kDeviceStateSpeaking);
                        }
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            } else if (strcmp(state->valuestring, "sentence_update") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< [update] %s", text->valuestring);
                    Schedule([
                        this,
                        display,
                        message = std::string(text->valuestring),
                        sentence_id
                    ]() {
                        if (!sentence_id.empty()) {
                            if (tts_playback_session_.sentence_id() != sentence_id) {
                                return;
                            }
                        }
                        display->UpdateChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([this, display, message = std::string(text->valuestring)]() {
                    if (GetDeviceState() == kDeviceStateListening && !IsSttThinkingSuppressed()) {
                        SetDeviceState(kDeviceStateThinking);
                    }
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "museum_state") == 0) {
            MuseumState state;
            std::string error;
            if (!ParseMuseumState(root, &state, &error)) {
                ESP_LOGW(TAG, "museum_state rejected: %s", error.c_str());
                return;
            }
            ESP_LOGI(TAG, "museum_state exhibit=%s status=%s", state.exhibit_id.c_str(),
                     state.grounding_status.c_str());
            std::string display_text = BuildMuseumStateDisplayText(state);
            Schedule([display, message = std::move(display_text)]() {
                display->SetMuseumState(message.c_str());
            });
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    
    if (!protocol_->Start()) {
        ESP_LOGE(TAG, "Failed to start configured protocol");
        protocol_.reset();
        return false;
    }
    BootDiagnosticsMark("protocol_start_done");
    return true;
}

void Application::HandleReliableTtsStart(const std::string& sentence_id) {
    TtsStartDecision decision;
    bool cleanup_active = false;
    {
        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
        cleanup_active = !tts_ownership_gate_.CanAcquireOwnership();
        if (!cleanup_active) {
            legacy_tts_active_ = false;
            const bool session_active =
                tts_playback_session_.phase() != TtsPlaybackPhase::kIdle;
            std::optional<TtsReturnState> explicit_return_state = std::nullopt;
            const DeviceState state = GetDeviceState();
            if (!session_active) {
                explicit_return_state = ReliableTtsReturnStateForStart();
            }
            decision =
                tts_playback_session_.Start(sentence_id, explicit_return_state);
            if (decision.action == TtsStartAction::kPrepare) {
                tts_prepare_started_us_ = esp_timer_get_time();
            }
        }
    }
    if (cleanup_active) {
        ESP_LOGW(TAG, "Ignoring TTS start during closed-channel cleanup: %s", sentence_id.c_str());
        return;
    }
    if (!decision.superseded_sentence_id.empty() && protocol_) {
        protocol_->SendTtsAck("error", decision.superseded_sentence_id, "superseded");
    }
    switch (decision.action) {
        case TtsStartAction::kPrepare:
            Schedule([this, generation = decision.generation, sentence_id]() {
                PrepareReliableTts(generation, sentence_id);
            });
            break;
        case TtsStartAction::kResendReady:
            if (protocol_) protocol_->SendTtsAck("ready", sentence_id);
            break;
        case TtsStartAction::kReplayFinal:
            if (protocol_) {
                protocol_->SendTtsAck(
                    decision.final_ack.state,
                    decision.final_ack.sentence_id,
                    decision.final_ack.reason);
            }
            break;
        case TtsStartAction::kRejectStale:
            if (protocol_) protocol_->SendTtsAck("error", sentence_id, "stale_start");
            break;
        case TtsStartAction::kContinuePreparing:
        case TtsStartAction::kContinueDraining:
            break;
    }
}

void Application::HandleAudioChannelOpened(AudioCodec* codec) {
    {
        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
        if (tts_ownership_gate_.DeferIfCleanupReserved([this, codec]() {
                HandleAudioChannelOpened(codec);
            })) {
            audio_open_request_pending_ = true;
            return;
        }
        ++tts_connection_epoch_;
    }
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
        ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
            protocol_->server_sample_rate(), codec->output_sample_rate());
    }
}

void Application::RunAudioChannelCloseCleanup(
    uint32_t close_epoch, uint32_t close_generation) {
    {
        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
        if (tts_connection_epoch_ != close_epoch ||
            tts_playback_session_.generation() != close_generation) {
            return;
        }
        if (!tts_ownership_gate_.ReserveCleanup()) {
            tts_ownership_gate_.DeferIfCleanupReserved(
                [this, close_epoch, close_generation]() {
                    RunAudioChannelCloseCleanup(close_epoch, close_generation);
                });
            return;
        }
    }

    audio_service_.ResetDecoder();

    TtsOwnershipGate::DeferredTasks deferred_tasks;
    DeviceStateTransition idle_transition;
    bool publish_idle = false;
    {
        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
        if (tts_connection_epoch_ == close_epoch &&
            tts_playback_session_.generation() == close_generation &&
            !audio_open_request_pending_) {
            idle_transition =
                state_machine_.CommitTransition(kDeviceStateIdle);
            publish_idle = true;
        }
        deferred_tasks = tts_ownership_gate_.ReleaseCleanup();
    }
    if (publish_idle) {
        state_machine_.PublishTransition(idle_transition);
    }
    for (auto& task : deferred_tasks) {
        Schedule(std::move(task));
    }
}

void Application::PrepareReliableTts(
    uint32_t generation, const std::string& sentence_id) {
    if (!tts_playback_session_.IsCurrent(generation, sentence_id)) return;
    const int64_t wake_started_us = esp_timer_get_time();
    Board::GetInstance().PrepareForAudioPlayback();
    const int64_t screen_wake_ms =
        (esp_timer_get_time() - wake_started_us) / 1000;
    aborted_ = false;
    audio_service_.EnableVoiceProcessing(false);
    audio_service_.EnableWakeWordDetection(false);
    audio_service_.ResetDecoder();
    if (!audio_service_.WaitForPlaybackDrained(std::chrono::milliseconds(500))) {
        FailReliableTts(generation, sentence_id, "pipeline_reset_timeout");
        return;
    }
    if (GetDeviceState() != kDeviceStateSpeaking) {
        SetDeviceState(kDeviceStateSpeaking);
    }
    if (!tts_playback_session_.MarkPlaying(generation)) return;
    const size_t preroll_packets = tts_playback_session_.buffered_packets();
    HandleTtsAudioPump();
    bool send_ready = false;
    {
        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
        send_ready = protocol_ &&
            tts_playback_session_.IsCurrent(generation, sentence_id);
    }
    if (send_ready) {
        ESP_LOGI(
            TAG,
            "tts_state=ready sentence_id=%s generation=%lu screen_wake_ms=%lld start_to_ready_ms=%lld preroll_packets=%u",
            sentence_id.c_str(),
            static_cast<unsigned long>(generation),
            static_cast<long long>(screen_wake_ms),
            static_cast<long long>((esp_timer_get_time() - tts_prepare_started_us_) / 1000),
            static_cast<unsigned>(preroll_packets));
        protocol_->SendTtsAck("ready", sentence_id);
    }
}

void Application::HandleTtsAudioPump() {
    tts_playback_session_.Pump(
        [this](std::unique_ptr<AudioStreamPacket>& packet) {
            return audio_service_.TryPushPacketToDecodeQueue(packet);
        });
}

void Application::HandleReliableTtsStop(const std::string& sentence_id) {
    uint32_t generation = 0;
    bool draining_started = false;
    bool defer_until_prepared = false;
    TtsFinalAck final_ack;
    {
        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
        generation = tts_playback_session_.generation();
        draining_started =
            tts_playback_session_.BeginDraining(sentence_id, generation);
        if (!draining_started) {
            if (tts_playback_session_.IsCurrent(generation, sentence_id) &&
                tts_playback_session_.phase() == TtsPlaybackPhase::kPreparing) {
                defer_until_prepared = true;
            } else {
                final_ack = tts_playback_session_.FinalAckFor(sentence_id);
            }
        }
    }
    if (defer_until_prepared) {
        Schedule([this, generation, sentence_id]() {
            if (!tts_playback_session_.IsCurrent(generation, sentence_id)) return;
            HandleReliableTtsStop(sentence_id);
        });
        return;
    }
    if (!draining_started) {
        if (protocol_ && !final_ack.state.empty()) {
            protocol_->SendTtsAck(
                final_ack.state, final_ack.sentence_id, final_ack.reason);
        }
        return;
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_TTS_AUDIO_PUMP);
    struct DrainContext {
        Application* app;
        uint32_t generation;
        std::string sentence_id;
    };
    auto* context = new DrainContext{this, generation, sentence_id};
    const BaseType_t created = xTaskCreate([](void* arg) {
        std::unique_ptr<DrainContext> context(static_cast<DrainContext*>(arg));
        context->app->RunTtsDrain(context->generation, context->sentence_id);
        vTaskDelete(nullptr);
    }, "tts_drain", 4096, context, 4, nullptr);
    if (created != pdPASS) {
        delete context;
        Schedule([this, generation, sentence_id]() {
            FailReliableTts(generation, sentence_id, "drain_task_create_failed");
        });
    }
}

void Application::RunTtsDrain(
    uint32_t generation, const std::string& sentence_id) {
    constexpr auto kDrainTimeout = kReliableTtsDrainWatchdog;
    const auto started = std::chrono::steady_clock::now();
    const bool ingress_empty =
        tts_playback_session_.WaitForIngressEmpty(generation, kDrainTimeout);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    const auto remaining = elapsed < kDrainTimeout
        ? kDrainTimeout - elapsed
        : std::chrono::milliseconds(0);
    const AudioPlaybackDrainResult drain_result = ingress_empty
        ? audio_service_.WaitForPlaybackDrainResult(generation, remaining)
        : AudioPlaybackDrainResult{};
    const int64_t done_wait_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
    Schedule([this, generation, sentence_id, drain_result, done_wait_ms]() {
        if (drain_result.failure != AudioPlaybackFailureReason::kNone) {
            FailReliableTts(
                generation,
                sentence_id,
                AudioPlaybackFailureReasonName(drain_result.failure));
            return;
        }
        if (!drain_result.drained) {
            FailReliableTts(generation, sentence_id, "playback_drain_timeout");
            return;
        }
        TtsCompletionResult completion;
        {
            std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
            if (!tts_playback_session_.IsCurrent(generation, sentence_id)) return;
            completion =
                tts_playback_session_.Complete(generation, "done", "");
        }
        if (!completion.finalized) return;
        ESP_LOGI(
            TAG,
            "tts_state=done sentence_id=%s generation=%lu done_wait_ms=%lld",
            sentence_id.c_str(),
            static_cast<unsigned long>(generation),
            static_cast<long long>(done_wait_ms));
        if (protocol_) protocol_->SendTtsAck("done", sentence_id);
        const DeviceState final_state =
            completion.return_state == TtsReturnState::kListening
                ? kDeviceStateListening
                : kDeviceStateIdle;
        SetDeviceStateIfTtsGenerationIdle(generation, final_state);
    });
}

void Application::FailReliableTts(
    uint32_t generation,
    const std::string& sentence_id,
    const std::string& reason) {
    if (!tts_playback_session_.IsCurrent(generation, sentence_id)) return;
    const size_t preroll_packets = tts_playback_session_.buffered_packets();
    audio_service_.ResetDecoder();
    TtsCompletionResult completion;
    {
        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
        if (!tts_playback_session_.IsCurrent(generation, sentence_id)) return;
        completion = tts_playback_session_.Fail(generation, reason);
    }
    if (!completion.finalized) return;
    ESP_LOGW(
        TAG,
        "tts_state=error sentence_id=%s generation=%lu failure_reason=%s preroll_packets=%u",
        sentence_id.c_str(),
        static_cast<unsigned long>(generation),
        reason.c_str(),
        static_cast<unsigned>(preroll_packets));
    if (protocol_) protocol_->SendTtsAck("error", sentence_id, reason);
    const DeviceState final_state =
        completion.return_state == TtsReturnState::kListening
            ? kDeviceStateListening
            : kDeviceStateIdle;
    SetDeviceStateIfTtsGenerationIdle(generation, final_state);
}

TtsReturnState Application::ReliableTtsReturnStateForStart() const {
    const DeviceState state = GetDeviceState();
    if (state == kDeviceStateIdle) {
        return TtsReturnState::kIdle;
    }
    return listening_mode_ == kListeningModeManualStop
        ? TtsReturnState::kIdle
        : TtsReturnState::kListening;
}

void Application::SetDeviceStateIfTtsGenerationIdle(
    uint32_t generation, DeviceState state) {
    DeviceStateTransition transition;
    {
        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
        if (tts_playback_session_.generation() != generation ||
            tts_playback_session_.phase() != TtsPlaybackPhase::kIdle) {
            return;
        }
        transition = state_machine_.CommitTransition(state);
    }
    state_machine_.PublishTransition(transition);
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (GetDeviceState() == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        error_message_visible_ = false;
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_TOGGLE_CHAT);
}

void Application::StartListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_START_LISTENING);
}

void Application::StopListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_STOP_LISTENING);
}

void Application::HandleToggleChatEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        ListeningMode mode = GetDefaultListeningMode();
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            ScheduleAudioOpenRequest([this, mode]() {
                ContinueOpenAudioChannel(mode);
            });
            return;
        }
        SetListeningMode(mode);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
    } else if (state == kDeviceStateListening || state == kDeviceStateThinking) {
        protocol_->CloseAudioChannel();
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::ContinueOpenAudioChannel(ListeningMode mode) {
    if (DeferUntilTtsCleanupComplete([this, mode]() {
            ContinueOpenAudioChannel(mode);
        })) {
        return;
    }
    ConsumeAudioOpenRequest();
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            return;
        }
    }

    if (DeferUntilTtsCleanupComplete([this, mode]() {
            ContinueOpenAudioChannel(mode);
        })) {
        return;
    }

    SetListeningMode(mode);
}

void Application::HandleStartListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (state == kDeviceStateIdle) {
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            ScheduleAudioOpenRequest([this]() {
                ContinueOpenAudioChannel(kListeningModeManualStop);
            });
            return;
        }
        SetListeningMode(kListeningModeManualStop);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
        SuppressSttThinkingFor(kSttThinkingSuppressionWindowUs);
        SetListeningMode(kListeningModeManualStop);
    }
}

void Application::HandleStopListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    } else if (state == kDeviceStateListening || state == kDeviceStateThinking) {
        if (protocol_) {
            protocol_->SendStopListening();
        }
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleWakeWordDetectedEvent() {
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    auto wake_word = audio_service_.GetLastWakeWord();
    ESP_LOGI(TAG, "Wake word detected: %s (state: %d)", wake_word.c_str(), (int)state);

    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();
        auto wake_word = audio_service_.GetLastWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update),
            // then continue with OpenAudioChannel which may block for ~1 second
            ScheduleAudioOpenRequest([this, wake_word]() {
                ContinueWakeWordInvoke(wake_word);
            });
            return;
        }
        // Channel already opened, continue directly
        ContinueWakeWordInvoke(wake_word);
    } else if (state == kDeviceStateSpeaking || state == kDeviceStateListening || state == kDeviceStateThinking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
        // Clear send queue to avoid sending residues to server
        while (audio_service_.PopPacketFromSendQueue());

        if (state == kDeviceStateListening) {
            SuppressSttThinkingFor(kSttThinkingSuppressionWindowUs);
            protocol_->SendStartListening(GetDefaultListeningMode());
            audio_service_.ResetDecoder();
            audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            // Re-enable wake word detection as it was stopped by the detection itself
            audio_service_.EnableWakeWordDetection(true);
        } else {
            // Play popup sound and start listening again
            play_popup_on_listening_ = true;
            SuppressSttThinkingFor(kSttThinkingSuppressionWindowUs);
            SetListeningMode(GetDefaultListeningMode());
        }
    } else if (state == kDeviceStateActivating) {
        // Restart the activation check if the wake word is detected during activation
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::ContinueWakeWordInvoke(const std::string& wake_word) {
    if (DeferUntilTtsCleanupComplete([this, wake_word]() {
            ContinueWakeWordInvoke(wake_word);
        })) {
        return;
    }
    ConsumeAudioOpenRequest();
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            audio_service_.EnableWakeWordDetection(true);
            return;
        }
    }

    if (DeferUntilTtsCleanupComplete([this, wake_word]() {
            ContinueWakeWordInvoke(wake_word);
        })) {
        return;
    }

    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
    // Keep the captured wake audio for server-side compatibility, then send the
    // structured wake-word text so the museum voice service can use the context.
    while (auto packet = audio_service_.PopWakeWordPacket()) {
        protocol_->SendAudio(std::move(packet));
    }
    protocol_->SendWakeWordDetected(wake_word);

    // Set flag to play popup sound after state changes to listening
    play_popup_on_listening_ = true;
    SetListeningMode(GetDefaultListeningMode());
#else
    // Set flag to play popup sound after state changes to listening
    // (PlaySound here would be cleared by ResetDecoder in EnableVoiceProcessing)
    play_popup_on_listening_ = true;
    SetListeningMode(GetDefaultListeningMode());
#endif
}

bool Application::DeferUntilTtsCleanupComplete(
    std::function<void()>&& callback) {
    std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
    const bool deferred =
        tts_ownership_gate_.DeferIfCleanupReserved(std::move(callback));
    if (deferred) {
        audio_open_request_pending_ = true;
    }
    return deferred;
}

void Application::ScheduleAudioOpenRequest(
    std::function<void()>&& callback) {
    {
        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
        audio_open_request_pending_ = true;
    }
    Schedule(std::move(callback));
}

void Application::ConsumeAudioOpenRequest() {
    std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
    audio_open_request_pending_ = false;
}

void Application::HandleStateChangedEvent() {
    DeviceState new_state = state_machine_.GetState();
    clock_ticks_ = 0;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    
    switch (new_state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            if (!error_message_visible_) {
                display->ClearChatMessages();  // Clear messages first
            }
            display->SetEmotion("neutral"); // Then set emotion (wechat mode checks child count)
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            error_message_visible_ = false;
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->ClearChatMessages();
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            // Make sure the audio processor is running
            if (play_popup_on_listening_ || !audio_service_.IsAudioProcessorRunning()) {
                // For auto mode, wait for playback queue to be empty before enabling voice processing
                // This prevents audio truncation when STOP arrives late due to network jitter
                if (listening_mode_ == kListeningModeAutoStop) {
                    audio_service_.WaitForPlaybackQueueEmpty();
                }
                
                // Send the start listening command
                protocol_->SendStartListening(listening_mode_);
                audio_service_.EnableVoiceProcessing(true);
            }

#ifdef CONFIG_WAKE_WORD_DETECTION_IN_LISTENING
            // Enable wake word detection in listening mode (configured via Kconfig)
            audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
#else
            // Disable wake word detection in listening mode
            audio_service_.EnableWakeWordDetection(false);
#endif
            
            // Play popup sound after ResetDecoder (in EnableVoiceProcessing) has been called
            if (play_popup_on_listening_) {
                play_popup_on_listening_ = false;
                audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            }
            break;
        case kDeviceStateThinking:
            display->SetStatus(Lang::Strings::THINKING);
            display->SetEmotion("neutral");
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            if (!tts_playback_session_.OwnsPlaybackPipeline()) {
                audio_service_.ResetDecoder();
            }
            break;
        case kDeviceStateWifiConfiguring:
            display->CompleteBootSplash();
            display->SetStatus(Lang::Strings::WIFI_CONFIG_MODE);
            display->UpdateStatusBar(true);
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Schedule(std::function<void()>&& callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    {
        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
        tts_playback_session_.AbortCurrent("interrupted");
        legacy_tts_active_ = false;
    }
    audio_service_.ResetDecoder();
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

ListeningMode Application::GetDefaultListeningMode() const {
    return aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime;
}

void Application::SuppressSttThinkingFor(int64_t duration_us) {
    suppress_stt_thinking_until_us_ = esp_timer_get_time() + duration_us;
}

void Application::ClearSttThinkingSuppression() {
    suppress_stt_thinking_until_us_ = 0;
}

bool Application::IsSttThinkingSuppressed() const {
    return esp_timer_get_time() < suppress_stt_thinking_until_us_;
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    RuntimeHealthForceCheckpoint();
    esp_restart();
}

bool Application::UpgradeFirmware(const std::string& url, const std::string& version) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    std::string upgrade_url = url;
    std::string version_info = version.empty() ? "(Manual upgrade)" : version;
    if (ota_ == nullptr) {
        ESP_LOGE(TAG, "Cannot upgrade because no checked OTA offer is available");
        BootDiagnosticsMarkError("ota_upgrade_missing_offer", ESP_FAIL);
        return false;
    }

    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());

    display->UpsertNotification("ota_update", "OTA 更新", "正在下载并安装更新", "系统", 4, 0);
    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);

    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = ota_->StartUpgrade([this, display](int progress, size_t speed) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
        Schedule([display, message = std::string(buffer)]() {
            display->SetChatMessage("system", message.c_str());
        });
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // Restart audio service
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER); // Restore power save level
        SetDeviceState(kDeviceStateActivating);
        BootDiagnosticsMarkError("ota_upgrade_failed", ESP_FAIL);
        ESP_LOGE(TAG, "OTA upgrade failed; current firmware will continue running");
        display->UpsertNotification("ota_update", "OTA 更新", "升级失败，请稍后重试", "系统", 4, 0);
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        display->RemoveNotification("ota_update");
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    
    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            ScheduleAudioOpenRequest([this, wake_word]() {
                ContinueWakeWordInvoke(wake_word);
            });
            return;
        }
        // Channel already opened, continue directly
        ContinueWakeWordInvoke(wake_word);
    } else if (state == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (state == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    const DeviceState state = GetDeviceState();
    if (state != kDeviceStateIdle && state != kDeviceStateWifiConfiguring) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    // Always schedule to run in main task for thread safety
    Schedule([this, payload = std::move(payload)]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
    });
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

void Application::AbortActivationForWifiConfig() {
    activation_abort_requested_ = true;
}

void Application::ResetProtocol() {
    Schedule([this]() {
        // Close audio channel if opened
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
        // Reset protocol
        protocol_.reset();
    });
}

