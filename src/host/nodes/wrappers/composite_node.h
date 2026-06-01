#ifndef SYNTH_CANVAS_HOST_COMPOSITE_NODE_H
#define SYNTH_CANVAS_HOST_COMPOSITE_NODE_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "host/graph/graph_processor.h"
#include "host/graph/graph_renderer.h"
#include "host/graph/graph_types.h"
#include "host/nodes/base/boundary_node.h"
#include "host/nodes/base/processing_node.h"
#include "readerwriterqueue.h"

namespace synth_canvas::host {

class CompositeNode final : public ProcessingNode {
   public:
    CompositeNode();
    ~CompositeNode() override;

    // ProcessingNode Lifecycle
    void activate(int32_t sample_rate, int32_t block_size) override;
    void deactivate() override;
    void setProcessingEnabled(bool enabled) override;
    void setTransport(const TransportState* transport) override;

    // Audio / Event Processing

    void setPorts(uint32_t num_inputs, clap_audio_buffer* inputs, uint32_t num_outputs,
                  clap_audio_buffer* outputs) override;
    void processBegin(int num_frames) override;
    void process() override;
    void processEvents(int num_frames) override;
    void processEnd(int num_frames) override;

    // Output Buffers
    auto getOutputBuffer(uint32_t port_idx) -> AudioBuffer* override;
    void reserveOutputBuffers(uint32_t count) override;

    // Parameters & External Events
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
    void pollMainThread() override;

    // Metadata Accessors
    void setInstanceId(uint32_t id) override { _instance_id = id; }
    [[nodiscard]] auto getInstanceId() const -> uint32_t override { return _instance_id; }
    [[nodiscard]] auto getNodeType() const -> std::string override { return "composite"; }
    [[nodiscard]] auto getCreationInfo() const -> std::string override;
    [[nodiscard]] auto getAudioPorts(bool is_input) const
        -> const std::vector<AudioPortInfo>& override;
    [[nodiscard]] auto getParameters() const
        -> const std::vector<std::unique_ptr<ParameterSlot>>& override;
    [[nodiscard]] auto getParameterSlot(clap_id param_id) const -> const ParameterSlot* override;
    [[nodiscard]] auto getParameterText(clap_id param_id, double value) const
        -> std::string override;
    [[nodiscard]] auto getParameterText(const std::string& param_id, double value) const
        -> std::string override;

    // --- ProcessingNode State Check ---

    [[nodiscard]] auto supportsMiniCurve() const -> bool override;
    [[nodiscard]] auto getMiniCurveCount() const -> uint32_t override;
    auto getMiniCurveAxisNames(uint32_t curve_index, std::string& out_x, std::string& out_y) const
        -> bool override;
    auto renderMiniCurve(uint32_t curve_index, std::vector<float>& out_values, uint32_t resolution)
        -> uint32_t override;
    void setMiniCurveObserved(bool is_observed) override;

    [[nodiscard]] auto isActive() const -> bool override;

    // Composite Specific Management
    auto load(const CompositeConfig& config) -> bool;
    void addInternalNode(uint32_t id, std::unique_ptr<ProcessingNode> node);
    void connectInternal(const PortConnection& conn);
    void setParameterMapping(const std::string& param_id, uint32_t internal_node_id,
                             uint32_t internal_param_id);

    void pushInternalState();

   private:
    struct ParameterTarget {
        ProcessingNode* node;
        clap_id internal_id;
    };

    void updateInternalRenderState();
    void handleInternalEvent(uint32_t internal_id, const PluginEvent& ev);
    void refreshParameterMetadata();
    void syncExternalParameterValues();
    [[nodiscard]] auto getInternalParameterTarget(clap_id external_id) const -> ParameterTarget;

    // Helper functions for load()
    void setupBoundaryNodes();
    auto loadInternalPlugins(const CompositeConfig& config,
                             std::map<std::string, uint32_t>& out_alias_to_id) -> bool;
    void setupInternalRoutings(const CompositeConfig& config,
                               const std::map<std::string, uint32_t>& alias_to_id);
    void setupInputProxies(const CompositeConfig& config,
                           const std::map<std::string, uint32_t>& alias_to_id);
    void setupOutputProxies(const CompositeConfig& config,
                            const std::map<std::string, uint32_t>& alias_to_id);
    void setupParameterMappings(const CompositeConfig& config,
                                const std::map<std::string, uint32_t>& alias_to_id);

    [[nodiscard]] auto getMiniCurveDelegate() const -> ProcessingNode*;

    GraphProcessor _internal_processor;
    AudioBufferManager _internal_buffers;
    GraphRenderer _renderer;

    // External Interface Cache (Metadata)
    std::vector<AudioPortInfo> _external_inputs;
    std::vector<AudioPortInfo> _external_outputs;

    std::vector<AudioBuffer> _output_buffers;

    std::vector<std::unique_ptr<ParameterSlot>> _external_params;
    std::unordered_map<std::string, uint32_t> _param_id_to_external_index;

    // Thread-safe target maps for metadata access (Main Thread)
    std::unordered_map<clap_id, ParameterTarget> _external_id_to_target;
    std::unordered_map<std::string, ParameterTarget> _param_id_to_target;

    // Current Buffers provided by parent
    clap_audio_buffer* _ext_inputs = nullptr;
    uint32_t _ext_input_count = 0;
    clap_audio_buffer* _ext_outputs = nullptr;
    uint32_t _ext_output_count = 0;

    uint32_t _instance_id = 0;
    const TransportState* _transport = nullptr;
    int32_t _sample_rate = 0;
    int32_t _block_size = 0;
    int32_t _current_num_frames = 0;
    bool _is_active = false;
    uint32_t _display_node_id = 0;

    // Boundary Nodes
    std::unique_ptr<BoundaryNode> _input_proxy_node_owned;
    BoundaryNode* _input_proxy_node = nullptr;
    std::unique_ptr<BoundaryNode> _output_proxy_node_owned;
    BoundaryNode* _output_proxy_node = nullptr;

    CompositeConfig _config;

    // Snapshot mechanism for internal graph state
    moodycamel::ReaderWriterQueue<std::unique_ptr<GraphProcessor::RenderState>> _pending_states;
    moodycamel::ReaderWriterQueue<std::unique_ptr<GraphProcessor::RenderState>> _released_states;
    std::unique_ptr<GraphProcessor::RenderState> _current_state;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_COMPOSITE_NODE_H
