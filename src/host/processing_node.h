#ifndef SYNTH_CANVAS_HOST_PROCESSING_NODE_H
#define SYNTH_CANVAS_HOST_PROCESSING_NODE_H

#include <clap/clap.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "graph_types.h"

namespace synth_canvas::host {

// Interface for anything that can process audio and events in the graph.
class ProcessingNode {
   public:
    virtual ~ProcessingNode() = default;

    // Lifecycle
    virtual void activate(int32_t sample_rate, int32_t block_size) = 0;
    virtual void deactivate() = 0;
    virtual void setProcessingEnabled(bool enabled) = 0;

    // Audio / Event Processing
    virtual void setPorts(uint32_t num_inputs, clap_audio_buffer* inputs, uint32_t num_outputs,
                          clap_audio_buffer* outputs) = 0;
    virtual void processBegin(int num_frames) = 0;
    virtual void process() = 0;
    virtual void processEnd(int num_frames) = 0;

    // Output Buffers
    // Returns a pointer to the pre-allocated output buffer for the given port.
    virtual auto getOutputBuffer(uint32_t port_idx) -> AudioBuffer* = 0;

    // Reserves and pre-allocates output buffers on the main thread.
    virtual void reserveOutputBuffers(uint32_t count) = 0;

    // Parameters & External Events
    virtual void setParameterValue(clap_id param_id, double value) = 0;
    virtual void setParameterValue(const std::string& param_id, double value) = 0;

    // Audio-thread safe modulation injection (additive)
    virtual void applyModulation(clap_id param_id, double value, uint32_t sample_offset) = 0;

    // Returns the baseline value (user-set) for a parameter
    [[nodiscard]] virtual auto getParameterBaseValue(clap_id param_id) const -> double = 0;

    // Returns the current modulation offset applied to a parameter
    [[nodiscard]] virtual auto getParameterModulationOffset(clap_id param_id) const -> double = 0;

    virtual void queueEvent(const PluginEvent& event) = 0;
    virtual auto popOutputEvent(PluginEvent& out_event) -> bool = 0;
    virtual void pollMainThread() = 0;

    // Metadata Accessors
    virtual void setInstanceId(uint32_t id) = 0;
    [[nodiscard]] virtual auto getInstanceId() const -> uint32_t = 0;
    [[nodiscard]] virtual auto getAudioPorts(bool is_input) const
        -> const std::vector<AudioPortInfo>& = 0;
    [[nodiscard]] virtual auto getParameters() const
        -> const std::vector<std::unique_ptr<ParameterSlot>>& = 0;

    // State Check
    [[nodiscard]] virtual auto isActive() const -> bool = 0;

    // Universal Event Callback (invoked during pollMainThread)
    std::function<void(uint32_t, const PluginEvent&)> on_event_occured;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_PROCESSING_NODE_H
