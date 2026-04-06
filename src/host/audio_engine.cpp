#include "audio_engine.h"

#include <cstring>

#include "host/plugin_host.h"
#include "logger.h"
#include "processing_node.h"

namespace synth_canvas::host {

AudioEngine::AudioEngine(ModuleRouter* router) : _module_router(router) {
    _sample_rate = constants::kUnspecifiedSampleRate;
    log("[AudioEngine] Created.");
}

AudioEngine::~AudioEngine() {
    stop();
    log("[AudioEngine] Destroyed.");
}

auto AudioEngine::openStream() -> bool {
    if (_stream) return true;

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setUsage(oboe::Usage::Game)
        ->setContentType(oboe::ContentType::Sonification)
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
    if (!_stream && !openStream()) return false;
    if (_stream->getState() == oboe::StreamState::Started) return true;

    oboe::Result result = _stream->requestStart();
    if (result != oboe::Result::OK) {
        log("[AudioEngine] Failed to start stream. Error: ", oboe::convertToText(result));
        return false;
    }

    _frames_per_block = _stream->getFramesPerDataCallback();
    if (_frames_per_block <= 0) {
        _frames_per_block = _stream->getFramesPerBurst();
        if (_frames_per_block <= 0) _frames_per_block = constants::kDefaultFramesPerBlock;
    }

    _buffer_manager.resize(_channel_count,
                           _frames_per_block * constants::kBufferCapacityMultiplier);

    if (_module_router) {
        for (uint32_t id : _module_router->getProcessOrder()) {
            _module_router->activateNode(id, _sample_rate, _frames_per_block);
        }
    }

    return true;
}

void AudioEngine::stop() {
    if (_module_router) {
        for (uint32_t id : _module_router->getProcessOrder()) {
            _module_router->deactivateNode(id);
        }
    }

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
    if (num_frames > _frames_per_block) _frames_per_block = num_frames;

    if (!_module_router) {
        std::memset(audio_data, 0, num_frames * _channel_count * sizeof(float));
        return oboe::DataCallbackResult::Continue;
    }

    updateRenderState();

    if (!_current_render_state) {
        std::memset(audio_data, 0, num_frames * _channel_count * sizeof(float));
        return oboe::DataCallbackResult::Continue;
    }

    for (ProcessingNode* node : _current_render_state->sorted_nodes) {
        if (!node || !node->isActive()) continue;

        processSingleNode(node, num_frames);
        routeNodeEvents(node);
    }

    _buffer_manager.mixToInterleaved(_current_render_state->master_output_sources,
                                     static_cast<float*>(audio_data), num_frames);

    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::updateRenderState() {
    std::unique_ptr<ModuleRouter::AudioRenderState> new_state;
    while (_module_router->pending_states.try_dequeue(new_state)) {
        if (_current_render_state) {
            _module_router->released_states.enqueue(std::move(_current_render_state));
        }
        _current_render_state = std::move(new_state);
    }
}

void AudioEngine::processSingleNode(ProcessingNode* node, int32_t num_frames) {
    uint32_t node_id = node->getInstanceId();

    // 1. Process Parameter Modulation
    auto mod_it = _current_render_state->input_modulations.find(node_id);
    if (mod_it != _current_render_state->input_modulations.end()) {
        for (const auto& mod : mod_it->second) {
            float** src_buffer =
                _buffer_manager.getReadOnlyBuffer(mod.source_node_id, mod.source_port_index);
            if (!src_buffer) continue;

            float mod_value = src_buffer[0][0];
            if (mod.source_port_index < static_cast<uint32_t>(_channel_count)) {
                mod_value = src_buffer[mod.source_port_index][0];
            }
            // Use the generic interface to set modulation
            // TODO(): In the future, processParamModulation could be part of the interface
            // For now, we assume setParameterValue or specialized note handling is enough
            node->setParameterValue(mod.target_param_id, static_cast<double>(mod_value));
        }
    }

    // 2. Prepare Audio Inputs
    const auto& input_ports = node->getAudioPorts(true);
    std::vector<clap_audio_buffer> clap_inputs(input_ports.size());

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
        clap_inputs[i].data32 = _buffer_manager.getInputMix(port_info.index, sources, num_frames);
    }

    // 3. Prepare Audio Outputs
    const auto& output_ports = node->getAudioPorts(false);
    std::vector<clap_audio_buffer> clap_outputs(output_ports.size());

    for (size_t i = 0; i < output_ports.size(); ++i) {
        const auto& port_info = output_ports[i];
        clap_outputs[i].channel_count = port_info.clap_info.channel_count;
        clap_outputs[i].constant_mask = 0;
        clap_outputs[i].latency = 0;
        clap_outputs[i].data64 = nullptr;
        clap_outputs[i].data32 = _buffer_manager.getBuffer(node_id, port_info.index, num_frames);
    }

    // 4. Execute Processing
    node->processBegin(num_frames);
    node->setPorts(static_cast<uint32_t>(clap_inputs.size()), clap_inputs.data(),
                   static_cast<uint32_t>(clap_outputs.size()), clap_outputs.data());
    node->process();
    node->processEnd(num_frames);
}

void AudioEngine::routeNodeEvents(ProcessingNode* node) {
    if (auto* plugin_host = dynamic_cast<PluginHost*>(node)) {
        auto& output_queue = plugin_host->getAudioThreadOutputQueue();
        uint32_t source_node_id = node->getInstanceId();

        auto it = _current_render_state->output_event_targets.find(source_node_id);
        if (it == _current_render_state->output_event_targets.end() || it->second.empty()) {
            PluginEvent dummy;
            while (output_queue.try_dequeue(dummy)) {
            }
            return;
        }

        PluginEvent ev;
        while (output_queue.try_dequeue(ev)) {
            for (ProcessingNode* target : it->second) {
                target->queueEvent(ev);
            }
        }
    }
    // CompositeNodes will handle their own internal event routing during their process() call
}

void AudioEngine::playNote(uint32_t instance_id, int note, double velocity, int32_t note_id) {
    if (_module_router) {
        if (auto* node = _module_router->getProcessingNode(instance_id)) {
            if (auto* host = dynamic_cast<PluginHost*>(node)) {
                host->processNoteOn(0, 0, note, velocity, note_id);
            }
        }
    }
}

void AudioEngine::stopNote(uint32_t instance_id, int note, double velocity, int32_t note_id) {
    if (_module_router) {
        if (auto* node = _module_router->getProcessingNode(instance_id)) {
            if (auto* host = dynamic_cast<PluginHost*>(node)) {
                host->processNoteOff(0, 0, note, velocity, note_id);
            }
        }
    }
}

void AudioEngine::setParameterValue(uint32_t instance_id, clap_id param_id, double value) {
    if (_module_router) {
        if (auto* node = _module_router->getProcessingNode(instance_id)) {
            node->setParameterValue(param_id, value);
        }
    }
}

}  // namespace synth_canvas::host
