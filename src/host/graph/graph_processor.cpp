#include "host/graph/graph_processor.h"

#include <algorithm>
#include <queue>

#include "utils/constants.h"
#include "utils/logger.h"

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

auto GraphProcessor::setConnectionProperties(const PortConnection& conn_id, float scale,
                                             bool bypass) -> bool {
    for (auto& conn : _connections) {
        if (conn.from_node == conn_id.from_node && conn.from_port == conn_id.from_port &&
            conn.to_node == conn_id.to_node && conn.to_port == conn_id.to_port &&
            conn.type == conn_id.type) {
            conn.scale = scale;
            conn.bypass = bypass;
            return true;
        }
    }
    return false;
}

void GraphProcessor::setParameterMapping(const std::string& param_id, uint32_t node_id,
                                         uint32_t clap_param_id) {
    _parameter_mappings[param_id] = {.target_node_id = node_id, .target_param_id = clap_param_id};
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

    struct Edge {
        uint32_t to;
        ConnectionType type;
    };

    std::unordered_map<uint32_t, int> in_degree;
    std::unordered_map<uint32_t, int> strong_in_degree;
    std::unordered_map<uint32_t, std::vector<Edge>> adj;

    for (const auto& [id, node] : _nodes) {
        in_degree[id] = 0;
        strong_in_degree[id] = 0;
    }

    // Build adjacency list and calculate in-degrees (consider ALL connection types)
    for (const auto& conn : _connections) {
        if (_nodes.count(conn.from_node) && _nodes.count(conn.to_node)) {
            adj[conn.from_node].push_back({conn.to_node, conn.type});
            in_degree[conn.to_node]++;
            if (conn.type != ConnectionType::kEvent) {
                strong_in_degree[conn.to_node]++;
            }
        }
    }

    std::queue<uint32_t> q;
    // Re-implementing correctly:
    for (const auto& [id, node] : _nodes) {
        if (in_degree[id] == 0) {
            q.push(id);
        }
    }

    auto is_processed = [&](uint32_t id) {
        return std::ranges::find(_process_order, id) != _process_order.end();
    };

    while (_process_order.size() < _nodes.size()) {
        if (q.empty()) {
            // Cycle detected! Perform Smart Cycle Breaking.

            // 1. Calculate active out-degree among remaining nodes
            std::unordered_map<uint32_t, int> active_out_degree;
            for (const auto& conn : _connections) {
                if (_nodes.count(conn.from_node) && _nodes.count(conn.to_node)) {
                    if (!is_processed(conn.from_node) && !is_processed(conn.to_node)) {
                        active_out_degree[conn.from_node]++;
                    }
                }
            }

            uint32_t best_node = 0xFFFFFFFF;

            // 2. Find a node to break the cycle.
            // Prioritize nodes with no incoming strong connections.
            // DO NOT pick "victim" downstream nodes (active_out_degree == 0).
            for (const auto& [id, node] : _nodes) {
                if (is_processed(id)) continue;

                // Skip nodes that don't point to any other unprocessed node
                if (active_out_degree[id] == 0) continue;

                if (strong_in_degree[id] == 0) {
                    if (best_node == 0xFFFFFFFF || id < best_node) {
                        best_node = id;
                    }
                }
            }

            // Fallback 1: If all nodes in cycle have strong connections, just pick the lowest ID in
            // cycle
            if (best_node == 0xFFFFFFFF) {
                for (const auto& [id, node] : _nodes) {
                    if (is_processed(id)) continue;
                    if (active_out_degree[id] == 0) continue;
                    if (best_node == 0xFFFFFFFF || id < best_node) {
                        best_node = id;
                    }
                }
            }

            // Fallback 2: Extreme edge case where everything left is a disconnected downstream node
            if (best_node == 0xFFFFFFFF) {
                for (const auto& [id, node] : _nodes) {
                    if (is_processed(id)) continue;
                    if (best_node == 0xFFFFFFFF || id < best_node) {
                        best_node = id;
                    }
                }
            }

            if (best_node != 0xFFFFFFFF) {
                q.push(best_node);
            } else {
                break;
            }
        }

        while (!q.empty()) {
            uint32_t u = q.front();
            q.pop();

            if (is_processed(u)) continue;
            _process_order.push_back(u);

            if (adj.count(u)) {
                for (const auto& edge : adj[u]) {
                    if (!is_processed(edge.to)) {
                        if (edge.type != ConnectionType::kEvent) {
                            strong_in_degree[edge.to]--;
                        }
                        if (--in_degree[edge.to] == 0) {
                            q.push(edge.to);
                        }
                    }
                }
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

    // --- Static Lifetime Analysis & Greedy Coloring ---
    struct Interval {
        uint32_t node_idx;
        uint32_t port_idx;
        uint32_t start;
        uint32_t end;
        bool is_feedback = false;
        uint32_t color = 0xFFFFFFFF;
    };

    std::vector<Interval> intervals;
    std::vector<std::vector<size_t>> interval_map(num_nodes);
    for (uint32_t i = 0; i < num_nodes; ++i) {
        auto out_ports =
            static_cast<uint32_t>(state->sorted_nodes[i]->getAudioPorts(false).size());
        interval_map[i].resize(out_ports);
        for (uint32_t o = 0; o < out_ports; ++o) {
            interval_map[i][o] = intervals.size();
            Interval iv;
            iv.node_idx = i;
            iv.port_idx = o;
            iv.start = i;
            iv.end = i;
            iv.is_feedback = false;
            intervals.push_back(iv);
        }
    }

    // Scan connections to update lifetimes and detect feedback loops
    for (const auto& conn : _connections) {
        if (!id_to_index.count(conn.from_node)) continue;
        uint32_t from_idx = id_to_index[conn.from_node];
        uint32_t from_port = conn.from_port;
        if (from_port >= interval_map[from_idx].size()) continue;
        size_t iv_idx = interval_map[from_idx][from_port];

        if (conn.type == ConnectionType::kAudio) {
            if (conn.to_node == master_node_id) {
                intervals[iv_idx].end =
                    std::max(intervals[iv_idx].end, static_cast<uint32_t>(num_nodes));
            } else if (id_to_index.count(conn.to_node)) {
                uint32_t to_idx = id_to_index[conn.to_node];
                if (to_idx <= from_idx) {
                    intervals[iv_idx].is_feedback = true;
                } else {
                    intervals[iv_idx].end =
                        std::max(intervals[iv_idx].end, to_idx);
                }
            }
        } else if (conn.type == ConnectionType::kModulation) {
            if (id_to_index.count(conn.to_node)) {
                uint32_t to_idx = id_to_index[conn.to_node];
                if (to_idx <= from_idx) {
                    intervals[iv_idx].is_feedback = true;
                } else {
                    intervals[iv_idx].end =
                        std::max(intervals[iv_idx].end, to_idx);
                }
            }
        }
    }

    // Sort and color non-feedback intervals with In-Place Coalescing
    std::vector<size_t> non_feedback_indices;
    for (size_t i = 0; i < intervals.size(); ++i) {
        if (!intervals[i].is_feedback) {
            non_feedback_indices.push_back(i);
        }
    }

    std::ranges::sort(non_feedback_indices,
                      [&](size_t a, size_t b) { return intervals[a].start < intervals[b].start; });

    uint32_t shared_buffer_count = 0;

    for (size_t idx : non_feedback_indices) {
        auto& iv = intervals[idx];
        uint32_t node_idx = iv.node_idx;
        uint32_t port_idx = iv.port_idx;

        bool coalesced = false;
        if (state->sorted_nodes[node_idx]->supportsInPlace()) {
            uint32_t target_node_id = state->sorted_nodes[node_idx]->getInstanceId();
            uint32_t incoming_count = 0;
            const PortConnection* single_conn = nullptr;
            for (const auto& conn : _connections) {
                if (conn.to_node == target_node_id && conn.to_port == port_idx &&
                    (conn.type == ConnectionType::kAudio ||
                     conn.type == ConnectionType::kModulation)) {
                    incoming_count++;
                    single_conn = &conn;
                }
            }

            if (incoming_count == 1 && single_conn != nullptr) {
                if (id_to_index.count(single_conn->from_node)) {
                    uint32_t src_idx = id_to_index[single_conn->from_node];
                    uint32_t src_port = single_conn->from_port;
                    if (src_port < interval_map[src_idx].size()) {
                        size_t src_iv_idx = interval_map[src_idx][src_port];
                        auto& src_iv = intervals[src_iv_idx];

                        if (src_iv.end == node_idx && !src_iv.is_feedback &&
                            src_iv.color != 0xFFFFFFFF) {
                            iv.color = src_iv.color;
                            src_iv.end = iv.end;
                            coalesced = true;
                        }
                    }
                }
            }
        }

        if (!coalesced) {
            uint32_t color = 0;
            while (true) {
                bool conflict = false;
                for (size_t other_idx : non_feedback_indices) {
                    if (other_idx == idx) continue;
                    const auto& other = intervals[other_idx];
                    if (other.color == color) {
                        if (iv.start <= other.end && other.start <= iv.end) {
                            conflict = true;
                            break;
                        }
                    }
                }
                if (!conflict) {
                    iv.color = color;
                    if (color + 1 > shared_buffer_count) {
                        shared_buffer_count = color + 1;
                    }
                    break;
                }
                color++;
            }
        }
    }

    // Color feedback intervals
    uint32_t feedback_count = 0;
    for (auto& iv : intervals) {
        if (iv.is_feedback) {
            iv.color = shared_buffer_count + feedback_count;
            feedback_count++;
        }
    }

    state->total_shared_buffers = shared_buffer_count + feedback_count;

    // Map 2D temporary indices
    std::vector<std::vector<uint32_t>> temp_output_buffer_indices(num_nodes);
    std::vector<std::vector<uint32_t>> temp_input_buffer_indices(num_nodes);

    for (uint32_t i = 0; i < num_nodes; ++i) {
        temp_output_buffer_indices[i].assign(state->sorted_nodes[i]->getAudioPorts(false).size(),
                                             kUnusedBufferIndex);
        temp_input_buffer_indices[i].assign(state->sorted_nodes[i]->getAudioPorts(true).size(),
                                            kUnusedBufferIndex);
    }

    for (const auto& iv : intervals) {
        temp_output_buffer_indices[iv.node_idx][iv.port_idx] = iv.color;
    }

    // Build flat mappings and offsets
    for (const auto& conn : _connections) {
        if (!id_to_index.count(conn.from_node)) continue;
        uint32_t from_idx = id_to_index[conn.from_node];
        uint32_t from_port = conn.from_port;

        if (conn.type == ConnectionType::kAudio) {
            if (id_to_index.count(conn.to_node)) {
                uint32_t to_idx = id_to_index[conn.to_node];
                uint32_t to_port = conn.to_port;
                if (to_port < temp_input_buffer_indices[to_idx].size()) {
                    uint32_t src_buf_idx = temp_output_buffer_indices[from_idx][from_port];
                    if (temp_input_buffer_indices[to_idx][to_port] == kUnusedBufferIndex) {
                        temp_input_buffer_indices[to_idx][to_port] = src_buf_idx;
                    } else if (temp_input_buffer_indices[to_idx][to_port] != src_buf_idx) {
                        temp_input_buffer_indices[to_idx][to_port] = kSummedBufferIndex;
                    }
                }
            }
        }
    }

    state->node_output_offsets.resize(num_nodes, 0);
    state->node_input_offsets.resize(num_nodes, 0);

    uint32_t total_out_ports = 0;
    uint32_t total_in_ports = 0;

    for (uint32_t i = 0; i < num_nodes; ++i) {
        state->node_output_offsets[i] = total_out_ports;
        total_out_ports += static_cast<uint32_t>(temp_output_buffer_indices[i].size());

        state->node_input_offsets[i] = total_in_ports;
        total_in_ports += static_cast<uint32_t>(temp_input_buffer_indices[i].size());
    }

    state->flat_output_buffer_indices.resize(total_out_ports, kUnusedBufferIndex);
    state->flat_input_buffer_indices.resize(total_in_ports, kUnusedBufferIndex);

    for (uint32_t i = 0; i < num_nodes; ++i) {
        uint32_t out_off = state->node_output_offsets[i];
        for (uint32_t o = 0; o < temp_output_buffer_indices[i].size(); ++o) {
            state->flat_output_buffer_indices[out_off + o] = temp_output_buffer_indices[i][o];
        }

        uint32_t in_off = state->node_input_offsets[i];
        for (uint32_t in = 0; in < temp_input_buffer_indices[i].size(); ++in) {
            state->flat_input_buffer_indices[in_off + in] = temp_input_buffer_indices[i][in];
        }
    }

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
        uint32_t from_idx = id_to_index[conn.from_node];
        uint32_t from_port = conn.from_port;
        uint32_t src_buf_idx = temp_output_buffer_indices[from_idx][from_port];

        if (conn.type == ConnectionType::kAudio) {
            if (conn.to_node == master_node_id) {
                state->master_output_sources.push_back(
                    {from_idx, from_port, src_buf_idx, conn.bypass});
            } else if (id_to_index.count(conn.to_node)) {
                uint32_t to_idx = id_to_index[conn.to_node];
                state->input_audio_sources[to_idx][conn.to_port].push_back(
                    {from_idx, from_port, src_buf_idx, conn.bypass});
            }
        } else if (conn.type == ConnectionType::kEvent) {
            if (id_to_index.count(conn.to_node)) {
                auto* to_node = state->sorted_nodes[id_to_index[conn.to_node]];
                RenderState::EventTarget target;
                target.type = RenderState::EventTarget::Type::kNode;
                target.source_port_index = conn.from_port;
                target.destination.node = to_node;
                target.scale = conn.scale;
                target.bypass = conn.bypass;

                target.target_param_id = constants::kClapInvalidId;
                const auto& ports = to_node->getAudioPorts(true);
                if (conn.to_port < ports.size()) {
                    target.target_param_id = ports[conn.to_port].target_param_id;
                }

                state->output_event_targets[from_idx].push_back(target);
            }
        } else if (conn.type == ConnectionType::kModulation) {
            if (id_to_index.count(conn.to_node)) {
                auto* to_node = state->sorted_nodes[id_to_index[conn.to_node]];

                clap_id resolved_param_id = constants::kClapInvalidId;
                const auto& ports = to_node->getAudioPorts(true);
                if (conn.to_port < ports.size()) {
                    resolved_param_id = ports[conn.to_port].target_param_id;
                }

                state->input_modulations[id_to_index[conn.to_node]].push_back(
                    {resolved_param_id, from_idx, from_port, src_buf_idx, conn.scale, conn.bypass});

                RenderState::EventTarget target;
                target.type = RenderState::EventTarget::Type::kNode;
                target.destination.node = to_node;
                target.target_param_id = resolved_param_id;
                target.scale = conn.scale;
                target.bypass = conn.bypass;
                state->output_event_targets[from_idx].push_back(target);
            }
        }
    }

    return state;
}

}  // namespace synth_canvas::host
