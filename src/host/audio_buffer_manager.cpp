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

    _buffers.clear();
    _mix_buffer.resize(_channels, _max_frames);
}

void AudioBufferManager::Buffer::resize(int ch, int caps) {
    if (channels == ch && capacity >= caps) {
        return;
    }

    channels = ch;
    capacity = caps;

    data.resize(channels * capacity);
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

auto AudioBufferManager::getBuffer(uint32_t node_id, int num_frames) -> float** {
    Buffer& buf = _buffers[node_id];
    ensureBuffer(buf, num_frames);
    buf.clear(num_frames);
    return buf.ptrs.data();
}

auto AudioBufferManager::getInputMix(const std::vector<uint32_t>& source_nodes, int num_frames)
    -> float** {
    ensureBuffer(_mix_buffer, num_frames);
    _mix_buffer.clear(num_frames);

    if (source_nodes.empty()) {
        return _mix_buffer.ptrs.data();
    }

    for (uint32_t src_id : source_nodes) {
        auto it = _buffers.find(src_id);
        if (it == _buffers.end()) continue;

        Buffer& src_buf = it->second;

        for (int c = 0; c < _channels; ++c) {
            float* dest = _mix_buffer.ptrs[c];
            const float* src = src_buf.ptrs[c];

            for (int i = 0; i < num_frames; ++i) {
                dest[i] += src[i];
            }
        }
    }

    return _mix_buffer.ptrs.data();
}

void AudioBufferManager::mixToInterleaved(const std::vector<uint32_t>& source_nodes,
                                          float* output_data, int num_frames) {
    std::memset(output_data, 0, num_frames * _channels * sizeof(float));

    for (uint32_t src_id : source_nodes) {
        auto it = _buffers.find(src_id);
        if (it == _buffers.end()) continue;

        Buffer& src_buf = it->second;

        for (int i = 0; i < num_frames; ++i) {
            for (int c = 0; c < _channels; ++c) {
                output_data[i * _channels + c] += src_buf.ptrs[c][i];
            }
        }
    }
}

}  // namespace synth_canvas::host