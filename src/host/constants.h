#pragma once

#include <cstdint>
#include <cstddef>

namespace synth_canvas {
namespace host {
namespace constants {

    // Audio Settings & Defaults
    // 0 allows Oboe/systems to choose the optimal native rate
    constexpr int32_t UNSPECIFIED_SAMPLE_RATE = 0; 
    constexpr int32_t DEFAULT_SAMPLE_RATE = 48000;
    constexpr int32_t FALLBACK_SAMPLE_RATE = 44100;
    
    constexpr int32_t DEFAULT_FRAMES_PER_BLOCK = 512;
    constexpr int32_t DEFAULT_CHANNEL_COUNT = 2;
    constexpr int32_t DEFAULT_AUDIO_PORT_COUNT = 1;
    constexpr int32_t BUFFER_CAPACITY_MULTIPLIER = 8;

    // Queue Sizes
    constexpr size_t SNAPSHOT_QUEUE_SIZE = 32;
    constexpr size_t EVENT_QUEUE_SIZE = 4096;

    // MIDI & Protocol
    constexpr double MIDI_MAX_VELOCITY = 127.0;
    constexpr int MIDI_CC_STATUS_BYTE = 0xB0;
    constexpr int16_t DEFAULT_EVENT_PORT_INDEX = 0;
    constexpr int32_t CLAP_INVALID_ID = -1;

    // Timing & Timeouts
    constexpr int PLUGIN_DEACTIVATE_TIMEOUT_MS = 200;
    constexpr int PLUGIN_DEACTIVATE_SLEEP_MS = 1;

    // Identifiers
    constexpr uint32_t AUDIO_OUTPUT_NODE_ID = 0;
    constexpr uint32_t INITIAL_PLUGIN_INSTANCE_ID = 1;

} // namespace constants
} // namespace host
} // namespace synth_canvas
