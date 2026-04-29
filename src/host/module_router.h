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
    void destroyInstance(uint32_t instance_id);
    auto registerSpecialNode(const std::string& type) -> uint32_t;

    // Connectivity
    void connectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port,
                      ConnectionType type = ConnectionType::kAudio);
    void disconnectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port,
                         ConnectionType type = ConnectionType::kAudio);
    void updateConnection(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                          uint32_t to_port, ConnectionType type, float scale, bool bypass);
    auto getConnectionProperties(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                                 uint32_t to_port, ConnectionType type, float& out_scale,
                                 bool& out_bypass) const -> bool;

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
    void setEventCallback(std::function<void(uint32_t, const PluginEvent&)> cb);

    // Activation
    void activateNode(uint32_t instance_id, int32_t sample_rate, int32_t frames_per_block);
    void deactivateNode(uint32_t instance_id);

    // Transport Control (Main Thread)
    void setTempo(double bpm) { _main_transport.tempo = bpm; }
    void setTransportPlaying(bool playing) { _main_transport.is_playing = playing; }
    [[nodiscard]] auto getTransportState() const -> const TransportState& {
        return _main_transport;
    }

   private:
    GraphProcessor _graph_processor;
    std::vector<std::unique_ptr<ProcessingNode>> _pending_deletion_nodes;

    TransportState _main_transport;

    std::atomic<uint32_t> _next_instance_id{constants::kInitialPluginInstanceId};
    std::function<void(uint32_t, const PluginEvent&)> _on_event_occured;

    void pushNewState();
    auto getConnectionCount(uint32_t to_node, uint32_t to_port, ConnectionType type) const
        -> size_t;
};

}  // namespace synth_canvas::host

#endif  // MODULE_ROUTER_H
