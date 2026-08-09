#ifndef _DOORBELL_MQTT_H_
#define _DOORBELL_MQTT_H_

#include <string>
#include <atomic>
#include <mutex>

#include <esp_event.h>
#include <esp_timer.h>
#include <mqtt_client.h>

#include "doorbell_config.h"
#include "doorbell_mqtt_reassembly.h"
#include "doorbell_mqtt_suback.h"

// 门铃 MQTT 客户端。
//
// 设备空闲/待机时 WebSocket 会断开，服务器就无法主动把消息送到设备。门铃是一条
// 极省电的常驻 MQTT 连接，开机联网后一直挂着，专门用来"被叫醒"：服务器往
// 配置的 notification topic 发一条很小的 {"type":"wake"} 消息，设备收到后把
// WebSocket 接上，真正的语音/通知内容仍走 WebSocket 下发。
//
// 本类完全自包含，直接使用 ESP-IDF 原生 esp-mqtt（mqtt_client.h），不依赖项目里
// 走 UDP 音频的那套 MQTT 协议栈，二者互不干扰：
//   - 连接前在配置的 status topic 登记 "offline" 遗嘱（qos1, retained），
//     设备异常掉线时由 broker 自动代发，服务器据此及时判定离线。
//   - 连上后在同一 status topic 发布 "online"（qos1, retained）覆盖遗嘱，
//     并订阅配置的 notification topic 与可选 overview topic。
//   - 收到 {"type":"wake"} 且设备空闲时，反向建立 WebSocket 语音通道。
//   - 断线后由底层 esp-mqtt 客户端按内置策略自动重连。
class DoorbellMqtt {
public:
    DoorbellMqtt();
    ~DoorbellMqtt();

    // 启动常驻门铃连接。连接、认证和主题均来自 OTA 或持久化配置；
    // device_id 用于拒绝尚未取得设备身份的启动。多次调用只有首次生效。
    void Start(const DoorbellMqttConfig& config, const std::string& device_id);

    bool IsConnected() const { return connected_.load(); }

private:
    void OnConnected();
    void OnSubscribed(esp_mqtt_event_handle_t event);
    void RequestReconnectAfterSubscriptionFailure();
    void OnMessage(const std::string& topic, const std::string& payload);
    void OnNotificationMessage(const std::string& payload);
    void OnOverviewMessage(const std::string& payload);
    void PublishTelemetry(bool force);
    static void TelemetryTimerCallback(void* arg);
    static void MqttEventHandler(void* handler_args, esp_event_base_t base,
                                 int32_t event_id, void* event_data);

    std::atomic<bool> connected_{false};
    std::atomic<bool> started_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> subscription_reconnect_pending_{false};

    esp_mqtt_client_handle_t client_ = nullptr;

    DoorbellMqttConfig config_;
    std::string device_id_;
    DoorbellMqttReceiveState receive_state_;
    int notification_subscribe_mid_ = -1;
    int overview_subscribe_mid_ = -1;
    esp_timer_handle_t telemetry_timer_ = nullptr;
    std::mutex telemetry_mutex_;
    bool last_battery_available_ = false;
    int last_battery_level_ = 0;
    int64_t last_telemetry_publish_us_ = 0;
};

#endif // _DOORBELL_MQTT_H_
