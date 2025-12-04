#include <godot_cpp/variant/utility_functions.hpp>
#include <queue>
#include <algorithm>
#include <cstring> // for std::memset

#include "audio_engine.h"
#include "plugin_host.h"

#if defined(__ANDROID__)

// TODO: change godot::print to custom log api

namespace synth_canvas::host
{

    AudioEngine::AudioEngine(ModuleRouter *router) : _module_router(router)
    {
        godot::UtilityFunctions::print("[AudioEngine] Created for Android.");
    }

    AudioEngine::~AudioEngine()
    {
        stop();
        godot::UtilityFunctions::print("[AudioEngine] Destroyed for Android.");
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

        // 1. Request start first to initialize stream state
        oboe::Result result = _stream->requestStart();
        if (result != oboe::Result::OK)
        {
            godot::UtilityFunctions::print("[AudioEngine] Failed to start stream. Error: ", oboe::convertToText(result));
            return false;
        }

        // 2. Try to get frames per block after start request
        _frames_per_block = _stream->getFramesPerDataCallback();

        // 3. Fallback if 0 (stream might be starting asynchronously)
        if (_frames_per_block <= 0)
        {
            _frames_per_block = 512; // Safe default
            godot::UtilityFunctions::print("[AudioEngine] Warning: Stream returned 0 frames per block. Using default: 512");
        }
        else
        {
            godot::UtilityFunctions::print("[AudioEngine] Stream started. Frames per block: ", godot::String::num_int64(_frames_per_block));
        }

        // Initialize buffer manager
        _buffer_manager.resize(_channel_count, _frames_per_block * 2); // Reserve a bit more space for safety

        // 4. Activate all plugins with the determined block size
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
        if (_stream)
        {
            _stream->requestStop();
            _stream->close();
            _stream.reset();
        }

        // Deactivate all plugins
        if (_module_router)
        {
            for (uint32_t instance_id : _module_router->get_process_order())
            {
                _module_router->deactivate_plugin(instance_id);
            }
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
        // Dynamically update frames_per_block if the actual buffer size exceeds our current setting.
        if (numFrames > _frames_per_block) {
            _frames_per_block = numFrames;
            // Note: Resizing buffer manager here might not be strictly RT safe if it allocates,
            // but we rely on AudioBufferManager::ensure_buffer to handle it or reserve enough initially.
        }

        if (!_module_router)
        {
            // Output silence if no router
            std::memset(audioData, 0, numFrames * _channel_count * sizeof(float));
            return oboe::DataCallbackResult::Continue;
        }

        const auto &process_order = _module_router->get_process_order();
        const auto &connections = _module_router->get_connections();

        // Process nodes in topological order.
        for (uint32_t node_id : process_order)
        {
            PluginHost *host = _module_router->get_plugin_instance(node_id);
            if (!host || !host->isPluginActive())
            {
                continue;
            }

            // --- Prepare Inputs ---
            std::vector<uint32_t> input_nodes;
            for (const auto &conn : connections)
            {
                if (conn.to_node == node_id)
                {
                    input_nodes.push_back(conn.from_node);
                }
            }

            float** input_ptrs = nullptr;
            int input_count = 0;

            if (!input_nodes.empty())
            {
                input_ptrs = _buffer_manager.get_input_mix(input_nodes, numFrames);
                input_count = _channel_count; // Assuming inputs match engine channel count
            }

            // --- Prepare Outputs ---
            float** output_ptrs = _buffer_manager.get_buffer(node_id, numFrames);

            // --- Process ---
            host->processBegin(numFrames);
            host->setPorts(input_nodes.empty() ? 0 : input_count, input_ptrs, _channel_count, output_ptrs);
            host->process();
            host->processEnd(numFrames);
        }

        // --- Final Output ---
        // Identify nodes connected to the final output
        std::vector<uint32_t> output_source_nodes;
        for (const auto &conn : connections)
        {
            if (conn.to_node == AudioEngine::AUDIO_OUTPUT_NODE_ID)
            {
                output_source_nodes.push_back(conn.from_node);
            }
        }

        // Mix to the interleaved Oboe output buffer
        _buffer_manager.mix_to_interleaved(output_source_nodes, static_cast<float*>(audioData), numFrames);

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
        godot::UtilityFunctions::print("[AudioEngine] Dummy: Created for non-Android. No audio processing will occur.");
    }

    AudioEngine::~AudioEngine()
    {
        godot::UtilityFunctions::print("[AudioEngine] Dummy: Destroyed for non-Android.");
    }

    bool AudioEngine::start()
    {
        godot::UtilityFunctions::print("[AudioEngine] Dummy: start called.");
        return true;
    }

    void AudioEngine::stop()
    {
        godot::UtilityFunctions::print("[AudioEngine] Dummy: stop called.");
    }

    void AudioEngine::playNote(uint32_t instance_id, int note, double velocity)
    {
        godot::UtilityFunctions::print("[AudioEngine] Dummy: playNote called. Instance: ", (int)instance_id, " Note: ", note);
    }

    void AudioEngine::stopNote(uint32_t instance_id, int note)
    {
        godot::UtilityFunctions::print("[AudioEngine] Dummy: stopNote called. Instance: ", (int)instance_id, " Note: ", note);
    }

    void AudioEngine::setParameterValue(uint32_t instance_id, clap_id param_id, double value)
    {
        godot::UtilityFunctions::print("[AudioEngine] Dummy: setParameterValue called.");
    }

} // namespace synth_canvas::host

#endif // defined(__ANDROID__)