#include "module_router.h"
#include "plugin_host.h"
#include "logger.h"
#include <queue>
#include <algorithm>

namespace synth_canvas::host
{

    ModuleRouter::ModuleRouter() : _pending_states(32), _released_states(32)
    {
        log("[ModuleRouter] Created.");
    }

    ModuleRouter::~ModuleRouter()
    {
        // First, stop sending states and clear any pending resources
        poll_resources();

        for (auto const &[id, host] : plugin_instances)
        {
            if (host)
            {
                host->unload();
            }
        }
        plugin_instances.clear();

        for (auto &host : _pending_deletion_plugins)
        {
            if (host)
                host->unload();
        }
        _pending_deletion_plugins.clear();

        log("[ModuleRouter] Destroyed.");
    }

    uint32_t ModuleRouter::create_plugin_instance(const std::string &path)
    {
        log("[ModuleRouter] Attempting to create plugin instance from path: ", path);
        auto host = std::make_unique<PluginHost>();
        if (host)
        {
            host->on_parameter_changed = on_parameter_changed;
            if (!host->load(path, 0))
            {
                host->unload();
                log("[ModuleRouter] Failed to load plugin from path: ", path);
                return 0; // Return 0 on failure
            }
            uint32_t id = next_instance_id++;
            host->setInstanceId(id); // Set the ID so the host knows who it is
            plugin_instances[id] = std::move(host);
            _topological_sort(); // Recalculate sort order
            _push_new_state();   // Notify audio thread

            log("[ModuleRouter] Created plugin instance with ID: ", id);
            return id;
        }
        log("[ModuleRouter] Failed to create PluginHost object.");
        return 0; // Return 0 on failure
    }

    void ModuleRouter::destroy_plugin_instance(uint32_t instance_id)
    {
        log("[ModuleRouter] Destroying plugin instance: ", instance_id);
        auto it = plugin_instances.find(instance_id);
        if (it != plugin_instances.end())
        {
            // Move ownership to pending deletion list instead of immediate deletion.
            // This ensures the PluginHost is kept alive until the audio thread switches to a new state
            // that doesn't reference this plugin anymore.
            _pending_deletion_plugins.push_back(std::move(it->second));
            plugin_instances.erase(it);

            _topological_sort(); // Recalculate sort order
            _push_new_state();   // Notify audio thread

            log("[ModuleRouter] Plugin instance ", instance_id, " moved to pending deletion.");
        }
        else
        {
            log("[ModuleRouter] Plugin instance ", instance_id, " not found for destruction.");
        }
    }

    uint32_t ModuleRouter::register_special_node()
    {
        uint32_t id = next_instance_id++;
        // No PluginHost is created for special nodes, they are just IDs in the graph.
        log("[ModuleRouter] Registered special node with ID: ", id);
        // We might want to push a state here if special nodes affect routing significantly,
        // but usually they are just terminals.
        return id;
    }

