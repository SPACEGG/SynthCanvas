#ifndef SYNTH_CANVAS_HOST_GRAPH_PROCESSOR_H
#define SYNTH_CANVAS_HOST_GRAPH_PROCESSOR_H

#include <clap/clap.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "graph_types.h"
#include "processing_node.h"

namespace synth_canvas::host {

class GraphProcessor {
   public:
    // Snapshot of the graph state for the audio thread
    struct RenderState {
        struct AudioSource {
            uint32_t node_index;
            uint32_t port_index;
        };

        struct ModulationSource {
            clap_id target_param_id;
            uint32_t source_node_index;
            uint32_t source_port_index;
        };

        struct ParameterMapping {
            uint32_t target_node_index;
            uint32_t target_param_id;
        };

        struct PortProxyMapping {
            uint32_t node_index;
            uint32_t port_index;
        };

        struct EventTarget {
            enum class Type { kNode, kExternalOutput };
            Type type;
            union {
                ProcessingNode* node;
                uint32_t port_index;
            } destination;
        };

        // Global Transport State
        TransportState transport;

        // Indexed by target node_index
        std::vector<std::unordered_map<uint32_t, std::vector<AudioSource>>> input_audio_sources;
        std::vector<std::vector<ModulationSource>> input_modulations;
        std::vector<std::vector<EventTarget>> output_event_targets;

        std::vector<ProcessingNode*> sorted_nodes;
        std::vector<AudioSource> master_output_sources;
        std::vector<PortConnection> connections;

        // CompositeNode states
        std::unordered_map<std::string, ParameterMapping> parameter_mappings;
        std::vector<ParameterMapping> direct_parameter_mappings;
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
    void setDirectParameterMapping(uint32_t index, uint32_t node_id, uint32_t clap_param_id);

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
    struct ParameterMappingRequest {
        uint32_t target_node_id;
        uint32_t target_param_id;
    };

    struct PortProxyRequest {
        uint32_t node_id;
        uint32_t port_index;
    };

    std::unordered_map<uint32_t, std::unique_ptr<ProcessingNode>> _nodes;
    std::vector<PortConnection> _connections;
    std::vector<uint32_t> _process_order;

    // Temporary storage for mappings before next RenderState creation
    std::unordered_map<std::string, ParameterMappingRequest> _parameter_mappings;
    std::vector<ParameterMappingRequest> _direct_parameter_mappings;

    void topologicalSort();
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_GRAPH_PROCESSOR_H
