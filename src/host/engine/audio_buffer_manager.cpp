#include "host/engine/audio_buffer_manager.h"

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

void AudioBufferManager::reserveSharedBuffers(size_t count) {
    if (count > _shared_buffers.size()) {
        size_t old_size = _shared_buffers.size();
        _shared_buffers.resize(count);
        for (size_t i = old_size; i < count; ++i) {
            _shared_buffers[i] = std::make_unique<AudioBuffer>();
            _shared_buffers[i]->owns_memory = true;
            _shared_buffers[i]->resize(_channels, _max_frames);
        }
    }
}

auto AudioBufferManager::getSharedBuffer(size_t index) -> AudioBuffer* {
    if (index < _shared_buffers.size()) {
        return _shared_buffers[index].get();
    }
    return nullptr;
}

void AudioBufferManager::resize(int channels, int max_frames) {
    _channels = channels;
    _max_frames = max_frames;
    for (auto& node_ports : _input_mix_buffers) {
        for (auto& buf : node_ports) {
            if (buf) buf->resize(channels, max_frames);
        }
    }
    for (auto& buf : _shared_buffers) {
        if (buf) buf->resize(channels, max_frames);
    }
}

}  // namespace synth_canvas::host
