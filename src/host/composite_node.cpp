#include "composite_node.h"

#include <cstring>
#include <map>

#include "constants.h"
#include "logger.h"
#include "plugin_host.h"
#include "utils/json_converters.h"

namespace synth_canvas::host {

constexpr uint32_t kInputProxyId = 0xFFFFFFFE;
constexpr uint32_t kOutputProxyId = 0xFFFFFFFF;

CompositeNode::CompositeNode()
    : _pending_states(constants::kSnapshotQueueSize),
      _released_states(constants::kSnapshotQueueSize) {
    _current_state = std::make_unique<GraphProcessor::RenderState>();
    _input_proxy_node_owned = std::make_unique<BoundaryNode>(BoundaryNode::Type::kInputProxy);
    _input_proxy_node = _input_proxy_node_owned.get();
    _output_proxy_node_owned = std::make_unique<BoundaryNode>(BoundaryNode::Type::kOutputProxy);
    _output_proxy_node = _output_proxy_node_owned.get();
}

CompositeNode::~CompositeNode() {
    _is_active = false;
    std::unique_ptr<GraphProcessor::RenderState> state;
    while (_pending_states.try_dequeue(state)) {
    }
    while (_released_states.try_dequeue(state)) {
    }
}

auto CompositeNode::getCreationInfo() const -> std::string {
    nlohmann::json j = _config;
    return j.dump();
}

void CompositeNode::activate(int32_t sample_rate, int32_t block_size) {
    _sample_rate = sample_rate;
    _block_size = block_size;

    if (_input_proxy_node) _input_proxy_node->activate(sample_rate, block_size);
    if (_output_proxy_node) _output_proxy_node->activate(sample_rate, block_size);

    for (uint32_t id : _internal_processor.getProcessOrder()) {
        if (auto* node = _internal_processor.getNode(id)) {
            node->activate(sample_rate, block_size);
        }
    }

    _internal_buffers.resize(constants::kDefaultChannelCount, block_size);
    _is_active = true;

    reserveOutputBuffers(static_cast<uint32_t>(_external_outputs.size()));
}

void CompositeNode::deactivate() {
    _is_active = false;
    if (_input_proxy_node) _input_proxy_node->deactivate();
    if (_output_proxy_node) _output_proxy_node->deactivate();
    for (uint32_t id : _internal_processor.getProcessOrder()) {
        if (auto* node = _internal_processor.getNode(id)) {
            node->deactivate();
        }
    }
}

void CompositeNode::setProcessingEnabled(bool enabled) {
    for (uint32_t id : _internal_processor.getProcessOrder()) {
        if (auto* node = _internal_processor.getNode(id)) {
            node->setProcessingEnabled(enabled);
        }
    }
}

void CompositeNode::setTransport(const TransportState* transport) {
    _transport = transport;
    if (_current_state) {
        for (auto* node : _current_state->sorted_nodes) {
            if (node) node->setTransport(transport);
        }
    }
}

void CompositeNode::setPorts(uint32_t num_inputs, clap_audio_buffer* inputs, uint32_t num_outputs,
                             clap_audio_buffer* outputs) {
    _ext_inputs = inputs;
    _ext_input_count = num_inputs;
    _ext_outputs = outputs;
    _ext_output_count = num_outputs;
}

void CompositeNode::processBegin(int num_frames) {
    updateInternalRenderState();
    _current_num_frames = num_frames;
    if (_input_proxy_node) _input_proxy_node->processBegin(num_frames);
    if (_output_proxy_node) _output_proxy_node->processBegin(num_frames);
}

void CompositeNode::processEvents(int num_frames) {
    if (!_is_active || !_current_state) return;

    // Defer to GraphRenderer's renderEvents once we implement it
    _renderer.renderEvents(*_current_state, _internal_buffers, num_frames, nullptr);
}

void CompositeNode::process() {
    if (!_is_active || !_current_state) return;
    int32_t num_frames = _current_num_frames;

    _internal_buffers.prepareBlock();

    // Prepare Proxies with current block's external buffers
    if (_input_proxy_node) {
        _input_proxy_node->setExternalBuffers(_ext_inputs, _ext_input_count);
    }
    if (_output_proxy_node) {
        _output_proxy_node->setExternalBuffers(_ext_outputs, _ext_output_count);
    }

    // Render Internal Graph (Proxies will handle data movement during process())
    _renderer.render(*_current_state, _internal_buffers, num_frames, nullptr);
}

void CompositeNode::processEnd(int num_frames) {
    if (_input_proxy_node) _input_proxy_node->processEnd(num_frames);
    if (_output_proxy_node) _output_proxy_node->processEnd(num_frames);
}

void CompositeNode::updateInternalRenderState() {
    std::unique_ptr<GraphProcessor::RenderState> new_state;
    while (_pending_states.try_dequeue(new_state)) {
        std::vector<uint32_t> port_counts;
        for (auto* node : new_state->sorted_nodes) {
            port_counts.push_back(static_cast<uint32_t>(node->getAudioPorts(true).size()));
        }
        _internal_buffers.reserveInputMixBuffers(port_counts);

        if (_current_state) _released_states.enqueue(std::move(_current_state));
        _current_state = std::move(new_state);
    }
}

void CompositeNode::handleInternalEvent(uint32_t internal_id, const PluginEvent& ev) {
    if (!_current_state) return;

    PluginEvent mapped_ev = ev;

    if (ev.event.header.type == CLAP_EVENT_PARAM_VALUE) {
        for (const auto& [ext_id, mapping] : _current_state->parameter_mappings) {
            if (mapping.target_param_id == ev.event.param_value.param_id) {
                ProcessingNode* target_node =
                    _current_state->sorted_nodes[mapping.target_node_index];
                if (target_node && target_node->getInstanceId() == internal_id) {
                    for (size_t i = 0; i < _current_state->direct_parameter_mappings.size(); ++i) {
                        const auto& d_mapping = _current_state->direct_parameter_mappings[i];
                        if (d_mapping.target_param_id == ev.event.param_value.param_id) {
                            ProcessingNode* d_node =
                                _current_state->sorted_nodes[d_mapping.target_node_index];
                            if (d_node && d_node->getInstanceId() == internal_id) {
                                mapped_ev.event.param_value.param_id = static_cast<clap_id>(i);

                                // Update cached external slot state
                                if (i < _external_params.size()) {
                                    _external_params[i]->base_value.store(
                                        ev.event.param_value.value, std::memory_order_relaxed);
                                    _external_params[i]->current_value.store(
                                        ev.event.param_value.value, std::memory_order_relaxed);
                                }

                                goto dispatch;
                            }
                        }
                    }
                }
            }
        }
        // Ignore unmapped(private internal) event
        return;
    }

dispatch:
    if (on_event_occured) {
        on_event_occured(_instance_id, mapped_ev);
    }
}

void CompositeNode::setParameterValue(clap_id param_id, double value) {
    if (param_id < _external_params.size() && _external_params[param_id]) {
        _external_params[param_id]->base_value.store(value, std::memory_order_relaxed);
        _external_params[param_id]->current_value.store(value, std::memory_order_relaxed);
    }

    auto target = getInternalParameterTarget(param_id);
    if (target.node) {
        target.node->setParameterValue(target.internal_id, value);
    }
}

void CompositeNode::setParameterValue(const std::string& param_id, double value) {
    auto it = _current_state->parameter_mappings.find(param_id);
    if (it != _current_state->parameter_mappings.end()) {
        auto ext_it = _param_id_to_external_index.find(param_id);
        if (ext_it != _param_id_to_external_index.end()) {
            uint32_t idx = ext_it->second;
            if (idx < _external_params.size() && _external_params[idx]) {
                _external_params[idx]->base_value.store(value, std::memory_order_relaxed);
                _external_params[idx]->current_value.store(value, std::memory_order_relaxed);
            }
        }

        auto* node = _current_state->sorted_nodes[it->second.target_node_index];
        if (node) {
            node->setParameterValue(it->second.target_param_id, value);
        }
    }
}

void CompositeNode::applyModulation(clap_id param_id, double value, uint32_t sample_offset) {
    auto target = getInternalParameterTarget(param_id);
    if (target.node) {
        target.node->applyModulation(target.internal_id, value, sample_offset);
    }
}

auto CompositeNode::saveState(std::vector<uint8_t>& data) -> bool {
    auto node_ids = _internal_processor.getProcessOrder();
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> states;

    for (uint32_t id : node_ids) {
        if (id == kInputProxyId || id == kOutputProxyId) continue;
        if (auto* node = _internal_processor.getNode(id)) {
            std::vector<uint8_t> node_data;
            if (node->saveState(node_data) && !node_data.empty()) {
                states.emplace_back(id, std::move(node_data));
            }
        }
    }

    if (states.empty()) return true;

    // Return directly if internal node is unique.
    if (states.size() == 1) {
        data = std::move(states[0].second);
        return true;
    }

    // Multi-node state: Package with "COMP" magic header.
    const char* magic = "COMP";
    data.insert(data.end(), magic, magic + 4);

    auto node_count = static_cast<uint32_t>(states.size());
    const auto* p_count = reinterpret_cast<const uint8_t*>(&node_count);
    data.insert(data.end(), p_count, p_count + sizeof(uint32_t));

    for (const auto& [id, node_data] : states) {
        const auto* p_id = reinterpret_cast<const uint8_t*>(&id);
        data.insert(data.end(), p_id, p_id + sizeof(uint32_t));

        auto size = static_cast<uint32_t>(node_data.size());
        const auto* p_size = reinterpret_cast<const uint8_t*>(&size);
        data.insert(data.end(), p_size, p_size + sizeof(uint32_t));

        data.insert(data.end(), node_data.begin(), node_data.end());
    }

    return true;
}

auto CompositeNode::loadState(const std::vector<uint8_t>& data) -> bool {
    if (data.empty()) return false;

    // Detect if this is a structured Composite state using the "COMP" magic header.
    bool is_structured = (data.size() >= 8 && std::memcmp(data.data(), "COMP", 4) == 0);

    if (is_structured) {
        uint32_t node_count = 0;
        std::memcpy(&node_count, data.data() + 4, sizeof(uint32_t));
        size_t offset = 8;

        for (uint32_t i = 0; i < node_count; ++i) {
            if (offset + sizeof(uint32_t) * 2 > data.size()) return false;

            uint32_t id = 0;
            std::memcpy(&id, data.data() + offset, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            uint32_t size = 0;
            std::memcpy(&size, data.data() + offset, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            if (offset + size > data.size()) return false;

            if (auto* node = _internal_processor.getNode(id)) {
                std::vector<uint8_t> node_data(data.begin() + offset, data.begin() + offset + size);
                node->loadState(node_data);
            }
            offset += size;
        }
    } else {
        // Broadcast / Transparent Forwarding:
        // If data is not structured (e.g. a raw command string "path=...;"),
        // forward it to all internal nodes.
        log("[CompositeNode] Non-structured state detected, broadcasting to internal nodes.");
        for (uint32_t id : _internal_processor.getProcessOrder()) {
            if (id == kInputProxyId || id == kOutputProxyId) continue;
            if (auto* node = _internal_processor.getNode(id)) {
                node->loadState(data);
            }
        }
    }

    return true;
}

auto CompositeNode::getParameterBaseValue(clap_id param_id) const -> double {
    auto target = getInternalParameterTarget(param_id);
    if (target.node) {
        return target.node->getParameterBaseValue(target.internal_id);
    }
    return 0.0;
}

auto CompositeNode::getParameterCurrentValue(clap_id param_id) const -> double {
    auto target = getInternalParameterTarget(param_id);
    if (target.node) {
        return target.node->getParameterCurrentValue(target.internal_id);
    }
    return 0.0;
}

auto CompositeNode::getParameterModulationOffset(clap_id param_id) const -> double {
    auto target = getInternalParameterTarget(param_id);
    if (target.node) {
        return target.node->getParameterModulationOffset(target.internal_id);
    }
    return 0.0;
}

void CompositeNode::queueEvent(const PluginEvent& event) {
    if (_input_proxy_node) {
        _input_proxy_node->queueEvent(event);
    }
}

auto CompositeNode::popOutputEvent(uint32_t port_index, PluginEvent& out_event) -> bool {
    return _output_proxy_node ? _output_proxy_node->popOutputEvent(port_index, out_event) : false;
}

void CompositeNode::pollMainThread() {
    std::unique_ptr<GraphProcessor::RenderState> old_state;
    while (_released_states.try_dequeue(old_state)) {
    }

    for (uint32_t id : _internal_processor.getProcessOrder()) {
        if (auto* node = _internal_processor.getNode(id)) {
            node->pollMainThread();
        }
    }
}

auto CompositeNode::load(const CompositeConfig& config) -> bool {
    _config = config;
    setupBoundaryNodes();

    std::map<std::string, uint32_t> alias_to_id;
    _external_inputs.clear();
    _external_outputs.clear();
    _external_params.clear();
    _param_id_to_external_index.clear();

    if (!loadInternalPlugins(config, alias_to_id)) {
        return false;
    }

    setupInternalRoutings(config, alias_to_id);
    setupInputProxies(config, alias_to_id);
    setupOutputProxies(config, alias_to_id);
    setupParameterMappings(config, alias_to_id);

    pushInternalState();
    return true;
}

void CompositeNode::setupBoundaryNodes() {
    if (_input_proxy_node_owned) {
        _internal_processor.addNode(kInputProxyId, std::move(_input_proxy_node_owned));
    }
    if (_output_proxy_node_owned) {
        _internal_processor.addNode(kOutputProxyId, std::move(_output_proxy_node_owned));
    }
}

auto CompositeNode::loadInternalPlugins(const CompositeConfig& config,
                                        std::map<std::string, uint32_t>& out_alias_to_id) -> bool {
    uint32_t next_internal_id = 1;
    for (const auto& p_cfg : config.plugins) {
        auto host = std::make_unique<PluginHost>();
        if (host->load(p_cfg.plugin_path, 0)) {
            uint32_t id = next_internal_id++;
            host->setInstanceId(id);
            out_alias_to_id[p_cfg.alias] = id;

            // Intercept internal events for bubbling up and parameter ID translation
            host->on_event_occured = [this](uint32_t internal_id, const PluginEvent& ev) {
                this->handleInternalEvent(internal_id, ev);
            };

            // Bubble up parameter rescan signal
            host->on_params_rescan = [this](uint32_t internal_id) {
                this->refreshParameterMetadata();
                if (this->on_params_rescan) {
                    this->on_params_rescan(this->_instance_id);
                }
            };

            addInternalNode(id, std::move(host));
        } else {
            return false;
        }
    }
    return true;
}

void CompositeNode::refreshParameterMetadata() {
    // Loop through all mapped parameters and refresh their metadata from the actual internal node.
    for (auto& [param_id, target] : _param_id_to_target) {
        auto ext_it = _param_id_to_external_index.find(param_id);
        if (ext_it != _param_id_to_external_index.end()) {
            uint32_t ext_idx = ext_it->second;
            if (ext_idx < _external_params.size()) {
                if (auto* src_slot = target.node->getParameterSlot(target.internal_id)) {
                    // Update the cached metadata info (especially min/max/default)
                    _external_params[ext_idx]->info = src_slot->info;
                    // Restore the external parameter name (which is the param_id)
                    std::strncpy(_external_params[ext_idx]->info.name, param_id.c_str(),
                                 CLAP_NAME_SIZE);
                }
            }
        }
    }
}

void CompositeNode::setupInternalRoutings(const CompositeConfig& config,
                                          const std::map<std::string, uint32_t>& alias_to_id) {
    for (const auto& r_cfg : config.routings) {
        auto from_it = alias_to_id.find(r_cfg.from_node);
        auto to_it = alias_to_id.find(r_cfg.to_node);

        if (from_it != alias_to_id.end() && to_it != alias_to_id.end()) {
            connectInternal({.from_node = from_it->second,
                             .from_port = r_cfg.from_port,
                             .to_node = to_it->second,
                             .to_port = r_cfg.to_port,
                             .type = r_cfg.type});
        }
    }
}

void CompositeNode::setupInputProxies(const CompositeConfig& config,
                                      const std::map<std::string, uint32_t>& alias_to_id) {
    std::vector<AudioPortInfo> input_proxy_outputs;
    for (const auto& i_cfg : config.input_proxies) {
        auto it = alias_to_id.find(i_cfg.internal_node);
        if (it != alias_to_id.end()) {
            AudioPortInfo info;
            info.index = i_cfg.external_port_index;
            info.is_input = true;
            info.is_modulation = (i_cfg.type == ConnectionType::kModulation);
            info.target_param_id = i_cfg.target_param_id;
            info.clap_info.channel_count = constants::kDefaultChannelCount;
            _external_inputs.push_back(info);

            // Conditional internal connection: Skip if it's a direct parameter modulation
            if (!(i_cfg.type == ConnectionType::kModulation &&
                  i_cfg.target_param_id != host::constants::kClapInvalidId)) {
                // InputProxyNode(OUT) -> Target Internal Node(IN)
                _internal_processor.connect({.from_node = kInputProxyId,
                                             .from_port = i_cfg.external_port_index,
                                             .to_node = it->second,
                                             .to_port = i_cfg.internal_port_index,
                                             .type = i_cfg.type});
            } else {
                log("[CompositeNode] Modulation port ", i_cfg.external_port_index,
                    " mapped directly to param: ", i_cfg.target_param_id);
            }

            AudioPortInfo out_info = info;
            out_info.is_input = false;
            input_proxy_outputs.push_back(out_info);
        }
    }
    _input_proxy_node->setAudioPorts(false, input_proxy_outputs);
}

void CompositeNode::setupOutputProxies(const CompositeConfig& config,
                                       const std::map<std::string, uint32_t>& alias_to_id) {
    std::vector<AudioPortInfo> output_proxy_inputs;
    for (const auto& o_cfg : config.output_proxies) {
        auto it = alias_to_id.find(o_cfg.internal_node);
        if (it != alias_to_id.end()) {
            // Source Internal Node(OUT) -> OutputProxyNode(IN)
            _internal_processor.connect({.from_node = it->second,
                                         .from_port = o_cfg.internal_port_index,
                                         .to_node = kOutputProxyId,
                                         .to_port = o_cfg.external_port_index,
                                         .type = o_cfg.type});

            AudioPortInfo info;
            info.index = o_cfg.external_port_index;
            info.is_input = false;
            info.is_modulation = (o_cfg.type == ConnectionType::kModulation);
            info.clap_info.channel_count = constants::kDefaultChannelCount;
            _external_outputs.push_back(info);

            AudioPortInfo in_info = info;
            in_info.is_input = true;
            output_proxy_inputs.push_back(in_info);
        }
    }
    _output_proxy_node->setAudioPorts(true, output_proxy_inputs);
}

void CompositeNode::setupParameterMappings(const CompositeConfig& config,
                                           const std::map<std::string, uint32_t>& alias_to_id) {
    _external_id_to_target.clear();
    _param_id_to_target.clear();
    uint32_t external_param_idx = 0;

    for (const auto& m_cfg : config.parameter_mappings) {
        auto it = alias_to_id.find(m_cfg.target_node);
        if (it != alias_to_id.end()) {
            uint32_t internal_id = it->second;
            uint32_t external_id = external_param_idx++;

            setParameterMapping(m_cfg.param_id, internal_id, m_cfg.target_param_index);
            _internal_processor.setDirectParameterMapping(external_id, internal_id,
                                                          m_cfg.target_param_index);

            if (auto* target_node = _internal_processor.getNode(internal_id)) {
                // Populate thread-safe maps for metadata access
                ParameterTarget target = {.node = target_node,
                                          .internal_id = m_cfg.target_param_index};
                _param_id_to_target[m_cfg.param_id] = target;
                _external_id_to_target[external_id] = target;

                if (auto* src_slot = target_node->getParameterSlot(m_cfg.target_param_index)) {
                    auto ext_slot = std::make_unique<ParameterSlot>();
                    ext_slot->info = src_slot->info;
                    std::strncpy(ext_slot->info.name, m_cfg.param_id.c_str(), CLAP_NAME_SIZE);
                    ext_slot->info.id = external_id;
                    ext_slot->base_value.store(src_slot->base_value.load());
                    _param_id_to_external_index[m_cfg.param_id] = external_id;

                    // Ensure _external_params is large enough for index-based access
                    if (external_id >= _external_params.size()) {
                        _external_params.resize(external_id + 1);
                    }
                    _external_params[external_id] = std::move(ext_slot);
                }
            }
        }
    }
}

void CompositeNode::addInternalNode(uint32_t id, std::unique_ptr<ProcessingNode> node) {
    if (_is_active) node->activate(_sample_rate, _block_size);
    _internal_processor.addNode(id, std::move(node));
}

void CompositeNode::connectInternal(const PortConnection& conn) {
    _internal_processor.connect(conn);
}

void CompositeNode::setParameterMapping(const std::string& param_id, uint32_t internal_node_id,
                                        uint32_t internal_param_id) {
    _internal_processor.setParameterMapping(param_id, internal_node_id, internal_param_id);
}

void CompositeNode::pushInternalState() {
    auto new_state = _internal_processor.createRenderState(0);
    _pending_states.enqueue(std::move(new_state));
}

auto CompositeNode::getAudioPorts(bool is_input) const -> const std::vector<AudioPortInfo>& {
    return is_input ? _external_inputs : _external_outputs;
}

auto CompositeNode::getParameters() const -> const std::vector<std::unique_ptr<ParameterSlot>>& {
    return _external_params;
}

auto CompositeNode::getParameterSlot(clap_id param_id) const -> const ParameterSlot* {
    if (param_id < _external_params.size()) {
        return _external_params[static_cast<int>(param_id)].get();
    }
    return nullptr;
}

auto CompositeNode::getParameterText(clap_id param_id, double value) const -> std::string {
    auto target = getInternalParameterTarget(param_id);
    if (target.node) {
        return target.node->getParameterText(target.internal_id, value);
    }
    return std::to_string(value);
}

auto CompositeNode::getParameterText(const std::string& param_id, double value) const
    -> std::string {
    auto it = _param_id_to_target.find(param_id);
    if (it != _param_id_to_target.end()) {
        if (it->second.node) {
            return it->second.node->getParameterText(it->second.internal_id, value);
        }
    }
    return std::to_string(value);
}

auto CompositeNode::isActive() const -> bool { return _is_active; }

auto CompositeNode::getInternalParameterTarget(clap_id external_id) const
    -> CompositeNode::ParameterTarget {
    auto it = _external_id_to_target.find(external_id);
    if (it != _external_id_to_target.end()) {
        return it->second;
    }
    return {.node = nullptr, .internal_id = 0};
}

auto CompositeNode::getOutputBuffer(uint32_t port_idx) -> AudioBuffer* {
    if (port_idx < _output_buffers.size()) {
        return &_output_buffers[port_idx];
    }
    return nullptr;
}

void CompositeNode::reserveOutputBuffers(uint32_t count) {
    _output_buffers.resize(count);
    for (auto& buf : _output_buffers) {
        buf.owns_memory = true;
        if (_is_active) buf.resize(constants::kDefaultChannelCount, _block_size);
    }
}

}  // namespace synth_canvas::host
