#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <clap/clap.h>
#include <oboe/Oboe.h>

#include <memory>
#include <vector>

#include "audio_buffer_manager.h"
#include "constants.h"
#include "module_router.h"

namespace synth_canvas::host {

class ProcessingNode;

// Main audio rendering engine using Oboe.
class AudioEngine : public oboe::AudioStreamDataCallback {
   public:
    explicit AudioEngine(ModuleRouter* router);
    ~AudioEngine() override;

    auto start() -> bool;
    void stop();

    void playNote(uint32_t instance_id, int note, double velocity,
                  int32_t note_id = constants::kClapInvalidId);
    void stopNote(uint32_t instance_id, int note, double velocity = 0.0,
                  int32_t note_id = constants::kClapInvalidId);
    void setParameterValue(uint32_t instance_id, clap_id param_id, double value);

    [[nodiscard]] auto getSampleRate() const -> int32_t { return _sample_rate; }
    [[nodiscard]] auto getFramesPerBlock() const -> int32_t { return _frames_per_block; }
    [[nodiscard]] auto isRunning() const -> bool;

    auto onAudioReady(oboe::AudioStream* oboe_stream, void* audio_data, int32_t num_frames)
        -> oboe::DataCallbackResult override;

   private:
    auto openStream() -> bool;

    void updateRenderState();
    void processSingleNode(ProcessingNode* node, int32_t num_frames);
    void routeNodeEvents(ProcessingNode* node);

    // Processing steps
    void applyParameterModulation(ProcessingNode* node, int32_t num_frames);
    void prepareAudioInputs(ProcessingNode* node, int32_t num_frames);
    void prepareAudioOutputs(ProcessingNode* node, int32_t num_frames);
    void executeNodeProcessing(ProcessingNode* node, int32_t num_frames,
                               std::vector<clap_audio_buffer>& inputs,
                               std::vector<clap_audio_buffer>& outputs);

    std::shared_ptr<oboe::AudioStream> _stream;
    ModuleRouter* _module_router;
    AudioBufferManager _buffer_manager;

    std::unique_ptr<ModuleRouter::AudioRenderState> _current_render_state;

    int32_t _channel_count = constants::kDefaultChannelCount;
    int32_t _sample_rate = constants::kDefaultSampleRate;
    int32_t _frames_per_block = 0;

    // Real-time safe workspace for modulation summing
    std::vector<std::pair<clap_id, double>> _mod_sum_workspace;

    // Real-time safe workspaces for port buffers
    std::vector<clap_audio_buffer> _inputs_workspace;
    std::vector<clap_audio_buffer> _outputs_workspace;
};

}  // namespace synth_canvas::host

#endif  // AUDIO_ENGINE_H
