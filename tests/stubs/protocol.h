#ifndef TEST_PROTOCOL_STUB_H
#define TEST_PROTOCOL_STUB_H

#include <cstdint>
#include <vector>

struct AudioStreamPacket {
    int sample_rate = 16000;
    int frame_duration = 60;
    uint32_t timestamp = 0;
    uint32_t playback_generation = 0;
    uint64_t pipeline_epoch = 0;
    std::vector<uint8_t> payload;
};

#endif
