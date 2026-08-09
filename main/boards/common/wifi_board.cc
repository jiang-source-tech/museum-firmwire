#include "wifi_board.h"

#include "display.h"
#include "application.h"
#include "system_info.h"
#include "settings.h"
#include "assets/lang_config.h"
#include "time_sync_status.h"
#include "boot_diagnostics.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_network.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <utility>

#include <font_awesome.h>
#include <wifi_manager.h>
#include <wifi_station.h>
#include <ssid_manager.h>
#include "afsk_demod.h"
#ifdef CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
#include "blufi.h"
#endif

static const char *TAG = "WifiBoard";

// Connection timeout in seconds
static constexpr int CONNECT_TIMEOUT_SEC = 60;
static constexpr char DEFAULT_WIFI_SSID[] = "Jiang";
static constexpr char DEFAULT_WIFI_PASSWORD[] = "lhj123456";
static constexpr const char* NTP_SERVERS[] = {
    "ntp.aliyun.com",
    "ntp.tencent.com",
    "ntp.ntsc.ac.cn",
};
static constexpr size_t k_ntp_server_count = sizeof(NTP_SERVERS) / sizeof(NTP_SERVERS[0]);
static constexpr char DEFAULT_TIMEZONE[] = "CST-8";

static void OnSntpTimeSync(struct timeval* tv) {
    MarkTimeSyncSucceeded();

    time_t now = tv != nullptr ? tv->tv_sec : time(nullptr);
    struct tm timeinfo = {};
    char time_text[24] = {};

    if (localtime_r(&now, &timeinfo) != nullptr &&
        strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S", &timeinfo) > 0) {
        ESP_LOGI(TAG, "SNTP time synchronized: %s", time_text);
        return;
    }

    ESP_LOGI(TAG, "SNTP time synchronized");
}

static const char* SntpSyncStatusName(sntp_sync_status_t status) {
    switch (status) {
        case SNTP_SYNC_STATUS_COMPLETED:
            return "completed";
        case SNTP_SYNC_STATUS_IN_PROGRESS:
            return "in_progress";
        case SNTP_SYNC_STATUS_RESET:
        default:
            return "reset";
    }
}

static void StartTimeSynchronization() {
    static bool sntp_started = false;

    setenv("TZ", DEFAULT_TIMEZONE, 1);
    tzset();

    if (sntp_started || esp_sntp_enabled()) {
        const sntp_sync_status_t sync_status = esp_sntp_get_sync_status();
        ESP_LOGI(
            TAG,
            "SNTP already started, status=%s; restarting after WiFi got IP",
            SntpSyncStatusName(sync_status)
        );
        MarkTimeSyncStarted();
        if (!esp_sntp_restart()) {
            ESP_LOGW(TAG, "SNTP restart request failed because SNTP is not running");
        }
        sntp_started = true;
        return;
    }

    ESP_LOGI(TAG, "Starting SNTP time synchronization with %d servers", (int)k_ntp_server_count);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_set_time_sync_notification_cb(OnSntpTimeSync);
    for (size_t i = 0; i < k_ntp_server_count; ++i) {
        esp_sntp_setservername(i, NTP_SERVERS[i]);
    }
    MarkTimeSyncStarted();
    esp_sntp_init();
    sntp_started = true;
}

static void EnsureDefaultWifiCredentials(SsidManager& ssid_manager) {
    for (const auto& item : ssid_manager.GetSsidList()) {
        if (item.ssid != DEFAULT_WIFI_SSID) {
            continue;
        }

        if (item.password != DEFAULT_WIFI_PASSWORD) {
            ESP_LOGI(TAG, "Updating local fallback WiFi credentials for SSID: %s", DEFAULT_WIFI_SSID);
            ssid_manager.AddSsid(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASSWORD);
        }
        return;
    }

    ESP_LOGI(TAG, "Adding local fallback WiFi credentials for SSID: %s", DEFAULT_WIFI_SSID);
    ssid_manager.AddSsid(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASSWORD);
}

static void ClearSavedWifiCredentialsForReconfiguration() {
    ESP_LOGI(TAG, "Clearing saved WiFi credentials before manual reconfiguration");
    SsidManager::GetInstance().Clear();
}

