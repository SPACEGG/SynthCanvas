#ifndef SYNTH_CANVAS_API_TYPES_H
#define SYNTH_CANVAS_API_TYPES_H

#include "../host/graph_types.h"

namespace synth_canvas {

// Re-export common types for external API users
using ConnectionType = host::ConnectionType;

// Metadata for a single parameter
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

// --- Composite Module Configurations ---
using InternalPluginConfig = host::InternalPluginConfig;
using InternalRoutingConfig = host::InternalRoutingConfig;
using ParameterMappingConfig = host::ParameterMappingConfig;
using PortProxyConfig = host::PortProxyConfig;
using CompositeConfig = host::CompositeConfig;

}  // namespace synth_canvas

#endif  // SYNTH_CANVAS_API_TYPES_H
