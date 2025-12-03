#include <godot_cpp/variant/utility_functions.hpp>
#include <queue>
#include <algorithm>

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
        // This ensures newly created plugins are activated with a sufficient buffer size.
        if (numFrames > _frames_per_block) {
            _frames_per_block = numFrames;
        }

        // Ensure all intermediate buffers are correctly sized and cleared.
        // TODO: This should ideally be moved to AudioBufferManager later
        for (auto &pair : _intermediate_buffers)
        {
            auto &buffer = pair.second;
            buffer.data.resize(_channel_count);
            for (auto &channel_data : buffer.data)
            {
                channel_data.resize(numFrames);
                std::fill(channel_data.begin(), channel_data.end(), 0.0f);
            }
            buffer.channels = _channel_count;
            buffer.frames = numFrames;
        }

        if (!_module_router)
        {
            // Output silence if no router
            memset(audioData, 0, numFrames * _channel_count * sizeof(float));
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

            // Ensure buffer exists for this node
            if (_intermediate_buffers.find(node_id) == _intermediate_buffers.end())
            {
                _intermediate_buffers[node_id] = AudioBuffer();
                // Resize will happen in next callback cycle or we can force it here if needed,
                // but for real-time safety avoiding allocation here is better.
                // For now, let's assume it was created during create_plugin_instance in System.
                // Actually, System/Router needs to notify Engine about new plugins to allocate buffers.
                // For this step, we'll do a quick check/resize which is not RT safe but functional.
                auto &buf = _intermediate_buffers[node_id];
                buf.data.resize(_channel_count);
                for (auto &ch : buf.data)
                    ch.resize(numFrames, 0.0f);
                buf.channels = _channel_count;
                buf.frames = numFrames;
            }

            // --- Prepare Inputs ---
            std::vector<float *> input_pointers;
            std::vector<std::vector<float>> summed_inputs;
            bool has_input = false;

            for (const auto &conn : connections)
            {
                if (conn.to_node == node_id)
                {
                    has_input = true;
                    break;
                }
            }

            if (has_input)
            {
                summed_inputs.resize(_channel_count, std::vector<float>(numFrames, 0.0f));
                for (const auto &conn : connections)
                {
                    if (conn.to_node == node_id)
                    {
                        auto it = _intermediate_buffers.find(conn.from_node);
                        if (it != _intermediate_buffers.end())
                        {
                            AudioBuffer &source_buffer = it->second;
                            // Sum de-interleaved buffers directly
                            for (uint32_t ch = 0; ch < _channel_count; ++ch)
                            {
                                for (uint32_t frame = 0; frame < numFrames; ++frame)
                                {
                                    if (ch < source_buffer.channels && frame < source_buffer.frames)
                                    {
                                        summed_inputs[ch][frame] += source_buffer.data[ch][frame];
                                    }
                                }
                            }
                        }
                    }
                }

                for (uint32_t i = 0; i < _channel_count; ++i)
                {
                    input_pointers.push_back(summed_inputs[i].data());
                }
            }

            // --- Prepare Outputs ---
            AudioBuffer &output_audio_buffer = _intermediate_buffers[node_id];
            std::vector<float *> output_pointers;
            for (uint32_t i = 0; i < output_audio_buffer.channels; ++i)
            {
                output_pointers.push_back(output_audio_buffer.data[i].data());
            }

            // --- Process ---
            host->processBegin(numFrames);
            host->setPorts(has_input ? input_pointers.size() : 0, has_input ? input_pointers.data() : nullptr, output_pointers.size(), output_pointers.data());
            host->process();
            host->processEnd(numFrames);
        }

        // --- Final Output ---
        // Mix all nodes connected to the designated AUDIO_OUTPUT_NODE_ID into a final de-interleaved buffer.
        std::vector<std::vector<float>> final_mix(_channel_count, std::vector<float>(numFrames, 0.0f));

        for (const auto &conn : connections)
        {
            if (conn.to_node == AudioEngine::AUDIO_OUTPUT_NODE_ID)
            {
                auto it = _intermediate_buffers.find(conn.from_node);
                if (it != _intermediate_buffers.end())
                {
                    AudioBuffer &source_buffer = it->second;
                    // Additively mix (sum) the de-interleaved source buffer into the final mix buffer.
                    for (uint32_t ch = 0; ch < _channel_count; ++ch)
                    {
                        for (uint32_t frame = 0; frame < numFrames; ++frame)
                        {
                            if (ch < source_buffer.channels && frame < source_buffer.frames)
                            {
                                final_mix[ch][frame] += source_buffer.data[ch][frame];
                            }
                        }
                    }
                }
            }
        }

        // Interleave the final mixed audio into the output buffer provided by Oboe.
        float *outputBuffer = static_cast<float *>(audioData);
        for (int32_t frame = 0; frame < numFrames; ++frame)
        {
            for (int32_t ch = 0; ch < _channel_count; ++ch)
            {
                outputBuffer[frame * _channel_count + ch] = final_mix[ch][frame];
            }
        }

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