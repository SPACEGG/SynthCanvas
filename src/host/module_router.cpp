#include "module_router.h"

#include "composite_node.h"
#include "constants.h"
#include "envelope_node.h"
#include "lfo_node.h"
#include "logger.h"
#include "midi_input_node.h"
#include "plugin_host.h"
#include "sequencer_node.h"
#include "stepper_node.h"
#include "transport_node.h"
#include "utils/base64.h"
#include "utils/json_converters.h"

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

auto ModuleRouter::createPluginInstance(const std::string& path, uint32_t forced_id) -> uint32_t {
    log("[ModuleRouter] Creating plugin instance from path: ", path);
    auto host = std::make_unique<PluginHost>();
    if (!host->load(path, 0)) {
        log("[ModuleRouter] Failed to load plugin: ", path);
        return 0;
    }

    uint32_t id;
    if (forced_id != constants::kClapInvalidId) {
        id = forced_id;
        uint32_t current = _next_instance_id.load();
        if (id >= current) {
            _next_instance_id.store(id + 1);
        }
    } else {
        id = _next_instance_id++;
    }

    host->setInstanceId(id);
    host->on_event_occured = _on_event_occured;

    _graph_processor.addNode(id, std::move(host));
    pushNewState();

    log("[ModuleRouter] Created plugin ID: ", id);
    return id;
}

