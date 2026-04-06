#ifndef SYNTH_CANVAS_HOST_GRAPH_TYPES_H
#define SYNTH_CANVAS_HOST_GRAPH_TYPES_H

#include <clap/clap.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace synth_canvas::host {

// Types of signals that can be routed between ports
enum class ConnectionType : int {
    kAudio = 0,
    kEvent = 1,
    kModulation = 2,
};

// --- Composite Module Configurations ---

// Configuration for an internal plugin within a composite node
struct InternalPluginConfig {
    std::string alias;      // e.g., "osc", "filter"
    std::string plugin_path; // Identifier or path to load the plugin
};

// Configuration for internal routing between nodes
struct InternalRoutingConfig {
    std::string from_node;  // alias of the source node
    uint32_t from_port;
    std::string to_node;    // alias of the target node
    uint32_t to_port;
    ConnectionType type;
};

// Mapping of a high-level parameter ID to an internal plugin parameter
struct ParameterMappingConfig {
    std::string param_id;    // e.g., "cutoff"
    std::string target_node; // alias of the internal node
    uint32_t target_param_index;
};

// Direct connection mapping between external ports and internal node ports
struct PortProxyConfig {
    uint32_t external_port_index;
    std::string internal_node; // alias of the internal node
    uint32_t internal_port_index;
};

// Complete blueprint for instantiating a CompositeNode
struct CompositeConfig {
    std::vector<InternalPluginConfig> plugins;
    std::vector<InternalRoutingConfig> routings;
    std::vector<ParameterMappingConfig> parameter_mappings;
    std::vector<PortProxyConfig> input_proxies;
    std::vector<PortProxyConfig> output_proxies;
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
    } event;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_GRAPH_TYPES_H
