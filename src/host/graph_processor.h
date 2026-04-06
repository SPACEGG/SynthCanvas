#ifndef SYNTH_CANVAS_HOST_GRAPH_PROCESSOR_H
#define SYNTH_CANVAS_HOST_GRAPH_PROCESSOR_H

#include <clap/clap.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio_buffer_manager.h"
#include "graph_types.h"
#include "processing_node.h"

namespace synth_canvas::host {

class GraphProcessor {
   public:
    // Snapshot of the graph state for the audio thread
    struct RenderState {
        struct ModulationSource {
            clap_id target_param_id;
            uint32_t source_node_id;
            uint32_t source_port_index;
        };

        // Mapping for composite parameters: "string_id" -> {node, param}
        struct ParameterMapping {
            uint32_t target_node_id;
            uint32_t target_param_id;
        };

        // Mapping for proxy ports: external_port -> {internal_node, internal_port}
        struct PortProxyMapping {
            uint32_t internal_node_id;
            uint32_t internal_port_index;
        };

        std::unordered_map<
            uint32_t, std::unordered_map<uint32_t, std::vector<AudioBufferManager::PortSource>>>
            input_audio_sources;
        std::unordered_map<uint32_t, std::vector<ModulationSource>> input_modulations;
        std::unordered_map<uint32_t, std::vector<ProcessingNode*>> output_event_targets;
        std::vector<ProcessingNode*> sorted_nodes;
        std::vector<AudioBufferManager::PortSource> master_output_sources;
        std::vector<PortConnection> connections;

        // New mapping tables for composite functionality
        std::unordered_map<std::string, ParameterMapping> parameter_mappings;
        std::unordered_map<uint32_t, PortProxyMapping> input_proxies;
        std::unordered_map<uint32_t, PortProxyMapping> output_proxies;
    };

    GraphProcessor();
    ~GraphProcessor();

    // Node management
    void addNode(uint32_t id, std::unique_ptr<ProcessingNode> node);
    auto removeNode(uint32_t id) -> std::unique_ptr<ProcessingNode>;
    [[nodiscard]] auto getNode(uint32_t id) const -> ProcessingNode*;

    // Connection management
    void connect(const PortConnection& conn);
    void disconnect(const PortConnection& conn);

    // Composite Mapping (Main Thread side)
    void setParameterMapping(const std::string& param_id, uint32_t node_id, uint32_t clap_param_id);
    void setInputProxy(uint32_t external_port, uint32_t internal_node, uint32_t internal_port);
    void setOutputProxy(uint32_t external_port, uint32_t internal_node, uint32_t internal_port);

    // Order and State
    void sort();
    auto createRenderState(uint32_t master_node_id) -> std::unique_ptr<RenderState>;

    [[nodiscard]] auto getProcessOrder() const -> const std::vector<uint32_t>& {
        return _process_order;
    }
    [[nodiscard]] auto getConnections() const -> const std::vector<PortConnection>& {
        return _connections;
    }
    [[nodiscard]] auto hasNode(uint32_t id) const -> bool { return _nodes.count(id) > 0; }

   private:
    std::unordered_map<uint32_t, std::unique_ptr<ProcessingNode>> _nodes;
    std::vector<PortConnection> _connections;
    std::vector<uint32_t> _process_order;

    // Temporary storage for mappings before next RenderState creation
    std::unordered_map<std::string, RenderState::ParameterMapping> _parameter_mappings;
    std::unordered_map<uint32_t, RenderState::PortProxyMapping> _input_proxies;
    std::unordered_map<uint32_t, RenderState::PortProxyMapping> _output_proxies;

    void topologicalSort();
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_GRAPH_PROCESSOR_H