auto ModuleRouter::createCompositeInstance(const CompositeConfig& config, uint32_t forced_id)
    -> uint32_t {
    log("[ModuleRouter] Creating composite instance.");
    auto composite = std::make_unique<CompositeNode>();

    if (!composite->load(config)) {
        log("[ModuleRouter] Failed to load composite configuration.");
        return 0;
    }

    uint32_t id;
    if (forced_id != constants::kClapInvalidId) {
        id = forced_id;
        uint32_t current = _next_instance_id.load();
        if (id >= current) {
            _next_instance_id.store(id + 1);
        }
    } else {
        id = _next_instance_id++;
    }

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

auto ModuleRouter::registerSpecialNode(const std::string& type, uint32_t forced_id) -> uint32_t {
    uint32_t id;
    if (type == "audio_out") {
        id = constants::kAudioOutputNoteId;
    } else if (forced_id != constants::kClapInvalidId) {
        id = forced_id;
        uint32_t current = _next_instance_id.load();
        if (id >= current) {
            _next_instance_id.store(id + 1);
        }
    } else {
        id = _next_instance_id++;
    }

    std::unique_ptr<InternalNodeBase> node;
    if (type == "midi_input") {
        node = std::make_unique<MidiInputNode>();
    } else if (type == "lfo") {
        node = std::make_unique<LFONode>();
    } else if (type == "envelope") {
        node = std::make_unique<EnvelopeNode>();
    } else if (type == "stepper") {
        node = std::make_unique<StepperNode>();
    } else if (type == "sequencer") {
        node = std::make_unique<SequencerNode>();
    } else if (type == "event_tunnel") {
        node = std::make_unique<EventTunnelNode>();
    } else if (type == "transport") {
        auto t_node = std::make_unique<TransportNode>();
        t_node->on_transport_change_requested = [this](double tempo, bool playing) {
            setTempo(tempo);
            setTransportPlaying(playing);
            pushNewState();
        };
        node = std::move(t_node);
    }

    if (node) {
        node->setInstanceId(id);
        node->on_event_occured = _on_event_occured;
        _graph_processor.addNode(id, std::move(node));
        pushNewState();
    }

    log("[ModuleRouter] Registered special node ID: ", id, " Type: ", type);
    return id;
}

void ModuleRouter::clearGraph() {
    log("[ModuleRouter] Clearing graph.");
    auto node_ids = _graph_processor.getProcessOrder();
    for (uint32_t id : node_ids) {
        if (id == constants::kAudioOutputNoteId) continue;
        auto node = _graph_processor.removeNode(id);
        if (node) {
            _pending_deletion_nodes.push_back(std::move(node));
        }
    }
    _next_instance_id.store(constants::kInitialPluginInstanceId);
    pushNewState();
}

auto ModuleRouter::serializeGraph() const -> std::string {
    nlohmann::json j;
    j["version"] = "1.0";
    j["transport"] = _main_transport;

    auto nodes_j = nlohmann::json::array();
    auto node_ids = _graph_processor.getProcessOrder();
    for (uint32_t id : node_ids) {
        if (id == constants::kAudioOutputNoteId) continue;
        if (auto* node = _graph_processor.getNode(id)) {
            nlohmann::json node_j;
            node_j["instance_id"] = id;
            node_j["type"] = node->getNodeType();
            node_j["creation_info"] = node->getCreationInfo();

            std::vector<uint8_t> state;
            if (node->saveState(state)) {
                node_j["state_data"] = utils::base64Encode(state);
            } else {
                node_j["state_data"] = "";
            }
            nodes_j.push_back(node_j);
        }
    }
    j["nodes"] = nodes_j;

    auto conns_j = nlohmann::json::array();
    for (const auto& conn : _graph_processor.getConnections()) {
        nlohmann::json conn_j;
        conn_j["from_node"] = conn.from_node;
        conn_j["from_port"] = conn.from_port;
        conn_j["to_node"] = conn.to_node;
        conn_j["to_port"] = conn.to_port;
        conn_j["type"] = static_cast<int>(conn.type);
        conn_j["scale"] = conn.scale;
        conn_j["bypass"] = conn.bypass;
        conns_j.push_back(conn_j);
    }
    j["connections"] = conns_j;

    return j.dump();
}

auto ModuleRouter::deserializeNodes(const std::string& json_str) -> bool {
    try {
        auto j = nlohmann::json::parse(json_str);
        clearGraph();

        if (j.contains("transport")) {
            _main_transport = j["transport"].get<TransportState>();
        }

        std::map<uint32_t, std::string> node_states;

        if (j.contains("nodes")) {
            for (const auto& node_j : j["nodes"]) {
                uint32_t id = node_j["instance_id"];
                std::string type = node_j["type"];
                std::string creation_info = node_j["creation_info"];
                std::string state_b64 = node_j.value("state_data", "");

                uint32_t created_id = 0;
                if (type == "plugin") {
                    created_id = createPluginInstance(creation_info, id);
                } else if (type == "special") {
                    created_id = registerSpecialNode(creation_info, id);
                } else if (type == "composite") {
                    try {
                        auto cfg_j = nlohmann::json::parse(creation_info);
                        created_id = createCompositeInstance(cfg_j.get<CompositeConfig>(), id);
                    } catch (...) {
                        log("[ModuleRouter] ERROR: Failed to parse composite config for node ", id);
                    }
                }

                if (created_id != 0 && !state_b64.empty()) {
                    node_states[created_id] = state_b64;
                }
            }
        }

        // Apply states after all nodes are created
        for (const auto& [id, state_b64] : node_states) {
            if (auto* node = _graph_processor.getNode(id)) {
                node->loadState(utils::base64Decode(state_b64));
            }
        }

        pushNewState();
        return true;
    } catch (const std::exception& e) {
        log("[ModuleRouter] ERROR: deserializeNodes failed: ", e.what());
        return false;
    }
}

auto ModuleRouter::deserializeConnections(const std::string& json_str) -> bool {
    try {
        auto j = nlohmann::json::parse(json_str);
        if (j.contains("connections")) {
            for (const auto& conn_j : j["connections"]) {
                uint32_t from = conn_j["from_node"];
                uint32_t to = conn_j["to_node"];

                // Allow connections to the virtual speaker node (ID 0)
                bool is_to_speaker = (to == constants::kAudioOutputNoteId);

                if (_graph_processor.getNode(from) &&
                    (is_to_speaker || _graph_processor.getNode(to))) {
                    auto type = static_cast<ConnectionType>(conn_j["type"].get<int>());
                    float scale = conn_j.value("scale", 1.0f);
                    bool bypass = conn_j.value("bypass", false);

                    connectNodes(from, conn_j["from_port"], to, conn_j["to_port"], type);
                    updateConnection(from, conn_j["from_port"], to, conn_j["to_port"], type, scale,
                                     bypass);
                } else {
                    log("[ModuleRouter] Skipping connection ", from, "->", to,
                        " (One or more nodes missing)");
                }
            }
        }
        pushNewState();
        return true;
    } catch (const std::exception& e) {
        log("[ModuleRouter] ERROR: deserializeConnections failed: ", e.what());
        return false;
    }
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
        // Sync transport state from the audio thread to main thread
        _main_transport.song_pos_beats = old_state->transport.song_pos_beats;
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
    // Sync transport state from the main thread to audio thread
    new_state->transport = _main_transport;
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

void ModuleRouter::updateConnection(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                                    uint32_t to_port, ConnectionType type, float scale,
                                    bool bypass) {
    if (_graph_processor.setConnectionProperties({.from_node = from_node,
                                                  .from_port = from_port,
                                                  .to_node = to_node,
                                                  .to_port = to_port,
                                                  .type = type},
                                                 scale, bypass)) {
        pushNewState();
    }
}

auto ModuleRouter::getConnectionProperties(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                                           uint32_t to_port, ConnectionType type, float& out_scale,
                                           bool& out_bypass) const -> bool {
    const auto& connections = _graph_processor.getConnections();
    for (const auto& conn : connections) {
        if (conn.from_node == from_node && conn.from_port == from_port && conn.to_node == to_node &&
            conn.to_port == to_port && conn.type == type) {
            out_scale = conn.scale;
            out_bypass = conn.bypass;
            return true;
        }
    }
    return false;
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

void ModuleRouter::updateNodePorts(uint32_t instance_id) {
    auto* node = _graph_processor.getNode(instance_id);
    if (!node) return;

    auto input_count = static_cast<uint32_t>(node->getAudioPorts(true).size());
    auto output_count = static_cast<uint32_t>(node->getAudioPorts(false).size());

    // Prune connections that refer to ports that no longer exist
    auto connections = _graph_processor.getConnections();
    bool changed = false;

    // Use a copy of connections to iterate since we might modify the list
    for (const auto& conn : connections) {
        bool should_disconnect = false;
        if (conn.from_node == instance_id && conn.from_port >= output_count) {
            should_disconnect = true;
        } else if (conn.to_node == instance_id && conn.to_port >= input_count) {
            should_disconnect = true;
        }

        if (should_disconnect) {
            _graph_processor.disconnect(conn);
            changed = true;
            if (on_connection_pruned) {
                on_connection_pruned(conn);
            }
        }
    }

    if (on_node_ports_changed) {
        on_node_ports_changed(instance_id, input_count, output_count);
    }

    pushNewState();
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
