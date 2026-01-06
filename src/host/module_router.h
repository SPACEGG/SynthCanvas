#ifndef MODULE_ROUTER_H
#define MODULE_ROUTER_H

#include <clap/clap.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "constants.h"
#include "readerwriterqueue.h"

// Forward declarations
namespace synth_canvas::host {
class PluginHost;
}

namespace synth_canvas::host {

enum class ConnectionType {
    kAudio,
    kEvent,
};

class ModuleRouter {
   public:
    struct PortConnection {
        uint32_t from_node;
        uint32_t from_port;
        uint32_t to_node;
        uint32_t to_port;
        ConnectionType type;
    };

    struct AudioRenderState {
        std::vector<PluginHost*> sorted_modules;
        std::vector<PortConnection> connections;
    };

    ModuleRouter();
    ~ModuleRouter();

    auto createPluginInstance(const std::string& path) -> uint32_t;
    void destroyPluginInstance(uint32_t instance_id);
    auto registerSpecialNode() -> uint32_t;
    void connectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port,
                      ConnectionType type = ConnectionType::kAudio);
    void disconnectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port,
                         ConnectionType type = ConnectionType::kAudio);

    auto getPluginInstance(uint32_t instance_id) const -> PluginHost*;
    auto getProcessOrder() const -> const std::vector<uint32_t>& { return _process_order; }
    auto getConnections() const -> const std::vector<PortConnection>& { return _connections; }
    auto getNextInstanceId() const -> uint32_t { return _next_instance_id.load(); }

    void pollAllMainThreads();

    void pollResources();

    // Queues for communication with Audio Thread
    moodycamel::ReaderWriterQueue<std::unique_ptr<AudioRenderState>> pending_states;
    moodycamel::ReaderWriterQueue<std::unique_ptr<AudioRenderState>> released_states;

    std::function<void(clap_id, double)> on_parameter_changed;

    void activatePlugin(uint32_t instance_id, int32_t sample_rate, int32_t frames_per_block);
    void deactivatePlugin(uint32_t instance_id);

   private:
    std::unordered_map<uint32_t, std::unique_ptr<PluginHost>> _plugin_instances;
    std::vector<std::unique_ptr<PluginHost>> _pending_deletion_plugins;

    std::atomic<uint32_t> _next_instance_id{constants::kInitialPluginInstanceId};
    std::vector<PortConnection> _connections;
    std::vector<uint32_t> _process_order;

    void topologicalSort();

    void pushNewState();
};

}  // namespace synth_canvas::host

#endif  // MODULE_ROUTER_H
