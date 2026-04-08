#ifndef SYNTH_CANVAS_HOST_COMPOSITE_NODE_H
#define SYNTH_CANVAS_HOST_COMPOSITE_NODE_H

#include <memory>
#include <string>
#include <vector>

#include "graph_processor.h"
#include "graph_types.h"
#include "processing_node.h"
#include "readerwriterqueue.h"

namespace synth_canvas::host {

// Represents a group of interconnected ProcessingNodes, acting as a single node in the parent
// graph.
class CompositeNode final : public ProcessingNode {
   public:
    CompositeNode();
    ~CompositeNode() override;

    // --- ProcessingNode Lifecycle ---
    void activate(int32_t sample_rate, int32_t block_size) override;
    void deactivate() override;
    void setProcessingEnabled(bool enabled) override;

    // --- ProcessingNode Audio / Event Processing ---
    void setPorts(uint32_t num_inputs, clap_audio_buffer* inputs, uint32_t num_outputs,
                  clap_audio_buffer* outputs) override;
    void processBegin(int num_frames) override;
    void process() override;
    void processEnd(int num_frames) override;

    // --- Parameters & External Events ---
    void setParameterValue(clap_id param_id, double value) override;
    void setParameterValue(const std::string& param_id, double value) override;
    void applyModulation(clap_id param_id, double value, uint32_t sample_offset) override;
    [[nodiscard]] auto getParameterBaseValue(clap_id param_id) const -> double override;
    [[nodiscard]] auto getParameterModulationOffset(clap_id param_id) const -> double override;
    void queueEvent(const PluginEvent& event) override;
    auto popOutputEvent(PluginEvent& out_event) -> bool override;
    void pollMainThread() override;

    // --- Metadata Accessors ---
    void setInstanceId(uint32_t id) override { _instance_id = id; }
    [[nodiscard]] auto getInstanceId() const -> uint32_t override { return _instance_id; }
    [[nodiscard]] auto getAudioPorts(bool is_input) const
        -> const std::vector<AudioPortInfo>& override;
    [[nodiscard]] auto getParameters() const
        -> const std::vector<std::unique_ptr<ParameterSlot>>& override;

    // --- ProcessingNode State Check ---
    [[nodiscard]] auto isActive() const -> bool override;

    // --- Composite Specific Management ---
    auto load(const CompositeConfig& config) -> bool;
    void addInternalNode(uint32_t id, std::unique_ptr<ProcessingNode> node);
    void connectInternal(const PortConnection& conn);
    void setParameterMapping(const std::string& param_id, uint32_t internal_node_id,
                             uint32_t internal_param_id);
    void setInputProxy(uint32_t external_port, uint32_t internal_node, uint32_t internal_port);
    void setOutputProxy(uint32_t external_port, uint32_t internal_node, uint32_t internal_port);

    void pushInternalState();

   private:
    struct ParameterTarget {
        ProcessingNode* node;
        clap_id internal_id;
    };

    void updateInternalRenderState();
    void processInternalNode(ProcessingNode* node, int32_t num_frames);
    [[nodiscard]] auto getInternalParameterTarget(clap_id external_id) const -> ParameterTarget;

    GraphProcessor _internal_processor;
    AudioBufferManager _internal_buffers;

    // External Interface Cache (Metadata)
    std::vector<AudioPortInfo> _external_inputs;
    std::vector<AudioPortInfo> _external_outputs;
    std::vector<std::unique_ptr<ParameterSlot>> _external_params;

    // Current Buffers provided by parent
    clap_audio_buffer* _ext_inputs = nullptr;
    uint32_t _ext_input_count = 0;
    clap_audio_buffer* _ext_outputs = nullptr;
    uint32_t _ext_output_count = 0;

    uint32_t _instance_id = 0;
    int32_t _sample_rate = 0;
    int32_t _block_size = 0;
    bool _is_active = false;

    // Snapshot mechanism for internal graph state
    moodycamel::ReaderWriterQueue<std::unique_ptr<GraphProcessor::RenderState>> _pending_states;
    moodycamel::ReaderWriterQueue<std::unique_ptr<GraphProcessor::RenderState>> _released_states;
    std::unique_ptr<GraphProcessor::RenderState> _current_state;

    // Workspaces for internal node processing
    std::vector<clap_audio_buffer> _inputs_workspace;
    std::vector<clap_audio_buffer> _outputs_workspace;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_COMPOSITE_NODE_H
