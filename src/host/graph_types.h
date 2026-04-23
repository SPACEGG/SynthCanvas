#ifndef SYNTH_CANVAS_HOST_GRAPH_TYPES_H
#define SYNTH_CANVAS_HOST_GRAPH_TYPES_H

#include <clap/clap.h>

#include <atomic>
#include <cstdint>

#include "../api/types.h"

namespace synth_canvas::host {

using ConnectionType = synth_canvas::ConnectionType;
using InternalPluginConfig = synth_canvas::InternalPluginConfig;
using InternalRoutingConfig = synth_canvas::InternalRoutingConfig;
using ParameterMappingConfig = synth_canvas::ParameterMappingConfig;
using PortProxyConfig = synth_canvas::PortProxyConfig;
using CompositeConfig = synth_canvas::CompositeConfig;

// Global transport and timing information provided to all processing nodes.
struct TransportState {
    double tempo = 120.0;         // BPM
    double song_pos_beats = 0.0;  // Current position in beats (0.0 to N.N)
    bool is_playing = false;

    int32_t ts_num = 4;    // Time signature numerator
    int32_t ts_denom = 4;  // Time signature denominator
};

// Represents a connection from an output port to an input port
struct PortConnection {
    uint32_t from_node;
    uint32_t from_port;
    uint32_t to_node;
    uint32_t to_port;
    ConnectionType type;
};

// Metadata for an audio port
struct AudioPortInfo {
    clap_audio_port_info clap_info;
    uint32_t index;
    bool is_input;
    bool is_modulation;
};

// Internal storage for a parameter
struct ParameterSlot {
    clap_param_info info;
    std::atomic<double> base_value{0.0};
    std::atomic<double> current_value{0.0};
    std::atomic<double> modulation_value{0.0};
    bool has_modulation = false;
};

// Standardized audio buffer object used across the engine.
struct AudioBuffer {
    float** data32 = nullptr;
    int32_t channels = 0;
    int32_t frames = 0;
    bool owns_memory = false;

    // Internal storage used only when owns_memory is true
    std::vector<float> internal_data;
    std::vector<float*> ptrs;

    AudioBuffer() = default;
    ~AudioBuffer() = default;

    AudioBuffer(const AudioBuffer&) = delete;
    auto operator=(const AudioBuffer&) -> AudioBuffer& = delete;

    AudioBuffer(AudioBuffer&& other) noexcept { *this = std::move(other); }
    auto operator=(AudioBuffer&& other) noexcept -> AudioBuffer& {
        if (this != &other) {
            channels = other.channels;
            frames = other.frames;
            owns_memory = other.owns_memory;
            internal_data = std::move(other.internal_data);
            ptrs = std::move(other.ptrs);

            if (owns_memory && !ptrs.empty()) {
                data32 = ptrs.data();
            } else {
                data32 = other.data32;
            }

            other.data32 = nullptr;
            other.channels = 0;
            other.frames = 0;
            other.owns_memory = false;
        }
        return *this;
    }

    void resize(int32_t ch, int32_t fr) {
        if (!owns_memory) return;
        channels = ch;
        frames = fr;
        internal_data.assign(static_cast<size_t>(ch) * fr, 0.0f);
        ptrs.resize(ch);
        for (int i = 0; i < ch; ++i) {
            ptrs[i] = &internal_data[static_cast<size_t>(i) * fr];
        }
        data32 = ptrs.data();
    }

    void clear() {
        if (data32 && channels > 0 && frames > 0) {
            for (int i = 0; i < channels; ++i) {
                if (data32[i]) std::memset(data32[i], 0, frames * sizeof(float));
            }
        }
    }

    void accumulate(const AudioBuffer* other, int32_t num_frames) {
        if (!data32 || !other || !other->data32) return;
        int32_t ch_to_mix = std::min(channels, other->channels);
        int32_t fr_to_mix = std::min(frames, num_frames);

        for (int c = 0; ch_to_mix > c; ++c) {
            float* dst = data32[c];
            const float* src = other->data32[c];
            if (!dst || !src) continue;
            for (int f = 0; fr_to_mix > f; ++f) {
                dst[f] += src[f];
            }
        }
    }
};

struct PluginEvent {
    union {
        clap_event_header_t header;
        clap_event_note_t note;
        clap_event_midi_t midi;
        clap_event_param_value_t param_value;
        clap_event_param_mod_t param_mod;
    } event;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_GRAPH_TYPES_H