    PluginHost *ModuleRouter::get_plugin_instance(uint32_t instance_id) const
    {
        auto it = plugin_instances.find(instance_id);
        if (it != plugin_instances.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    void ModuleRouter::poll_all_main_threads()
    {
        // Collect any old resources from the audio thread
        poll_resources();

        for (auto const &[id, host] : plugin_instances)
        {
            if (host)
            {
                host->pollMainThread();
            }
        }
    }

    void ModuleRouter::poll_resources()
    {
        // Collect and delete old render states returned by the audio thread
        std::unique_ptr<AudioRenderState> old_state;
        bool state_returned = false;
        // unique_ptr will automatically delete the object when it goes out of scope here
        while (_released_states.try_dequeue(old_state))
        {
            state_returned = true;
        }

        // Safe delete the plugins in _pending_deletion_plugins
        if (state_returned && _pending_states.size_approx() == 0)
        {
            for (auto &host : _pending_deletion_plugins)
            {
                if (host)
                    host->unload();
            }
            _pending_deletion_plugins.clear();
        }
    }

    void ModuleRouter::_push_new_state()
    {
        auto new_state = std::make_unique<AudioRenderState>();

        // Populate the new state with pointers to current plugin instances
        // in the correct topological order.
        for (uint32_t id : _process_order)
        {
            if (auto *host = get_plugin_instance(id))
            {
                new_state->sorted_modules.push_back(host);
            }
        }

        // Copy connections
        new_state->connections = connections;

        // Push to audio thread
        if (!_pending_states.enqueue(std::move(new_state)))
        {
            // TODO:
            log("[ModuleRouter] ERROR: Failed to enqueue new render state. Queue might be full.");
        }
    }

    void ModuleRouter::_topological_sort()
    {
        _process_order.clear();
        if (plugin_instances.empty())
        {
            log("[ModuleRouter] No plugin instances to sort.");
            return;
        }

        std::unordered_map<uint32_t, int> in_degree;
        std::unordered_map<uint32_t, std::vector<uint32_t>> adj;

        // Initialize in-degrees for all actual plugin instances
        for (const auto &pair : plugin_instances)
        {
            in_degree[pair.first] = 0;
        }
        // Also ensure AUDIO_OUTPUT_NODE_ID is considered if it's a destination
        in_degree[AUDIO_OUTPUT_NODE_ID] = 0;

        for (const auto &conn : connections)
        {
            // Only consider connections where the 'from' node is an actual plugin instance
            // and the 'to' node is either a plugin instance or the special audio output node.
            bool from_is_plugin = plugin_instances.count(conn.from_node);
            bool to_is_plugin = plugin_instances.count(conn.to_node);
            bool to_is_audio_out = (conn.to_node == AUDIO_OUTPUT_NODE_ID);

            if (from_is_plugin && (to_is_plugin || to_is_audio_out))
            {
                adj[conn.from_node].push_back(conn.to_node);
                in_degree[conn.to_node]++;
            }
        }

        std::queue<uint32_t> q;
        for (const auto &pair : in_degree)
        {
            // Push nodes with an in-degree of 0 (no incoming connections from other plugins)
            // but ONLY if they are actual plugin instances (not the output node, unless it's a source, which it shouldn't be)
            // or if it's the audio output node and has no incoming connections yet.
            if (pair.second == 0 && plugin_instances.count(pair.first))
            {
                q.push(pair.first);
            }
        }

        // Add any plugin instances that have no connections at all (they won't be in in_degree map if not connected)
        for (const auto &pair : plugin_instances)
        {
            if (in_degree.find(pair.first) == in_degree.end())
            {
                q.push(pair.first);
            }
        }

        while (!q.empty())
        {
            uint32_t u = q.front();
            q.pop();
            _process_order.push_back(u);

            if (adj.count(u))
            {
                for (uint32_t v : adj[u])
                {
                    in_degree[v]--;
                    if (in_degree[v] == 0)
                    {
                        q.push(v);
                    }
                }
            }
        }

        // If after processing, there are still nodes with in_degree > 0, it means there's a cycle or unconnected nodes.
        // For simplicity, we'll just log cycles for now.
        if (_process_order.size() < plugin_instances.size()) // We only care about actual plugins here. Output node is not part of _process_order
        {
            log("[ModuleRouter] Cycle detected or unconnected nodes exist in the graph. Process order might be incomplete.");
            // If there's a cycle, add remaining plugins not in _process_order to the end for processing
            for (const auto &pair : plugin_instances)
            {
                bool found = false;
                for (uint32_t processed_id : _process_order)
                {
                    if (pair.first == processed_id)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    _process_order.push_back(pair.first);
                    log("[ModuleRouter] Added plugin ID ", pair.first, " to process order after cycle detection.");
                }
            }
        }
        log("[ModuleRouter] Topological sort complete. Process order size: ", _process_order.size());
    }

    void ModuleRouter::connect_nodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port)
    {
        log("[ModuleRouter] Connecting ", from_node, ":", from_port, " -> ", to_node, ":", to_port);
        connections.push_back({from_node, from_port, to_node, to_port});
        _topological_sort();
        _push_new_state(); // Notify audio thread
    }

    void ModuleRouter::disconnect_nodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port)
    {
        log("[ModuleRouter] Disconnecting ", from_node, ":", from_port, " -> ", to_node, ":", to_port);
        for (auto it = connections.begin(); it != connections.end();)
        {
            if (it->from_node == from_node && it->from_port == from_port &&
                it->to_node == to_node && it->to_port == to_port)
            {
                it = connections.erase(it);
                log("[ModuleRouter] Connection removed.");
            }
            else
            {
                ++it;
            }
        }
        _topological_sort();
        _push_new_state(); // Notify audio thread
    }

    void ModuleRouter::activate_plugin(uint32_t instance_id, int32_t sample_rate, int32_t frames_per_block)
    {
        if (auto *host = get_plugin_instance(instance_id))
        {
            if (!host->isPluginActive())
            {
                host->activate(sample_rate, frames_per_block);
                log("[ModuleRouter] Plugin ", instance_id, " activated.");
            }
            else
            {
                log("[ModuleRouter] Plugin ", instance_id, " already active.");
            }
        }
        else
        {
            log("[ModuleRouter] Attempted to activate non-existent plugin ID: ", instance_id);
        }
    }

    void ModuleRouter::deactivate_plugin(uint32_t instance_id)
    {
        if (auto *host = get_plugin_instance(instance_id))
        {
            if (host->isPluginActive())
            {
                host->deactivate();
                log("[ModuleRouter] Plugin ", instance_id, " deactivated.");
            }
            else
            {
                log("[ModuleRouter] Plugin ", instance_id, " already inactive.");
            }
        }
        else
        {
            log("[ModuleRouter] Attempted to deactivate non-existent plugin ID: ", instance_id);
        }
    }

} // namespace synth_canvas::host