WifiBoard::WifiBoard() {
    // Create connection timeout timer
    esp_timer_create_args_t timer_args = {
        .callback = OnWifiConnectTimeout,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_connect_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&timer_args, &connect_timer_);
}

WifiBoard::~WifiBoard() {
    if (connect_timer_) {
        esp_timer_stop(connect_timer_);
        esp_timer_delete(connect_timer_);
    }
}

std::string WifiBoard::GetBoardType() {
    return "wifi";
}

void WifiBoard::StartNetwork() {
    auto& wifi_manager = WifiManager::GetInstance();

    // Initialize WiFi manager
    WifiManagerConfig config;
    config.ssid_prefix = "Xiaozhi";
    config.language = Lang::CODE;
    wifi_manager.Initialize(config);

    // Set unified event callback - forward to NetworkEvent with SSID data
    wifi_manager.SetEventCallback([this](WifiEvent event, const std::string& data) {
        switch (event) {
            case WifiEvent::Scanning:
                OnNetworkEvent(NetworkEvent::Scanning);
                break;
            case WifiEvent::Connecting:
                OnNetworkEvent(NetworkEvent::Connecting, data);
                break;
            case WifiEvent::Connected:
                OnNetworkEvent(NetworkEvent::Connected, data);
                break;
            case WifiEvent::Disconnected:
                OnNetworkEvent(NetworkEvent::Disconnected);
                break;
            case WifiEvent::ConfigModeEnter:
                OnNetworkEvent(NetworkEvent::WifiConfigModeEnter);
                break;
            case WifiEvent::ConfigModeExit:
                OnNetworkEvent(NetworkEvent::WifiConfigModeExit);
                break;
        }
    });

    // Try to connect or enter config mode
    TryWifiConnect();
}

void WifiBoard::TryWifiConnect() {
    auto& ssid_manager = SsidManager::GetInstance();
    EnsureDefaultWifiCredentials(ssid_manager);
    bool have_ssid = !ssid_manager.GetSsidList().empty();

    if (have_ssid) {
        // Start connection attempt with timeout
        ESP_LOGI(TAG, "Starting WiFi connection attempt");
        esp_timer_start_once(connect_timer_, CONNECT_TIMEOUT_SEC * 1000000ULL);
        WifiManager::GetInstance().StartStation();
    } else {
        // No SSID configured, enter config mode
        // Wait for the board version to be shown
        vTaskDelay(pdMS_TO_TICKS(1500));
        StartWifiConfigMode();
    }
}

void WifiBoard::OnNetworkEvent(NetworkEvent event, const std::string& data) {
    switch (event) {
        case NetworkEvent::Connected:
            BootDiagnosticsMark("wifi_connected");
            // Stop timeout timer
            esp_timer_stop(connect_timer_);
#ifdef CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
            // make sure blufi resources has been released
            Blufi::GetInstance().deinit();
#endif
            in_config_mode_ = false;
            StartTimeSynchronization();
            ESP_LOGI(TAG, "Connected to WiFi: %s", data.c_str());
            break;
        case NetworkEvent::Scanning:
            BootDiagnosticsMark("wifi_scanning");
            ESP_LOGI(TAG, "WiFi scanning");
            break;
        case NetworkEvent::Connecting:
            BootDiagnosticsMark("wifi_connecting");
            ESP_LOGI(TAG, "WiFi connecting to %s", data.c_str());
            break;
        case NetworkEvent::Disconnected:
            BootDiagnosticsMark("wifi_disconnected");
            ESP_LOGW(TAG, "WiFi disconnected");
            break;
        case NetworkEvent::WifiConfigModeEnter:
            BootDiagnosticsMark("wifi_config_mode_enter");
            ESP_LOGI(TAG, "WiFi config mode entered");
            in_config_mode_ = true;
            break;
        case NetworkEvent::WifiConfigModeExit:
            BootDiagnosticsMark("wifi_config_mode_exit");
            ESP_LOGI(TAG, "WiFi config mode exited");
            in_config_mode_ = false;
            // Try to connect with the new credentials
            TryWifiConnect();
            break;
        default:
            break;
    }

    // Notify external callback if set
    if (network_event_callback_) {
        network_event_callback_(event, data);
    }
}

