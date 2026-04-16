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

        std::erase_if(_connections, [id](const PortConnection& c) {
            return c.from_node == id || c.to_node == id;
        });

        std::erase_if(_input_proxies, [id](const auto& item) {
            for (const auto& mapping : item.second) {
                if (mapping.node_id == id) return true;
            }
            return false;
        });
        std::erase_if(_output_proxies, [id](const auto& item) {
            for (const auto& mapping : item.second) {
                if (mapping.node_id == id) return true;
            }
            return false;
        });
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
    _input_proxies[external_port].push_back(
        {.node_id = internal_node, .port_index = internal_port});
}

void GraphProcessor::setOutputProxy(uint32_t external_port, uint32_t internal_node,
                                    uint32_t internal_port) {
    _output_proxies[external_port].push_back(
        {.node_id = internal_node, .port_index = internal_port});
}

void GraphProcessor::setDirectParameterMapping(uint32_t index, uint32_t node_id,
                                               uint32_t clap_param_id) {
    if (index >= _direct_parameter_mappings.size()) {
        _direct_parameter_mappings.resize(index + 1);
    }
    _direct_parameter_mappings[index] = {.target_node_id = node_id,
                                         .target_param_id = clap_param_id};
}

void GraphProcessor::sort() { topologicalSort(); }

void GraphProcessor::topologicalSort() {
    _process_order.clear();
    if (_nodes.empty()) return;

    std::unordered_map<uint32_t, int> in_degree;
    std::unordered_map<uint32_t, std::vector<uint32_t>> adj;

    for (const auto& [id, node] : _nodes) {
        in_degree[id] = 0;
    }

    for (const auto& conn : _connections) {
        if (_nodes.count(conn.from_node) && _nodes.count(conn.to_node)) {
            adj[conn.from_node].push_back(conn.to_node);
            in_degree[conn.to_node]++;
        }
    }

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
    std::unordered_map<uint32_t, uint32_t> id_to_index;

    // Build sorted_nodes and id_to_index mapping
    for (uint32_t id : _process_order) {
        if (auto* node = getNode(id)) {
            auto index = static_cast<uint32_t>(state->sorted_nodes.size());
            state->sorted_nodes.push_back(node);
            id_to_index[id] = index;
        }
    }

    size_t num_nodes = state->sorted_nodes.size();
    state->input_audio_sources.resize(num_nodes);
    state->input_modulations.resize(num_nodes);
    state->output_event_targets.resize(num_nodes);

    state->connections = _connections;

    // Convert Parameter Mappings (ID to Index)
    for (const auto& [name, req] : _parameter_mappings) {
        if (id_to_index.count(req.target_node_id)) {
            state->parameter_mappings[name] = {.target_node_index = id_to_index[req.target_node_id],
                                               .target_param_id = req.target_param_id};
        }
    }

    for (const auto& req : _direct_parameter_mappings) {
        if (id_to_index.count(req.target_node_id)) {
            state->direct_parameter_mappings.push_back(
                {.target_node_index = id_to_index[req.target_node_id],
                 .target_param_id = req.target_param_id});
        }
    }

    // Convert ID-based connections to Index-based
    for (const auto& conn : _connections) {
        if (!id_to_index.count(conn.from_node)) continue;

        if (conn.type == ConnectionType::kAudio) {
            if (conn.to_node == master_node_id) {
                state->master_output_sources.push_back(
                    {id_to_index[conn.from_node], conn.from_port});
            } else if (id_to_index.count(conn.to_node)) {
                uint32_t to_idx = id_to_index[conn.to_node];
                state->input_audio_sources[to_idx][conn.to_port].push_back(
                    {id_to_index[conn.from_node], conn.from_port});
            }
        } else if (conn.type == ConnectionType::kEvent) {
            if (id_to_index.count(conn.to_node)) {
                RenderState::EventTarget target;
                target.type = RenderState::EventTarget::Type::kNode;
                target.destination.node = state->sorted_nodes[id_to_index[conn.to_node]];
                state->output_event_targets[id_to_index[conn.from_node]].push_back(target);
            }
        } else if (conn.type == ConnectionType::kModulation) {
            if (id_to_index.count(conn.to_node)) {
                state->input_modulations[id_to_index[conn.to_node]].push_back(
                    {static_cast<clap_id>(conn.to_port), id_to_index[conn.from_node],
                     conn.from_port});
            }
        }
    }

    // Convert Proxies (ID to Index)
    for (const auto& [ext_port, mappings] : _input_proxies) {
        for (const auto& mapping : mappings) {
            if (id_to_index.count(mapping.node_id)) {
                state->input_proxies[ext_port].push_back(
                    {id_to_index[mapping.node_id], mapping.port_index});
            }
        }
    }

    for (const auto& [ext_port, mappings] : _output_proxies) {
        for (const auto& mapping : mappings) {
            if (id_to_index.count(mapping.node_id)) {
                uint32_t src_idx = id_to_index[mapping.node_id];
                state->output_proxies[ext_port].push_back({src_idx, mapping.port_index});

                RenderState::EventTarget target;
                target.type = RenderState::EventTarget::Type::kExternalOutput;
                target.destination.port_index = ext_port;
                state->output_event_targets[src_idx].push_back(target);
            }
        }
    }

    return state;
}

}  // namespace synth_canvas::host
