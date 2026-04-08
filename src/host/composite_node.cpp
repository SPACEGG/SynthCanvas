#include "composite_node.h"

#include <cstring>
#include <map>

#include "constants.h"
#include "logger.h"
#include "plugin_host.h"

namespace synth_canvas::host {

CompositeNode::CompositeNode()
    : _pending_states(constants::kSnapshotQueueSize),
      _released_states(constants::kSnapshotQueueSize) {
    // Initial internal state setup
    _current_state = std::make_unique<GraphProcessor::RenderState>();
}

CompositeNode::~CompositeNode() {
    _is_active = false;
    // Release resources while ensuring no dangling pointers
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

    // Recursively activate all internal nodes
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
    // Check for state updates from the main thread at the start of the block
    updateInternalRenderState();

    for (ProcessingNode* node : _current_state->sorted_nodes) {
        node->processBegin(num_frames);
    }
}

void CompositeNode::process() {
    if (!_is_active || !_current_state) return;

    int32_t num_frames = _block_size;  // Assuming block size remains constant during process()

    // Execute internal nodes in sorted order
    for (ProcessingNode* node : _current_state->sorted_nodes) {
        if (!node->isActive()) continue;
        processInternalNode(node, num_frames);
    }
}

void CompositeNode::processEnd(int num_frames) {
    for (ProcessingNode* node : _current_state->sorted_nodes) {
        node->processEnd(num_frames);
    }
}

void CompositeNode::processInternalNode(ProcessingNode* node, int32_t num_frames) {
    uint32_t node_id = node->getInstanceId();

    // 1. Prepare Audio Inputs (Checking for proxies)
    const auto& input_ports = node->getAudioPorts(true);
    std::vector<clap_audio_buffer> clap_inputs(input_ports.size());

    for (size_t i = 0; i < input_ports.size(); ++i) {
        const auto& port_info = input_ports[i];

        // Find if this specific internal port is mapped to an external module input
        bool is_proxied = false;
        for (const auto& [ext_idx, proxy] : _current_state->input_proxies) {
            if (proxy.internal_node_id == node_id && proxy.internal_port_index == port_info.index) {
                // Point directly to external buffer (Zero-copy Injection)
                if (ext_idx < _ext_input_count) {
                    clap_inputs[i] = _ext_inputs[ext_idx];
                    is_proxied = true;
                }
                break;
            }
        }

        if (!is_proxied) {
            // Standard internal routing
            auto it = _current_state->input_audio_sources.find(node_id);
            std::vector<AudioBufferManager::PortSource> sources;
            if (it != _current_state->input_audio_sources.end()) {
                auto port_sources_it = it->second.find(port_info.index);
                if (port_sources_it != it->second.end()) {
                    sources = port_sources_it->second;
                }
            }
            clap_inputs[i].channel_count = port_info.clap_info.channel_count;
            clap_inputs[i].data32 =
                _internal_buffers.getInputMix(port_info.index, sources, num_frames);
        }
    }

    // 2. Prepare Audio Outputs (Checking for proxies)
    const auto& output_ports = node->getAudioPorts(false);
    std::vector<clap_audio_buffer> clap_outputs(output_ports.size());

    for (size_t i = 0; i < output_ports.size(); ++i) {
        const auto& port_info = output_ports[i];

        // Find if this internal output should go directly to the module's external output
        bool is_proxied = false;
        for (const auto& [ext_idx, proxy] : _current_state->output_proxies) {
            if (proxy.internal_node_id == node_id && proxy.internal_port_index == port_info.index) {
                if (ext_idx < _ext_output_count) {
                    clap_outputs[i] = _ext_outputs[ext_idx];
                    is_proxied = true;
                }
                break;
            }
        }

        if (!is_proxied) {
            clap_outputs[i].channel_count = port_info.clap_info.channel_count;
            clap_outputs[i].data32 =
                _internal_buffers.getBuffer(node_id, port_info.index, num_frames);
        }
    }

    // 3. Dispatch to the internal node
    node->setPorts(static_cast<uint32_t>(clap_inputs.size()), clap_inputs.data(),
                   static_cast<uint32_t>(clap_outputs.size()), clap_outputs.data());
    node->process();
}

void CompositeNode::updateInternalRenderState() {
    std::unique_ptr<GraphProcessor::RenderState> new_state;
    while (_pending_states.try_dequeue(new_state)) {
        if (_current_state) {
            _released_states.enqueue(std::move(_current_state));
        }
        _current_state = std::move(new_state);
    }
}

void CompositeNode::setParameterValue(clap_id param_id, double value) {
    // Numeric fallback: treat param_id as index into our exposed parameter list
    if (param_id < _external_params.size()) {
        setParameterValue(_external_params[param_id]->info.name, value);
    }
}

void CompositeNode::setParameterValue(const std::string& param_id, double value) {
    // Use the authoritative mapping on the main thread
    // The AudioThread uses its own snapshot via _current_state
    // However, for immediate UI-triggered changes, we look up the internal target
    auto it = _current_state->parameter_mappings.find(param_id);
    if (it != _current_state->parameter_mappings.end()) {
        if (auto* node = _internal_processor.getNode(it->second.target_node_id)) {
            node->setParameterValue(it->second.target_param_id, value);
        }
    }
}

void CompositeNode::queueEvent(const PluginEvent& event) {
    // FIXME: Implement precise event routing based on external-to-internal event port mapping.
    // For now, we broadcast to all internal nodes (Omni-mode).
    for (uint32_t id : _internal_processor.getProcessOrder()) {
        if (auto* node = _internal_processor.getNode(id)) {
            node->queueEvent(event);
        }
    }
}

bool CompositeNode::popOutputEvent(PluginEvent& out_event) {
    // FIXME: Implement event output mapping.
    // For now, composite nodes do not output events to the external graph.
    return false;
}

void CompositeNode::pollMainThread() {
    // Poll resources for the internal graph
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

    // Clear existing external interface
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
            log("[CompositeNode] Error: Failed to load internal plugin: ", p_cfg.plugin_path);
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

    // 3. Setup parameter mappings and expose them
    uint32_t external_param_idx = 0;
    for (const auto& m_cfg : config.parameter_mappings) {
        if (alias_to_id.count(m_cfg.target_node)) {
            uint32_t internal_id = alias_to_id[m_cfg.target_node];
            setParameterMapping(m_cfg.param_id, internal_id, m_cfg.target_param_index);

            // Expose metadata
            if (auto* target_node = _internal_processor.getNode(internal_id)) {
                const auto& internal_params = target_node->getParameters();
                if (m_cfg.target_param_index < internal_params.size()) {
                    const auto& src_slot = internal_params[m_cfg.target_param_index];
                    auto ext_slot = std::make_unique<ParameterSlot>();
                    ext_slot->info = src_slot->info;
                    // Override with alias name and sequential ID
                    std::strncpy(ext_slot->info.name, m_cfg.param_id.c_str(), CLAP_NAME_SIZE);
                    ext_slot->info.id = external_param_idx++;
                    ext_slot->base_value.store(src_slot->base_value.load());

                    _external_params.push_back(std::move(ext_slot));
                }
            }
        }
    }

    // 4. Setup port proxies and expose ports
    for (const auto& i_cfg : config.input_proxies) {
        if (alias_to_id.count(i_cfg.internal_node)) {
            uint32_t internal_id = alias_to_id[i_cfg.internal_node];
            setInputProxy(i_cfg.external_port_index, internal_id, i_cfg.internal_port_index);

            // Expose metadata
            if (auto* target_node = _internal_processor.getNode(internal_id)) {
                const auto& internal_ports = target_node->getAudioPorts(true);
                for (const auto& p : internal_ports) {
                    if (p.index == i_cfg.internal_port_index) {
                        AudioPortInfo ext_info = p;
                        ext_info.index = i_cfg.external_port_index;
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

            // Expose metadata
            if (auto* target_node = _internal_processor.getNode(internal_id)) {
                const auto& internal_ports = target_node->getAudioPorts(false);
                for (const auto& p : internal_ports) {
                    if (p.index == o_cfg.internal_port_index) {
                        AudioPortInfo ext_info = p;
                        ext_info.index = o_cfg.external_port_index;
                        _external_outputs.push_back(ext_info);
                        break;
                    }
                }
            }
        }
    }

    // 5. Finalize
    pushInternalState();
    log("[CompositeNode] Configuration loaded successfully. Exposed ", _external_params.size(),
        " params.");
    return true;
}

void CompositeNode::addInternalNode(uint32_t id, std::unique_ptr<ProcessingNode> node) {
    if (_is_active) {
        node->activate(_sample_rate, _block_size);
    }
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
    // 0 is a placeholder for master_node_id as internal routing handles final outputs via proxies
    auto new_state = _internal_processor.createRenderState(0);
    if (!_pending_states.enqueue(std::move(new_state))) {
        log("[CompositeNode] Error: Internal state queue full.");
    }
}

auto CompositeNode::getAudioPorts(bool is_input) const -> const std::vector<AudioPortInfo>& {
    return is_input ? _external_inputs : _external_outputs;
}

auto CompositeNode::getParameters() const -> const std::vector<std::unique_ptr<ParameterSlot>>& {
    return _external_params;
}

auto CompositeNode::isActive() const -> bool { return _is_active; }

}  // namespace synth_canvas::host
