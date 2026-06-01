#ifndef SYNTH_CANVAS_HOST_PROCESSING_NODE_H
#define SYNTH_CANVAS_HOST_PROCESSING_NODE_H

#include <clap/clap.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "host/graph/graph_types.h"

namespace synth_canvas::host {

// Interface for anything that can process audio and events in the graph.
class ProcessingNode {
   public:
    virtual ~ProcessingNode() = default;

    // Lifecycle
    virtual void activate(int32_t sample_rate, int32_t block_size) = 0;
    virtual void deactivate() = 0;
    virtual void setProcessingEnabled(bool enabled) = 0;
    virtual void setTransport(const TransportState* transport) = 0;

    // Audio / Event Processing
    virtual void setPorts(uint32_t num_inputs, clap_audio_buffer* inputs, uint32_t num_outputs,
                          clap_audio_buffer* outputs) = 0;
    virtual void processBegin(int num_frames) = 0;
    virtual void process() = 0;
    virtual void processEvents(int num_frames) = 0;
    virtual void processEnd(int num_frames) = 0;

    // Output Buffers
    // Returns a pointer to the pre-allocated output buffer for the given port.
    virtual auto getOutputBuffer(uint32_t port_idx) -> AudioBuffer* = 0;

    // Reserves and pre-allocates output buffers on the main thread.
    virtual void reserveOutputBuffers(uint32_t count) = 0;

    // Parameters & External Events
    virtual void setParameterValue(clap_id param_id, double value) = 0;
    virtual void setParameterValue(const std::string& param_id, double value) = 0;

    // State Management
    virtual auto saveState(std::vector<uint8_t>& data) -> bool = 0;
    virtual auto loadState(const std::vector<uint8_t>& data) -> bool = 0;

    // Audio-thread safe modulation injection (additive)
    virtual void applyModulation(clap_id param_id, double value, uint32_t sample_offset) = 0;

    // Returns the baseline value (user-set) for a parameter
    [[nodiscard]] virtual auto getParameterBaseValue(clap_id param_id) const -> double = 0;

    // Returns the current value (base + modulation) for a parameter
    [[nodiscard]] virtual auto getParameterCurrentValue(clap_id param_id) const -> double = 0;

    // Returns the current modulation offset applied to a parameter
    [[nodiscard]] virtual auto getParameterModulationOffset(clap_id param_id) const -> double = 0;

    virtual void queueEvent(const PluginEvent& event) = 0;
    virtual auto popOutputEvent(uint32_t port_index, PluginEvent& out_event) -> bool = 0;
    virtual void pollMainThread() = 0;

    // Metadata Accessors
    virtual void setInstanceId(uint32_t id) = 0;
    [[nodiscard]] virtual auto getInstanceId() const -> uint32_t = 0;
    [[nodiscard]] virtual auto getNodeType() const -> std::string = 0;
    [[nodiscard]] virtual auto getCreationInfo() const -> std::string = 0;
    [[nodiscard]] virtual auto getAudioPorts(bool is_input) const
        -> const std::vector<AudioPortInfo>& = 0;
    [[nodiscard]] virtual auto getParameters() const
        -> const std::vector<std::unique_ptr<ParameterSlot>>& = 0;
    [[nodiscard]] virtual auto getParameterSlot(clap_id param_id) const -> const ParameterSlot* = 0;
    [[nodiscard]] virtual auto getParameterText(clap_id param_id, double value) const
        -> std::string = 0;
    [[nodiscard]] virtual auto getParameterText(const std::string& param_id, double value) const
        -> std::string {
        return "";
    }

    // State Check
    [[nodiscard]] virtual auto isActive() const -> bool = 0;

    // Universal Event Callback (invoked during pollMainThread)
    std::function<void(uint32_t, const PluginEvent&)> on_event_occured;

    // Direct Parameter Rescan Callback (invoked on the main thread when plugin metadata changes)
    std::function<void(uint32_t)> on_params_rescan;
    // Port Count Change Callback (invoked on the main thread when a node's active port count
    // changes)
    std::function<void(uint32_t)> on_ports_changed;

    // Mini curve display interface
    [[nodiscard]] virtual auto supportsMiniCurve() const -> bool { return false; }
    [[nodiscard]] virtual auto getMiniCurveCount() const -> uint32_t { return 0; }
    virtual auto getMiniCurveAxisNames(uint32_t curve_index, std::string& out_x,
                                       std::string& out_y) const -> bool {
        return false;
    }
    virtual auto renderMiniCurve(uint32_t curve_index, std::vector<float>& out_values,
                                 uint32_t resolution) -> uint32_t {
        return 0;
    }
    virtual void setMiniCurveObserved(bool is_observed) {}

    // Curve change callback
    std::function<void(uint32_t)> on_curve_changed;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_PROCESSING_NODE_H
