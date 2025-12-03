
#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include <clap/clap.h> // For clap_id
#include "module_router.h" // Include ModuleRouter

#if defined(__ANDROID__)
// --- Android implementation using Oboe ---
#include <oboe/Oboe.h>

namespace synth_canvas::host
{

    class AudioEngine : public oboe::AudioStreamDataCallback
    {
    public:
        // Struct for de-interleaved audio buffer, remains in AudioEngine for now
        struct AudioBuffer
        {
            std::vector<std::vector<float>> data;
            uint32_t channels = 0;
            uint32_t frames = 0;
        };

        static constexpr uint32_t AUDIO_OUTPUT_NODE_ID = 0; // Remains in AudioEngine for mixing

        // Constructor accepts ModuleRouter dependency
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
        ModuleRouter* _module_router; // Weak reference to ModuleRouter
        std::unordered_map<uint32_t, AudioBuffer> _intermediate_buffers; // Still managed by AudioEngine

        int32_t _channel_count = 2;
        int32_t _sample_rate = 48000;
        int32_t _frames_per_block = 0; // Will be determined after stream opens
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
        // Struct for de-interleaved audio buffer, remains in AudioEngine
        struct AudioBuffer
        {
            std::vector<std::vector<float>> data;
            uint32_t channels = 0;
            uint32_t frames = 0;
        };

        static constexpr uint32_t AUDIO_OUTPUT_NODE_ID = 0; // Remains in AudioEngine for mixing

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
        std::unordered_map<uint32_t, AudioBuffer> _intermediate_buffers; // Still managed by AudioEngine
    };

} // namespace synth_canvas::host

#endif // defined(__ANDROID__)

#endif // AUDIO_ENGINE_H
