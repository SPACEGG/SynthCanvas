#include <queue>
#include <algorithm>
#include <cstring> // for std::memset

#include "audio_engine.h"
#include "plugin_host.h"
#include "logger.h"

#if defined(__ANDROID__)

namespace synth_canvas::host
{

    AudioEngine::AudioEngine(ModuleRouter *router) : _module_router(router)
    {
        log("[AudioEngine] Created for Android.");
    }

    AudioEngine::~AudioEngine()
    {
        stop();
        log("[AudioEngine] Destroyed for Android.");
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
            log("[AudioEngine] Failed to create stream. Error: ", oboe::convertToText(result));
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

        oboe::Result result = _stream->requestStart();
        if (result != oboe::Result::OK)
        {
            log("[AudioEngine] Failed to start stream. Error: ", oboe::convertToText(result));
            return false;
        }

        _frames_per_block = _stream->getFramesPerDataCallback();

        if (_frames_per_block <= 0)
        {
            _frames_per_block = 512; // Safe default
            log("[AudioEngine] Warning: Stream returned 0 frames per block. Using default: 512");
        }
        else
        {
            log("[AudioEngine] Stream started. Frames per block: ", _frames_per_block);
        }

        _buffer_manager.resize(_channel_count, _frames_per_block * 2); // Reserve a bit more space for safety

        if (_module_router)
        {
            for (uint32_t instance_id : _module_router->get_process_order())
            {
                _module_router->activate_plugin(instance_id, _sample_rate, _frames_per_block);
            }
        }

        return true;
    }

    void AudioEngine::stop()
    {
        // 1. Deactivate all plugins first (while audio thread is still active)
        if (_module_router)
        {
            for (uint32_t instance_id : _module_router->get_process_order())
            {
                _module_router->deactivate_plugin(instance_id);
            }
        }

        // 2. Then stop the audio stream
        if (_stream)
        {
            _stream->requestStop();
            _stream->close();
            _stream.reset();
        }
    }

    bool AudioEngine::isRunning() const
    {
        return _stream && _stream->getState() == oboe::StreamState::Started;
    }

    oboe::DataCallbackResult AudioEngine::onAudioReady(
        oboe::AudioStream *oboeStream,
        void *audioData,
        int32_t numFrames)
    {
        // 1. Update frames_per_block if needed.
        if (numFrames > _frames_per_block)
        {
            _frames_per_block = numFrames;
        }

        if (!_module_router)
        {
            std::memset(audioData, 0, numFrames * _channel_count * sizeof(float));
            return oboe::DataCallbackResult::Continue;
        }

        // 2. Lock-free state swap: check if ModuleRouter has a new graph snapshot for us.
        std::unique_ptr<ModuleRouter::AudioRenderState> new_state;
        while (_module_router->_pending_states.try_dequeue(new_state))
        {
            // If we already had a state, return it to the router for cleanup.
            if (_current_render_state)
            {
                // Move ownership of the old state to the release queue
                _module_router->_released_states.enqueue(std::move(_current_render_state));
            }
            // Take ownership of the new state
            _current_render_state = std::move(new_state);
        }

        // If we have no state yet, output silence.
        if (!_current_render_state)
        {
            std::memset(audioData, 0, numFrames * _channel_count * sizeof(float));
            return oboe::DataCallbackResult::Continue;
        }

        // 3. Process modules using the current render state snapshot.
        // We use sorted_modules directly from the snapshot.
        for (PluginHost *host : _current_render_state->sorted_modules)
        {
            if (!host || !host->isPluginActive())
            {
                continue;
            }

            // --- Prepare Inputs ---
            // We need the original node ID of this host to find its connections.
            // Since PluginHost doesn't store its own ID, we can find it by looking
            // at the connections in the current state.
            // (Note: For better performance, ModuleRouter could store the ID inside PluginHost)
            uint32_t node_id = host->getInstanceId();

            std::vector<uint32_t> input_nodes;
            for (const auto &conn : _current_render_state->connections)
            {
                if (conn.to_node == node_id)
                {
                    input_nodes.push_back(conn.from_node);
                }
            }

            float **input_ptrs = nullptr;
            int input_count = 0;

            if (!input_nodes.empty())
            {
                input_ptrs = _buffer_manager.get_input_mix(input_nodes, numFrames);
                input_count = _channel_count;
            }

            // --- Prepare Outputs ---
            float **output_ptrs = _buffer_manager.get_buffer(node_id, numFrames);

            // --- Process ---
            host->processBegin(numFrames);
            host->setPorts(input_nodes.empty() ? 0 : input_count, input_ptrs, _channel_count, output_ptrs);
            host->process();
            host->processEnd(numFrames);
        }

        // --- Final Output Mix ---
        std::vector<uint32_t> output_source_nodes;
        for (const auto &conn : _current_render_state->connections)
        {
            if (conn.to_node == AudioEngine::AUDIO_OUTPUT_NODE_ID)
            {
                output_source_nodes.push_back(conn.from_node);
            }
        }

        _buffer_manager.mix_to_interleaved(output_source_nodes, static_cast<float *>(audioData), numFrames);

        return oboe::DataCallbackResult::Continue;
    }

    void AudioEngine::playNote(uint32_t instance_id, int note, double velocity)
    {
        if (_module_router)
        {
            if (auto *host = _module_router->get_plugin_instance(instance_id))
            {
                host->processNoteOn(0, 0, note, static_cast<int>(velocity * 127));
            }
        }
    }

    void AudioEngine::stopNote(uint32_t instance_id, int note)
    {
        if (_module_router)
        {
            if (auto *host = _module_router->get_plugin_instance(instance_id))
            {
                host->processNoteOff(0, 0, note, 0);
            }
        }
    }

    void AudioEngine::setParameterValue(uint32_t instance_id, clap_id param_id, double value)
    {
        if (_module_router)
        {
            if (auto *host = _module_router->get_plugin_instance(instance_id))
            {
                host->setParameterValue(param_id, value);
            }
        }
    }

} // namespace synth_canvas::host

#else
// --- Dummy implementation for non-Android platforms ---

namespace synth_canvas::host
{

    AudioEngine::AudioEngine(ModuleRouter *router) : _module_router(router)
    {
        log("[AudioEngine] Dummy: Created for non-Android. No audio processing will occur.");
    }

    AudioEngine::~AudioEngine()
    {
        log("[AudioEngine] Dummy: Destroyed for non-Android.");
    }

    bool AudioEngine::start()
    {
        log("[AudioEngine] Dummy: start called.");
        return true;
    }

    void AudioEngine::stop()
    {
        log("[AudioEngine] Dummy: stop called.");
    }

    void AudioEngine::playNote(uint32_t instance_id, int note, double velocity)
    {
        log("[AudioEngine] Dummy: playNote called. Instance: ", instance_id, " Note: ", note);
    }

    void AudioEngine::stopNote(uint32_t instance_id, int note)
    {
        log("[AudioEngine] Dummy: stopNote called. Instance: ", instance_id, " Note: ", note);
    }

    void AudioEngine::setParameterValue(uint32_t instance_id, clap_id param_id, double value)
    {
        log("[AudioEngine] Dummy: setParameterValue called.");
    }

} // namespace synth_canvas::host

#endif // defined(__ANDROID__)
