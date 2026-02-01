#ifndef AUDIO_BUFFER_MANAGER_H
#define AUDIO_BUFFER_MANAGER_H

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "constants.h"

namespace synth_canvas::host {

class AudioBufferManager {
   public:
    AudioBufferManager();
    ~AudioBufferManager();

    // Get a zero-initialized buffer for a specific node and port
    auto getBuffer(uint32_t node_id, uint32_t port_index, int num_frames) -> float**;

    // Get a read-only buffer for a specific node and port (does NOT clear)
    auto getReadOnlyBuffer(uint32_t node_id, uint32_t port_index) -> float**;

    // Get a mixed input buffer for a specific target port from multiple source (node, port) pairs
    struct PortSource {
        uint32_t node_id;
        uint32_t port_index;
    };
    auto getInputMix(uint32_t target_port_index, const std::vector<PortSource>& sources,
                     int num_frames) -> float**;

    // Mix multiple sources into a single interleaved buffer (for final output)
    void mixToInterleaved(const std::vector<PortSource>& sources, float* output_data,
                          int num_frames);

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

    void ensureBuffer(Buffer& buf, int frames);

    // Map: NodeID -> Vector of Output Port Buffers
    std::unordered_map<uint32_t, std::vector<Buffer>> _node_outputs;
    
    // Multiple mix buffers for multiple input ports
    std::vector<Buffer> _input_mix_buffers;

    int _channels = 2;
    int _max_frames = constants::kDefaultFramesPerBlock;
};

}  // namespace synth_canvas::host

#endif  // AUDIO_BUFFER_MANAGER_H
