#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <clap/clap.h>
#include <oboe/Oboe.h>

#include <memory>
#include <vector>

#include "host/engine/audio_buffer_manager.h"
#include "host/engine/module_router.h"
#include "host/graph/graph_renderer.h"
#include "utils/constants.h"

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
    void handleEvent(ProcessingNode* source, const PluginEvent& ev, uint32_t port_index);

    void printProfilingStats();
    void resetProfilingStats();

    // Accumulates planar source buffer to interleaved destination buffer (for oboe output)
    void accumulateToInterleaved(const AudioBuffer* src, float* dst_interleaved,
                                 int32_t num_frames);

    std::shared_ptr<oboe::AudioStream> _stream;
    ModuleRouter* _module_router;
    AudioBufferManager _buffer_manager;
    GraphRenderer _renderer;

    std::unique_ptr<ModuleRouter::AudioRenderState> _current_render_state;

    int32_t _channel_count = constants::kDefaultChannelCount;
    int32_t _sample_rate = constants::kDefaultSampleRate;
    int32_t _frames_per_block = 0;

    // Profiling Histogram (0.01ms resolution, up to 50ms)
    std::vector<uint32_t> _profile_histogram;
    uint64_t _profile_total_blocks = 0;
    uint64_t _blocks_since_last_print = 0;
    double _profile_max_time_ms = 0.0;
    int32_t _last_xrun_count = 0;

    [[nodiscard]] auto getPercentile(double p) const -> double;
};

}  // namespace synth_canvas::host

#endif  // AUDIO_ENGINE_H
