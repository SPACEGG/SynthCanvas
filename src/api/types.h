#ifndef SYNTH_CANVAS_API_TYPES_H
#define SYNTH_CANVAS_API_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace synth_canvas {

enum class ConnectionType : int {
    kAudio = 0,
    kEvent = 1,
    kModulation = 2,
};

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
