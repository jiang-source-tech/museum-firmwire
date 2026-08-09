#include "device_location_heartbeat.h"
#include "device_location_heartbeat_state.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <new>

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "LocationHeartbeat"

struct DeviceLocationHeartbeat::State {
    std::mutex mutex;
    RequestConfig config;
    std::string heartbeat_url_;
    bool configured_ = false;
    bool network_connected_ = false;
    bool boot_heartbeat_sent_ = false;
    bool reconnect_armed_ = false;
    DeviceLocationHeartbeatRequestGate request_gate;
    bool stopping_ = false;
    TaskHandle_t worker_task_ = nullptr;
};

struct DeviceLocationHeartbeat::WorkerContext {
    std::shared_ptr<State> state;
    RequestConfig config;
    std::string heartbeat_url;
};

DeviceLocationHeartbeat::DeviceLocationHeartbeat()
    : state_(std::make_shared<State>()) {
}

DeviceLocationHeartbeat::~DeviceLocationHeartbeat() {
    auto state = std::move(state_);
    if (state == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    state->stopping_ = true;
    state->request_gate.CancelPending();
}

std::string DeviceLocationHeartbeat::DeriveHeartbeatUrl(const std::string& ota_url) {
    const size_t scheme_end = ota_url.find("://");
    if (scheme_end == std::string::npos) {
        return {};
    }

    std::string scheme = ota_url.substr(0, scheme_end);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (scheme != "http" && scheme != "https") {
        return {};
    }

    const size_t authority_start = scheme_end + 3;
    const size_t authority_end = ota_url.find_first_of("/?#", authority_start);
    const std::string authority = ota_url.substr(
        authority_start,
        authority_end == std::string::npos ? std::string::npos
                                           : authority_end - authority_start);
    if (authority.empty() || authority.find('@') != std::string::npos ||
        std::any_of(authority.begin(), authority.end(), [](unsigned char value) {
            return std::isspace(value) != 0;
        })) {
        return {};
    }

    const auto valid_port = [](const std::string& port_text) {
        if (port_text.empty()) {
            return false;
        }
        unsigned int port_value = 0;
        for (unsigned char value : port_text) {
            if (!std::isdigit(value)) {
                return false;
            }
            port_value = port_value * 10 + static_cast<unsigned int>(value - '0');
            if (port_value > 65535) {
                return false;
            }
        }
        return port_value != 0;
    };

    if (authority.front() == '[') {
        const size_t closing_bracket = authority.find(']');
        if (closing_bracket == std::string::npos || closing_bracket == 1) {
            return {};
        }
        const std::string suffix = authority.substr(closing_bracket + 1);
        if (!suffix.empty() &&
            (suffix.front() != ':' || !valid_port(suffix.substr(1)))) {
            return {};
        }
    } else {
        if (authority.find('[') != std::string::npos ||
            authority.find(']') != std::string::npos) {
            return {};
        }
        const size_t port_separator = authority.find(':');
        if (port_separator != std::string::npos) {
            // Reject unbracketed IPv6 so a colon can only introduce a port.
            if (authority.find(':', port_separator + 1) != std::string::npos ||
                port_separator == 0 ||
                !valid_port(authority.substr(port_separator + 1))) {
                return {};
            }
        }
    }

    const size_t origin_end = authority_end == std::string::npos
                                  ? ota_url.size()
                                  : authority_end;
    return ota_url.substr(0, origin_end) +
           "/api/xiaoxin/device/location-heartbeat";
}

void DeviceLocationHeartbeat::Configure(const std::string& ota_url,
                                        const std::string& device_id,
                                        const std::string& username,
                                        const std::string& password) {
    const std::string heartbeat_url = DeriveHeartbeatUrl(ota_url);
    auto state = state_;
    std::lock_guard<std::mutex> lock(state->mutex);

    if (heartbeat_url.empty() || device_id.empty() || username.empty() ||
        password.empty()) {
        state->configured_ = false;
        state->request_gate.CancelPending();
        state->reconnect_armed_ = false;
        ESP_LOGW(TAG, "Location heartbeat disabled due to invalid configuration");
        return;
    }

    state->config = RequestConfig{device_id, username, password};
    state->heartbeat_url_ = heartbeat_url;
    state->configured_ = true;

    if (state->network_connected_ && !state->boot_heartbeat_sent_ &&
        QueueHeartbeatLocked(state)) {
        state->boot_heartbeat_sent_ = true;
    }
}

void DeviceLocationHeartbeat::OnNetworkConnected() {
    auto state = state_;
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->network_connected_) {
        return;
    }

    state->network_connected_ = true;
    if (!state->configured_) {
        return;
    }

    if (!state->boot_heartbeat_sent_) {
        if (QueueHeartbeatLocked(state)) {
            state->boot_heartbeat_sent_ = true;
        }
    } else if (state->reconnect_armed_ && QueueHeartbeatLocked(state)) {
        state->reconnect_armed_ = false;
    }
}

