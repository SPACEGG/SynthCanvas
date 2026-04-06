#ifndef SYNTH_CANVAS_HOST_PROCESSING_NODE_H
#define SYNTH_CANVAS_HOST_PROCESSING_NODE_H

#include <clap/clap.h>

#include <cstdint>
#include <vector>

#include "graph_types.h"

namespace synth_canvas::host {

class ProcessingNode {
   public:
    virtual ~ProcessingNode() = default;

    // --- Lifecycle ---
    virtual void activate(int32_t sample_rate, int32_t block_size) = 0;
    virtual void deactivate() = 0;
    virtual void setProcessingEnabled(bool enabled) = 0;

    // --- Audio / Event Processing ---
    virtual void setPorts(uint32_t num_inputs, clap_audio_buffer* inputs, uint32_t num_outputs,
                          clap_audio_buffer* outputs) = 0;
    virtual void processBegin(int num_frames) = 0;
    virtual void process() = 0;
    virtual void processEnd(int num_frames) = 0;

    // --- Parameters & External Events ---
    virtual void setParameterValue(clap_id param_id, double value) = 0;
    virtual void queueEvent(const PluginEvent& event) = 0;
    virtual void pollMainThread() = 0;

    // --- Metadata Accessors ---
    virtual void setInstanceId(uint32_t id) = 0;
    [[nodiscard]] virtual auto getInstanceId() const -> uint32_t = 0;
    [[nodiscard]] virtual auto getAudioPorts(bool is_input) const
        -> const std::vector<AudioPortInfo>& = 0;
    [[nodiscard]] virtual auto getParameters() const
        -> const std::vector<std::unique_ptr<ParameterSlot>>& = 0;

    // --- State Check ---
    [[nodiscard]] virtual auto isActive() const -> bool = 0;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_PROCESSING_NODE_H
