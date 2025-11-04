
#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <memory>
#include <string>
#include <functional>

// Forward declarations
namespace synth_canvas::host {
    class PluginHost;
}

#if defined(__ANDROID__)
// --- Android implementation using Oboe ---
#include <oboe/Oboe.h>

namespace synth_canvas::host {

class AudioEngine : public oboe::AudioStreamDataCallback {
public:
    AudioEngine();
    ~AudioEngine();

    bool loadPlugin(const std::string& path);
    bool start();
    void stop();

    void playNote(int note, double velocity);
    void stopNote(int note);

    std::function<void(int, float)> on_parameter_changed;
    PluginHost* getPluginHost() const { return _plugin_host.get(); }

    oboe::DataCallbackResult onAudioReady(
        oboe::AudioStream *oboeStream,
        void *audioData,
        int32_t numFrames) override;

private:
    bool openStream();

    std::shared_ptr<oboe::AudioStream> _stream;
    std::unique_ptr<PluginHost> _plugin_host;

    int32_t _channel_count = 2;
    int32_t _sample_rate = 48000;
};

} // namespace synth_canvas::host

#else
// --- Dummy implementation for non-Android platforms (e.g., Windows) ---

namespace synth_canvas::host {

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool loadPlugin(const std::string& path);
    bool start();
    void stop();

    void playNote(int note, double velocity);
    void stopNote(int note);

    std::function<void(int, float)> on_parameter_changed;
    PluginHost* getPluginHost() const;

private:
    std::unique_ptr<PluginHost> _plugin_host;
};

} // namespace synth_canvas::host

#endif // defined(__ANDROID__)

#endif // AUDIO_ENGINE_H
