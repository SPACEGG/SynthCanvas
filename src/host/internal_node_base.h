#ifndef SYNTH_CANVAS_HOST_INTERNAL_NODE_BASE_H
#define SYNTH_CANVAS_HOST_INTERNAL_NODE_BASE_H

#include <memory>
#include <string>
#include <vector>

#include "processing_node.h"
#include "readerwriterqueue.h"

namespace synth_canvas::host {

/**
 * Base class for internal C++ DSP modules (LFO, Envelope, etc.).
 * Handles boilerplate for parameters, buffers, and transport state.
 */
class InternalNodeBase : public ProcessingNode {
   public:
    InternalNodeBase();
    ~InternalNodeBase() override = default;

    // Lifecycle
    void activate(int32_t sample_rate, int32_t block_size) override;
    void deactivate() override;
    void setProcessingEnabled(bool enabled) override { _processing_enabled = enabled; }
    void setTransport(const TransportState* transport) override { _transport = transport; }

    // Audio / Event Processing
    void setPorts(uint32_t num_inputs, clap_audio_buffer* inputs, uint32_t num_outputs,
                  clap_audio_buffer* outputs) override;
    void processBegin(int num_frames) override;
    void processEnd(int num_frames) override;

    // Output Buffers
    auto getOutputBuffer(uint32_t port_idx) -> AudioBuffer* override;
    void reserveOutputBuffers(uint32_t count) override;

    // Parameters
    void setParameterValue(clap_id param_id, double value) override;
    void setParameterValue(const std::string& param_id, double value) override;
    void applyModulation(clap_id param_id, double value, uint32_t sample_offset) override;

    auto saveState(std::vector<uint8_t>& data) -> bool override;
    auto loadState(const std::vector<uint8_t>& data) -> bool override;

    [[nodiscard]] auto getParameterBaseValue(clap_id param_id) const -> double override;
    [[nodiscard]] auto getParameterCurrentValue(clap_id param_id) const -> double override;
    [[nodiscard]] auto getParameterModulationOffset(clap_id param_id) const -> double override;

    void queueEvent(const PluginEvent& event) override;
    auto popOutputEvent(uint32_t port_index, PluginEvent& out_event) -> bool override;
    void pollMainThread() override {}

    // Metadata
    void setInstanceId(uint32_t id) override { _instance_id = id; }
    [[nodiscard]] auto getInstanceId() const -> uint32_t override { return _instance_id; }
    [[nodiscard]] auto getAudioPorts(bool is_input) const
        -> const std::vector<AudioPortInfo>& override;
    [[nodiscard]] auto getParameters() const
        -> const std::vector<std::unique_ptr<ParameterSlot>>& override {
        return _parameters;
    }
    [[nodiscard]] auto getParameterSlot(clap_id param_id) const -> const ParameterSlot* override;
    [[nodiscard]] auto getParameterText(clap_id param_id, double value) const
        -> std::string override;

    [[nodiscard]] auto isActive() const -> bool override { return _is_active; }

   protected:
    // Protected helpers for derived classes
    auto getParameterSlot(clap_id param_id) -> ParameterSlot*;
    void addParameter(clap_id id, const std::string& name, const std::string& module,
                      double min_val, double max_val, double def_val, uint32_t flags = 0);
    void addAudioPort(const std::string& name, bool is_input, uint32_t channel_count = 2,
                      bool is_mod = false, clap_id target_param_id = -1);
    void addEventPort(const std::string& name, bool is_input);

    // Derived classes must implement these
    virtual void onSampleRateChanged(int32_t sample_rate) {}

    AudioBuffer _output_buffer;
    const TransportState* _transport = nullptr;
    std::vector<std::unique_ptr<ParameterSlot>> _parameters;
    std::vector<AudioPortInfo> _input_ports;
    std::vector<AudioPortInfo> _output_ports;
    std::vector<std::unique_ptr<moodycamel::ReaderWriterQueue<PluginEvent>>> _output_event_queues;

    uint32_t _instance_id = 0;
    bool _is_active = false;
    bool _processing_enabled = true;
    int32_t _current_sample_rate = 44100;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_INTERNAL_NODE_BASE_H
