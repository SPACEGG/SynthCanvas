#include "audio_engine.h"

#include <cstring>

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

    _renderer.render(*_current_render_state, _buffer_manager, num_frames,
                     [this](ProcessingNode* source, const PluginEvent& ev, uint32_t port_index) {
                         this->handleEvent(source, ev, port_index);
                     });

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

void AudioEngine::handleEvent(ProcessingNode* source, const PluginEvent& ev, uint32_t port_index) {
    // TODO(): AudioEngine is the root. Events reaching here are for the system's MIDI output.
}

void AudioEngine::playNote(uint32_t instance_id, int note, double velocity, int32_t note_id) {
    if (_module_router) {
        if (auto* node = _module_router->getProcessingNode(instance_id)) {
            PluginEvent ev;
            ev.event.header.size = sizeof(clap_event_note);
            ev.event.header.time = 0;
            ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev.event.header.type = CLAP_EVENT_NOTE_ON;
            ev.event.header.flags = 0;

            ev.event.note.port_index = 0;
            ev.event.note.key = static_cast<int16_t>(note);
            ev.event.note.channel = 0;
            ev.event.note.note_id = note_id;
            ev.event.note.velocity = velocity;

            node->queueEvent(ev);
        }
    }
}

void AudioEngine::stopNote(uint32_t instance_id, int note, double velocity, int32_t note_id) {
    if (_module_router) {
        if (auto* node = _module_router->getProcessingNode(instance_id)) {
            PluginEvent ev;
            ev.event.header.size = sizeof(clap_event_note);
            ev.event.header.time = 0;
            ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev.event.header.type = CLAP_EVENT_NOTE_OFF;
            ev.event.header.flags = 0;

            ev.event.note.port_index = 0;
            ev.event.note.key = static_cast<int16_t>(note);
            ev.event.note.channel = 0;
            ev.event.note.note_id = note_id;
            ev.event.note.velocity = velocity;

            node->queueEvent(ev);
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
