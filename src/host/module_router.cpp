#include "module_router.h"
#include "plugin_host.h"
#include <godot_cpp/variant/utility_functions.hpp>
#include <queue>
#include <algorithm>

namespace synth_canvas::host
{

    ModuleRouter::ModuleRouter()
    {
        godot::UtilityFunctions::print("[ModuleRouter] Created.");
    }

    ModuleRouter::~ModuleRouter()
    {
        for (auto const &[id, host] : plugin_instances)
        {
            if (host)
            {
                host->unload();
            }
        }
        plugin_instances.clear();
        godot::UtilityFunctions::print("[ModuleRouter] Destroyed.");
    }

    uint32_t ModuleRouter::create_plugin_instance(const std::string &path)
    {
        godot::UtilityFunctions::print("[ModuleRouter] Attempting to create plugin instance from path: ", path.c_str());
        auto host = std::make_unique<PluginHost>();
        if (host)
        {
            host->on_parameter_changed = on_parameter_changed;
            if (!host->load(path, 0))
            {
                host->unload();
                godot::UtilityFunctions::print("[ModuleRouter] Failed to load plugin from path: ", path.c_str());
                return 0; // Return 0 on failure
            }
            uint32_t id = next_instance_id++;
            plugin_instances[id] = std::move(host);
            _topological_sort(); // Recalculate sort order

            // Plugins are not activated here, they will be activated by AudioEngine or explicitly.

            godot::UtilityFunctions::print("[ModuleRouter] Created plugin instance with ID: ", godot::String::num_int64(id));
            return id;
        }
        godot::UtilityFunctions::print("[ModuleRouter] Failed to create PluginHost object.");
        return 0; // Return 0 on failure
    }

    void ModuleRouter::destroy_plugin_instance(uint32_t instance_id)
    {
        godot::UtilityFunctions::print("[ModuleRouter] Destroying plugin instance: ", godot::String::num_int64(instance_id));
        auto it = plugin_instances.find(instance_id);
        if (it != plugin_instances.end())
        {
            if (it->second)
            {
                it->second->unload();
            }
            plugin_instances.erase(it);
            _topological_sort(); // Recalculate sort order
            godot::UtilityFunctions::print("[ModuleRouter] Plugin instance ", godot::String::num_int64(instance_id), " destroyed.");
        }
        else
        {
            godot::UtilityFunctions::print("[ModuleRouter] Plugin instance ", godot::String::num_int64(instance_id), " not found for destruction.");
        }
    }

    uint32_t ModuleRouter::register_special_node()
    {
        uint32_t id = next_instance_id++;
        // No PluginHost is created for special nodes, they are just IDs in the graph.
        godot::UtilityFunctions::print("[ModuleRouter] Registered special node with ID: ", godot::String::num_int64(id));
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
        for (auto const &[id, host] : plugin_instances)
        {
            if (host)
            {
                host->pollMainThread();
            }
        }
    }

    void ModuleRouter::_topological_sort()
    {
        _process_order.clear();
        if (plugin_instances.empty())
        {
            godot::UtilityFunctions::print("[ModuleRouter] No plugin instances to sort.");
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
            if (in_degree.find(pair.first) == in_degree.end()) {
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
             godot::UtilityFunctions::print("[ModuleRouter] Cycle detected or unconnected nodes exist in the graph. Process order might be incomplete.");
             // If there's a cycle, add remaining plugins not in _process_order to the end for processing
             for (const auto &pair : plugin_instances) {
                 bool found = false;
                 for (uint32_t processed_id : _process_order) {
                     if (pair.first == processed_id) {
                         found = true;
                         break;
                     }
                 }
                 if (!found) {
                     _process_order.push_back(pair.first);
                     godot::UtilityFunctions::print("[ModuleRouter] Added plugin ID ", godot::String::num_int64(pair.first), " to process order after cycle detection.");
                 }
             }
        }
        godot::UtilityFunctions::print("[ModuleRouter] Topological sort complete. Process order size: ", godot::String::num_int64(_process_order.size()));
    }

    void ModuleRouter::connect_nodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port)
    {
        godot::UtilityFunctions::print("[ModuleRouter] Connecting ", godot::String::num_int64(from_node), ":", godot::String::num_int64(from_port), " -> ", godot::String::num_int64(to_node), ":", godot::String::num_int64(to_port));
        connections.push_back({from_node, from_port, to_node, to_port});
        _topological_sort();
    }

    void ModuleRouter::disconnect_nodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port)
    {
        godot::UtilityFunctions::print("[ModuleRouter] Disconnecting ", godot::String::num_int64(from_node), ":", godot::String::num_int64(from_port), " -> ", godot::String::num_int64(to_node), ":", godot::String::num_int64(to_port));
        for (auto it = connections.begin(); it != connections.end();)
        {
            if (it->from_node == from_node && it->from_port == from_port &&
                it->to_node == to_node && it->to_port == to_port)
            {
                it = connections.erase(it);
                godot::UtilityFunctions::print("[ModuleRouter] Connection removed.");
            }
            else
            {
                ++it;
            }
        }
        _topological_sort();
    }

    void ModuleRouter::activate_plugin(uint32_t instance_id, int32_t sample_rate, int32_t frames_per_block)
    {
        if (auto *host = get_plugin_instance(instance_id))
        {
            if (!host->isPluginActive()) {
                host->activate(sample_rate, frames_per_block);
                godot::UtilityFunctions::print("[ModuleRouter] Plugin ", godot::String::num_int64(instance_id), " activated.");
            } else {
                godot::UtilityFunctions::print("[ModuleRouter] Plugin ", godot::String::num_int64(instance_id), " already active.");
            }
        }
        else
        {
            godot::UtilityFunctions::print("[ModuleRouter] Attempted to activate non-existent plugin ID: ", godot::String::num_int64(instance_id));
        }
    }

    void ModuleRouter::deactivate_plugin(uint32_t instance_id)
    {
        if (auto *host = get_plugin_instance(instance_id))
        {
            if (host->isPluginActive()) {
                host->deactivate();
                godot::UtilityFunctions::print("[ModuleRouter] Plugin ", godot::String::num_int64(instance_id), " deactivated.");
            } else {
                godot::UtilityFunctions::print("[ModuleRouter] Plugin ", godot::String::num_int64(instance_id), " already inactive.");
            }
        }
        else
        {
            godot::UtilityFunctions::print("[ModuleRouter] Attempted to deactivate non-existent plugin ID: ", godot::String::num_int64(instance_id));
        }
    }


} // namespace synth_canvas::host