void WifiBoard::SetNetworkEventCallback(NetworkEventCallback callback) {
    network_event_callback_ = std::move(callback);
}

void WifiBoard::OnWifiConnectTimeout(void* arg) {
    auto* board = static_cast<WifiBoard*>(arg);
    ESP_LOGW(TAG, "WiFi connection timeout, entering config mode");
    BootDiagnosticsMark("wifi_connect_timeout");

    WifiManager::GetInstance().StopStation();
    board->StartWifiConfigMode();
}

void WifiBoard::StartWifiConfigMode() {
    in_config_mode_ = true;
    // Transition to wifi configuring state
    Application::GetInstance().SetDeviceState(kDeviceStateWifiConfiguring);
#ifdef CONFIG_USE_HOTSPOT_WIFI_PROVISIONING
    auto& wifi_manager = WifiManager::GetInstance();

    wifi_manager.StartConfigAp();

    // Show config prompt after a short delay
    Application::GetInstance().Schedule([&wifi_manager]() {
        std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
        hint += wifi_manager.GetApSsid();
        hint += Lang::Strings::ACCESS_VIA_BROWSER;
        hint += wifi_manager.GetApWebUrl();

        Application::GetInstance().Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "gear", Lang::Sounds::OGG_WIFICONFIG);
    });
#elif CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
    auto &blufi = Blufi::GetInstance();
    // initialize esp-blufi protocol
    blufi.init();
#endif
#if CONFIG_USE_ACOUSTIC_WIFI_PROVISIONING
    // Start acoustic provisioning task
    auto codec = Board::GetInstance().GetAudioCodec();
    int channel = codec ? codec->input_channels() : 1;
    ESP_LOGI(TAG, "Starting acoustic WiFi provisioning, channels: %d", channel);

    xTaskCreate([](void* arg) {
        auto ch = reinterpret_cast<intptr_t>(arg);
        auto& app = Application::GetInstance();
        auto& wifi = WifiManager::GetInstance();
        auto disp = Board::GetInstance().GetDisplay();
        audio_wifi_config::ReceiveWifiCredentialsFromAudio(&app, &wifi, disp, ch);
        vTaskDelete(NULL);
    }, "acoustic_wifi", 4096, reinterpret_cast<void*>(channel), 2, NULL);
#endif
}

void WifiBoard::EnterWifiConfigMode() {
    ESP_LOGI(TAG, "EnterWifiConfigMode called");
    GetDisplay()->ShowNotification(Lang::Strings::ENTERING_WIFI_CONFIG_MODE);

    auto& app = Application::GetInstance();
    auto state = app.GetDeviceState();
    app.AbortActivationForWifiConfig();

    if (state == kDeviceStateSpeaking || state == kDeviceStateListening || state == kDeviceStateIdle ||
            state == kDeviceStateActivating) {
        // Reset protocol (close audio channel, reset protocol)
        Application::GetInstance().ResetProtocol();

        xTaskCreate([](void* arg) {
            auto* board = static_cast<WifiBoard*>(arg);

            // Wait for 1 second to allow speaking to finish gracefully
            vTaskDelay(pdMS_TO_TICKS(1000));

            // Stop any ongoing connection attempt
            esp_timer_stop(board->connect_timer_);
            WifiManager::GetInstance().StopStation();
            ClearSavedWifiCredentialsForReconfiguration();

            // Enter config mode
            board->StartWifiConfigMode();

            vTaskDelete(NULL);
        }, "wifi_cfg_delay", 4096, this, 2, NULL);
        return;
    }

    if (state != kDeviceStateStarting) {
        ESP_LOGE(TAG, "EnterWifiConfigMode called but device state is not starting or speaking, device state: %d", state);
        return;
    }

    // Stop any ongoing connection attempt
    esp_timer_stop(connect_timer_);
    WifiManager::GetInstance().StopStation();
    ClearSavedWifiCredentialsForReconfiguration();

    StartWifiConfigMode();
}

bool WifiBoard::IsInWifiConfigMode() const {
    return WifiManager::GetInstance().IsConfigMode();
}

NetworkInterface* WifiBoard::GetNetwork() {
    static EspNetwork network;
    return &network;
}

