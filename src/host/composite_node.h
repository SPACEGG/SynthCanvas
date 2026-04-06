#ifndef SYNTH_CANVAS_HOST_COMPOSITE_NODE_H
#define SYNTH_CANVAS_HOST_COMPOSITE_NODE_H

#include <memory>
#include <string>
#include <vector>

#include "audio_buffer_manager.h"
#include "graph_processor.h"
#include "processing_node.h"
#include "readerwriterqueue.h"

namespace synth_canvas::host {

// A node that encapsulates an internal graph of other processing nodes.
// It acts as a single node to the external world while managing local routing.
class CompositeNode final : public ProcessingNode {
   public:
    CompositeNode();
    ~CompositeNode() override;

    // --- ProcessingNode Interface ---
    void activate(int32_t sample_rate, int32_t block_size) override;
    void deactivate() override;
    void setProcessingEnabled(bool enabled) override;

    void setPorts(uint32_t num_inputs, clap_audio_buffer* inputs, uint32_t num_outputs,
                  clap_audio_buffer* outputs) override;
    void processBegin(int num_frames) override;
    void process() override;
    void processEnd(int num_frames) override;

    void setParameterValue(clap_id param_id, double value) override;
    void queueEvent(const PluginEvent& event) override;
    void pollMainThread() override;

    void setInstanceId(uint32_t id) override { _instance_id = id; }
    [[nodiscard]] auto getInstanceId() const -> uint32_t override { return _instance_id; }
    [[nodiscard]] auto getAudioPorts(bool is_input) const
        -> const std::vector<AudioPortInfo>& override;
    [[nodiscard]] auto getParameters() const
        -> const std::vector<std::unique_ptr<ParameterSlot>>& override;
    [[nodiscard]] auto isActive() const -> bool override;

    // --- Composite Management (Main Thread) ---
    auto load(const CompositeConfig& config) -> bool;
    void addInternalNode(uint32_t id, std::unique_ptr<ProcessingNode> node);
    void connectInternal(const PortConnection& conn);
    void setParameterMapping(const std::string& param_id, uint32_t internal_node_id,
                             uint32_t internal_param_id);
    void setInputProxy(uint32_t external_port, uint32_t internal_node, uint32_t internal_port);
    void setOutputProxy(uint32_t external_port, uint32_t internal_node, uint32_t internal_port);

    // High-level parameter control (called via string IDs)
    void setCompositeParameter(const std::string& param_id, double value);

    // Commits changes to the internal graph state
    void pushInternalState();

   private:
    void updateInternalRenderState();
    void processInternalNode(ProcessingNode* node, int32_t num_frames);

    GraphProcessor _internal_processor;
    AudioBufferManager _internal_buffers;

    uint32_t _instance_id = 0;
    bool _is_active = false;
    int32_t _sample_rate = 0;
    int32_t _block_size = 0;

    // External metadata
    std::vector<AudioPortInfo> _external_inputs;
    std::vector<AudioPortInfo> _external_outputs;
    std::vector<std::unique_ptr<ParameterSlot>> _external_params;

    // Pointers to the external buffers provided via setPorts()
    clap_audio_buffer* _ext_inputs = nullptr;
    uint32_t _ext_input_count = 0;
    clap_audio_buffer* _ext_outputs = nullptr;
    uint32_t _ext_output_count = 0;

    // State synchronization
    std::unique_ptr<GraphProcessor::RenderState> _current_state;
    moodycamel::ReaderWriterQueue<std::unique_ptr<GraphProcessor::RenderState>> _pending_states;
    moodycamel::ReaderWriterQueue<std::unique_ptr<GraphProcessor::RenderState>> _released_states;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_COMPOSITE_NODE_H
