#pragma once

#include <vector>
#include <unordered_map>
#include <cstdint>

namespace synth_canvas::host {

class AudioBufferManager {
public:
    AudioBufferManager();
    ~AudioBufferManager();

    void resize(int channels, int max_frames);

    float** get_buffer(uint32_t node_id, int num_frames);

    float** get_input_mix(const std::vector<uint32_t>& source_nodes, int num_frames);

    void mix_to_interleaved(const std::vector<uint32_t>& source_nodes, float* output_data, int num_frames);

private:
    struct Buffer {
        std::vector<float> data;    
        std::vector<float*> ptrs;   
        int channels = 0;
        int capacity = 0;

        void resize(int ch, int caps);
        void clear(int frames);
    };

    std::unordered_map<uint32_t, Buffer> _buffers;
    Buffer _mix_buffer; 
    
    int _channels = 2;
    int _max_frames = 512;

    void ensure_buffer(Buffer& buf, int frames);
};

} // namespace synth_canvas::host
