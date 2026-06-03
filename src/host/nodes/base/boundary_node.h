#ifndef SYNTH_CANVAS_HOST_BOUNDARY_NODE_H
#define SYNTH_CANVAS_HOST_BOUNDARY_NODE_H

#include <memory>
#include <string>
#include <vector>

#include "host/graph/graph_types.h"
#include "host/nodes/base/processing_node.h"
#include "readerwriterqueue.h"
#include "utils/constants.h"

namespace synth_canvas::host {

class BoundaryNode : public ProcessingNode {
   public:
    enum class Type { kInputProxy, kOutputProxy };

    explicit BoundaryNode(Type type);
    ~BoundaryNode() override = default;

    // ProcessingNode Interface
    void activate(int32_t sample_rate, int32_t block_size) override;
    void deactivate() override;
    void setProcessingEnabled(bool enabled) override {}
    void setTransport(const TransportState* transport) override {}

    void setPorts(uint32_t num_inputs, clap_audio_buffer* inputs, uint32_t num_outputs,
                  clap_audio_buffer* outputs) override;

    void processBegin(int num_frames) override;
    void process() override;
    void processEvents(int num_frames) override {}
    void processEnd(int num_frames) override {}

    auto getOutputBuffer(uint32_t port_idx) -> AudioBuffer* override;
    void reserveOutputBuffers(uint32_t count) override;

    void setParameterValue(clap_id param_id, double value) override {}
    void setParameterValue(const std::string& param_id, double value) override {}
    void applyModulation(clap_id param_id, double value, uint32_t sample_offset) override {}

    auto saveState(std::vector<uint8_t>& data) -> bool override;
    auto loadState(const std::vector<uint8_t>& data) -> bool override;

    [[nodiscard]] auto getParameterBaseValue(clap_id param_id) const -> double override {
        return 0.0;
    }
    [[nodiscard]] auto getParameterCurrentValue(clap_id param_id) const -> double override {
        return 0.0;
    }
    [[nodiscard]] auto getParameterModulationOffset(clap_id param_id) const -> double override {
        return 0.0;
    }

    void queueEvent(const PluginEvent& event) override;
    auto popOutputEvent(uint32_t port_index, PluginEvent& out_event) -> bool override;
    void pollMainThread() override {}

    void setInstanceId(uint32_t id) override { _instance_id = id; }
    [[nodiscard]] auto getInstanceId() const -> uint32_t override { return _instance_id; }
    [[nodiscard]] auto getNodeType() const -> std::string override { return "boundary"; }
    [[nodiscard]] auto getCreationInfo() const -> std::string override;

    [[nodiscard]] auto getAudioPorts(bool is_input) const
        -> const std::vector<AudioPortInfo>& override;
    [[nodiscard]] auto getParameters() const
        -> const std::vector<std::unique_ptr<ParameterSlot>>& override;
    [[nodiscard]] auto getParameterSlot(clap_id param_id) const -> const ParameterSlot* override;
    [[nodiscard]] auto getParameterText(clap_id param_id, double value) const
        -> std::string override;

    [[nodiscard]] auto isActive() const -> bool override { return _is_active; }

    // Boundary Control
    void setAudioPorts(bool is_input, const std::vector<AudioPortInfo>& ports);
    void setExternalBuffers(clap_audio_buffer* buffers, uint32_t count);

   private:
    Type _type;
    uint32_t _instance_id = 0;
    bool _is_active = false;
    int32_t _sample_rate = constants::kDefaultSampleRate;
    int32_t _block_size = constants::kDefaultFramesPerBlock;
    int32_t _current_num_frames = 0;

    std::vector<AudioPortInfo> _input_port_info;
    std::vector<AudioPortInfo> _output_port_info;
    std::vector<std::unique_ptr<AudioBuffer>> _output_buffers;

    clap_audio_buffer* _current_inputs = nullptr;
    uint32_t _current_num_inputs = 0;
    clap_audio_buffer* _external_buffers = nullptr;
    uint32_t _external_count = 0;

    moodycamel::ReaderWriterQueue<PluginEvent> _event_queue;

    std::vector<std::unique_ptr<ParameterSlot>> _empty_params;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_BOUNDARY_NODE_H
