#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "doorbell_mqtt.h"
#include "doorbell_mqtt_contract.h"
#include "xiaoxin_overview_payload_contract.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "settings.h"
#include "boot_diagnostics.h"
#include "runtime_health.h"
#include "ota_release_audit.h"
#include "xiaoxin_event_validation.h"

#include <cstring>
#include <climits>
#include <cmath>
#include <string>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>

#define TAG "Application"

// 常驻门铃 MQTT 客户端（设备空闲/待机时被服务器叫醒用）。激活完成后启动一次。
static DoorbellMqtt g_doorbell_mqtt;
static constexpr int64_t kSttThinkingSuppressionWindowUs = 800 * 1000;

static std::string NormalizeXiaoxinDeviceName(std::string text) {
    const char* variants[] = {"小新", "晓新"};
    for (const char* variant : variants) {
        size_t pos = 0;
        while ((pos = text.find(variant, pos)) != std::string::npos) {
            text.replace(pos, std::strlen(variant), "小芯");
            pos += std::strlen("小芯");
        }
    }
    return text;
}

static const char* JsonStringOrNull(const cJSON* root, const char* name) {
    cJSON* item = cJSON_GetObjectItem(root, name);
    return cJSON_IsString(item) ? item->valuestring : nullptr;
}

static std::string JsonStringOrEmpty(const cJSON* root, const char* name) {
    const char* value = JsonStringOrNull(root, name);
    return value != nullptr ? value : "";
}

static int JsonIntOrDefault(const cJSON* root, const char* name, int fallback) {
    cJSON* item = cJSON_GetObjectItem(root, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static bool JsonBoolOrDefault(const cJSON* root, const char* name, bool fallback) {
    cJSON* item = cJSON_GetObjectItem(root, name);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return fallback;
}

static constexpr size_t kXiaoxinOverviewTextMaxBytes = 192;
static constexpr size_t kXiaoxinOverviewBodyMaxBytes = 39;
static constexpr size_t kXiaoxinOverviewDetailMaxBytes = 63;

static bool IsValidUtf8(const char* text, size_t length) {
    size_t index = 0;
    while (index < length) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        if (lead <= 0x7F) {
            index++;
            continue;
        }

        size_t continuation_count = 0;
        uint32_t code_point = 0;
        uint32_t minimum_code_point = 0;
        if ((lead & 0xE0) == 0xC0) {
            continuation_count = 1;
            code_point = lead & 0x1F;
            minimum_code_point = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            continuation_count = 2;
            code_point = lead & 0x0F;
            minimum_code_point = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            continuation_count = 3;
            code_point = lead & 0x07;
            minimum_code_point = 0x10000;
        } else {
            return false;
        }

        if (index + continuation_count >= length) {
            return false;
        }
        for (size_t offset = 1; offset <= continuation_count; ++offset) {
            const unsigned char continuation =
                static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xC0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (continuation & 0x3F);
        }
        if (code_point < minimum_code_point || code_point > 0x10FFFF ||
            (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            return false;
        }
        index += continuation_count + 1;
    }
    return true;
}

static bool JsonRequiredUtf8String(
    const cJSON* object, const char* name, size_t max_bytes) {
    const cJSON* item = cJSON_GetObjectItem(object, name);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return false;
    }
    const size_t length = std::strlen(item->valuestring);
    return length <= max_bytes && IsValidUtf8(item->valuestring, length);
}

static bool JsonRequiredBool(const cJSON* object, const char* name) {
    return cJSON_IsBool(cJSON_GetObjectItem(object, name));
}

static bool IsExactJsonIntegerInRange(
    const cJSON* item, int minimum, int maximum) {
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        std::floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < static_cast<double>(INT_MIN) ||
        item->valuedouble > static_cast<double>(INT_MAX)) {
        return false;
    }
    return item->valuedouble >= static_cast<double>(minimum) &&
           item->valuedouble <= static_cast<double>(maximum);
}

