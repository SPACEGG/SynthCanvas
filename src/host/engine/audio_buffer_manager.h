#ifndef AUDIO_BUFFER_MANAGER_H
#define AUDIO_BUFFER_MANAGER_H

#include <cstdint>
#include <memory>
#include <vector>

#include "utils/constants.h"
#include "host/graph/graph_types.h"

namespace synth_canvas::host {

class AudioBufferManager {
   public:
    AudioBufferManager();
    ~AudioBufferManager();

    // Pre-allocation (Main Thread)
    // Reserves memory for internal summing buffers.
    void reserveInputMixBuffers(const std::vector<uint32_t>& port_counts);

    // Mix Buffer Access (Audio Thread)
    // Returns a pre-allocated mix buffer for internal summing at the specified node and port.
    auto getMixBuffer(size_t node_index, uint32_t port_idx) -> AudioBuffer*;

    // Synchronization
    // Prepares the manager for a new processing block (e.g., state resets).
    void prepareBlock();

    // Utilities
    // Resizes all managed mix buffers to match the current engine configuration.
    void resize(int channels, int max_frames);

   private:
    // Pre-allocated owned buffers for mixing: [node_index][port_index]
    std::vector<std::vector<std::unique_ptr<AudioBuffer>>> _input_mix_buffers;

    int _channels = 2;
    int _max_frames = constants::kDefaultFramesPerBlock;
};

}  // namespace synth_canvas::host

#endif  // AUDIO_BUFFER_MANAGER_H
