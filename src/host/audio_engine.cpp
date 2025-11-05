#include <godot_cpp/variant/utility_functions.hpp>

#include "audio_engine.h"
#include "plugin_host.h"

#if defined(__ANDROID__)
// --- Real implementation for Android using Oboe ---

namespace synth_canvas::host
{

    AudioEngine::AudioEngine()
    {
        godot::UtilityFunctions::print("[AudioEngine] Created for Android.");
    }

    AudioEngine::~AudioEngine()
    {
        stop();
    }

    bool AudioEngine::openStream()
    {
        if (_stream)
        {
            return true;
        }

        oboe::AudioStreamBuilder builder;
        builder.setDirection(oboe::Direction::Output)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setSharingMode(oboe::SharingMode::Exclusive)
            ->setFormat(oboe::AudioFormat::Float)
            ->setChannelCount(_channel_count)
            ->setSampleRate(_sample_rate)
            ->setDataCallback(this);

        oboe::Result result = builder.openStream(_stream);
        if (result != oboe::Result::OK)
        {
            godot::UtilityFunctions::print("[AudioEngine] Failed to create stream. Error: ", oboe::convertToText(result));
            _stream.reset();
            return false;
        }

        _sample_rate = _stream->getSampleRate();
        return true;
    }

    bool AudioEngine::start()
    {
        if (!_stream && !openStream())
        {
            return false;
        }
        if (_stream->getState() == oboe::StreamState::Started)
        {
            return true;
        }

        if (_plugin_host)
        {
            _plugin_host->activate(_sample_rate, _stream->getFramesPerCallback());
        }

        oboe::Result result = _stream->requestStart();
        if (result != oboe::Result::OK)
        {
            godot::UtilityFunctions::print("[AudioEngine] Failed to start stream. Error: ", oboe::convertToText(result));
            return false;
        }
        return true;
    }

    void AudioEngine::stop()
    {
        if (_stream)
        {
            _stream->requestStop();
            _stream->close();
            _stream.reset();
        }
        if (_plugin_host)
        {
            _plugin_host->deactivate();
        }
    }

    bool AudioEngine::loadPlugin(const std::string &path)
    {
        if (!_plugin_host)
        {
            _plugin_host = std::make_unique<PluginHost>();
            if (_plugin_host)
            {
                _plugin_host->on_parameter_changed = on_parameter_changed;
            }
        }

        if (_plugin_host)
        {
            if (!_plugin_host->load(path, 0))
            {
                _plugin_host->unload();
                return false;
            }
        }
        return true;
    }

    oboe::DataCallbackResult AudioEngine::onAudioReady(
        oboe::AudioStream *oboeStream,
        void *audioData,
        int32_t numFrames)
    {

        if (!_plugin_host || !_plugin_host->isPluginActive())
        {
            memset(audioData, 0, numFrames * _channel_count * sizeof(float));
            return oboe::DataCallbackResult::Continue;
        }

        // This part needs a proper stereo-to-stereo or mono-to-stereo handling
        // For now, we assume the plugin matches the output format.
        float *outputBuffer = static_cast<float *>(audioData);
        std::vector<float *> channel_buffers;
        channel_buffers.resize(_channel_count);
        for (int i = 0; i < _channel_count; ++i)
        {
            channel_buffers[i] = outputBuffer + i * numFrames; // This assumes interleaved, need to de-interleave
        }

        // Simplified processing, assuming plugin can write directly to interleaved buffer for now
        // A real implementation needs de-interleaving and interleaving steps.
        _plugin_host->setPorts(_channel_count, channel_buffers.data(), _channel_count, channel_buffers.data());
        _plugin_host->process();

        return oboe::DataCallbackResult::Continue;
    }

    void AudioEngine::playNote(int note, double velocity)
    {
        if (_plugin_host)
        {
            _plugin_host->processNoteOn(0, 0, note, static_cast<int>(velocity * 127));
        }
    }

    void AudioEngine::stopNote(int note)
    {
        if (_plugin_host)
        {
            _plugin_host->processNoteOff(0, 0, note, 0);
        }
    }

    void AudioEngine::setParameterValue(clap_id param_id, double value)
    {
        if (_plugin_host)
        {
            _plugin_host->setParameterValue(param_id, value);
        }
    }

} // namespace synth_canvas::host

#else
// --- Dummy implementation for non-Android platforms ---

namespace synth_canvas::host
{

    AudioEngine::AudioEngine()
    {
        godot::UtilityFunctions::print("[AudioEngine] Created with dummy implementation for non-Android.");
        // On non-android, we still need to create the plugin host
        if (!_plugin_host)
        {
            _plugin_host = std::make_unique<PluginHost>();
        }
    }

    AudioEngine::~AudioEngine()
    {
        if (_plugin_host)
        {
            _plugin_host->unload();
        }
    }

    bool AudioEngine::loadPlugin(const std::string &path)
    {
        godot::UtilityFunctions::print("[AudioEngine] Dummy: loading plugin ", path.c_str());
        if (_plugin_host)
        {
            return _plugin_host->load(path, 0);
        }
        return false;
    }

    bool AudioEngine::start()
    {
        godot::UtilityFunctions::print("[AudioEngine] Dummy: start called.");
        // Cannot start audio on non-Android, but we can activate the plugin for UI
        if (_plugin_host)
        {
            // Activate with dummy values
            _plugin_host->activate(44100, 512);
        }
        return false;
    }

    void AudioEngine::stop()
    {
        godot::UtilityFunctions::print("[AudioEngine] Dummy: stop called.");
        if (_plugin_host)
        {
            _plugin_host->deactivate();
        }
    }

    void AudioEngine::playNote(int note, double velocity)
    {
        // No-op
    }

    void AudioEngine::stopNote(int note)
    {
        // No-op
    }

    void AudioEngine::setParameterValue(clap_id param_id, double value)
    {
        if (_plugin_host)
        {
            _plugin_host->setParameterValue(param_id, value);
        }
    }

    PluginHost *AudioEngine::getPluginHost() const
    {
        return _plugin_host.get();
    }

} // namespace synth_canvas::host

#endif // defined(__ANDROID__)
