#include <assert.h>
#include <stdio.h>

#include "../main/audio/audio_backpressure.h"

static void realtime_capture_drops_when_encode_queue_is_full() {
    assert(audio_backpressure_action(2, 2, false) == kAudioBackpressureDrop);
}

static void realtime_capture_enqueues_when_space_is_available() {
    assert(audio_backpressure_action(1, 2, false) == kAudioBackpressureEnqueue);
}

static void non_realtime_path_waits_when_encode_queue_is_full() {
    assert(audio_backpressure_action(2, 2, true) == kAudioBackpressureWait);
}

int main(void) {
    realtime_capture_drops_when_encode_queue_is_full();
    realtime_capture_enqueues_when_space_is_available();
    non_realtime_path_waits_when_encode_queue_is_full();
    puts("audio_backpressure tests passed");
    return 0;
}
