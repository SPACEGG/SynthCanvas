#ifndef MODULE_ROUTER_H
#define MODULE_ROUTER_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <functional> // For std::function

#include <clap/clap.h> // For clap_id
#include "readerwriterqueue.h" // Lock-free queue

// Forward declarations
namespace synth_canvas::host
{
    class PluginHost;
}

namespace synth_canvas::host
{

    class ModuleRouter
    {
    public:
        struct PortConnection
        {
            uint32_t from_node;
            uint32_t from_port;
            uint32_t to_node;
            uint32_t to_port;
        };

        // Snapshot of the audio graph state for the audio thread
        struct AudioRenderState
        {
            std::vector<PluginHost *> sorted_modules; // Topologically sorted modules
            std::vector<PortConnection> connections;
        };

        static constexpr uint32_t AUDIO_OUTPUT_NODE_ID = 0;

        ModuleRouter();
        ~ModuleRouter();

        uint32_t create_plugin_instance(const std::string &path);
        void destroy_plugin_instance(uint32_t instance_id);
        uint32_t register_special_node(); // For nodes like "Audio Output"
        void connect_nodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port);
        void disconnect_nodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port);

        PluginHost *get_plugin_instance(uint32_t instance_id) const;
        const std::vector<uint32_t>& get_process_order() const { return _process_order; }
        const std::vector<PortConnection>& get_connections() const { return connections; }
        uint32_t get_next_instance_id() const { return next_instance_id.load(); }

        void poll_all_main_threads();

        // Process garbage collection for deleted plugins and old states
        void poll_resources();

        // Queues for communication with Audio Thread
        // Audio Thread reads from here to get new states
        moodycamel::ReaderWriterQueue<std::unique_ptr<AudioRenderState>> _pending_states;
        // Audio Thread writes here to return old states
        moodycamel::ReaderWriterQueue<std::unique_ptr<AudioRenderState>> _released_states;

        // Callback for parameter changes (forwarded from PluginHost)
        std::function<void(clap_id, double)> on_parameter_changed;

        // Methods to activate/deactivate plugins (might be called from AudioEngine or here)
        void activate_plugin(uint32_t instance_id, int32_t sample_rate, int32_t frames_per_block);
        void deactivate_plugin(uint32_t instance_id);


    private:
        std::unordered_map<uint32_t, std::unique_ptr<PluginHost>> plugin_instances;
        // Plugins that are removed from the graph but waiting for the audio thread to release them
        std::vector<std::unique_ptr<PluginHost>> _pending_deletion_plugins;

        std::atomic<uint32_t> next_instance_id{1}; // Start IDs from 1
        std::vector<PortConnection> connections;
        std::vector<uint32_t> _process_order; // Topological sort result

        void _topological_sort();

        // Helper to create a new state snapshot and push it to the audio thread
        void _push_new_state();
    };

} // namespace synth_canvas::host

#endif // MODULE_ROUTER_H
