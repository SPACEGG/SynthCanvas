#include "host/nodes/base/boundary_node.h"

#include <algorithm>
#include <cstring>

namespace synth_canvas::host {

BoundaryNode::BoundaryNode(Type type) : _type(type), _event_queue(constants::kEventQueueSize) {}

void BoundaryNode::activate(int32_t sample_rate, int32_t block_size) {
    _sample_rate = sample_rate;
    _block_size = block_size;
    _is_active = true;

    for (auto& buf : _output_buffers) {
        buf->resize(constants::kDefaultChannelCount, block_size);
    }
}

void BoundaryNode::deactivate() { _is_active = false; }

auto BoundaryNode::getCreationInfo() const -> std::string {
    return _type == Type::kInputProxy ? "input" : "output";
}

void BoundaryNode::setPorts(uint32_t num_inputs, clap_audio_buffer* inputs, uint32_t num_outputs,
                            clap_audio_buffer* outputs) {
    _current_inputs = inputs;
    _current_num_inputs = num_inputs;

    for (uint32_t i = 0; i < _output_buffers.size(); ++i) {
        if (i < num_outputs && outputs != nullptr && outputs[i].data32 != nullptr) {
            _output_buffers[i]->data32 = outputs[i].data32;
        } else {
            _output_buffers[i]->data32 = _output_buffers[i]->ptrs.data();
        }
    }
}

void BoundaryNode::processBegin(int num_frames) { _current_num_frames = num_frames; }

void BoundaryNode::process() {
    if (_type == Type::kInputProxy) {
        // Input Proxy Node: Copy from Parent External Inputs to Node's Own Output Buffers
        for (uint32_t i = 0;
             i < std::min(_external_count, static_cast<uint32_t>(_output_buffers.size())); ++i) {
            auto* dst = _output_buffers[i].get();
            auto& src = _external_buffers[i];

            if (dst && src.data32) {
                uint32_t ch_to_copy =
                    std::min(static_cast<uint32_t>(dst->channels), src.channel_count);
                for (uint32_t c = 0; c < ch_to_copy; ++c) {
                    std::memcpy(dst->data32[c], src.data32[c], _current_num_frames * sizeof(float));
                }
            }
        }
    } else {
        // Output Proxy Node: Copy from Mixed Internal Graph Inputs to Parent External Outputs
        for (uint32_t i = 0; i < std::min(_current_num_inputs, _external_count); ++i) {
            auto& src = _current_inputs[i];
            auto& dst = _external_buffers[i];

            if (src.data32 && dst.data32) {
                uint32_t ch_to_copy = std::min(src.channel_count, dst.channel_count);
                for (uint32_t c = 0; c < ch_to_copy; ++c) {
                    std::memcpy(dst.data32[c], src.data32[c], _current_num_frames * sizeof(float));
                }
            }
        }
    }
}

auto BoundaryNode::saveState(std::vector<uint8_t>& data) -> bool { return true; }
auto BoundaryNode::loadState(const std::vector<uint8_t>& data) -> bool { return true; }

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

auto BoundaryNode::popOutputEvent(uint32_t port_index, PluginEvent& out_event) -> bool {
    if (port_index == 0) {
        return _event_queue.try_dequeue(out_event);
    }
    return false;
}

auto BoundaryNode::getAudioPorts(bool is_input) const -> const std::vector<AudioPortInfo>& {
    return is_input ? _input_port_info : _output_port_info;
}

auto BoundaryNode::getParameters() const -> const std::vector<std::unique_ptr<ParameterSlot>>& {
    return _empty_params;
}

auto BoundaryNode::getParameterSlot(clap_id param_id) const -> const ParameterSlot* {
    return nullptr;
}

auto BoundaryNode::getParameterText(clap_id param_id, double value) const -> std::string {
    return std::to_string(value);
}

void BoundaryNode::setAudioPorts(bool is_input, const std::vector<AudioPortInfo>& ports) {
    if (is_input) {
        _input_port_info = ports;
    } else {
        _output_port_info = ports;
        reserveOutputBuffers(static_cast<uint32_t>(ports.size()));
    }
}

void BoundaryNode::setExternalBuffers(clap_audio_buffer* buffers, uint32_t count) {
    _external_buffers = buffers;
    _external_count = count;
}

}  // namespace synth_canvas::host
