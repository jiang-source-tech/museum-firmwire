#include <cassert>
#include <string>

#include "xiaoxin_event_validation.h"

int main() {
    assert(!IsValidXiaoxinDeliveryId(""));
    assert(IsValidXiaoxinDeliveryId(
        std::string(kXiaoxinDeliveryIdMaxLength, 'a')));
    assert(!IsValidXiaoxinDeliveryId(
        std::string(kXiaoxinDeliveryIdMaxLength + 1, 'b')));
    assert(kXiaoxinEventNotificationPrefixLength +
               kXiaoxinDeliveryIdMaxLength + 1 ==
           kXiaoxinNotificationIdStorageSize);
    return 0;
}