static bool ValidateXiaoxinOverviewCards(const cJSON* root) {
    const cJSON* weather = cJSON_GetObjectItem(root, "weather");
    const cJSON* course = cJSON_GetObjectItem(root, "course");
    const cJSON* todo = cJSON_GetObjectItem(root, "todo");
    if (!cJSON_IsObject(weather) || !cJSON_IsObject(course) ||
        !cJSON_IsObject(todo)) {
        return false;
    }

    const bool weather_valid =
        JsonRequiredBool(weather, "configured") &&
        JsonRequiredBool(weather, "available") &&
        JsonRequiredUtf8String(weather, "province", kXiaoxinOverviewTextMaxBytes) &&
        JsonRequiredUtf8String(weather, "city", kXiaoxinOverviewTextMaxBytes) &&
        JsonRequiredUtf8String(weather, "date", kXiaoxinOverviewTextMaxBytes) &&
        JsonRequiredUtf8String(weather, "summary", kXiaoxinOverviewBodyMaxBytes) &&
        JsonRequiredUtf8String(weather, "detail", kXiaoxinOverviewDetailMaxBytes) &&
        JsonRequiredUtf8String(weather, "fetched_at", kXiaoxinOverviewTextMaxBytes);
    const bool course_valid =
        JsonRequiredBool(course, "configured") &&
        JsonRequiredBool(course, "available_today") &&
        JsonRequiredUtf8String(course, "title", kXiaoxinOverviewBodyMaxBytes) &&
        JsonRequiredUtf8String(course, "detail", kXiaoxinOverviewDetailMaxBytes);
    const cJSON* todo_count = cJSON_GetObjectItem(todo, "count");
    const bool todo_valid =
        JsonRequiredBool(todo, "configured") &&
        IsExactJsonIntegerInRange(todo_count, 0, 99) &&
        JsonRequiredUtf8String(todo, "detail", kXiaoxinOverviewDetailMaxBytes);
    return weather_valid && course_valid && todo_valid;
}

static bool IsValidIsoDate(const std::string& date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }
    for (size_t index = 0; index < date.size(); ++index) {
        if (index == 4 || index == 7) {
            continue;
        }
        if (date[index] < '0' || date[index] > '9') {
            return false;
        }
    }

    const int year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 +
                     (date[2] - '0') * 10 + (date[3] - '0');
    const int month = (date[5] - '0') * 10 + (date[6] - '0');
    const int day = (date[8] - '0') * 10 + (date[9] - '0');
    if (year == 0 || month < 1 || month > 12) {
        return false;
    }

    static constexpr int days_by_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool is_leap_year =
        (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    int days_in_month = days_by_month[month - 1];
    if (month == 2 && is_leap_year) {
        days_in_month = 29;
    }
    return day >= 1 && day <= days_in_month;
}

static bool ValidateUnboundXiaoxinOverviewCards(const cJSON* root) {
    const cJSON* weather = cJSON_GetObjectItem(root, "weather");
    const cJSON* course = cJSON_GetObjectItem(root, "course");
    const cJSON* todo = cJSON_GetObjectItem(root, "todo");
    return cJSON_IsFalse(cJSON_GetObjectItem(weather, "configured")) &&
           cJSON_IsFalse(cJSON_GetObjectItem(weather, "available")) &&
           JsonStringOrEmpty(weather, "province").empty() &&
           JsonStringOrEmpty(weather, "city").empty() &&
           IsValidIsoDate(JsonStringOrEmpty(weather, "date")) &&
           JsonStringOrEmpty(weather, "fetched_at").empty() &&
           JsonStringOrEmpty(weather, "summary") == "设备未绑定" &&
           JsonStringOrEmpty(weather, "detail") == "绑定后显示天气" &&
           cJSON_IsFalse(cJSON_GetObjectItem(course, "configured")) &&
           cJSON_IsFalse(cJSON_GetObjectItem(course, "available_today")) &&
           JsonStringOrEmpty(course, "title") == "设备未绑定" &&
           JsonStringOrEmpty(course, "detail") == "绑定后显示课程" &&
           cJSON_IsFalse(cJSON_GetObjectItem(todo, "configured")) &&
           JsonIntOrDefault(todo, "count", -1) == 0 &&
           JsonStringOrEmpty(todo, "detail") == "绑定后显示待办";
}

static XiaoxinContractString JsonContractString(
    const cJSON* object, const char* name) {
    const cJSON* item = cJSON_GetObjectItem(object, name);
    return cJSON_IsString(item) && item->valuestring != nullptr
               ? XiaoxinContractString{true, item->valuestring}
               : XiaoxinContractString{};
}

static XiaoxinContractBool JsonContractBool(
    const cJSON* object, const char* name) {
    const cJSON* item = cJSON_GetObjectItem(object, name);
    return {cJSON_IsBool(item) != 0, cJSON_IsTrue(item) != 0};
}

