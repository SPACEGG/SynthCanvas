#include "audio_buffer_manager.h"
#include <algorithm>
#include <cstring>

namespace synth_canvas::host {

AudioBufferManager::AudioBufferManager() {
    // Default values
    _channels = 2;
    _max_frames = 512;
}

AudioBufferManager::~AudioBufferManager() {
}

void AudioBufferManager::resize(int channels, int max_frames) {
    _channels = channels;
    _max_frames = max_frames;

    // Clear existing buffers to force reallocation with new layout if needed,
    // or just clear data.
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
    
    // Update pointers
    for (int i = 0; i < channels; ++i) {
        ptrs[i] = data.data() + (i * capacity);
    }
}

void AudioBufferManager::Buffer::clear(int frames) {
    // Zero out only the used frames for each channel
    // Since data is flattened [Ch0...][Ch1...], we can't just memset the whole thing 
    // if capacity > frames (unless we don't care about garbage in the tail).
    // But for safety/correctness with variable frames, let's clear what we use.
    // Optimization: If capacity == frames, memset everything.
    
    int safe_frames = std::min(frames, capacity);
    
    for (int c = 0; c < channels; ++c) {
        float* ch_ptr = ptrs[c];
        std::memset(ch_ptr, 0, safe_frames * sizeof(float));
    }
}

void AudioBufferManager::ensure_buffer(Buffer& buf, int frames) {
    // In RT thread, this might allocate if frames > capacity.
    // 'resize' (public method) should be called with a sufficient max_frames to avoid this.
    if (buf.channels != _channels || buf.capacity < frames) {
        // We need to resize (Allocation!)
        // Just resize to at least _max_frames to avoid frequent resizing
        int target_frames = std::max(frames, _max_frames);
        buf.resize(_channels, target_frames);
    }
}

float** AudioBufferManager::get_buffer(uint32_t node_id, int num_frames) {
    Buffer& buf = _buffers[node_id];
    ensure_buffer(buf, num_frames);
    buf.clear(num_frames); // Always return a clean buffer? 
    // Wait, usually output buffers are overwritten by the plugin.
    // But if the plugin accumulates or doesn't write everything, silence is safer.
    // CLAP plugins usually replace, but let's be safe.
    return buf.ptrs.data();
}

float** AudioBufferManager::get_input_mix(const std::vector<uint32_t>& source_nodes, int num_frames) {
    ensure_buffer(_mix_buffer, num_frames);
    _mix_buffer.clear(num_frames);
    
    if (source_nodes.empty()) {
        return _mix_buffer.ptrs.data(); // Silence
    }

    // Sum sources
    for (uint32_t src_id : source_nodes) {
        auto it = _buffers.find(src_id);
        if (it == _buffers.end()) continue;
        
        Buffer& src_buf = it->second;
        // Use the min of num_frames and src_buf capacity/frames to be safe
        // But logically, source should have produced num_frames.
        
        for (int c = 0; c < _channels; ++c) {
            float* dest = _mix_buffer.ptrs[c];
            const float* src = src_buf.ptrs[c];
            
            // Vectorize this loop ideally
            for (int i = 0; i < num_frames; ++i) {
                dest[i] += src[i];
            }
        }
    }
    
    return _mix_buffer.ptrs.data();
}

void AudioBufferManager::mix_to_interleaved(const std::vector<uint32_t>& source_nodes, float* output_data, int num_frames) {
    // output_data is interleaved [L, R, L, R...]
    
    // Clear output first? Oboe buffer might contain garbage or previous data?
    // Usually we just overwrite.
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

} // namespace synth_canvas::host
