#include "module_router.h"

#include <queue>

#include "constants.h"  // Explicitly include constants
#include "logger.h"
#include "plugin_host.h"

namespace synth_canvas::host {

ModuleRouter::ModuleRouter()
    : pending_states(constants::kSnapshotQueueSize),
      released_states(constants::kSnapshotQueueSize) {
    log("[ModuleRouter] Created.");
}

ModuleRouter::~ModuleRouter() {
    pollResources();

    for (auto const& [id, host] : _plugin_instances) {
        if (host) {
            host->unload();
        }
    }
    _plugin_instances.clear();

    for (auto& host : _pending_deletion_plugins) {
        if (host) host->unload();
    }
    _pending_deletion_plugins.clear();

    log("[ModuleRouter] Destroyed.");
}

auto ModuleRouter::createPluginInstance(const std::string& path) -> uint32_t {
    log("[ModuleRouter] Attempting to create plugin instance from path: ", path);
    auto host = std::make_unique<PluginHost>();
    if (host) {
        host->on_parameter_changed = on_parameter_changed;
        if (!host->load(path, 0)) {
            host->unload();
            log("[ModuleRouter] Failed to load plugin from path: ", path);
            return 0;
        }
        uint32_t id = _next_instance_id++;
        host->setInstanceId(id);
        _plugin_instances[id] = std::move(host);
        topologicalSort();
        pushNewState();

        log("[ModuleRouter] Created plugin instance with ID: ", id);
        return id;
    }
    log("[ModuleRouter] Failed to create PluginHost object.");
    return 0;
}

void ModuleRouter::destroyPluginInstance(uint32_t instance_id) {
    log("[ModuleRouter] Destroying plugin instance: ", instance_id);
    auto it = _plugin_instances.find(instance_id);
    if (it != _plugin_instances.end()) {
        // Move ownership to pending deletion list instead of immediate deletion.
        // This ensures the PluginHost is kept alive until the audio thread switches to a new state
        // that doesn't reference this plugin anymore.
        _pending_deletion_plugins.push_back(std::move(it->second));
        _plugin_instances.erase(it);

        topologicalSort();
        pushNewState();

        log("[ModuleRouter] Plugin instance ", instance_id, " moved to pending deletion.");
    } else {
        log("[ModuleRouter] Plugin instance ", instance_id, " not found for destruction.");
    }
}

auto ModuleRouter::registerSpecialNode() -> uint32_t {
    uint32_t id = _next_instance_id++;
    log("[ModuleRouter] Registered special node with ID: ", id);
    return id;
}

auto ModuleRouter::getPluginInstance(uint32_t instance_id) const -> PluginHost* {
    auto it = _plugin_instances.find(instance_id);
    if (it != _plugin_instances.end()) {
        return it->second.get();
    }
    return nullptr;
}

void ModuleRouter::pollAllMainThreads() {
    pollResources();

    for (auto const& [id, host] : _plugin_instances) {
        if (host) {
            host->pollMainThread();
        }
    }
}

void ModuleRouter::pollResources() {
    std::unique_ptr<AudioRenderState> old_state;
    bool state_returned = false;
    while (released_states.try_dequeue(old_state)) {
        state_returned = true;
    }

    if (state_returned && pending_states.size_approx() == 0) {
        for (auto& host : _pending_deletion_plugins) {
            if (host) host->unload();
        }
        _pending_deletion_plugins.clear();
    }
}

void ModuleRouter::pushNewState() {
    auto new_state = std::make_unique<AudioRenderState>();

    for (uint32_t id : _process_order) {
        if (auto* host = getPluginInstance(id)) {
            new_state->sorted_modules.push_back(host);
        }
    }

    new_state->connections = _connections;

    if (!pending_states.enqueue(std::move(new_state))) {
        log("[ModuleRouter] ERROR: Failed to enqueue new render state. Queue might be full.");
    }
}

void ModuleRouter::topologicalSort() {
    _process_order.clear();
    if (_plugin_instances.empty()) {
        log("[ModuleRouter] No plugin instances to sort.");
        return;
    }

    std::unordered_map<uint32_t, int> in_degree;
    std::unordered_map<uint32_t, std::vector<uint32_t>> adj;

    for (const auto& pair : _plugin_instances) {
        in_degree[pair.first] = 0;
    }
    in_degree[constants::kAudioOutputNoteId] = 0;

    for (const auto& conn : _connections) {
        bool from_is_plugin = _plugin_instances.count(conn.from_node);
        bool to_is_plugin = _plugin_instances.count(conn.to_node);
        bool to_is_audio_out = (conn.to_node == constants::kAudioOutputNoteId);

        if (from_is_plugin && (to_is_plugin || to_is_audio_out)) {
            adj[conn.from_node].push_back(conn.to_node);
            in_degree[conn.to_node]++;
        }
    }

    std::queue<uint32_t> q;
    for (const auto& pair : in_degree) {
        if (pair.second == 0 && _plugin_instances.count(pair.first)) {
            q.push(pair.first);
        }
    }

    for (const auto& pair : _plugin_instances) {
        if (in_degree.find(pair.first) == in_degree.end()) {
            q.push(pair.first);
        }
    }

    while (!q.empty()) {
        uint32_t u = q.front();
        q.pop();
        _process_order.push_back(u);

        if (adj.count(u)) {
            for (uint32_t v : adj[u]) {
                in_degree[v]--;
                if (in_degree[v] == 0) {
                    q.push(v);
                }
            }
        }
    }

    if (_process_order.size() < _plugin_instances.size()) {
        log("[ModuleRouter] Cycle detected or unconnected nodes exist in the graph. Process order "
            "might be incomplete.");
        for (const auto& pair : _plugin_instances) {
            bool found = false;
            for (uint32_t processed_id : _process_order) {
                if (pair.first == processed_id) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                _process_order.push_back(pair.first);
                log("[ModuleRouter] Added plugin ID ", pair.first,
                    " to process order after cycle detection.");
            }
        }
    }
    log("[ModuleRouter] Topological sort complete. Process order size: ", _process_order.size());
}

void ModuleRouter::connectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                                uint32_t to_port, ConnectionType type) {
    log("[ModuleRouter] Connecting ", from_node, ":", from_port, " -> ", to_node, ":", to_port,
        " (Type: ", (type == ConnectionType::kAudio ? "Audio" : "Event"), ")");
    _connections.push_back({from_node, from_port, to_node, to_port, type});
    topologicalSort();
    pushNewState();
}

