#ifndef DEVICE_LOCATION_HEARTBEAT_H
#define DEVICE_LOCATION_HEARTBEAT_H

#include <memory>
#include <string>

class DeviceLocationHeartbeat {
public:
    DeviceLocationHeartbeat();
    ~DeviceLocationHeartbeat();

    void Configure(const std::string& ota_url,
                   const std::string& device_id,
                   const std::string& username,
                   const std::string& password);
    void OnNetworkConnected();
    void OnNetworkDisconnected();

private:
    struct RequestConfig {
        std::string device_id;
        std::string username;
        std::string password;
    };
    struct State;
    struct WorkerContext;

    static std::string DeriveHeartbeatUrl(const std::string& ota_url);
    static bool QueueHeartbeatLocked(const std::shared_ptr<State>& state);
    static void WorkerTask(void* argument);
    static void SendHeartbeat(const RequestConfig& config,
                              const std::string& heartbeat_url);

    std::shared_ptr<State> state_;
};

#endif  // DEVICE_LOCATION_HEARTBEAT_H
