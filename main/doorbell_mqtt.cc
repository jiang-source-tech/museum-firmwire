#include "doorbell_mqtt.h"
#include "application.h"
#include "device_status.h"
#include "doorbell_mqtt_contract.h"
#include "settings.h"

#include <esp_log.h>
#include <cJSON.h>

#define TAG "Doorbell"

namespace {
constexpr int64_t kTelemetryPollIntervalUs = 30LL * 1000 * 1000;
constexpr int64_t kTelemetryHeartbeatIntervalUs = 10LL * 60 * 1000 * 1000;
}  // namespace

DoorbellMqtt::DoorbellMqtt() {}

DoorbellMqtt::~DoorbellMqtt() {
    stopping_.store(true);
    subscription_reconnect_pending_.store(false);
    receive_state_.Clear();
    if (telemetry_timer_ != nullptr) {
        esp_timer_stop(telemetry_timer_);
        esp_timer_delete(telemetry_timer_);
        telemetry_timer_ = nullptr;
    }
    if (client_ != nullptr) {
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
    }
}

void DoorbellMqtt::Start(const DoorbellMqttConfig& config,
                         const std::string& device_id) {
    if (!IsDoorbellMqttConfigValidForDevice(config, device_id)) {
        ESP_LOGW(TAG, "doorbell mqtt config unusable");
        return;
    }

    std::string broker_uri;
    if (!NormalizeDoorbellMqttEndpoint(config.endpoint, broker_uri)) {
        ESP_LOGW(TAG, "doorbell mqtt broker address invalid");
        return;
    }

    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        ESP_LOGW(TAG, "doorbell already started");
        return;
    }

    config_ = config;
    device_id_ = device_id;

    if (telemetry_timer_ == nullptr) {
        const esp_timer_create_args_t telemetry_timer_args = {
            .callback = TelemetryTimerCallback,
            .arg = this,
            .name = "doorbell_telemetry",
        };
        const esp_err_t timer_err = esp_timer_create(
            &telemetry_timer_args, &telemetry_timer_);
        if (timer_err != ESP_OK) {
            ESP_LOGW(TAG, "doorbell telemetry timer unavailable: %s",
                     esp_err_to_name(timer_err));
            telemetry_timer_ = nullptr;
        }
    }

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = broker_uri.c_str();
    cfg.credentials.client_id = config_.client_id.c_str();
    cfg.credentials.username = config_.username.c_str();
    cfg.credentials.authentication.password = config_.password.c_str();
    cfg.session.keepalive = config_.keepalive_seconds;
    // 遗嘱（LWT）：异常掉线时 broker 代发，使服务器及时判定离线。
    cfg.session.last_will.topic = config_.status_topic.c_str();
    cfg.session.last_will.msg = "offline";
    cfg.session.last_will.msg_len = 7;
    cfg.session.last_will.qos = config_.qos;
    cfg.session.last_will.retain = 1;

    client_ = esp_mqtt_client_init(&cfg);
    if (client_ == nullptr) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        started_.store(false);
        return;
    }

    esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, MqttEventHandler, this);

    ESP_LOGI(TAG, "starting configured doorbell MQTT");
    esp_err_t err = esp_mqtt_client_start(client_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
        started_.store(false);
    }
    // 已启动客户端的连接失败/断线由 esp-mqtt 内置自动重连处理。
}