void ModuleRouter::disconnectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                                   uint32_t to_port, ConnectionType type) {
    log("[ModuleRouter] Disconnecting ", from_node, ":", from_port, " -> ", to_node, ":", to_port,
        " (Type: ", (type == ConnectionType::kAudio ? "Audio" : "Event"), ")");
    for (auto it = _connections.begin(); it != _connections.end();) {
        if (it->from_node == from_node && it->from_port == from_port && it->to_node == to_node &&
            it->to_port == to_port && it->type == type) {
            it = _connections.erase(it);
            log("[ModuleRouter] Connection removed.");
        } else {
            ++it;
        }
    }
    topologicalSort();
    pushNewState();
}

void ModuleRouter::activatePlugin(uint32_t instance_id, int32_t sample_rate,
                                  int32_t frames_per_block) {
    if (auto* host = getPluginInstance(instance_id)) {
        if (!host->isPluginActive()) {
            host->activate(sample_rate, frames_per_block);
            log("[ModuleRouter] Plugin ", instance_id, " activated.");
        } else {
            log("[ModuleRouter] Plugin ", instance_id, " already active.");
        }
    } else {
        log("[ModuleRouter] Attempted to activate non-existent plugin ID: ", instance_id);
    }
}

void ModuleRouter::deactivatePlugin(uint32_t instance_id) {
    if (auto* host = getPluginInstance(instance_id)) {
        if (host->isPluginActive()) {
            host->deactivate();
            log("[ModuleRouter] Plugin ", instance_id, " deactivated.");
        } else {
            log("[ModuleRouter] Plugin ", instance_id, " already inactive.");
        }
    } else {
        log("[ModuleRouter] Attempted to deactivate non-existent plugin ID: ", instance_id);
    }
}

}  // namespace synth_canvas::host