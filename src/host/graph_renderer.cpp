#include "graph_renderer.h"

#include <cmath>

#include "constants.h"
#include "processing_node.h"

namespace synth_canvas::host {

void GraphRenderer::reserve() {
    _mod_sum_workspace.reserve(constants::kMaxConnectionsPerPort);
    _inputs_workspace.reserve(constants::kMaxConnectionsPerPort);
    _outputs_workspace.reserve(constants::kMaxConnectionsPerPort);
}

void GraphRenderer::render(const GraphProcessor::RenderState& state, AudioBufferManager& buffers,
                           int32_t num_frames, const EventHandler& handler) {
    // Execute nodes in pre-calculated topological order
    for (size_t i = 0; i < state.sorted_nodes.size(); ++i) {
        ProcessingNode* node = state.sorted_nodes[i];
        if (!node || !node->isActive()) continue;

        node->setTransport(&(state.transport));
        processSingleNode(i, node, state, buffers, num_frames);
        collectAndRouteEvents(i, node, state, handler);
    }
}

void GraphRenderer::processSingleNode(size_t node_index, ProcessingNode* node,
                                      const GraphProcessor::RenderState& state,
                                      AudioBufferManager& buffers, int32_t num_frames) {
    applyParameterModulation(node_index, node, state, num_frames);
    prepareAudioInputs(node_index, node, state, buffers, num_frames);
    prepareAudioOutputs(node, buffers, num_frames);
    executeNodeProcessing(node, num_frames);
}

void GraphRenderer::applyParameterModulation(size_t node_index, ProcessingNode* node,
                                             const GraphProcessor::RenderState& state,
                                             int32_t num_frames) {
    const auto& modulations = state.input_modulations[node_index];
    if (modulations.empty()) return;

    for (int t = 0; t < num_frames; t += constants::kModulationStepSize) {
        _mod_sum_workspace.clear();

        for (const auto& mod : modulations) {
            ProcessingNode* src_node = state.sorted_nodes[mod.source_node_index];
            if (!src_node) continue;

            auto* src_buf = src_node->getOutputBuffer(mod.source_port_index);
            if (!src_buf || !src_buf->data32) continue;

            float mod_value = src_buf->data32[0][t];

            bool found = false;
            for (auto& entry : _mod_sum_workspace) {
                if (entry.first == mod.target_param_id) {
                    entry.second += static_cast<double>(mod_value);
                    found = true;
                    break;
                }
            }

            if (!found) {
                _mod_sum_workspace.emplace_back(mod.target_param_id,
                                                static_cast<double>(mod_value));
            }
        }

        for (const auto& entry : _mod_sum_workspace) {
            double last_offset = node->getParameterModulationOffset(entry.first);
            if (t == 0 || std::abs(entry.second - last_offset) > constants::kModulationThreshold) {
                node->applyModulation(entry.first, entry.second, t);
            }
        }
    }
}

void GraphRenderer::prepareAudioInputs(size_t node_index, ProcessingNode* node,
                                       const GraphProcessor::RenderState& state,
                                       AudioBufferManager& buffers, int32_t num_frames) {
    const auto& input_ports = node->getAudioPorts(true);
    _inputs_workspace.assign(input_ports.size(), clap_audio_buffer{});

    const auto& port_map = state.input_audio_sources[node_index];

    for (size_t i = 0; i < input_ports.size(); ++i) {
        const auto& port_info = input_ports[i];

        AudioBuffer* final_input = nullptr;
        auto it = port_map.find(port_info.index);
        const auto& sources = it->second;

        if (it == port_map.end() || sources.empty()) {
            final_input = nullptr;
        } else if (sources.size() == 1) {
            const auto& src = sources[0];
            ProcessingNode* src_node = state.sorted_nodes[src.node_index];
            if (src_node) final_input = src_node->getOutputBuffer(src.port_index);
        } else {
            AudioBuffer* mix_buf = buffers.getMixBuffer(node_index, port_info.index);
            if (mix_buf) {
                mix_buf->clear();
                for (const auto& src : sources) {
                    ProcessingNode* src_node = state.sorted_nodes[src.node_index];
                    if (src_node) {
                        AudioBuffer* src_buf = src_node->getOutputBuffer(src.port_index);
                        mix_buf->accumulate(src_buf, num_frames);
                    }
                }
                final_input = mix_buf;
            }
        }

        _inputs_workspace[i].channel_count = port_info.clap_info.channel_count;
        _inputs_workspace[i].data32 = final_input ? final_input->data32 : nullptr;
        _inputs_workspace[i].constant_mask = 0;
        _inputs_workspace[i].latency = 0;
        _inputs_workspace[i].data64 = nullptr;
    }
}

void GraphRenderer::prepareAudioOutputs(ProcessingNode* node, AudioBufferManager& buffers,
                                        int32_t num_frames) {
    const auto& output_ports = node->getAudioPorts(false);
    _outputs_workspace.assign(output_ports.size(), clap_audio_buffer{});

    for (size_t i = 0; i < output_ports.size(); ++i) {
        const auto& port_info = output_ports[i];

        AudioBuffer* buf = node->getOutputBuffer(port_info.index);
        if (buf && buf->owns_memory) {
            buf->clear();
        }

        _outputs_workspace[i].channel_count = port_info.clap_info.channel_count;
        _outputs_workspace[i].data32 = buf ? buf->data32 : nullptr;
        _outputs_workspace[i].constant_mask = 0;
        _outputs_workspace[i].latency = 0;
        _outputs_workspace[i].data64 = nullptr;
    }
}

void GraphRenderer::executeNodeProcessing(ProcessingNode* node, int32_t num_frames) {
    node->processBegin(num_frames);
    node->setPorts(static_cast<uint32_t>(_inputs_workspace.size()), _inputs_workspace.data(),
                   static_cast<uint32_t>(_outputs_workspace.size()), _outputs_workspace.data());
    node->process();
    node->processEnd(num_frames);
}

void GraphRenderer::collectAndRouteEvents(size_t node_index, ProcessingNode* node,
                                          const GraphProcessor::RenderState& state,
                                          const EventHandler& handler) {
    const auto& targets = state.output_event_targets[node_index];
    if (targets.empty()) {
        PluginEvent ev;
        while (node->popOutputEvent(ev)) {
        }
        return;
    }

    PluginEvent ev;
    while (node->popOutputEvent(ev)) {
        for (const auto& target : targets) {
            if (target.type == GraphProcessor::RenderState::EventTarget::Type::kNode) {
                // If CLAP_EVENT_PARAM_MOD: Rewrite the param_id to the target port's id
                if (ev.event.header.type == CLAP_EVENT_PARAM_MOD &&
                    target.target_param_id != constants::kClapInvalidId) {
                    PluginEvent rewritten_ev = ev;
                    rewritten_ev.event.param_mod.param_id = target.target_param_id;
                    target.destination.node->queueEvent(rewritten_ev);
                } else {
                    target.destination.node->queueEvent(ev);
                }
            } else if (target.type ==
                       GraphProcessor::RenderState::EventTarget::Type::kExternalOutput) {
                if (handler) {
                    handler(node, ev, target.destination.port_index);
                }
            }
        }
    }
}

}  // namespace synth_canvas::host
