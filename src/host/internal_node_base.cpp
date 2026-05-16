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

void InternalNodeBase::processBegin(int num_frames) {
    _output_buffer.frames = num_frames;
    _output_buffer.clear();
}

void InternalNodeBase::processEvents(int num_frames) {
    // Default implementation: do nothing. Derived classes (like SequencerNode)
    // can override this to re-run event generation logic during re-injections.
}

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

void InternalNodeBase::setPorts(uint32_t num_inputs, clap_audio_buffer* inputs,
                                uint32_t num_outputs, clap_audio_buffer* outputs) {
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

auto InternalNodeBase::saveState(std::vector<uint8_t>& data) -> bool {
    auto param_count = static_cast<uint32_t>(_parameters.size());
    const auto* p_count = reinterpret_cast<const uint8_t*>(&param_count);
    data.insert(data.end(), p_count, p_count + sizeof(uint32_t));

    for (const auto& param : _parameters) {
        uint32_t id = param->info.id;
        double val = param->base_value.load(std::memory_order_relaxed);

        const auto* p_id = reinterpret_cast<const uint8_t*>(&id);
        data.insert(data.end(), p_id, p_id + sizeof(uint32_t));

        const auto* p_val = reinterpret_cast<const uint8_t*>(&val);
        data.insert(data.end(), p_val, p_val + sizeof(double));
    }
    return true;
}

auto InternalNodeBase::loadState(const std::vector<uint8_t>& data) -> bool {
    if (data.size() < sizeof(uint32_t)) return false;

    uint32_t param_count = 0;
    std::memcpy(&param_count, data.data(), sizeof(uint32_t));

    size_t offset = sizeof(uint32_t);
    if (data.size() < offset + param_count * (sizeof(uint32_t) + sizeof(double))) return false;

    for (uint32_t i = 0; i < param_count; ++i) {
        uint32_t id = 0;
        double val = 0.0;

        std::memcpy(&id, data.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        std::memcpy(&val, data.data() + offset, sizeof(double));
        offset += sizeof(double);

        setParameterValue(id, val);
    }
    return true;
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

auto InternalNodeBase::getParameterCurrentValue(clap_id param_id) const -> double {
    if (const auto* slot = getParameterSlot(param_id)) {
        return slot->current_value.load(std::memory_order_relaxed);
    }
    return 0.0;
}

auto InternalNodeBase::getParameterModulationOffset(clap_id param_id) const -> double {
    if (const auto* slot = getParameterSlot(param_id)) {
        return slot->modulation_value.load(std::memory_order_relaxed);
    }
    return 0.0;
}

auto InternalNodeBase::popOutputEvent(uint32_t port_index, PluginEvent& out_event) -> bool {
    if (port_index < _output_event_queues.size()) {
        return _output_event_queues[port_index]->try_dequeue(out_event);
    }
    return false;
}

void InternalNodeBase::pollMainThread() {
    PluginEvent ev;
    while (_output_events_to_main.try_dequeue(ev)) {
        if (on_event_occured) {
            on_event_occured(_instance_id, ev);
        }
    }
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
                                    bool is_mod, clap_id target_param_id) {
    AudioPortInfo port;
    port.index = static_cast<uint32_t>(is_input ? _input_ports.size() : _output_ports.size());
    port.is_input = is_input;
    port.is_modulation = is_mod;
    port.target_param_id = target_param_id;
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

void InternalNodeBase::addEventPort(const std::string& name, bool is_input) {
    // Note: We use the same AudioPortInfo structure for now but set port_type to something else if
    // needed. However, the GraphProcessor expects nodes to have ports. For internal nodes, we just
    // need to ensure _output_event_queues has a queue for every output port index.
    AudioPortInfo port;
    port.index = static_cast<uint32_t>(is_input ? _input_ports.size() : _output_ports.size());
    port.is_input = is_input;
    port.is_modulation = false;
    port.target_param_id = -1;
    port.clap_info.id = port.index;
    std::strncpy(port.clap_info.name, name.c_str(), sizeof(port.clap_info.name) - 1);
    port.clap_info.channel_count = 0;
    port.clap_info.flags = CLAP_AUDIO_PORT_IS_MAIN;
    port.clap_info.port_type = "event";  // Custom type or use CLAP_PORT_MIDI

    if (is_input) {
        _input_ports.push_back(port);
    } else {
        _output_ports.push_back(port);
        _output_event_queues.push_back(std::make_unique<moodycamel::ReaderWriterQueue<PluginEvent>>(
            constants::kEventQueueSize));
    }
}

void InternalNodeBase::resizeOutputPorts(uint32_t new_count) {
    if (new_count == _output_ports.size()) return;

    if (new_count < _output_ports.size()) {
        _output_ports.resize(new_count);
        _output_event_queues.resize(new_count);
    } else {
        for (auto i = static_cast<uint32_t>(_output_ports.size()); i < new_count; ++i) {
            addEventPort("Note OUT " + std::to_string(i), false);
        }
    }
}

}  // namespace synth_canvas::host