static XiaoxinContractInt JsonContractInt(
    const cJSON* object, const char* name) {
    const cJSON* item = cJSON_GetObjectItem(object, name);
    if (cJSON_IsNull(item)) {
        return {true, 0, true};
    }
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        std::floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < static_cast<double>(INT_MIN) ||
        item->valuedouble > static_cast<double>(INT_MAX)) {
        return {};
    }
    return {true, static_cast<int>(item->valuedouble), false};
}

static XiaoxinOverviewPayloadContract ReadXiaoxinOverviewPayloadContract(
    const cJSON* root) {
    XiaoxinOverviewPayloadContract payload;
    payload.root_object_valid = cJSON_IsObject(root);
    payload.type = JsonContractString(root, "type");
    payload.version = JsonContractInt(root, "version");
    payload.device_id = JsonContractString(root, "device_id");
    payload.revision = JsonContractInt(root, "revision");
    payload.generated_at = JsonContractString(root, "generated_at");
    payload.bound = JsonContractBool(root, "bound");
    payload.notifications_absent =
        cJSON_GetObjectItem(root, "notifications") == nullptr;

    const cJSON* weather = cJSON_GetObjectItem(root, "weather");
    payload.weather.object_valid = cJSON_IsObject(weather);
    payload.weather.configured = JsonContractBool(weather, "configured");
    payload.weather.available = JsonContractBool(weather, "available");
    payload.weather.province = JsonContractString(weather, "province");
    payload.weather.city = JsonContractString(weather, "city");
    payload.weather.date = JsonContractString(weather, "date");
    payload.weather.summary = JsonContractString(weather, "summary");
    payload.weather.detail = JsonContractString(weather, "detail");
    payload.weather.fetched_at = JsonContractString(weather, "fetched_at");

    const cJSON* course = cJSON_GetObjectItem(root, "course");
    payload.course.object_valid = cJSON_IsObject(course);
    payload.course.configured = JsonContractBool(course, "configured");
    payload.course.available_today =
        JsonContractBool(course, "available_today");
    payload.course.title = JsonContractString(course, "title");
    payload.course.detail = JsonContractString(course, "detail");

    const cJSON* todo = cJSON_GetObjectItem(root, "todo");
    payload.todo.object_valid = cJSON_IsObject(todo);
    payload.todo.configured = JsonContractBool(todo, "configured");
    payload.todo.count = JsonContractInt(todo, "count");
    payload.todo.detail = JsonContractString(todo, "detail");

    const cJSON* companion = cJSON_GetObjectItem(root, "companion");
    payload.companion.object_present = companion != nullptr;
    payload.companion.object_valid = cJSON_IsObject(companion);
    payload.companion.xiaoxin_age = JsonContractInt(companion, "xiaoxin_age");
    payload.companion.academic_stage =
        JsonContractString(companion, "academic_stage");
    payload.companion.growth_moment_id =
        JsonContractString(companion, "growth_moment_id");
    payload.companion.growth_summary =
        JsonContractString(companion, "growth_summary");
    payload.companion.expression = JsonContractString(companion, "expression");
    return payload;
}

