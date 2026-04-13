#include "composite_node.h"

#include <cstring>
#include <map>

#include "constants.h"
#include "logger.h"
#include "plugin_host.h"

namespace synth_canvas::host {

CompositeNode::CompositeNode()
    : _pending_states(constants::kSnapshotQueueSize),
      _released_states(constants::kSnapshotQueueSize),
      _external_output_events(constants::kEventQueueSize) {
    _current_state = std::make_unique<GraphProcessor::RenderState>();
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
    _internal_buffers.resize(constants::kDefaultChannelCount, block_size);

    for (uint32_t id : _internal_processor.getProcessOrder()) {
        if (auto* node = _internal_processor.getNode(id)) {
            node->activate(sample_rate, block_size);
        }
    }
    _is_active = true;
}

void CompositeNode::deactivate() {
    _is_active = false;
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
    for (ProcessingNode* node : _current_state->sorted_nodes) {
        node->processBegin(num_frames);
    }
}

void CompositeNode::process() {
    if (!_is_active || !_current_state) return;
    int32_t num_frames = _block_size;

    _renderer.render(*_current_state, _internal_buffers, num_frames, 
        [this](ProcessingNode* source, const PluginEvent& ev, uint32_t ext_port) {
            PluginEvent proxy_ev = ev;
            
            switch (ev.event.header.type) {
                case CLAP_EVENT_NOTE_ON:
                case CLAP_EVENT_NOTE_OFF:
                case CLAP_EVENT_NOTE_CHOKE:
                case CLAP_EVENT_NOTE_EXPRESSION:
                    proxy_ev.event.note.port_index = static_cast<int16_t>(ext_port);
                    break;
                case CLAP_EVENT_PARAM_VALUE:
                    proxy_ev.event.param_value.port_index = static_cast<int16_t>(ext_port);
                    break;
                case CLAP_EVENT_PARAM_MOD:
                    proxy_ev.event.param_mod.port_index = static_cast<int16_t>(ext_port);
                    break;
                case CLAP_EVENT_MIDI:
                    proxy_ev.event.midi.port_index = static_cast<int16_t>(ext_port);
                    break;
            }
            
            this->_external_output_events.enqueue(proxy_ev);
        });
}

void CompositeNode::processEnd(int num_frames) {
    for (ProcessingNode* node : _current_state->sorted_nodes) {
        node->processEnd(num_frames);
    }
}

void CompositeNode::updateInternalRenderState() {
    std::unique_ptr<GraphProcessor::RenderState> new_state;
    while (_pending_states.try_dequeue(new_state)) {
        if (_current_state) _released_states.enqueue(std::move(_current_state));
        _current_state = std::move(new_state);
    }
}

void CompositeNode::setParameterValue(clap_id param_id, double value) {
    auto target = getInternalParameterTarget(param_id);
    if (target.node) {
        target.node->setParameterValue(target.internal_id, value);
    }
}

void CompositeNode::setParameterValue(const std::string& param_id, double value) {
    auto it = _current_state->parameter_mappings.find(param_id);
    if (it != _current_state->parameter_mappings.end()) {
        if (auto* node = _internal_processor.getNode(it->second.target_node_id)) {
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

auto CompositeNode::getInternalParameterTarget(clap_id external_id) const
    -> CompositeNode::ParameterTarget {
    if (external_id < _current_state->direct_parameter_mappings.size()) {
        const auto& mapping = _current_state->direct_parameter_mappings[external_id];
        if (auto* node = _internal_processor.getNode(mapping.target_node_id)) {
            return {.node = node, .internal_id = mapping.target_param_id};
        }
    }
    return {.node = nullptr, .internal_id = 0};
}

void CompositeNode::queueEvent(const PluginEvent& event) {
    uint32_t ext_port = 0;
    
    // Extract external port index based on event type
    switch (event.event.header.type) {
        case CLAP_EVENT_NOTE_ON:
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE:
        case CLAP_EVENT_NOTE_EXPRESSION:
            ext_port = event.event.note.port_index;
            break;
        case CLAP_EVENT_PARAM_VALUE:
            ext_port = event.event.param_value.port_index;
            break;
        case CLAP_EVENT_PARAM_MOD:
            ext_port = event.event.param_mod.port_index;
            break;
        case CLAP_EVENT_MIDI:
            ext_port = event.event.midi.port_index;
            break;
        default:
            return;
    }

    // Strict 1:N routing using input proxies
    auto it = _current_state->input_proxies.find(ext_port);
    if (it != _current_state->input_proxies.end()) {
        for (const auto& mapping : it->second) {
            if (auto* node = _internal_processor.getNode(mapping.node_id)) {
                PluginEvent internal_ev = event;
                
                // Translate port index to internal port
                switch (event.event.header.type) {
                    case CLAP_EVENT_NOTE_ON:
                    case CLAP_EVENT_NOTE_OFF:
                    case CLAP_EVENT_NOTE_CHOKE:
                    case CLAP_EVENT_NOTE_EXPRESSION:
                        internal_ev.event.note.port_index = static_cast<int16_t>(mapping.port_index);
                        break;
                    case CLAP_EVENT_PARAM_VALUE:
                        internal_ev.event.param_value.port_index = static_cast<int16_t>(mapping.port_index);
                        break;
                    case CLAP_EVENT_PARAM_MOD:
                        internal_ev.event.param_mod.port_index = static_cast<int16_t>(mapping.port_index);
                        break;
                    case CLAP_EVENT_MIDI:
                        internal_ev.event.midi.port_index = static_cast<int16_t>(mapping.port_index);
                        break;
                }
                node->queueEvent(internal_ev);
            }
        }
    }
}

auto CompositeNode::popOutputEvent(PluginEvent& out_event) -> bool {
    return _external_output_events.try_dequeue(out_event);
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
    std::map<std::string, uint32_t> alias_to_id;

    _external_inputs.clear();
    _external_outputs.clear();
    _external_params.clear();

    // 1. Create internal plugins
    uint32_t next_internal_id = 1;
    for (const auto& p_cfg : config.plugins) {
        auto host = std::make_unique<PluginHost>();
        if (host->load(p_cfg.plugin_path, 0)) {
            uint32_t id = next_internal_id++;
            host->setInstanceId(id);
            alias_to_id[p_cfg.alias] = id;
            addInternalNode(id, std::move(host));
        } else {
            return false;
        }
    }

    // 2. Setup internal routings
    for (const auto& r_cfg : config.routings) {
        if (alias_to_id.count(r_cfg.from_node) && alias_to_id.count(r_cfg.to_node)) {
            connectInternal({.from_node = alias_to_id[r_cfg.from_node],
                             .from_port = r_cfg.from_port,
                             .to_node = alias_to_id[r_cfg.to_node],
                             .to_port = r_cfg.to_port,
                             .type = r_cfg.type});
        }
    }

    // 3. Setup parameter mappings and direct lookup
    uint32_t external_param_idx = 0;
    for (const auto& m_cfg : config.parameter_mappings) {
        if (alias_to_id.count(m_cfg.target_node)) {
            uint32_t internal_id = alias_to_id[m_cfg.target_node];
            setParameterMapping(m_cfg.param_id, internal_id, m_cfg.target_param_index);
            _internal_processor.setDirectParameterMapping(external_param_idx, internal_id,
                                                           m_cfg.target_param_index);

            if (auto* target_node = _internal_processor.getNode(internal_id)) {
                const auto& internal_params = target_node->getParameters();
                if (m_cfg.target_param_index < internal_params.size()) {
                    const auto& src_slot = internal_params[m_cfg.target_param_index];
                    auto ext_slot = std::make_unique<ParameterSlot>();
                    ext_slot->info = src_slot->info;
                    std::strncpy(ext_slot->info.name, m_cfg.param_id.c_str(), CLAP_NAME_SIZE);
                    ext_slot->info.id = external_param_idx++;
                    ext_slot->base_value.store(src_slot->base_value.load());
                    _external_params.push_back(std::move(ext_slot));
                }
            }
        }
    }

    // 4. Setup port proxies
    for (const auto& i_cfg : config.input_proxies) {
        if (alias_to_id.count(i_cfg.internal_node)) {
            uint32_t internal_id = alias_to_id[i_cfg.internal_node];
            setInputProxy(i_cfg.external_port_index, internal_id, i_cfg.internal_port_index);

            if (auto* internal_node = _internal_processor.getNode(internal_id)) {
                const auto& internal_ports = internal_node->getAudioPorts(true);
                for (const auto& p : internal_ports) {
                    if (p.index == i_cfg.internal_port_index) {
                        AudioPortInfo ext_info = p;
                        ext_info.index = i_cfg.external_port_index;
                        ext_info.is_modulation = (i_cfg.type == ConnectionType::kModulation);
                        _external_inputs.push_back(ext_info);
                        break;
                    }
                }
            }
        }
    }
    for (const auto& o_cfg : config.output_proxies) {
        if (alias_to_id.count(o_cfg.internal_node)) {
            uint32_t internal_id = alias_to_id[o_cfg.internal_node];
            setOutputProxy(o_cfg.external_port_index, internal_id, o_cfg.internal_port_index);

            if (auto* internal_node = _internal_processor.getNode(internal_id)) {
                const auto& internal_ports = internal_node->getAudioPorts(false);
                for (const auto& p : internal_ports) {
                    if (p.index == o_cfg.internal_port_index) {
                        AudioPortInfo ext_info = p;
                        ext_info.index = o_cfg.external_port_index;
                        ext_info.is_modulation = (o_cfg.type == ConnectionType::kModulation);
                        _external_outputs.push_back(ext_info);
                        break;
                    }
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

void CompositeNode::setInputProxy(uint32_t external_port, uint32_t internal_node,
                                  uint32_t internal_port) {
    _internal_processor.setInputProxy(external_port, internal_node, internal_port);
}

void CompositeNode::setOutputProxy(uint32_t external_port, uint32_t internal_node,
                                   uint32_t internal_port) {
    _internal_processor.setOutputProxy(external_port, internal_node, internal_port);
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

auto CompositeNode::isActive() const -> bool { return _is_active; }

}  // namespace synth_canvas::host
