#ifndef AUDIO_BACKPRESSURE_H
#define AUDIO_BACKPRESSURE_H

#include <stddef.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

enum AudioBackpressureAction {
    kAudioBackpressureEnqueue,
    kAudioBackpressureWait,
    kAudioBackpressureDrop,
};

static inline AudioBackpressureAction audio_backpressure_action(
    size_t queue_size,
    size_t max_queue_size,
    bool allow_wait
) {
    if (queue_size < max_queue_size) {
        return kAudioBackpressureEnqueue;
    }
    return allow_wait ? kAudioBackpressureWait : kAudioBackpressureDrop;
}

#endif
