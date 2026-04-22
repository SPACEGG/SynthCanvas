#include "composite_node.h"

#include <cstring>
#include <map>

#include "constants.h"
#include "logger.h"
#include "plugin_host.h"

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

void CompositeNode::process() {
    if (!_is_active || !_current_state) return;
    int32_t num_frames = _current_num_frames;

    _internal_buffers.prepareBlock();

    // 1. Prepare Proxies with current block's external buffers
    if (_input_proxy_node) {
        _input_proxy_node->setExternalBuffers(_ext_inputs, _ext_input_count);
    }
    if (_output_proxy_node) {
        _output_proxy_node->setExternalBuffers(_ext_outputs, _ext_output_count);
    }

    // 2. Render Internal Graph (Proxies will handle data movement during process())
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
    if (param_id < _external_params.size()) {
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
            if (idx < _external_params.size()) {
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

auto CompositeNode::getParameterBaseValue(clap_id param_id) const -> double {
    auto target = getInternalParameterTarget(param_id);
    if (target.node) {
        return target.node->getParameterBaseValue(target.internal_id);
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

auto CompositeNode::popOutputEvent(PluginEvent& out_event) -> bool {
    return _output_proxy_node ? _output_proxy_node->popOutputEvent(out_event) : false;
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
    log("[CompositeNode] Loading configuration.");

    // 1. Register Proxy Nodes (Ownership is moved to processor)
    if (_input_proxy_node_owned) {
        _internal_processor.addNode(kInputProxyId, std::move(_input_proxy_node_owned));
    }
    if (_output_proxy_node_owned) {
        _internal_processor.addNode(kOutputProxyId, std::move(_output_proxy_node_owned));
    }

    std::map<std::string, uint32_t> alias_to_id;
    _external_inputs.clear();
    _external_outputs.clear();
    _external_params.clear();
    _param_id_to_external_index.clear();

    // Load Internal Plugins
    uint32_t next_internal_id = 1;
    for (const auto& p_cfg : config.plugins) {
        auto host = std::make_unique<PluginHost>();
        if (host->load(p_cfg.plugin_path, 0)) {
            uint32_t id = next_internal_id++;
            host->setInstanceId(id);
            alias_to_id[p_cfg.alias] = id;

            // Intercept internal events for bubbling up and parameter ID translation
            host->on_event_occured = [this](uint32_t internal_id, const PluginEvent& ev) {
                this->handleInternalEvent(internal_id, ev);
            };

            addInternalNode(id, std::move(host));
        } else {
            return false;
        }
    }

    // Connect Internal Nodes
    for (const auto& r_cfg : config.routings) {
        if (alias_to_id.count(r_cfg.from_node) && alias_to_id.count(r_cfg.to_node)) {
            connectInternal({.from_node = alias_to_id[r_cfg.from_node],
                             .from_port = r_cfg.from_port,
                             .to_node = alias_to_id[r_cfg.to_node],
                             .to_port = r_cfg.to_port,
                             .type = r_cfg.type});
        }
    }

    // Setup External Proxies via Boundary Nodes
    std::vector<AudioPortInfo> input_proxy_outputs;
    for (const auto& i_cfg : config.input_proxies) {
        if (alias_to_id.count(i_cfg.internal_node)) {
            // InputProxyNode(OUT) -> Target Internal Node(IN)
            _internal_processor.connect({.from_node = kInputProxyId,
                                         .from_port = i_cfg.external_port_index,
                                         .to_node = alias_to_id[i_cfg.internal_node],
                                         .to_port = i_cfg.internal_port_index,
                                         .type = i_cfg.type});

            AudioPortInfo info;
            info.index = i_cfg.external_port_index;
            info.is_input = true;
            info.is_modulation = (i_cfg.type == ConnectionType::kModulation);
            info.clap_info.channel_count = constants::kDefaultChannelCount;
            _external_inputs.push_back(info);

            AudioPortInfo out_info = info;
            out_info.is_input = false;
            input_proxy_outputs.push_back(out_info);
        }
    }
    _input_proxy_node->setAudioPorts(false, input_proxy_outputs);

    std::vector<AudioPortInfo> output_proxy_inputs;
    for (const auto& o_cfg : config.output_proxies) {
        if (alias_to_id.count(o_cfg.internal_node)) {
            // Source Internal Node(OUT) -> OutputProxyNode(IN)
            _internal_processor.connect({.from_node = alias_to_id[o_cfg.internal_node],
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

    // Setup Parameter Mappings
    uint32_t external_param_idx = 0;
    _external_id_to_target.clear();
    _param_id_to_target.clear();

    for (const auto& m_cfg : config.parameter_mappings) {
        if (alias_to_id.count(m_cfg.target_node)) {
            uint32_t internal_id = alias_to_id[m_cfg.target_node];
            setParameterMapping(m_cfg.param_id, internal_id, m_cfg.target_param_index);
            _internal_processor.setDirectParameterMapping(external_param_idx, internal_id,
                                                          m_cfg.target_param_index);

            if (auto* target_node = _internal_processor.getNode(internal_id)) {
                // Populate thread-safe maps for metadata access
                ParameterTarget target = {.node = target_node,
                                          .internal_id = m_cfg.target_param_index};
                _param_id_to_target[m_cfg.param_id] = target;
                _external_id_to_target[external_param_idx] = target;

                if (auto* src_slot = target_node->getParameterSlot(m_cfg.target_param_index)) {
                    auto ext_slot = std::make_unique<ParameterSlot>();
                    ext_slot->info = src_slot->info;
                    std::strncpy(ext_slot->info.name, m_cfg.param_id.c_str(), CLAP_NAME_SIZE);
                    ext_slot->info.id = external_param_idx++;
                    ext_slot->base_value.store(src_slot->base_value.load());
                    _param_id_to_external_index[m_cfg.param_id] = ext_slot->info.id;
                    _external_params.push_back(std::move(ext_slot));
                }
            }
        }
    }

    pushInternalState();
    return true;
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
