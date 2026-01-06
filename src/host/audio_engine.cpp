#include "audio_engine.h"

#include <cstring>

#include "logger.h"
#include "plugin_host.h"

#if defined(__ANDROID__)

namespace synth_canvas::host {

AudioEngine::AudioEngine(ModuleRouter* router) : _module_router(router) {
    // Use UNSPECIFIED (0) to let Oboe choose the optimal native sample rate
    _sample_rate = constants::kUnspecifiedSampleRate;
    log("[AudioEngine] Created for Android.");
}

AudioEngine::~AudioEngine() {
    stop();
    log("[AudioEngine] Destroyed for Android.");
}

auto AudioEngine::openStream() -> bool {
    if (_stream) {
        return true;
    }

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setUsage(oboe::Usage::Game)
        ->setContentType(oboe::ContentType::Music)
        ->setChannelCount(_channel_count)
        ->setSampleRate(constants::kDefaultSampleRate)
        ->setDataCallback(this);

    oboe::Result result = builder.openStream(_stream);
    if (result != oboe::Result::OK) {
        log("[AudioEngine] Failed to create stream. Error: ", oboe::convertToText(result));
        _stream.reset();
        return false;
    }

    _sample_rate = _stream->getSampleRate();

    return true;
}

auto AudioEngine::start() -> bool {
    if (!_stream && !openStream()) {
        return false;
    }
    if (_stream->getState() == oboe::StreamState::Started) {
        return true;
    }

    oboe::Result result = _stream->requestStart();
    if (result != oboe::Result::OK) {
        log("[AudioEngine] Failed to start stream. Error: ", oboe::convertToText(result));
        return false;
    }

    _frames_per_block = _stream->getFramesPerDataCallback();

    if (_frames_per_block <= 0) {
        // Try to use the hardware burst size as a better default
        _frames_per_block = _stream->getFramesPerBurst();

        if (_frames_per_block > 0) {
            log("[AudioEngine] Stream using variable callback size. Initializing with burst size: ",
                _frames_per_block);
        } else {
            _frames_per_block = constants::kDefaultFramesPerBlock;  // Safe default
            log("[AudioEngine] Warning: Stream returned 0 frames per block and unknown burst size. "
                "Using default: ",
                constants::kDefaultFramesPerBlock);
        }
    } else {
        log("[AudioEngine] Stream started. Frames per block: ", _frames_per_block);
    }

    // Optimize buffer size for low latency (Double buffering)
    int32_t burst_size = _stream->getFramesPerBurst();
    if (burst_size > 0) {
        _stream->setBufferSizeInFrames(burst_size * 2);
        log("[AudioEngine] Buffer size set to: ", _stream->getBufferSizeInFrames());
    }

    _buffer_manager.resize(
        _channel_count,
        _frames_per_block *
            constants::kBufferCapacityMultiplier);  // Reserve a bit more space for safety

    if (_module_router) {
        for (uint32_t instance_id : _module_router->getProcessOrder()) {
            _module_router->activatePlugin(instance_id, _sample_rate, _frames_per_block);
        }
    }

    return true;
}

void AudioEngine::stop() {
    // 1. Deactivate all plugins first (while audio thread is still active)
    if (_module_router) {
        for (uint32_t instance_id : _module_router->getProcessOrder()) {
            _module_router->deactivatePlugin(instance_id);
        }
    }

    // 2. Then stop the audio stream
    if (_stream) {
        _stream->requestStop();
        _stream->close();
        _stream.reset();
    }
}

auto AudioEngine::isRunning() const -> bool {
    return _stream && _stream->getState() == oboe::StreamState::Started;
}

auto AudioEngine::onAudioReady(oboe::AudioStream* oboe_stream, void* audio_data, int32_t num_frames)
    -> oboe::DataCallbackResult {
    // 1. Update frames_per_block if needed.
    if (num_frames > _frames_per_block) {
        _frames_per_block = num_frames;
    }

    if (!_module_router) {
        std::memset(audio_data, 0, num_frames * _channel_count * sizeof(float));
        return oboe::DataCallbackResult::Continue;
    }

    // 2. Lock-free state swap: check if ModuleRouter has a new graph snapshot for us.
    std::unique_ptr<ModuleRouter::AudioRenderState> new_state;
    while (_module_router->pending_states.try_dequeue(new_state)) {
        // If we already had a state, return it to the router for cleanup.
        if (_current_render_state) {
            // Move ownership of the old state to the release queue
            _module_router->released_states.enqueue(std::move(_current_render_state));
        }
        // Take ownership of the new state
        _current_render_state = std::move(new_state);
    }

    // If we have no state yet, output silence.
    if (!_current_render_state) {
        std::memset(audio_data, 0, num_frames * _channel_count * sizeof(float));
        return oboe::DataCallbackResult::Continue;
    }

    // 3. Process modules using the current render state snapshot.
    // We use sorted_modules directly from the snapshot.
    for (PluginHost* host : _current_render_state->sorted_modules) {
        if (!host || !host->isPluginActive()) {
            continue;
        }

        // --- Prepare Inputs ---
        // We need the original node ID of this host to find its connections.
        // Since PluginHost doesn't store its own ID, we can find it by looking
        // at the connections in the current state.
        // (Note: For better performance, ModuleRouter could store the ID inside PluginHost)
        uint32_t node_id = host->getInstanceId();

        std::vector<uint32_t> input_nodes;
        for (const auto& conn : _current_render_state->connections) {
            if (conn.to_node == node_id) {
                input_nodes.push_back(conn.from_node);
            }
        }

        float** input_ptrs = nullptr;
        int input_count = 0;

        if (!input_nodes.empty()) {
            input_ptrs = _buffer_manager.getInputMix(input_nodes, num_frames);
            input_count = _channel_count;
        }

        // --- Prepare Outputs ---
        float** output_ptrs = _buffer_manager.getBuffer(node_id, num_frames);

        // --- Process ---
        host->processBegin(num_frames);
        host->setPorts(input_nodes.empty() ? 0 : input_count, input_ptrs, _channel_count,
                       output_ptrs);
        host->process();
        host->processEnd(num_frames);
    }

    // --- Final Output Mix ---
    std::vector<uint32_t> output_source_nodes;
    for (const auto& conn : _current_render_state->connections) {
        if (conn.to_node == constants::kAudioOutputNoteId) {
            output_source_nodes.push_back(conn.from_node);
        }
    }

    _buffer_manager.mixToInterleaved(output_source_nodes, static_cast<float*>(audio_data),
                                     num_frames);

    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::playNote(uint32_t instance_id, int note, double velocity, int32_t note_id) {
    if (_module_router) {
        if (auto* host = _module_router->getPluginInstance(instance_id)) {
            host->processNoteOn(0, 0, note, velocity, note_id);
        }
    }
}

void AudioEngine::stopNote(uint32_t instance_id, int note, double velocity, int32_t note_id) {
    if (_module_router) {
        if (auto* host = _module_router->getPluginInstance(instance_id)) {
            host->processNoteOff(0, 0, note, velocity, note_id);
        }
    }
}

void AudioEngine::setParameterValue(uint32_t instance_id, clap_id param_id, double value) {
    if (_module_router) {
        if (auto* host = _module_router->getPluginInstance(instance_id)) {
            host->setParameterValue(param_id, value);
        }
    }
}

}  // namespace synth_canvas::host

#else
// --- Dummy implementation for non-Android platforms ---

namespace synth_canvas::host {

AudioEngine::AudioEngine(ModuleRouter* router) : _module_router(router) {
    log("[AudioEngine] Dummy: Created for non-Android. No audio processing will occur.");
}

AudioEngine::~AudioEngine() { log("[AudioEngine] Dummy: Destroyed for non-Android."); }

bool AudioEngine::start() {
    log("[AudioEngine] Dummy: start called.");
    return true;
}

void AudioEngine::stop() { log("[AudioEngine] Dummy: stop called."); }

void AudioEngine::playNote(uint32_t instance_id, int note, double velocity, int32_t note_id) {
    log("[AudioEngine] Dummy: playNote called. Instance: ", instance_id, " Note: ", note);
}

void AudioEngine::stopNote(uint32_t instance_id, int note, double velocity, int32_t note_id) {
    log("[AudioEngine] Dummy: stopNote called. Instance: ", instance_id, " Note: ", note);
}

void AudioEngine::setParameterValue(uint32_t instance_id, clap_id param_id, double value) {
    log("[AudioEngine] Dummy: setParameterValue called.");
}

}  // namespace synth_canvas::host

#endif  // defined(__ANDROID__)
