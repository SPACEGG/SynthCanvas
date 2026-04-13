#include "graph_renderer.h"
#include "processing_node.h"
#include "constants.h"
#include <cmath>

namespace synth_canvas::host {

GraphRenderer::GraphRenderer() {
    _mod_sum_workspace.reserve(constants::kMaxConnectionsPerPort);
    _inputs_workspace.reserve(constants::kMaxConnectionsPerPort);
    _outputs_workspace.reserve(constants::kMaxConnectionsPerPort);
}

void GraphRenderer::render(const GraphProcessor::RenderState& state, 
                           AudioBufferManager& buffers, 
                           int32_t num_frames,
                           const EventHandler& handler) {
    for (ProcessingNode* node : state.sorted_nodes) {
        if (!node || !node->isActive()) continue;

        processSingleNode(node, state, buffers, num_frames);
        collectAndRouteEvents(node, state, handler);
    }
}

void GraphRenderer::processSingleNode(ProcessingNode* node, 
                                      const GraphProcessor::RenderState& state,
                                      AudioBufferManager& buffers,
                                      int32_t num_frames) {
    applyParameterModulation(node, state, buffers, num_frames);
    prepareAudioInputs(node, state, buffers, num_frames);
    prepareAudioOutputs(node, buffers, num_frames);
    executeNodeProcessing(node, num_frames);
}

void GraphRenderer::applyParameterModulation(ProcessingNode* node, 
                                             const GraphProcessor::RenderState& state,
                                             AudioBufferManager& buffers,
                                             int32_t num_frames) {
    uint32_t node_id = node->getInstanceId();
    auto mod_it = state.input_modulations.find(node_id);
    if (mod_it == state.input_modulations.end()) return;

    for (int t = 0; t < num_frames; t += constants::kModulationStepSize) {
        _mod_sum_workspace.clear();

        for (const auto& mod : mod_it->second) {
            float** src_buffer = buffers.getReadOnlyBuffer(mod.source_node_id, mod.source_port_index);
            if (!src_buffer) continue;

            float mod_value = src_buffer[0][t];

            bool found = false;
            for (auto& entry : _mod_sum_workspace) {
                if (entry.first == mod.target_param_id) {
                    entry.second += static_cast<double>(mod_value);
                    found = true;
                    break;
                }
            }

            if (!found) {
                _mod_sum_workspace.emplace_back(mod.target_param_id, static_cast<double>(mod_value));
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

void GraphRenderer::prepareAudioInputs(ProcessingNode* node, 
                                       const GraphProcessor::RenderState& state,
                                       AudioBufferManager& buffers,
                                       int32_t num_frames) {
    uint32_t node_id = node->getInstanceId();
    const auto& input_ports = node->getAudioPorts(true);
    _inputs_workspace.assign(input_ports.size(), clap_audio_buffer{});

    auto input_map_it = state.input_audio_sources.find(node_id);

    for (size_t i = 0; i < input_ports.size(); ++i) {
        const auto& port_info = input_ports[i];
        _inputs_workspace[i].channel_count = port_info.clap_info.channel_count;
        _inputs_workspace[i].constant_mask = 0;
        _inputs_workspace[i].latency = 0;
        _inputs_workspace[i].data64 = nullptr;

        std::vector<AudioBufferManager::PortSource> sources;
        if (input_map_it != state.input_audio_sources.end()) {
            auto port_sources_it = input_map_it->second.find(port_info.index);
            if (port_sources_it != input_map_it->second.end()) {
                sources = port_sources_it->second;
            }
        }
        _inputs_workspace[i].data32 = buffers.getInputMix(port_info.index, sources, num_frames);
    }
}

void GraphRenderer::prepareAudioOutputs(ProcessingNode* node, 
                                        AudioBufferManager& buffers,
                                        int32_t num_frames) {
    uint32_t node_id = node->getInstanceId();
    const auto& output_ports = node->getAudioPorts(false);
    _outputs_workspace.assign(output_ports.size(), clap_audio_buffer{});

    for (size_t i = 0; i < output_ports.size(); ++i) {
        const auto& port_info = output_ports[i];
        _outputs_workspace[i].channel_count = port_info.clap_info.channel_count;
        _outputs_workspace[i].constant_mask = 0;
        _outputs_workspace[i].latency = 0;
        _outputs_workspace[i].data64 = nullptr;
        _outputs_workspace[i].data32 = buffers.getBuffer(node_id, port_info.index, num_frames);
    }
}

void GraphRenderer::executeNodeProcessing(ProcessingNode* node, int32_t num_frames) {
    node->processBegin(num_frames);
    node->setPorts(static_cast<uint32_t>(_inputs_workspace.size()), _inputs_workspace.data(),
                   static_cast<uint32_t>(_outputs_workspace.size()), _outputs_workspace.data());
    node->process();
    node->processEnd(num_frames);
}

void GraphRenderer::collectAndRouteEvents(ProcessingNode* node, 
                                          const GraphProcessor::RenderState& state,
                                          const EventHandler& handler) {
    uint32_t source_node_id = node->getInstanceId();
    auto it = state.output_event_targets.find(source_node_id);
    
    PluginEvent ev;
    while (node->popOutputEvent(ev)) {
        if (it != state.output_event_targets.end()) {
            for (const auto& target : it->second) {
                if (target.type == GraphProcessor::RenderState::EventTarget::Type::kNode) {
                    target.destination.node->queueEvent(ev);
                } else if (target.type == GraphProcessor::RenderState::EventTarget::Type::kExternalOutput) {
                    if (handler) {
                        handler(node, ev, target.destination.port_index);
                    }
                }
            }
        }
    }
}

} // namespace synth_canvas::host
