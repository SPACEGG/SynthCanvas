#include "audio_engine.h"

#include <cstring>

#include "logger.h"
#include "plugin_host.h"

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

    // 2. Lock-free state swap
    updateRenderState();

    // If we have no state yet, output silence.
    if (!_current_render_state) {
        std::memset(audio_data, 0, num_frames * _channel_count * sizeof(float));
        return oboe::DataCallbackResult::Continue;
    }

    // 3. Process modules using the current render state snapshot.
    for (PluginHost* host : _current_render_state->sorted_modules) {
        if (!host || !host->isPluginActive()) {
            continue;
        }

        processSinglePlugin(host, num_frames);
        routePluginOutputs(host);
    }

    // --- Final Output Mix ---
    _buffer_manager.mixToInterleaved(_current_render_state->master_output_sources,
                                     static_cast<float*>(audio_data), num_frames);

    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::updateRenderState() {
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
}

void AudioEngine::processSinglePlugin(PluginHost* host, int32_t num_frames) {
    uint32_t node_id = host->getInstanceId();

    // 1. Process Parameter Modulation
    auto mod_it = _current_render_state->input_modulations.find(node_id);
    if (mod_it != _current_render_state->input_modulations.end()) {
        const auto& mod_sources = mod_it->second;
        for (const auto& mod : mod_sources) {
            // Get source buffer (read-only)
            float** src_buffer =
                _buffer_manager.getReadOnlyBuffer(mod.source_node_id, mod.source_port_index);
            if (!src_buffer) continue;

            // Simple Control Rate Modulation: Take the first sample of the first channel.
            // TODO(): Implement Audio Rate Modulation (sample-accurate) later.
            // TODO(): Handle stereo sources (mix or left channel?). Currently using Left (0).
            float mod_value = src_buffer[0][0];

            // Check if source port index matches output channel?
            // Currently assuming source port maps to audio channel index directly.
            // If source_port_index > 0, we should use that channel if available.
            if (mod.source_port_index < (static_cast<uint32_t>(_channel_count))) {
                mod_value = src_buffer[mod.source_port_index][0];
            }

            // Update parameter
            host->processParamModulation(mod.target_param_id, static_cast<double>(mod_value), 0);
        }
    }

    // 2. Prepare Audio Inputs
    const auto& input_ports = host->getAudioPorts(true);
    std::vector<clap_audio_buffer> clap_inputs(input_ports.size());

    // We need to keep the vectors of PortSource alive until process() is done
    std::vector<std::vector<AudioBufferManager::PortSource>> input_sources_storage;
    input_sources_storage.reserve(input_ports.size());

    auto input_map_it = _current_render_state->input_audio_sources.find(node_id);

    for (size_t i = 0; i < input_ports.size(); ++i) {
        const auto& port_info = input_ports[i];
        clap_inputs[i].channel_count = port_info.clap_info.channel_count;
        clap_inputs[i].constant_mask = 0;
        clap_inputs[i].latency = 0;
        clap_inputs[i].data64 = nullptr;

        std::vector<AudioBufferManager::PortSource> sources;
        if (input_map_it != _current_render_state->input_audio_sources.end()) {
            auto port_sources_it = input_map_it->second.find(port_info.index);
            if (port_sources_it != input_map_it->second.end()) {
                sources = port_sources_it->second;
            }
        }

        // getInputMix takes care of zeroing/mixing
        clap_inputs[i].data32 = _buffer_manager.getInputMix(port_info.index, sources, num_frames);
    }

    // 3. Prepare Audio Outputs
    const auto& output_ports = host->getAudioPorts(false);
    std::vector<clap_audio_buffer> clap_outputs(output_ports.size());

    for (size_t i = 0; i < output_ports.size(); ++i) {
        const auto& port_info = output_ports[i];
        clap_outputs[i].channel_count = port_info.clap_info.channel_count;
        clap_outputs[i].constant_mask = 0;
        clap_outputs[i].latency = 0;
        clap_outputs[i].data64 = nullptr;

        clap_outputs[i].data32 = _buffer_manager.getBuffer(node_id, port_info.index, num_frames);
    }

    // --- Process ---
    host->processBegin(num_frames);
    host->setPorts(static_cast<uint32_t>(clap_inputs.size()), clap_inputs.data(),
                   static_cast<uint32_t>(clap_outputs.size()), clap_outputs.data());
    host->process();
    host->processEnd(num_frames);
}

void AudioEngine::routePluginOutputs(PluginHost* host) {
    auto& output_queue = host->getAudioThreadOutputQueue();
    uint32_t source_node_id = host->getInstanceId();

    const std::vector<PluginHost*>* targets = nullptr;
    auto it = _current_render_state->output_event_targets.find(source_node_id);
    if (it != _current_render_state->output_event_targets.end()) {
        targets = &it->second;
    }

    if (!targets || targets->empty()) {
        PluginHost::PluginEvent dummy;
        while (output_queue.try_dequeue(dummy)) {
        }
        return;
    }

    PluginHost::PluginEvent ev;
    while (output_queue.try_dequeue(ev)) {
        for (PluginHost* target_host : *targets) {
            target_host->queueEvent(ev);
        }
    }
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