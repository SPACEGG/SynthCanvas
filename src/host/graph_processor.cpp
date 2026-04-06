#include "graph_processor.h"

#include <algorithm>
#include <queue>

namespace synth_canvas::host {

GraphProcessor::GraphProcessor() = default;
GraphProcessor::~GraphProcessor() = default;

void GraphProcessor::addNode(uint32_t id, std::unique_ptr<ProcessingNode> node) {
    _nodes[id] = std::move(node);
    topologicalSort();
}

auto GraphProcessor::removeNode(uint32_t id) -> std::unique_ptr<ProcessingNode> {
    auto it = _nodes.find(id);
    if (it != _nodes.end()) {
        auto node = std::move(it->second);
        _nodes.erase(it);

        // Cleanly remove all connections associated with this node
        std::erase_if(_connections, [id](const PortConnection& c) {
            return c.from_node == id || c.to_node == id;
        });

        // Clean up mapping tables for containers
        std::erase_if(_input_proxies,
                      [id](const auto& item) { return item.second.internal_node_id == id; });
        std::erase_if(_output_proxies,
                      [id](const auto& item) { return item.second.internal_node_id == id; });
        std::erase_if(_parameter_mappings,
                      [id](const auto& item) { return item.second.target_node_id == id; });

        topologicalSort();
        return node;
    }
    return nullptr;
}

auto GraphProcessor::getNode(uint32_t id) const -> ProcessingNode* {
    auto it = _nodes.find(id);
    return (it != _nodes.end()) ? it->second.get() : nullptr;
}

void GraphProcessor::connect(const PortConnection& conn) {
    _connections.push_back(conn);
    topologicalSort();
}

void GraphProcessor::disconnect(const PortConnection& conn) {
    // Concise conditional removal from vector
    std::erase_if(_connections, [&conn](const PortConnection& c) {
        return c.from_node == conn.from_node && c.from_port == conn.from_port &&
               c.to_node == conn.to_node && c.to_port == conn.to_port && c.type == conn.type;
    });
    topologicalSort();
}

void GraphProcessor::setParameterMapping(const std::string& param_id, uint32_t node_id,
                                         uint32_t clap_param_id) {
    _parameter_mappings[param_id] = {.target_node_id = node_id, .target_param_id = clap_param_id};
}

void GraphProcessor::setInputProxy(uint32_t external_port, uint32_t internal_node,
                                   uint32_t internal_port) {
    _input_proxies[external_port] = {.internal_node_id = internal_node,
                                     .internal_port_index = internal_port};
}

void GraphProcessor::setOutputProxy(uint32_t external_port, uint32_t internal_node,
                                    uint32_t internal_port) {
    _output_proxies[external_port] = {.internal_node_id = internal_node,
                                      .internal_port_index = internal_port};
}

void GraphProcessor::sort() { topologicalSort(); }

void GraphProcessor::topologicalSort() {
    _process_order.clear();
    if (_nodes.empty()) return;

    std::unordered_map<uint32_t, int> in_degree;
    std::unordered_map<uint32_t, std::vector<uint32_t>> adj;

    // Initialize in-degree for all existing nodes
    for (const auto& [id, node] : _nodes) {
        in_degree[id] = 0;
    }

    // Build adjacency list and calculate in-degrees
    for (const auto& conn : _connections) {
        if (_nodes.count(conn.from_node) && _nodes.count(conn.to_node)) {
            adj[conn.from_node].push_back(conn.to_node);
            in_degree[conn.to_node]++;
        }
    }

    // Kahn's Algorithm: Start with nodes having 0 in-degree
    std::queue<uint32_t> q;
    for (const auto& [id, degree] : in_degree) {
        if (degree == 0) {
            q.push(id);
        }
    }

    while (!q.empty()) {
        uint32_t u = q.front();
        q.pop();
        _process_order.push_back(u);

        if (adj.count(u)) {
            for (uint32_t v : adj[u]) {
                if (--in_degree[v] == 0) q.push(v);
            }
        }
    }

    // Safety fallback: Check for missing nodes (probably due to cycle connection)
    if (_process_order.size() < _nodes.size()) {
        for (const auto& [id, node] : _nodes) {
            if (std::ranges::find(_process_order, id) == _process_order.end()) {
                _process_order.push_back(id);
            }
        }
    }
}

auto GraphProcessor::createRenderState(uint32_t master_node_id) -> std::unique_ptr<RenderState> {
    auto state = std::make_unique<RenderState>();

    // Map sorted IDs to actual node pointers
    for (uint32_t id : _process_order) {
        if (auto* node = getNode(id)) {
            state->sorted_nodes.push_back(node);
        }
    }

    state->connections = _connections;
    state->parameter_mappings = _parameter_mappings;
    state->input_proxies = _input_proxies;
    state->output_proxies = _output_proxies;

    // Build lookup tables for audio processing
    for (const auto& conn : _connections) {
        if (conn.type == ConnectionType::kAudio) {
            if (conn.to_node == master_node_id) {
                state->master_output_sources.push_back({conn.from_node, conn.from_port});
            } else {
                state->input_audio_sources[conn.to_node][conn.to_port].push_back(
                    {conn.from_node, conn.from_port});
            }
        } else if (conn.type == ConnectionType::kEvent) {
            if (auto* target = getNode(conn.to_node)) {
                state->output_event_targets[conn.from_node].push_back(target);
            }
        } else if (conn.type == ConnectionType::kModulation) {
            state->input_modulations[conn.to_node].push_back(
                {static_cast<clap_id>(conn.to_port), conn.from_node, conn.from_port});
        }
    }

    return state;
}

}  // namespace synth_canvas::host
