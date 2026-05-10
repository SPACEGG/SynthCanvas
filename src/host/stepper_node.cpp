#include "stepper_node.h"

#include <cstring>


namespace synth_canvas::host {

StepperNode::StepperNode() {
    // 1. Register Parameters
    addParameter(kParamSteps, "Steps", "Logic", 1.0, kMaxSteps, 4.0,
                 CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED);
    addParameter(kParamPorts, "Ports", "Logic", 1.0, kMaxPorts, 2.0,
                 CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED);
    addParameter(kParamCurrentStep, "Current Step", "Logic", 0.0, kMaxSteps - 1, 0.0,
                 CLAP_PARAM_IS_READONLY | CLAP_PARAM_IS_STEPPED);

    // 2. Register Trigger IN
    addEventPort("Trigger IN", true);

    // 3. Pre-allocate all potential Trigger OUT ports (Max 16)
    // This ensures RT-safety by creating all queues at construction.
    for (uint32_t i = 0; i < kMaxPorts; ++i) {
        addEventPort("Trigger OUT " + std::to_string(i), false);
    }

    // Initialize matrix to 0
    for (auto& slot : _matrix) {
        slot.store(0, std::memory_order_relaxed);
    }

    // Sync initial metadata for active ports
    updateActivePortsMetadata();
}

void StepperNode::activate(int32_t sample_rate, int32_t block_size) {
    InternalNodeBase::activate(sample_rate, block_size);
    // Reset playback state on activation
    _current_index.store(-1, std::memory_order_relaxed);
}

void StepperNode::setParameterValue(clap_id param_id, double value) {
    InternalNodeBase::setParameterValue(param_id, value);

    if (param_id == kParamSteps) {
        _active_steps.store(static_cast<int32_t>(value), std::memory_order_relaxed);
    } else if (param_id == kParamPorts) {
        auto new_ports = static_cast<int32_t>(value);
        int32_t old_ports = _active_ports.load(std::memory_order_relaxed);

        if (new_ports < old_ports) {
            // Permanent bit clearing for removed ports
            uint16_t mask = (1 << new_ports) - 1;
            for (auto& slot : _matrix) {
                uint16_t current = slot.load(std::memory_order_relaxed);
                slot.store(current & mask, std::memory_order_relaxed);
            }
        }

        _active_ports.store(new_ports, std::memory_order_relaxed);
        updateActivePortsMetadata();

        // Notify router to rebuild graph and prune connections
        if (on_ports_changed) {
            on_ports_changed(_instance_id);
        }
    }
}

void StepperNode::queueEvent(const PluginEvent& event) {
    if (event.event.header.type == CLAP_EVENT_NOTE_ON) {
        // Trigger received: Advance step
        int32_t steps = _active_steps.load(std::memory_order_relaxed);
        int32_t next_index = (_current_index.load(std::memory_order_relaxed) + 1) % steps;
        _current_index.store(next_index, std::memory_order_relaxed);

        // Routing logic
        uint16_t bitmask = _matrix[next_index].load(std::memory_order_relaxed);
        int32_t ports = _active_ports.load(std::memory_order_relaxed);

        for (int32_t p = 0; p < ports; ++p) {
            if (bitmask & (1 << p)) {
                // Bit is set: replicate trigger to this port
                PluginEvent replicated = event;
                replicated.event.note.port_index = static_cast<int16_t>(p);
                // Note: InternalNodeBase::popOutputEvent uses port_index as vector index
                if (p < static_cast<int32_t>(_output_event_queues.size())) {
                    _output_event_queues[p]->try_enqueue(replicated);
                }
            }
        }

        // GUI Sync: Send parameter update for kParamCurrentStep
        PluginEvent gui_ev;
        gui_ev.event.header.size = sizeof(clap_event_param_value);
        gui_ev.event.header.time = event.event.header.time;
        gui_ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        gui_ev.event.header.type = CLAP_EVENT_PARAM_VALUE;
        gui_ev.event.header.flags = 0;
        gui_ev.event.param_value.param_id = kParamCurrentStep;
        gui_ev.event.param_value.value = static_cast<double>(next_index);
        gui_ev.event.param_value.note_id = -1;
        gui_ev.event.param_value.port_index = -1;
        gui_ev.event.param_value.key = -1;
        gui_ev.event.param_value.channel = -1;

        // Push to main thread queue for GUI polling/signals
        _output_events_to_main.try_enqueue(gui_ev);
    } else {
        // Pass-through other events if necessary?
        // For stepper, usually we only care about Trigger IN.
    }
}

void StepperNode::process() {
    // Stepper is purely event-driven, nothing to do in audio process loop for now.
}

auto StepperNode::saveState(std::vector<uint8_t>& data) -> bool {
    // 64 bytes for 32 x uint16_t matrix
    size_t start = data.size();
    data.resize(start + (kMaxSteps * sizeof(uint16_t)));

    std::array<uint16_t, kMaxSteps> buffer;
    for (uint32_t i = 0; i < kMaxSteps; ++i) {
        buffer[i] = _matrix[i].load(std::memory_order_relaxed);
    }

    std::memcpy(data.data() + start, buffer.data(), buffer.size() * sizeof(uint16_t));
    return true;
}

auto StepperNode::loadState(const std::vector<uint8_t>& data) -> bool {
    if (data.size() < (kMaxSteps * sizeof(uint16_t))) return false;

    std::array<uint16_t, kMaxSteps> buffer;
    std::memcpy(buffer.data(), data.data(), buffer.size() * sizeof(uint16_t));

    for (uint32_t i = 0; i < kMaxSteps; ++i) {
        _matrix[i].store(buffer[i], std::memory_order_relaxed);
    }
    return true;
}
auto StepperNode::getAudioPorts(bool is_input) const -> const std::vector<AudioPortInfo>& {
    if (is_input) {
        return _input_ports;
    }
    return _active_output_ports;
}

void StepperNode::updateActivePortsMetadata() {
    _active_output_ports.clear();
    int32_t count = _active_ports.load(std::memory_order_relaxed);

    // Slice from pre-allocated _output_ports
    for (int32_t i = 0; i < count && i < static_cast<int32_t>(_output_ports.size()); ++i) {
        _active_output_ports.push_back(_output_ports[i]);
    }
}

}  // namespace synth_canvas::host
