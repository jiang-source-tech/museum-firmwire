#ifndef XIAOXIN_EVENT_VALIDATION_H
#define XIAOXIN_EVENT_VALIDATION_H

#include <cstddef>
#include <string_view>

inline constexpr size_t kXiaoxinNotificationIdStorageSize = 96;
inline constexpr char kXiaoxinEventNotificationPrefix[] = "xiaoxin_event:";
inline constexpr size_t kXiaoxinEventNotificationPrefixLength =
    sizeof(kXiaoxinEventNotificationPrefix) - 1;
inline constexpr size_t kXiaoxinDeliveryIdMaxLength =
    kXiaoxinNotificationIdStorageSize -
    kXiaoxinEventNotificationPrefixLength - 1;

inline bool IsValidXiaoxinDeliveryId(std::string_view delivery_id) {
    return !delivery_id.empty() &&
        delivery_id.size() <= kXiaoxinDeliveryIdMaxLength;
}

#endif
