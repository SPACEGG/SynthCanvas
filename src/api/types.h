#ifndef SYNTH_CANVAS_API_TYPES_H
#define SYNTH_CANVAS_API_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace synth_canvas {

// Types of signals that can be routed between ports
enum class ConnectionType : int {
    kAudio = 0,
    kEvent = 1,
    kModulation = 2,
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
};

struct CompositeConfig {
    std::vector<InternalPluginConfig> plugins;
    std::vector<InternalRoutingConfig> routings;
    std::vector<ParameterMappingConfig> parameter_mappings;
    std::vector<PortProxyConfig> input_proxies;
    std::vector<PortProxyConfig> output_proxies;
};

// --- Parameter Metadata ---

struct ParameterInfo {
    uint32_t id;
    std::string name;
    std::string module;
    double min_value;
    double max_value;
    double default_value;
    double current_value;
};

using ParameterList = std::vector<ParameterInfo>;

}  // namespace synth_canvas

#endif  // SYNTH_CANVAS_API_TYPES_H
