#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <clap/clap.h>
#include <oboe/Oboe.h>

#include <memory>

#include "audio_buffer_manager.h"
#include "constants.h"
#include "module_router.h"

namespace synth_canvas::host {

class AudioEngine : public oboe::AudioStreamDataCallback {
   public:
    explicit AudioEngine(ModuleRouter* router);
    ~AudioEngine();

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
    void processSinglePlugin(PluginHost* host, int32_t num_frames);
    void routePluginOutputs(PluginHost* host);

    std::shared_ptr<oboe::AudioStream> _stream;
    ModuleRouter* _module_router;
    AudioBufferManager _buffer_manager;

    std::unique_ptr<ModuleRouter::AudioRenderState> _current_render_state;

    int32_t _channel_count = constants::kDefaultChannelCount;
    int32_t _sample_rate = constants::kDefaultSampleRate;
    int32_t _frames_per_block = 0;
};

}  // namespace synth_canvas::host

#endif  // AUDIO_ENGINE_H