const char* WifiBoard::GetNetworkStateIcon() {
    auto& wifi = WifiManager::GetInstance();

    if (wifi.IsConfigMode()) {
        return FONT_AWESOME_WIFI_SLASH;
    }
    if (!wifi.IsConnected()) {
        return FONT_AWESOME_WIFI_SLASH;
    }

    int rssi = wifi.GetRssi();
    if (rssi >= -65) {
        return FONT_AWESOME_WIFI;
    } else if (rssi >= -75) {
        return FONT_AWESOME_WIFI_FAIR;
    }
    return FONT_AWESOME_WIFI_WEAK;
}

std::string WifiBoard::GetBoardJson() {
    auto& wifi = WifiManager::GetInstance();
    std::string json = R"({"type":")" + std::string(BOARD_TYPE) + R"(",)";
    json += R"("name":")" + std::string(BOARD_NAME) + R"(",)";

    if (!wifi.IsConfigMode()) {
        json += R"("ssid":")" + wifi.GetSsid() + R"(",)";
        json += R"("rssi":)" + std::to_string(wifi.GetRssi()) + R"(,)";
        json += R"("channel":)" + std::to_string(wifi.GetChannel()) + R"(,)";
        json += R"("ip":")" + wifi.GetIpAddress() + R"(",)";
    }

    json += R"("mac":")" + SystemInfo::GetMacAddress() + R"("})";
    return json;
}

void WifiBoard::SetPowerSaveLevel(PowerSaveLevel level) {
    WifiPowerSaveLevel wifi_level;
    switch (level) {
        case PowerSaveLevel::LOW_POWER:
            wifi_level = WifiPowerSaveLevel::LOW_POWER;
            break;
        case PowerSaveLevel::BALANCED:
            wifi_level = WifiPowerSaveLevel::BALANCED;
            break;
        case PowerSaveLevel::PERFORMANCE:
        default:
            wifi_level = WifiPowerSaveLevel::PERFORMANCE;
            break;
    }
    WifiManager::GetInstance().SetPowerSaveLevel(wifi_level);
}

std::string WifiBoard::GetDeviceStatusJson() {
    auto& board = Board::GetInstance();
    auto root = cJSON_CreateObject();

    // Audio speaker
    auto audio_speaker = cJSON_CreateObject();
    if (auto codec = board.GetAudioCodec()) {
        cJSON_AddNumberToObject(audio_speaker, "volume", codec->output_volume());
    }
    cJSON_AddItemToObject(root, "audio_speaker", audio_speaker);

    // Screen
    auto screen = cJSON_CreateObject();
    if (auto backlight = board.GetBacklight()) {
        cJSON_AddNumberToObject(screen, "brightness", backlight->brightness());
    }
    if (auto display = board.GetDisplay(); display && display->height() > 64) {
        if (auto theme = display->GetTheme()) {
            cJSON_AddStringToObject(screen, "theme", theme->name().c_str());
        }
    }
    cJSON_AddItemToObject(root, "screen", screen);

    // Battery
    int level = 0;
    bool charging = false, discharging = false;
    if (board.GetBatteryLevel(level, charging, discharging)) {
        auto battery = cJSON_CreateObject();
        cJSON_AddNumberToObject(battery, "level", level);
        cJSON_AddBoolToObject(battery, "charging", charging);
        cJSON_AddItemToObject(root, "battery", battery);
    }

    // Network
    auto& wifi = WifiManager::GetInstance();
    auto network = cJSON_CreateObject();
    cJSON_AddStringToObject(network, "type", "wifi");
    cJSON_AddStringToObject(network, "ssid", wifi.GetSsid().c_str());
    int rssi = wifi.GetRssi();
    const char* signal = rssi >= -60 ? "strong" : (rssi >= -70 ? "medium" : "weak");
    cJSON_AddStringToObject(network, "signal", signal);
    cJSON_AddItemToObject(root, "network", network);

    // Chip temperature
    float temp = 0.0f;
    if (board.GetTemperature(temp)) {
        auto chip = cJSON_CreateObject();
        cJSON_AddNumberToObject(chip, "temperature", temp);
        cJSON_AddItemToObject(root, "chip", chip);
    }

    auto str = cJSON_PrintUnformatted(root);
    std::string result(str);
    cJSON_free(str);
    cJSON_Delete(root);
    return result;
}
