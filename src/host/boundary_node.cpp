#include "boundary_node.h"

namespace synth_canvas::host {

BoundaryNode::BoundaryNode() : _event_queue(constants::kEventQueueSize) {}

void BoundaryNode::activate(int32_t sample_rate, int32_t block_size) {
    _sample_rate = sample_rate;
    _block_size = block_size;
    _is_active = true;

    for (auto& buf : _output_buffers) {
        buf->resize(constants::kDefaultChannelCount, block_size);
    }
}

void BoundaryNode::deactivate() { _is_active = false; }

auto BoundaryNode::getOutputBuffer(uint32_t port_idx) -> AudioBuffer* {
    if (port_idx < _output_buffers.size()) {
        return _output_buffers[port_idx].get();
    }
    return nullptr;
}

void BoundaryNode::reserveOutputBuffers(uint32_t count) {
    if (count > _output_buffers.size()) {
        size_t old_size = _output_buffers.size();
        _output_buffers.resize(count);
        for (size_t i = old_size; i < count; ++i) {
            _output_buffers[i] = std::make_unique<AudioBuffer>();
            _output_buffers[i]->owns_memory = true;
            if (_is_active) {
                _output_buffers[i]->resize(constants::kDefaultChannelCount, _block_size);
            }
        }
    }
}

void BoundaryNode::queueEvent(const PluginEvent& event) { _event_queue.try_enqueue(event); }

auto BoundaryNode::popOutputEvent(PluginEvent& out_event) -> bool {
    return _event_queue.try_dequeue(out_event);
}

auto BoundaryNode::getAudioPorts(bool is_input) const -> const std::vector<AudioPortInfo>& {
    return is_input ? _input_port_info : _output_port_info;
}

auto BoundaryNode::getParameters() const -> const std::vector<std::unique_ptr<ParameterSlot>>& {
    return _empty_params;
}

void BoundaryNode::setAudioPorts(bool is_input, const std::vector<AudioPortInfo>& ports) {
    if (is_input) {
        _input_port_info = ports;
    } else {
        _output_port_info = ports;
        reserveOutputBuffers(static_cast<uint32_t>(ports.size()));
    }
}

}  // namespace synth_canvas::host
