#include "audio_buffer_manager.h"

#include <cstring>


namespace synth_canvas::host {

AudioBufferManager::AudioBufferManager() = default;
AudioBufferManager::~AudioBufferManager() = default;

void AudioBufferManager::reserveInputMixBuffers(const std::vector<uint32_t>& port_counts) {
    if (port_counts.size() > _input_mix_buffers.size()) {
        _input_mix_buffers.resize(port_counts.size());
    }

    for (size_t i = 0; i < port_counts.size(); ++i) {
        auto& node_ports = _input_mix_buffers[i];
        uint32_t needed_ports = port_counts[i];

        if (needed_ports > node_ports.size()) {
            node_ports.resize(needed_ports);
        }

        for (uint32_t p = 0; p < needed_ports; ++p) {
            if (!node_ports[p]) {
                node_ports[p] = std::make_unique<AudioBuffer>();
                node_ports[p]->owns_memory = true;
                node_ports[p]->resize(_channels, _max_frames);
            }
        }
    }
}

auto AudioBufferManager::getMixBuffer(size_t node_index, uint32_t port_idx) -> AudioBuffer* {
    if (node_index < _input_mix_buffers.size()) {
        auto& node_ports = _input_mix_buffers[node_index];
        if (port_idx < node_ports.size()) {
            return node_ports[port_idx].get();
        }
    }
    return nullptr;
}

void AudioBufferManager::prepareBlock() {
    for (auto& node_ports : _input_mix_buffers) {
        for (auto& buf : node_ports) {
            if (buf) buf->clear();
        }
    }
}

void AudioBufferManager::resize(int channels, int max_frames) {
    _channels = channels;
    _max_frames = max_frames;
    for (auto& node_ports : _input_mix_buffers) {
        for (auto& buf : node_ports) {
            if (buf) buf->resize(channels, max_frames);
        }
    }
}

}  // namespace synth_canvas::host
