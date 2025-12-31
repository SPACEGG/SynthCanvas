#ifndef AUDIO_BUFFER_MANAGER_H
#define AUDIO_BUFFER_MANAGER_H

#include <vector>
#include <unordered_map>
#include <cstdint>
#include "constants.h"

namespace synth_canvas::host {

class AudioBufferManager {
public:
    AudioBufferManager();
    ~AudioBufferManager();

    // Get a zero-initialized buffer for a specific node (plugin output)
    float** get_buffer(uint32_t node_id, int num_frames);

    // Get a mixed input buffer from multiple source nodes
    float** get_input_mix(const std::vector<uint32_t>& source_nodes, int num_frames);

    // Mix multiple sources into a single interleaved buffer (for final output)
    void mix_to_interleaved(const std::vector<uint32_t>& source_nodes, float* output_data, int num_frames);

    void resize(int channels, int max_frames);

private:
    struct Buffer {
        std::vector<float> data;
        std::vector<float*> ptrs;
        int channels = 0;
        int capacity = 0;

        void resize(int ch, int caps);
        void clear(int frames);
    };

    void ensure_buffer(Buffer& buf, int frames);

    std::unordered_map<uint32_t, Buffer> _buffers;
    Buffer _mix_buffer; // Temp buffer for mixing inputs
    
    int _channels = 2;
    int _max_frames = constants::DEFAULT_FRAMES_PER_BLOCK;
};

} // namespace synth_canvas::host

#endif // AUDIO_BUFFER_MANAGER_H
