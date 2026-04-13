#ifndef SYNTH_CANVAS_HOST_GRAPH_RENDERER_H
#define SYNTH_CANVAS_HOST_GRAPH_RENDERER_H

#include <clap/clap.h>
#include <vector>
#include <utility>
#include <functional>

#include "graph_types.h"
#include "graph_processor.h"
#include "audio_buffer_manager.h"

namespace synth_canvas::host {

class ProcessingNode;

// Node graph execution engine.
// Provides real-time safe processing and event routing logic.
class GraphRenderer {
public:
    // Callback for events destined for external graph outputs.
    // Internal node-to-node routing is handled automatically.
    using EventHandler = std::function<void(ProcessingNode* source, const PluginEvent& ev, uint32_t port_index)>;

    GraphRenderer();
    ~GraphRenderer() = default;

    // Executes one audio block for the given graph state.
    void render(const GraphProcessor::RenderState& state, 
                AudioBufferManager& buffers, 
                int32_t num_frames,
                const EventHandler& handler);

private:
    void processSingleNode(ProcessingNode* node, 
                           const GraphProcessor::RenderState& state,
                           AudioBufferManager& buffers,
                           int32_t num_frames);

    void applyParameterModulation(ProcessingNode* node, 
                                  const GraphProcessor::RenderState& state,
                                  AudioBufferManager& buffers,
                                  int32_t num_frames);

    void prepareAudioInputs(ProcessingNode* node, 
                            const GraphProcessor::RenderState& state,
                            AudioBufferManager& buffers,
                            int32_t num_frames);

    void prepareAudioOutputs(ProcessingNode* node, 
                             AudioBufferManager& buffers,
                             int32_t num_frames);

    void executeNodeProcessing(ProcessingNode* node, int32_t num_frames);

    void collectAndRouteEvents(ProcessingNode* node, 
                               const GraphProcessor::RenderState& state,
                               const EventHandler& handler);

    // Real-time safe workspaces
    std::vector<std::pair<clap_id, double>> _mod_sum_workspace;
    std::vector<clap_audio_buffer> _inputs_workspace;
    std::vector<clap_audio_buffer> _outputs_workspace;
};

} // namespace synth_canvas::host

#endif // SYNTH_CANVAS_HOST_GRAPH_RENDERER_H
