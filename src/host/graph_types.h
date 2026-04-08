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
    uint32_t index;
    bool is_input;
    clap_audio_port_info clap_info;
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

// Generic event structure for inter-thread communication
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