void DeviceLocationHeartbeat::OnNetworkDisconnected() {
    auto state = state_;
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->network_connected_) {
        return;
    }

    state->network_connected_ = false;
    state->request_gate.OnNetworkDisconnected();
    if (state->configured_ && state->boot_heartbeat_sent_) {
        state->reconnect_armed_ = true;
    }
}

bool DeviceLocationHeartbeat::QueueHeartbeatLocked(
    const std::shared_ptr<State>& state) {
    if (state->stopping_ || !state->configured_ ||
        !state->network_connected_) {
        return false;
    }

    if (state->request_gate.request_in_flight_) {
        state->request_gate.MarkPending();
        return true;
    }

    auto* context = new (std::nothrow) WorkerContext{
        state, state->config, state->heartbeat_url_};
    if (context == nullptr) {
        ESP_LOGE(TAG, "Unable to allocate location heartbeat worker");
        return false;
    }

    state->request_gate.MarkStarted();
    const BaseType_t created = xTaskCreate(
        WorkerTask, "location_hb", 6144, context, 2, &state->worker_task_);
    if (created != pdPASS) {
        state->request_gate.MarkStartFailed();
        state->worker_task_ = nullptr;
        delete context;
        ESP_LOGE(TAG, "Unable to start location heartbeat worker");
        return false;
    }
    return true;
}

void DeviceLocationHeartbeat::WorkerTask(void* argument) {
    auto* context = static_cast<WorkerContext*>(argument);

    while (true) {
        SendHeartbeat(context->config, context->heartbeat_url);

        std::unique_lock<std::mutex> lock(context->state->mutex);
        if (context->state->request_gate.Complete(
                !context->state->stopping_ && context->state->configured_ &&
                context->state->network_connected_)) {
            context->config = context->state->config;
            context->heartbeat_url = context->state->heartbeat_url_;
            continue;
        }

        context->state->worker_task_ = nullptr;
        lock.unlock();
        delete context;
        vTaskDelete(nullptr);
    }
}

void DeviceLocationHeartbeat::SendHeartbeat(
    const RequestConfig& config,
    const std::string& heartbeat_url) {
    esp_http_client_config_t http_config = {};
    http_config.url = heartbeat_url.c_str();
    http_config.method = HTTP_METHOD_POST;
    http_config.timeout_ms = 10000;
    http_config.crt_bundle_attach = esp_crt_bundle_attach;
    http_config.disable_auto_redirect = true;

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) {
        ESP_LOGW(TAG, "Location heartbeat skipped: HTTP client unavailable");
        return;
    }

    const std::string authorization = "Bearer " + config.password;
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Device-Id", config.device_id.c_str());
    esp_http_client_set_header(client, "Device-Username", config.username.c_str());
    esp_http_client_set_header(client, "Authorization", authorization.c_str());
    esp_http_client_set_post_field(client, "{}", 2);

    const esp_err_t result = esp_http_client_perform(client);
    if (result != ESP_OK) {
        esp_http_client_cleanup(client);
        ESP_LOGW(TAG, "Location heartbeat transport failed, class=perform");
        return;
    }

    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (status_code >= 200 && status_code < 300) {
        ESP_LOGI(TAG, "Location heartbeat accepted");
    } else {
        ESP_LOGW(TAG, "Location heartbeat failed, status=%d", status_code);
    }
}
