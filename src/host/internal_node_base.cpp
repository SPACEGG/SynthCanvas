#include "internal_node_base.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace synth_canvas::host {

InternalNodeBase::InternalNodeBase() { _output_buffer.owns_memory = true; }

void InternalNodeBase::activate(int32_t sample_rate, int32_t block_size) {
    _current_sample_rate = sample_rate;
    _output_buffer.resize(1, block_size);
    onSampleRateChanged(sample_rate);
    _is_active = true;
}

void InternalNodeBase::deactivate() { _is_active = false; }

void InternalNodeBase::processBegin(int num_frames) { _output_buffer.clear(); }

void InternalNodeBase::processEnd(int num_frames) {
    // Standard internal nodes might not need much here
}

auto InternalNodeBase::getOutputBuffer(uint32_t port_idx) -> AudioBuffer* {
    if (port_idx == 0) return &_output_buffer;
    return nullptr;
}

void InternalNodeBase::reserveOutputBuffers(uint32_t count) {
    // Already handled via _output_buffer resize in activate
}

void InternalNodeBase::setPorts(uint32_t num_inputs, clap_audio_buffer* inputs, uint32_t num_outputs,
                                clap_audio_buffer* outputs) {
    // Internal nodes usually manage their own buffers, 
    // but we can map the framework's output pointers if needed.
    // For now, we rely on getOutputBuffer() for internal routing.
}

void InternalNodeBase::setParameterValue(clap_id param_id, double value) {
    if (auto* slot = getParameterSlot(param_id)) {
        slot->base_value.store(value, std::memory_order_relaxed);
        slot->current_value.store(value, std::memory_order_relaxed);
    }
}

void InternalNodeBase::setParameterValue(const std::string& param_id, double value) {
    try {
        auto id = static_cast<clap_id>(std::stoul(param_id));
        setParameterValue(id, value);
    } catch (...) {
        // Handle non-numeric string IDs if necessary, or log error
    }
}

void InternalNodeBase::applyModulation(clap_id param_id, double value, uint32_t sample_offset) {
    if (auto* slot = getParameterSlot(param_id)) {
        slot->modulation_value.store(value, std::memory_order_relaxed);
        slot->has_modulation = std::abs(value) > 1e-6;

        // Update current value: base + mod (clamped to info range)
        double base = slot->base_value.load(std::memory_order_relaxed);
        double combined = base + value;
        combined = std::max(slot->info.min_value, std::min(slot->info.max_value, combined));
        slot->current_value.store(combined, std::memory_order_relaxed);
    }
}

auto InternalNodeBase::getParameterBaseValue(clap_id param_id) const -> double {
    if (const auto* slot = getParameterSlot(param_id)) {
        return slot->base_value.load(std::memory_order_relaxed);
    }
    return 0.0;
}

auto InternalNodeBase::getParameterModulationOffset(clap_id param_id) const -> double {
    if (const auto* slot = getParameterSlot(param_id)) {
        return slot->modulation_value.load(std::memory_order_relaxed);
    }
    return 0.0;
}

auto InternalNodeBase::popOutputEvent(PluginEvent& out_event) -> bool {
    return _output_events.try_dequeue(out_event);
}

auto InternalNodeBase::getParameterSlot(clap_id param_id) const -> const ParameterSlot* {
    for (const auto& param : _parameters) {
        if (param->info.id == param_id) return param.get();
    }
    return nullptr;
}

auto InternalNodeBase::getParameterSlot(clap_id param_id) -> ParameterSlot* {
    for (auto& param : _parameters) {
        if (param->info.id == param_id) return param.get();
    }
    return nullptr;
}

auto InternalNodeBase::getParameterText(clap_id param_id, double value) const -> std::string {
    return std::to_string(value);
}

void InternalNodeBase::queueEvent(const PluginEvent& event) {
    // Default implementation: do nothing
}

auto InternalNodeBase::getAudioPorts(bool is_input) const -> const std::vector<AudioPortInfo>& {
    return is_input ? _input_ports : _output_ports;
}

void InternalNodeBase::addParameter(clap_id id, const std::string& name, const std::string& module,
                                    double min_val, double max_val, double def_val,
                                    uint32_t flags) {
    auto slot = std::make_unique<ParameterSlot>();
    slot->info.id = id;
    std::strncpy(slot->info.name, name.c_str(), sizeof(slot->info.name) - 1);
    std::strncpy(slot->info.module, module.c_str(), sizeof(slot->info.module) - 1);
    slot->info.min_value = min_val;
    slot->info.max_value = max_val;
    slot->info.default_value = def_val;
    slot->info.flags = CLAP_PARAM_IS_MODULATABLE | flags;

    slot->base_value.store(def_val);
    slot->current_value.store(def_val);
    slot->modulation_value.store(0.0);

    _parameters.push_back(std::move(slot));
}

void InternalNodeBase::addAudioPort(const std::string& name, bool is_input, uint32_t channel_count,
                                   bool is_mod) {
    AudioPortInfo port;
    port.index = static_cast<uint32_t>(is_input ? _input_ports.size() : _output_ports.size());
    port.is_input = is_input;
    port.is_modulation = is_mod;
    port.clap_info.id = port.index;
    std::strncpy(port.clap_info.name, name.c_str(), sizeof(port.clap_info.name) - 1);
    port.clap_info.channel_count = channel_count;
    port.clap_info.flags = CLAP_AUDIO_PORT_IS_MAIN;
    port.clap_info.port_type = CLAP_PORT_STEREO;

    if (is_input) {
        _input_ports.push_back(port);
    } else {
        _output_ports.push_back(port);
    }
}

}  // namespace synth_canvas::host