static const char* RequiredXiaoxinFieldMissing(const cJSON* root) {
    const char* required[] = {"delivery_id", "event", "title", "body"};
    for (const char* name : required) {
        if (JsonStringOrNull(root, name) == nullptr) {
            return name;
        }
    }
    return nullptr;
}

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
        MAIN_EVENT_NOTIFICATION_WAKE |
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

        if (bits & MAIN_EVENT_NOTIFICATION_WAKE) {
            HandleNotificationWakeEvent();
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
    location_heartbeat_.OnNetworkConnected();
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
    location_heartbeat_.OnNetworkDisconnected();

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

    const std::string ota_url = ota_->GetCheckVersionUrl();
    const auto& config = ota_->GetDoorbellMqttConfig();
    const std::string device_id = SystemInfo::GetMacAddress();
    location_heartbeat_.Configure(ota_url, device_id, config.username, config.password);

    if (ota_->HasDoorbellMqttConfig() &&
        IsDoorbellMqttConfigValidForDevice(config, device_id)) {
        g_doorbell_mqtt.Start(config, device_id);
    }

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

    const auto& doorbell_config = ota_->GetDoorbellMqttConfig();
    const std::string device_id = SystemInfo::GetMacAddress();
    const bool doorbell_configuration_ready =
        !ota_->HasDoorbellMqttConfig() ||
        IsDoorbellMqttConfigValidForDevice(doorbell_config, device_id);
    if (!doorbell_configuration_ready) {
        BootDiagnosticsMarkError("activation_doorbell_config_invalid", ESP_ERR_INVALID_ARG);
        ESP_LOGE(TAG, "Activation cannot complete because doorbell MQTT configuration is invalid");
        return;
    }
    const bool mqtt_overview_authoritative =
        ota_->HasDoorbellMqttConfig() &&
        doorbell_configuration_ready &&
        !doorbell_config.overview_topic.empty();
    overview_authority_.Configure(mqtt_overview_authoritative);

    // Initialize the protocol only after its Overview authority is explicit.
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

void Application::HandleXiaoxinEvent(const cJSON* root) {
    std::string delivery_id = JsonStringOrEmpty(root, "delivery_id");
    const char* missing = RequiredXiaoxinFieldMissing(root);
    if (missing != nullptr) {
        ESP_LOGW(TAG, "xiaoxin_event missing required field: %s", missing);
        if (protocol_ != nullptr) {
            protocol_->SendXiaoxinAck(delivery_id, "failed", "invalid_payload");
        }
        return;
    }
    if (!IsValidXiaoxinDeliveryId(delivery_id)) {
        ESP_LOGW(
            TAG,
            "xiaoxin_event invalid delivery_id length: %u (max=%u)",
            static_cast<unsigned>(delivery_id.size()),
            static_cast<unsigned>(kXiaoxinDeliveryIdMaxLength));
        if (protocol_ != nullptr) {
            protocol_->SendXiaoxinAck(
                delivery_id, "failed", "invalid_payload");
        }
        return;
    }

    std::string event = JsonStringOrEmpty(root, "event");
    std::string title = JsonStringOrEmpty(root, "title");
    std::string body = JsonStringOrEmpty(root, "body");
    std::string tag = JsonStringOrEmpty(root, "tag");
    const int priority = JsonIntOrDefault(root, "priority", 2);
    const int ttl_ms = JsonIntOrDefault(root, "ttl_ms", 0);

    if (event != "notification" &&
        event != "course_reminder" &&
        event != "todo_reminder") {
        ESP_LOGW(TAG, "xiaoxin_event unsupported event: %s", event.c_str());
        if (protocol_ != nullptr) {
            protocol_->SendXiaoxinAck(delivery_id, "failed", "invalid_payload");
        }
        return;
    }

    auto display = Board::GetInstance().GetDisplay();
    std::string notification_id = std::string("xiaoxin_event:") + delivery_id;
    Schedule([display,
              notification_id = std::move(notification_id),
              event = std::move(event),
              title = std::move(title),
              body = std::move(body),
              tag = std::move(tag),
              priority,
              ttl_ms]() {
        bool shown = display->UpsertNotification(
            notification_id.c_str(),
            title.c_str(),
            body.c_str(),
            tag.empty() ? nullptr : tag.c_str(),
            priority,
            ttl_ms,
            event.c_str()
        );
        if (!shown) {
            display->ShowNotification(body.c_str(), ttl_ms > 0 ? ttl_ms : 8000);
        }
    });
    if (protocol_ != nullptr) {
        protocol_->SendXiaoxinAck(delivery_id, "device_received");
    }
}

void Application::HandleXiaoxinOverviewUpdate(const cJSON* root,
                                              XiaoxinOverviewSource source,
                                              int revision) {
    if (!overview_authority_.Allows(source)) {
        ESP_LOGI(TAG, "xiaoxin overview ignored, result=non_authoritative_source");
        return;
    }

    const cJSON* weather = cJSON_GetObjectItem(root, "weather");
    const cJSON* course = cJSON_GetObjectItem(root, "course");
    const cJSON* todo = cJSON_GetObjectItem(root, "todo");
    const cJSON* companion = cJSON_GetObjectItem(root, "companion");
    const cJSON* notifications = cJSON_GetObjectItem(root, "notifications");
    if (!cJSON_IsObject(weather) || !cJSON_IsObject(course) || !cJSON_IsObject(todo)) {
        ESP_LOGW(TAG, "xiaoxin_overview_update missing weather/course/todo object");
        return;
    }

    int todo_count = JsonIntOrDefault(todo, "count", 0);
    if (todo_count < 0) {
        todo_count = 0;
    } else if (todo_count > 99) {
        todo_count = 99;
    }

    const std::string weather_summary = JsonStringOrEmpty(weather, "summary");
    const std::string weather_detail = JsonStringOrEmpty(weather, "detail");
    const std::string course_title = JsonStringOrEmpty(course, "title");
    const std::string course_detail = JsonStringOrEmpty(course, "detail");
    const std::string todo_detail = JsonStringOrEmpty(todo, "detail");
    int xiaoxin_age = JsonIntOrDefault(companion, "xiaoxin_age", 0);
    if (xiaoxin_age < 1 || xiaoxin_age > 4) {
        xiaoxin_age = 0;
    }
    const bool companion_available = cJSON_IsObject(companion) && xiaoxin_age > 0;
    const std::string growth_summary =
        companion_available ? JsonStringOrEmpty(companion, "growth_summary") : "";

    ESP_LOGI(TAG, "xiaoxin overview apply queued, source=%s revision=%d",
             source == XiaoxinOverviewSource::kMqtt ? "mqtt" : "websocket",
             revision);

    auto display = Board::GetInstance().GetDisplay();
    Schedule([display,
              weather_configured = JsonBoolOrDefault(weather, "configured", false),
              weather_available = JsonBoolOrDefault(weather, "available", false),
              weather_summary,
              weather_detail,
              course_configured = JsonBoolOrDefault(course, "configured", false),
              course_available_today = JsonBoolOrDefault(course, "available_today", false),
              course_title,
              course_detail,
              todo_configured = JsonBoolOrDefault(todo, "configured", false),
              todo_count = static_cast<uint8_t>(todo_count),
              todo_detail,
              companion_available,
              xiaoxin_age = static_cast<uint8_t>(xiaoxin_age),
              growth_summary]() {
        display->UpdateOverviewData(
            weather_configured,
            weather_available,
            weather_summary.c_str(),
            weather_detail.c_str(),
            course_configured,
            course_available_today,
            course_title.c_str(),
            course_detail.c_str(),
            todo_configured,
            todo_count,
            todo_detail.c_str(),
            companion_available,
            xiaoxin_age,
            growth_summary.c_str()
        );
    });

    if (!cJSON_IsArray(notifications)) {
        return;
    }

    uint8_t notification_index = 0;
    cJSON* notification = nullptr;
    cJSON_ArrayForEach(notification, notifications) {
        if (!cJSON_IsObject(notification)) {
            continue;
        }

        std::string event = JsonStringOrEmpty(notification, "event");
        if (event.empty()) {
            event = "notification";
        }
        std::string id = JsonStringOrEmpty(notification, "id");
        if (id.empty()) {
            id = std::to_string(notification_index);
        }
        notification_index++;

        std::string notification_id = std::string("xiaoxin_event:") + id;
        std::string title = JsonStringOrEmpty(notification, "title");
        std::string body = JsonStringOrEmpty(notification, "body");
        std::string tag = JsonStringOrEmpty(notification, "tag");

        int priority = JsonIntOrDefault(notification, "priority", 2);
        if (priority < 0) {
            priority = 0;
        }
        int ttl_ms = JsonIntOrDefault(notification, "ttl_ms", 0);
        if (ttl_ms < 0) {
            ttl_ms = 0;
        }

        Schedule([display,
                  notification_id = std::move(notification_id),
                  event = std::move(event),
                  title = std::move(title),
                  body = std::move(body),
                  tag = std::move(tag),
                  priority,
                  ttl_ms]() {
            bool shown = display->UpsertNotification(
                notification_id.c_str(),
                title.c_str(),
                body.c_str(),
                tag.empty() ? nullptr : tag.c_str(),
                static_cast<uint32_t>(priority),
                static_cast<uint32_t>(ttl_ms),
                event.c_str()
            );
            if (!shown && !body.empty()) {
                display->ShowNotification(body.c_str(), ttl_ms > 0 ? ttl_ms : 8000);
            }
        });
    }
}

void Application::HandleXiaoxinOverviewMqttMessage(
    const std::string& payload,
    const std::string& expected_device) {
    if (payload.empty() || payload.size() > 2048 || expected_device.empty() ||
        payload.find('\0') != std::string::npos ||
        payload.find("\\u0000") != std::string::npos) {
        return;
    }

    const char* parse_end = nullptr;
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root_holder(
        cJSON_ParseWithLengthOpts(
            payload.data(), payload.size(), &parse_end, false),
        &cJSON_Delete);
    cJSON* root = root_holder.get();
    if (!cJSON_IsObject(root)) {
        return;
    }
    const char* payload_end = payload.data() + payload.size();
    while (parse_end < payload_end &&
           (*parse_end == ' ' || *parse_end == '\t' ||
            *parse_end == '\r' || *parse_end == '\n')) {
        ++parse_end;
    }
    if (parse_end != payload_end) {
        return;
    }

    const XiaoxinOverviewPayloadContract contract =
        ReadXiaoxinOverviewPayloadContract(root);
    if (!ValidateXiaoxinOverviewPayloadContract(
            contract, expected_device,
            overview_authority_.last_overview_revision_)) {
        return;
    }

    const int revision = contract.revision.value;
    HandleXiaoxinOverviewUpdate(root, XiaoxinOverviewSource::kMqtt, revision);
    overview_authority_.CommitMqttRevision(revision);
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
                Schedule([this, display, message = NormalizeXiaoxinDeviceName(std::string(text->valuestring))]() {
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
        } else if (strcmp(type->valuestring, "notification") == 0) {
            // 服务器主动下发的屏幕通知文字（门铃唤醒后经 WebSocket 送达）。
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                auto duration = cJSON_GetObjectItem(root, "duration_ms");
                int duration_ms = cJSON_IsNumber(duration) ? duration->valueint : 3000;
                ESP_LOGI(TAG, "Notification: %s", text->valuestring);
                Schedule([display, message = std::string(text->valuestring), duration_ms]() {
                    display->ShowNotification(message.c_str(), duration_ms);
                });
            } else {
                ESP_LOGW(TAG, "Notification message missing 'text'");
            }
        } else if (strcmp(type->valuestring, "xiaoxin_event") == 0) {
            HandleXiaoxinEvent(root);
        } else if (strcmp(type->valuestring, "xiaoxin_overview_update") == 0) {
            HandleXiaoxinOverviewUpdate(
                root, XiaoxinOverviewSource::kWebSocket, 0);
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
            if ((state == kDeviceStateIdle ||
                 state == kDeviceStateConnecting) &&
                notification_tts_origin_.ConsumeForTtsStart()) {
                explicit_return_state = TtsReturnState::kIdle;
            } else if (!session_active) {
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

void Application::WakeForNotification() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_NOTIFICATION_WAKE);
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

void Application::HandleNotificationWakeEvent() {
    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    auto state = GetDeviceState();
    if (state != kDeviceStateIdle) {
        ESP_LOGI(TAG, "Notification wake ignored; device not idle");
        return;
    }

    if (!protocol_->IsAudioChannelOpened()) {
        NotificationTtsOrigin::Token notification_token;
        {
            std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
            notification_token = notification_tts_origin_.BeginOpenIntent();
        }
        SetDeviceState(kDeviceStateConnecting);
        ScheduleAudioOpenRequest([this, notification_token]() {
            ContinueOpenNotificationChannel(notification_token);
        });
        return;
    }
}

void Application::ContinueOpenNotificationChannel(
    NotificationTtsOrigin::Token notification_token) {
    {
        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
        if (!notification_tts_origin_.IsCurrent(notification_token)) {
            return;
        }
    }
    if (DeferUntilTtsCleanupComplete([this, notification_token]() {
            ContinueOpenNotificationChannel(notification_token);
        })) {
        return;
    }
    ConsumeAudioOpenRequest();
    // Notification wake only connects WS. It must not enter listening or send microphone audio.
    if (GetDeviceState() != kDeviceStateConnecting) {
        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
        notification_tts_origin_.ClearOpenIntent(notification_token);
        return;
    }

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            SetDeviceState(kDeviceStateIdle);
            {
                std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
                notification_tts_origin_.ClearOpenIntent(notification_token);
            }
            return;
        }
    }

    if (DeferUntilTtsCleanupComplete([this, notification_token]() {
            ContinueOpenNotificationChannel(notification_token);
        })) {
        return;
    }

    SetDeviceState(kDeviceStateIdle);
    {
        std::lock_guard<std::mutex> control_lock(tts_control_mutex_);
        notification_tts_origin_.ClearOpenIntent(notification_token);
    }
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
    // structured wake-word text so Xiaoxin-specific handlers still have context.
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

