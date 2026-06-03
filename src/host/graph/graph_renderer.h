#ifndef SYNTH_CANVAS_HOST_GRAPH_RENDERER_H
#define SYNTH_CANVAS_HOST_GRAPH_RENDERER_H

#include <clap/clap.h>

#include <functional>
#include <utility>
#include <vector>

#include "host/engine/audio_buffer_manager.h"
#include "host/graph/graph_processor.h"
#include "host/graph/graph_types.h"

namespace synth_canvas::host {

class ProcessingNode;

class GraphRenderer {
   public:
    using EventHandler =
        std::function<void(ProcessingNode* source, const PluginEvent& ev, uint32_t port_index)>;

    GraphRenderer() = default;
    ~GraphRenderer() = default;

    void reserve();

    void render(const GraphProcessor::RenderState& state, AudioBufferManager& buffers,
                int32_t num_frames, const EventHandler& handler);

    void renderEvents(const GraphProcessor::RenderState& state, AudioBufferManager& buffers,
                      int32_t num_frames, const EventHandler& handler);

   private:
    void processSingleNode(size_t node_index, ProcessingNode* node,
                           const GraphProcessor::RenderState& state, AudioBufferManager& buffers,
                           int32_t num_frames);

    void applyParameterModulation(size_t node_index, ProcessingNode* node,
                                  const GraphProcessor::RenderState& state,
                                  AudioBufferManager& buffers, int32_t num_frames);

    void prepareAudioInputs(size_t node_index, ProcessingNode* node,
                            const GraphProcessor::RenderState& state, AudioBufferManager& buffers,
                            int32_t num_frames);

    void prepareAudioOutputs(size_t node_index, ProcessingNode* node,
                             const GraphProcessor::RenderState& state, AudioBufferManager& buffers,
                             int32_t num_frames);

    void executeNodeProcessing(ProcessingNode* node, int32_t num_frames);

    void collectAndRouteEvents(size_t node_index, ProcessingNode* node,
                               const GraphProcessor::RenderState& state,
                               const EventHandler& handler,
                               std::vector<bool>* needs_reprocessing = nullptr);

    std::vector<std::pair<clap_id, double>> _mod_sum_workspace;
    std::vector<clap_audio_buffer> _inputs_workspace;
    std::vector<clap_audio_buffer> _outputs_workspace;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_GRAPH_RENDERER_H
