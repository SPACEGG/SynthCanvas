#include "host/nodes/internal/transport_node.h"

#include <cmath>

namespace synth_canvas::host {

TransportNode::TransportNode() {
    _node_type_name = "transport";
    ParameterConfig tempo_config{.type = MappingType::kLinear,
                                 .min_functional = 20.0,
                                 .max_functional = 300.0,
                                 .unit_suffix = ""};
    addParameter(kParamTempo, "Tempo", "Transport", tempo_config.toNormalized(120.0), tempo_config,
                 CLAP_PARAM_IS_AUTOMATABLE);
    addSteppedParameter(kParamPlaying, "Playing", "Transport", 0.0, 1.0, 0.0,
                        CLAP_PARAM_IS_AUTOMATABLE);
}

void TransportNode::processBegin(int num_frames) {
    InternalNodeBase::processBegin(num_frames);

    if (_transport) {
        bool changed = false;

        // Sync from global transport if different from what we last saw
        if (std::abs(_transport->tempo - _last_tempo) > 0.001) {
            _last_tempo = _transport->tempo;
            changed = true;
        }
        if (_transport->is_playing != _last_playing) {
            _last_playing = _transport->is_playing;
            changed = true;
        }

        if (changed) {
            _is_syncing.store(true, std::memory_order_release);
            // setParameterValue expectation depends on whether it's functional or normalized.
            // InternalNodeBase::setParameterValue expects NORMALIZED if it's a mapped param.
            auto* slot = getParameterSlot(kParamTempo);
            if (slot) {
                double normalized = _param_configs[kParamTempo].toNormalized(_last_tempo);
                setParameterValue(kParamTempo, normalized);
            }
            setParameterValue(kParamPlaying, _last_playing ? 1.0 : 0.0);
            _is_syncing.store(false, std::memory_order_release);
        }
    }
}

void TransportNode::process() {
    // No audio/event processing needed in the main loop
}

void TransportNode::setParameterValue(clap_id param_id, double value) {
    InternalNodeBase::setParameterValue(param_id, value);

    if (!_is_syncing.load(std::memory_order_acquire)) {
        _needs_update_to_main.store(true, std::memory_order_release);
    }
}

void TransportNode::pollMainThread() {
    InternalNodeBase::pollMainThread();

    if (_needs_update_to_main.exchange(false, std::memory_order_acq_rel)) {
        if (on_transport_change_requested) {
            double tempo = getFunctionalValue(kParamTempo);
            bool playing = getFunctionalValue(kParamPlaying) > 0.5;
            on_transport_change_requested(tempo, playing);
        }
    }
}

auto TransportNode::getParameterText(clap_id param_id, double value) const -> std::string {
    if (param_id == kParamTempo) {
        double functional = getFunctionalValue(kParamTempo);
        return std::to_string(static_cast<int>(std::round(functional)));
    }
    return InternalNodeBase::getParameterText(param_id, value);
}

}  // namespace synth_canvas::host
