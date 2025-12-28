#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include <clap/clap.h> // For clap_id
#include "module_router.h" // Include ModuleRouter
#include "audio_buffer_manager.h"

#if defined(__ANDROID__)
// --- Android implementation using Oboe ---
#include <oboe/Oboe.h>

namespace synth_canvas::host
{

    class AudioEngine : public oboe::AudioStreamDataCallback
    {
    public:
        static constexpr uint32_t AUDIO_OUTPUT_NODE_ID = 0; 

        AudioEngine(ModuleRouter* router);
        ~AudioEngine();

        bool start();
        void stop();

        void playNote(uint32_t instance_id, int note, double velocity);
        void stopNote(uint32_t instance_id, int note);
        void setParameterValue(uint32_t instance_id, clap_id param_id, double value);

        int32_t getSampleRate() const { return _sample_rate; }
        int32_t getFramesPerBlock() const { return _frames_per_block; }
        bool isRunning() const;

        oboe::DataCallbackResult onAudioReady(
            oboe::AudioStream *oboeStream,
            void *audioData,
            int32_t numFrames) override;

    private:
        bool openStream();

        std::shared_ptr<oboe::AudioStream> _stream;
        ModuleRouter* _module_router;
        AudioBufferManager _buffer_manager;
        
        std::unique_ptr<ModuleRouter::AudioRenderState> _current_render_state;

        int32_t _channel_count = 2;
        int32_t _sample_rate = 48000;
        int32_t _frames_per_block = 0; 
    };

} // namespace synth_canvas::host

#else
// --- Dummy implementation for non-Android platforms (e.g., Windows) ---
#include <clap/clap.h> // For clap_id

namespace synth_canvas::host
{

    class AudioEngine
    {
    public:
        static constexpr uint32_t AUDIO_OUTPUT_NODE_ID = 0;

        AudioEngine(ModuleRouter* router);
        ~AudioEngine();

        bool start();
        void stop();

        void playNote(uint32_t instance_id, int note, double velocity);
        void stopNote(uint32_t instance_id, int note);
        void setParameterValue(uint32_t instance_id, clap_id param_id, double value);

        int32_t getSampleRate() const { return 44100; }
        int32_t getFramesPerBlock() const { return 512; }
        bool isRunning() const { return false; }

    private:
        ModuleRouter* _module_router; // Weak reference to ModuleRouter
        // AudioBufferManager _buffer_manager; // Not strictly needed for dummy, but good for consistency if dummy logic expands
    };

} // namespace synth_canvas::host

#endif // defined(__ANDROID__)

#endif // AUDIO_ENGINE_H