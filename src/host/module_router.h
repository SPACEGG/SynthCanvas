#ifndef MODULE_ROUTER_H
#define MODULE_ROUTER_H

#include <clap/clap.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "constants.h"
#include "graph_processor.h"
#include "graph_types.h"
#include "readerwriterqueue.h"

namespace synth_canvas::host {

// Global coordinator for the audio graph, managing node lifecycles
// and synchronizing state with the AudioEngine.
class ModuleRouter {
   public:
    // Re-use the RenderState defined in GraphProcessor
    using AudioRenderState = GraphProcessor::RenderState;

    ModuleRouter();
    ~ModuleRouter();

    // Node Lifecycle
    auto createPluginInstance(const std::string& path) -> uint32_t;
    auto createCompositeInstance(const CompositeConfig& config) -> uint32_t;
    void destroyPluginInstance(uint32_t instance_id);
    auto registerSpecialNode() -> uint32_t;

    // Connectivity
    void connectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port,
                      ConnectionType type = ConnectionType::kAudio);
    void disconnectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port,
                         ConnectionType type = ConnectionType::kAudio);

    // Accessors
    auto getProcessingNode(uint32_t instance_id) const -> ProcessingNode*;
    auto getProcessOrder() const -> const std::vector<uint32_t>&;
    auto getConnections() const -> const std::vector<PortConnection>&;
    auto getNextInstanceId() const -> uint32_t { return _next_instance_id.load(); }

    // Maintenance
    void pollAllMainThreads();
    void pollResources();

    // State Synchronization
    moodycamel::ReaderWriterQueue<std::unique_ptr<AudioRenderState>> pending_states;
    moodycamel::ReaderWriterQueue<std::unique_ptr<AudioRenderState>> released_states;

    // Callbacks
    std::function<void(uint32_t, clap_id, double)> on_parameter_changed;

    // Activation
    void activateNode(uint32_t instance_id, int32_t sample_rate, int32_t frames_per_block);
    void deactivateNode(uint32_t instance_id);

   private:
    GraphProcessor _graph_processor;
    std::vector<std::unique_ptr<ProcessingNode>> _pending_deletion_nodes;

    std::atomic<uint32_t> _next_instance_id{constants::kInitialPluginInstanceId};

    void pushNewState();
};

}  // namespace synth_canvas::host

#endif  // MODULE_ROUTER_H
