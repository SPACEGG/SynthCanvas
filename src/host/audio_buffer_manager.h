#pragma once

#include <vector>
#include <unordered_map>
#include <cstdint>

namespace synth_canvas::host {

class AudioBufferManager {
public:
    AudioBufferManager();
    ~AudioBufferManager();

    // Initialize/Resize the buffer configuration.
    // Should be called when stream starts or configuration changes.
    // Not real-time safe (allocates memory).
    void resize(int channels, int max_frames);

    // Returns a pointer array (float**) for the given node, suitable for CLAP process.
    // The buffer is resized if necessary (though try to avoid this in RT path) and cleared/prepared.
    // In a strict RT context, all buffers should be pre-allocated.
    // For now, we lazily allocate but with capacity reservation.
    float** get_buffer(uint32_t node_id, int num_frames);

    // Sums the outputs of source_nodes into an internal mix buffer and returns its pointers.
    // Use this to prepare the input for a plugin.
    float** get_input_mix(const std::vector<uint32_t>& source_nodes, int num_frames);

    // Mixes the outputs of source_nodes into the final interleaved output buffer.
    void mix_to_interleaved(const std::vector<uint32_t>& source_nodes, float* output_data, int num_frames);

private:
    struct Buffer {
        std::vector<float> data;    // Flattened audio data: [Channel0][Channel1]...
        std::vector<float*> ptrs;   // Pointers to the start of each channel in 'data'
        int channels = 0;
        int capacity = 0;

        void resize(int ch, int caps);
        void clear(int frames);
    };

    std::unordered_map<uint32_t, Buffer> _buffers;
    Buffer _mix_buffer; // Shared buffer for summing inputs
    
    int _channels = 2;
    int _max_frames = 512;

    // Helper to ensure a buffer is ready
    void ensure_buffer(Buffer& buf, int frames);
};

} // namespace synth_canvas::host
