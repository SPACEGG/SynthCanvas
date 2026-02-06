#include "audio_buffer_manager.h"

#include <algorithm>
#include <cstring>

#include "constants.h"

namespace synth_canvas::host {

AudioBufferManager::AudioBufferManager() {
    _channels = constants::kDefaultChannelCount;
    _max_frames = constants::kDefaultFramesPerBlock;
}

AudioBufferManager::~AudioBufferManager() {}

void AudioBufferManager::resize(int channels, int max_frames) {
    _channels = channels;
    _max_frames = max_frames;

    _node_outputs.clear();
    _input_mix_buffers.clear();
}

void AudioBufferManager::Buffer::resize(int ch, int caps) {
    if (channels == ch && capacity >= caps) {
        return;
    }

    channels = ch;
    capacity = caps;

    data.assign(channels * capacity, 0.0f);
    ptrs.resize(channels);

    for (int i = 0; i < channels; ++i) {
        ptrs[i] = data.data() + (i * capacity);
    }
}

void AudioBufferManager::Buffer::clear(int frames) {
    int safe_frames = std::min(frames, capacity);

    for (int c = 0; c < channels; ++c) {
        float* ch_ptr = ptrs[c];
        std::memset(ch_ptr, 0, safe_frames * sizeof(float));
    }
}

void AudioBufferManager::ensureBuffer(Buffer& buf, int frames) {
    if (buf.channels != _channels || buf.capacity < frames) {
        int target_frames = std::max(frames, _max_frames);
        buf.resize(_channels, target_frames);
    }
}

auto AudioBufferManager::getBuffer(uint32_t node_id, uint32_t port_index, int num_frames)
    -> float** {
    auto& ports = _node_outputs[node_id];
    if (port_index >= ports.size()) {
        ports.resize(port_index + 1);
    }

    Buffer& buf = ports[port_index];
    ensureBuffer(buf, num_frames);
    buf.clear(num_frames);
    return buf.ptrs.data();
}

auto AudioBufferManager::getReadOnlyBuffer(uint32_t node_id, uint32_t port_index) -> float** {
    auto it = _node_outputs.find(node_id);
    if (it != _node_outputs.end()) {
        const auto& ports = it->second;
        if (port_index < ports.size()) {
            return const_cast<float**>(ports[port_index].ptrs.data());
        }
    }
    return nullptr;
}

auto AudioBufferManager::getInputMix(uint32_t target_port_index,
                                     const std::vector<PortSource>& sources, int num_frames)
    -> float** {
    if (target_port_index >= _input_mix_buffers.size()) {
        _input_mix_buffers.resize(target_port_index + 1);
    }

    Buffer& mix_buf = _input_mix_buffers[target_port_index];
    ensureBuffer(mix_buf, num_frames);
    mix_buf.clear(num_frames);

    if (sources.empty()) {
        return mix_buf.ptrs.data();
    }

    for (const auto& src : sources) {
        auto it = _node_outputs.find(src.node_id);
        if (it == _node_outputs.end()) continue;

        const auto& ports = it->second;
        if (src.port_index >= ports.size()) continue;

        const Buffer& src_buf = ports[src.port_index];

        for (int c = 0; c < _channels; ++c) {
            float* dest = mix_buf.ptrs[c];
            const float* src_ptr = src_buf.ptrs[c];

            for (int i = 0; i < num_frames; ++i) {
                dest[i] += src_ptr[i];
            }
        }
    }

    return mix_buf.ptrs.data();
}

void AudioBufferManager::mixToInterleaved(const std::vector<PortSource>& sources,
                                          float* output_data, int num_frames) {
    std::memset(output_data, 0, num_frames * _channels * sizeof(float));

    for (const auto& src : sources) {
        auto it = _node_outputs.find(src.node_id);
        if (it == _node_outputs.end()) continue;

        const auto& ports = it->second;
        if (src.port_index >= ports.size()) continue;

        const Buffer& src_buf = ports[src.port_index];

        for (int i = 0; i < num_frames; ++i) {
            for (int c = 0; c < _channels; ++c) {
                output_data[i * _channels + c] += src_buf.ptrs[c][i];
            }
        }
    }
}

}  // namespace synth_canvas::host