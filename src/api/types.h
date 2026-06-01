#ifndef SYNTH_CANVAS_API_TYPES_H
#define SYNTH_CANVAS_API_TYPES_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "utils/constants.h"

namespace synth_canvas {

// Types of signals that can be routed between ports
enum class ConnectionType : int {
    kAudio = 0,
    kEvent = 1,
    kModulation = 2,
};

// --- Abstracted Events for GUI/External API (No CLAP dependency) ---

enum class SystemEventType : uint32_t {
    kNoteOn,
    kNoteOff,
    kNoteChoke,
    kNoteExpression,
    kParameterValue,
    kParameterMod,
    kMidi,
    kConnectionDisconnected,
    kNodePortsChanged,
};

struct SystemEvent {
    SystemEventType type;
    uint32_t instance_id;

    union {
        struct {
            int16_t port_index;
            int16_t key;
            int16_t channel;
            double velocity;
            int32_t note_id;
        } note;

        struct {
            uint32_t param_id;
            double value;
            int16_t key;
            int16_t channel;
        } parameter;

        struct {
            int16_t port_index;
            std::array<uint8_t, 3> data;
        } midi;

        struct {
            uint32_t from_node;
            uint32_t from_port;
            uint32_t to_node;
            uint32_t to_port;
            int32_t type;
        } connection;

        struct {
            uint32_t input_count;
            uint32_t output_count;
        } port_change;
    } data;
};

// Global transport and timing information
struct TransportState {
    double tempo = 120.0;         // BPM
    double song_pos_beats = 0.0;  // Current position in beats (0.0 to N.N)
    bool is_playing = false;

    int32_t ts_num = 4;    // Time signature numerator
    int32_t ts_denom = 4;  // Time signature denominator
};

// --- Composite Module Configurations ---

struct InternalPluginConfig {
    std::string alias;
    std::string plugin_path;
};

struct InternalRoutingConfig {
    std::string from_node;
    uint32_t from_port;
    std::string to_node;
    uint32_t to_port;
    ConnectionType type;
    float scale = 1.0f;
    bool bypass = false;
};

struct ParameterMappingConfig {
    std::string param_id;
    std::string target_node;
    uint32_t target_param_index;
};

struct PortProxyConfig {
    uint32_t external_port_index;
    std::string internal_node;
    uint32_t internal_port_index;
    ConnectionType type;
    uint32_t target_param_id = host::constants::kClapInvalidId;
};

struct CompositeConfig {
    std::vector<InternalPluginConfig> plugins;
    std::vector<InternalRoutingConfig> routings;
    std::vector<ParameterMappingConfig> parameter_mappings;
    std::vector<PortProxyConfig> input_proxies;
    std::vector<PortProxyConfig> output_proxies;
    std::string display;
};

// --- Parameter Metadata ---

struct ParameterInfo {
    uint32_t id;
    std::string name;
    std::string module;
    double min_value;
    double max_value;
    double default_value;
    double base_value;
    double current_value;
};

using ParameterList = std::vector<ParameterInfo>;

struct PortInfo {
    uint32_t index;
    std::string name;
    bool is_input;
    ConnectionType type;
};

using PortList = std::vector<PortInfo>;

}  // namespace synth_canvas

#endif  // SYNTH_CANVAS_API_TYPES_H
