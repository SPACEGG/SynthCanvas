#include "module_router.h"

#include "composite_node.h"
#include "constants.h"
#include "logger.h"
#include "midi_input_node.h"
#include "plugin_host.h"

namespace synth_canvas::host {

ModuleRouter::ModuleRouter()
    : pending_states(constants::kSnapshotQueueSize),
      released_states(constants::kSnapshotQueueSize) {
    log("[ModuleRouter] Created.");
}

ModuleRouter::~ModuleRouter() {
    pollResources();
    log("[ModuleRouter] Destroyed.");
}

auto ModuleRouter::createPluginInstance(const std::string& path) -> uint32_t {
    log("[ModuleRouter] Creating plugin instance from path: ", path);
    auto host = std::make_unique<PluginHost>();
    if (!host->load(path, 0)) {
        log("[ModuleRouter] Failed to load plugin: ", path);
        return 0;
    }

    uint32_t id = _next_instance_id++;
    host->setInstanceId(id);
    host->on_event_occured = _on_event_occured;

    _graph_processor.addNode(id, std::move(host));
    pushNewState();

    log("[ModuleRouter] Created plugin ID: ", id);
    return id;
}

auto ModuleRouter::createCompositeInstance(const CompositeConfig& config) -> uint32_t {
    log("[ModuleRouter] Creating composite instance.");
    auto composite = std::make_unique<CompositeNode>();

    if (!composite->load(config)) {
        log("[ModuleRouter] Failed to load composite configuration.");
        return 0;
    }

    uint32_t id = _next_instance_id++;
    composite->setInstanceId(id);
    composite->on_event_occured = _on_event_occured;

    _graph_processor.addNode(id, std::move(composite));
    pushNewState();

    log("[ModuleRouter] Created composite ID: ", id);
    return id;
}

void ModuleRouter::destroyInstance(uint32_t instance_id) {
    log("[ModuleRouter] Destroying instance: ", instance_id);
    auto node = _graph_processor.removeNode(instance_id);
    if (node) {
        _pending_deletion_nodes.push_back(std::move(node));
        pushNewState();
    }
}

auto ModuleRouter::registerSpecialNode(const std::string& type) -> uint32_t {
    uint32_t id;
    if (type == "audio_out") {
        id = constants::kAudioOutputNoteId;
    } else {
        id = _next_instance_id++;
    }

    if (type == "midi_input") {
        auto node = std::make_unique<MidiInputNode>();
        node->setInstanceId(id);
        node->on_event_occured = _on_event_occured;
        _graph_processor.addNode(id, std::move(node));
        pushNewState();
    }

    log("[ModuleRouter] Registered special node ID: ", id, " Type: ", type);
    return id;
}

auto ModuleRouter::getProcessingNode(uint32_t instance_id) const -> ProcessingNode* {
    return _graph_processor.getNode(instance_id);
}

auto ModuleRouter::getProcessOrder() const -> const std::vector<uint32_t>& {
    return _graph_processor.getProcessOrder();
}

auto ModuleRouter::getConnections() const -> const std::vector<PortConnection>& {
    return _graph_processor.getConnections();
}

void ModuleRouter::pollAllMainThreads() {
    pollResources();
    for (uint32_t id : _graph_processor.getProcessOrder()) {
        if (auto* node = _graph_processor.getNode(id)) {
            node->pollMainThread();
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
        _pending_deletion_nodes.clear();
    }
}

void ModuleRouter::setEventCallback(std::function<void(uint32_t, const PluginEvent&)> cb) {
    _on_event_occured = cb;
    for (uint32_t id : _graph_processor.getProcessOrder()) {
        if (auto* node = _graph_processor.getNode(id)) {
            node->on_event_occured = cb;
        }
    }
}

void ModuleRouter::pushNewState() {
    auto new_state = _graph_processor.createRenderState(constants::kAudioOutputNoteId);
    if (!pending_states.enqueue(std::move(new_state))) {
        log("[ModuleRouter] ERROR: Failed to enqueue new render state.");
    }
}

void ModuleRouter::connectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                                uint32_t to_port, ConnectionType type) {
    if (getConnectionCount(to_node, to_port, type) >= constants::kMaxConnectionsPerPort) {
        log("[ModuleRouter] ERROR: Cannot connect. Max connections reached for target port.");
        return;
    }

    _graph_processor.connect({.from_node = from_node,
                              .from_port = from_port,
                              .to_node = to_node,
                              .to_port = to_port,
                              .type = type});
    pushNewState();
}

void ModuleRouter::disconnectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                                   uint32_t to_port, ConnectionType type) {
    _graph_processor.disconnect({.from_node = from_node,
                                 .from_port = from_port,
                                 .to_node = to_node,
                                 .to_port = to_port,
                                 .type = type});
    pushNewState();
}

void ModuleRouter::activateNode(uint32_t instance_id, int32_t sample_rate,
                                int32_t frames_per_block) {
    if (auto* node = _graph_processor.getNode(instance_id)) {
        if (!node->isActive()) {
            node->activate(sample_rate, frames_per_block);
        }
    }
}

void ModuleRouter::deactivateNode(uint32_t instance_id) {
    if (auto* node = _graph_processor.getNode(instance_id)) {
        if (node->isActive()) {
            node->deactivate();
        }
    }
}

auto ModuleRouter::getConnectionCount(uint32_t to_node, uint32_t to_port, ConnectionType type) const
    -> size_t {
    const auto& connections = _graph_processor.getConnections();
    size_t count = 0;
    for (const auto& conn : connections) {
        if (conn.to_node == to_node && conn.to_port == to_port && conn.type == type) {
            count++;
        }
    }
    return count;
}

}  // namespace synth_canvas::host