void DoorbellMqtt::MqttEventHandler(void* handler_args, esp_event_base_t base,
                                    int32_t event_id, void* event_data) {
    (void)base;
    DoorbellMqtt* self = static_cast<DoorbellMqtt*>(handler_args);
    auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
    case MQTT_EVENT_CONNECTED:
        self->receive_state_.Clear();
        self->connected_.store(true);
        self->OnConnected();
        break;
    case MQTT_EVENT_DISCONNECTED:
        self->receive_state_.Clear();
        self->connected_.store(false);
        if (self->telemetry_timer_ != nullptr) {
            esp_timer_stop(self->telemetry_timer_);
        }
        if (self->subscription_reconnect_pending_.exchange(false) &&
            !self->stopping_.load()) {
            const esp_err_t err = esp_mqtt_client_reconnect(self->client_);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "doorbell subscription reconnect requested");
            } else {
                ESP_LOGW(TAG, "doorbell subscription reconnect request failed");
            }
        } else if (!self->stopping_.load()) {
            ESP_LOGI(TAG, "doorbell disconnected; will auto-reconnect");
        }
        break;
    case MQTT_EVENT_DATA: {
        std::string topic;
        std::string payload;
        const auto result = AppendDoorbellMqttFragment(
            self->receive_state_, event->topic, event->topic_len,
            event->data, event->data_len, event->total_data_len,
            event->current_data_offset, topic, payload);
        if (result == DoorbellMqttFragmentResult::kComplete) {
            self->OnMessage(topic, payload);
        } else if (result == DoorbellMqttFragmentResult::kRejected) {
            ESP_LOGW(TAG, "discarding invalid doorbell mqtt fragment sequence");
        }
        break;
    }
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "doorbell mqtt error event");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        self->OnSubscribed(event);
        break;
    default:
        break;
    }
}

void DoorbellMqtt::OnConnected() {
    // 发布在线状态（retained）覆盖遗嘱，并订阅门铃 topic。
    const int mid_pub = esp_mqtt_client_publish(
        client_, config_.status_topic.c_str(), "online", 6, config_.qos, 1);
    notification_subscribe_mid_ = esp_mqtt_client_subscribe(
        client_, config_.notification_topic.c_str(), config_.qos);
    overview_subscribe_mid_ = -1;
    if (!config_.overview_topic.empty()) {
        overview_subscribe_mid_ = esp_mqtt_client_subscribe(
            client_, config_.overview_topic.c_str(), config_.qos);
    }
    PublishTelemetry(true);
    if (telemetry_timer_ != nullptr) {
        esp_timer_stop(telemetry_timer_);
        const esp_err_t timer_err = esp_timer_start_periodic(
            telemetry_timer_, kTelemetryPollIntervalUs);
        if (timer_err != ESP_OK) {
            ESP_LOGW(TAG, "doorbell telemetry timer start failed: %s",
                     esp_err_to_name(timer_err));
        }
    }
    if (mid_pub < 0 || notification_subscribe_mid_ < 0 ||
        (!config_.overview_topic.empty() && overview_subscribe_mid_ < 0)) {
        ESP_LOGW(TAG, "doorbell publish/subscription request queue failed");
        return;
    }
    ESP_LOGI(TAG, "doorbell publish/subscription request queued");
}

void DoorbellMqtt::PublishTelemetry(bool force) {
    if (!connected_.load() || client_ == nullptr) {
        return;
    }
    const std::string topic = DoorbellTelemetryTopic(config_);
    if (topic.empty()) {
        return;
    }

    cJSON* status = xiaoxin::BuildDeviceStatusJson();
    if (status == nullptr) {
        return;
    }
    const cJSON* battery = cJSON_GetObjectItem(status, "battery_level");
    const bool battery_available = cJSON_IsNumber(battery);
    const int battery_level = battery_available ? battery->valueint : 0;
    const int64_t now_us = esp_timer_get_time();

    std::lock_guard<std::mutex> lock(telemetry_mutex_);
    const bool battery_changed =
        battery_available != last_battery_available_ ||
        (battery_available && battery_level != last_battery_level_);
    const bool heartbeat_due =
        last_telemetry_publish_us_ == 0 ||
        now_us - last_telemetry_publish_us_ >= kTelemetryHeartbeatIntervalUs;
    if (!force && !battery_changed && !heartbeat_due) {
        cJSON_Delete(status);
        return;
    }

    cJSON_AddStringToObject(status, "type", "device_status");
    char* payload = cJSON_PrintUnformatted(status);
    if (payload == nullptr) {
        cJSON_Delete(status);
        return;
    }

    const int mid = esp_mqtt_client_publish(
        client_, topic.c_str(), payload, 0, config_.qos, 0);
    cJSON_free(payload);
    cJSON_Delete(status);
    if (mid < 0) {
        ESP_LOGW(TAG, "doorbell telemetry publish queue failed");
        return;
    }

    last_battery_available_ = battery_available;
    last_battery_level_ = battery_level;
    last_telemetry_publish_us_ = now_us;
    ESP_LOGI(TAG, "doorbell telemetry publish queued");
}

void DoorbellMqtt::TelemetryTimerCallback(void* arg) {
    static_cast<DoorbellMqtt*>(arg)->PublishTelemetry(false);
}

void DoorbellMqtt::OnSubscribed(esp_mqtt_event_handle_t event) {
    const bool has_error_handle = event->error_handle != nullptr;
    const bool subscribe_failed =
        has_error_handle &&
        event->error_handle->error_type == MQTT_ERROR_TYPE_SUBSCRIBE_FAILED;
    const auto result = EvaluateDoorbellMqttSuback(
        has_error_handle, subscribe_failed,
        reinterpret_cast<const uint8_t*>(event->data), event->data_len);
    if (result != DoorbellMqttSubackResult::kAccepted) {
        ESP_LOGW(TAG, "doorbell subscription rejected or malformed");
        RequestReconnectAfterSubscriptionFailure();
        return;
    }

    if (event->msg_id == notification_subscribe_mid_) {
        notification_subscribe_mid_ = -1;
        ESP_LOGI(TAG, "doorbell notification subscription confirmed");
    } else if (event->msg_id == overview_subscribe_mid_) {
        overview_subscribe_mid_ = -1;
        ESP_LOGI(TAG, "doorbell overview subscription confirmed");
    } else {
        ESP_LOGD(TAG, "doorbell subscription confirmation unmatched");
    }
}

void DoorbellMqtt::RequestReconnectAfterSubscriptionFailure() {
    if (stopping_.load() || client_ == nullptr ||
        subscription_reconnect_pending_.exchange(true)) {
        return;
    }

    const esp_err_t err = esp_mqtt_client_disconnect(client_);
    if (err != ESP_OK) {
        subscription_reconnect_pending_.store(false);
        ESP_LOGW(TAG, "doorbell subscription disconnect request failed");
    }
}

void DoorbellMqtt::OnMessage(const std::string& topic, const std::string& payload) {
    switch (ClassifyDoorbellMqttTopic(config_, topic)) {
    case DoorbellMqttTopic::kNotification:
        OnNotificationMessage(payload);
        return;
    case DoorbellMqttTopic::kOverview:
        OnOverviewMessage(payload);
        return;
    default:
        ESP_LOGD(TAG, "ignoring doorbell mqtt message on unconfigured topic");
        return;
    }
}

void DoorbellMqtt::OnNotificationMessage(const std::string& payload) {
    if (payload.find('\0') != std::string::npos) {
        ESP_LOGW(TAG, "invalid doorbell notification body");
        return;
    }

    cJSON* root = cJSON_ParseWithLength(payload.data(), payload.size());
    if (root == nullptr) {
        ESP_LOGE(TAG, "failed to parse doorbell notification");
        return;
    }

    cJSON* type = cJSON_GetObjectItem(root, "type");
    // 先判空再比较，避免对缺失/非字符串字段解引用导致崩溃。
    if (!cJSON_IsString(type)) {
        ESP_LOGW(TAG, "doorbell 'type' missing or not a string; discarding");
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type->valuestring, "wake") == 0) {
        auto& app = Application::GetInstance();
        // 仅在空闲时反向建 WebSocket；正在对话则不打断。
        if (app.GetDeviceState() == kDeviceStateIdle) {
            ESP_LOGI(TAG, "doorbell 'wake' received; reverse-opening WebSocket");
            app.WakeForNotification();  // 线程安全：内部投递事件到主任务
        } else {
            ESP_LOGI(TAG, "doorbell 'wake' ignored; device not idle");
        }
    } else {
        ESP_LOGI(TAG, "doorbell notification is not wake; ignoring");
    }

    cJSON_Delete(root);
}

void DoorbellMqtt::OnOverviewMessage(const std::string& payload) {
    Application::GetInstance().HandleXiaoxinOverviewMqttMessage(
        payload, device_id_);
}
