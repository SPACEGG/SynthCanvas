#include "system.h"

#if defined(__ANDROID__)
#include <clap/clap.h>

#include "host/audio_engine.h"
#include "host/graph_types.h"
#include "host/logger.h"
#include "host/module_router.h"
#include "host/plugin_host.h"

#endif

namespace synth_canvas {

#if defined(__ANDROID__)
struct System::Impl {
    std::unique_ptr<synth_canvas::host::ModuleRouter> module_router;
    std::unique_ptr<synth_canvas::host::AudioEngine> audio_engine;
    EventOccuredCallback on_event_occured;

    Impl() {
        module_router = std::make_unique<synth_canvas::host::ModuleRouter>();
        audio_engine = std::make_unique<synth_canvas::host::AudioEngine>(module_router.get());
    }

    void handleInternalEvent(uint32_t instance_id, const synth_canvas::host::PluginEvent& ev) {
        if (!on_event_occured) return;

        SystemEvent sev;
        sev.instance_id = instance_id;

        switch (ev.event.header.type) {
            case CLAP_EVENT_NOTE_ON:
                sev.type = SystemEventType::kNoteOn;
                sev.data.note.port_index = ev.event.note.port_index;
                sev.data.note.key = ev.event.note.key;
                sev.data.note.channel = ev.event.note.channel;
                sev.data.note.velocity = ev.event.note.velocity;
                sev.data.note.note_id = ev.event.note.note_id;
                break;
            case CLAP_EVENT_NOTE_OFF:
                sev.type = SystemEventType::kNoteOff;
                sev.data.note.port_index = ev.event.note.port_index;
                sev.data.note.key = ev.event.note.key;
                sev.data.note.channel = ev.event.note.channel;
                sev.data.note.velocity = ev.event.note.velocity;
                sev.data.note.note_id = ev.event.note.note_id;
                break;
            case CLAP_EVENT_NOTE_CHOKE:
                sev.type = SystemEventType::kNoteChoke;
                sev.data.note.port_index = ev.event.note.port_index;
                sev.data.note.key = ev.event.note.key;
                sev.data.note.channel = ev.event.note.channel;
                break;
            case CLAP_EVENT_NOTE_EXPRESSION:
                sev.type = SystemEventType::kNoteExpression;
                sev.data.note.key = ev.event.note.key;
                sev.data.note.velocity = ev.event.note.velocity;
                break;
            case CLAP_EVENT_PARAM_VALUE:
                sev.type = SystemEventType::kParameterValue;
                sev.data.parameter.param_id = ev.event.param_value.param_id;
                sev.data.parameter.value = ev.event.param_value.value;
                sev.data.parameter.key = ev.event.param_value.key;
                sev.data.parameter.channel = ev.event.param_value.channel;
                break;
            case CLAP_EVENT_PARAM_MOD:
                sev.type = SystemEventType::kParameterMod;
                sev.data.parameter.param_id = ev.event.param_mod.param_id;
                sev.data.parameter.value = ev.event.param_mod.amount;
                sev.data.parameter.key = ev.event.param_mod.key;
                sev.data.parameter.channel = ev.event.param_mod.channel;
                break;
            case CLAP_EVENT_MIDI:
                sev.type = SystemEventType::kMidi;
                sev.data.midi.port_index = ev.event.midi.port_index;
                std::memcpy(sev.data.midi.data.data(), ev.event.midi.data, 3);
                break;
            default:
                return;
        }
        on_event_occured(sev);
    }
};
#else
struct System::Impl {
    EventOccuredCallback on_event_occured;
};
#endif

System::System() : _pimpl(std::make_unique<Impl>()) {}
System::~System() = default;

void System::initialize(LogCallback log_cb) {
#if defined(__ANDROID__)
    synth_canvas::host::setLogCallback(log_cb);
    if (_pimpl->module_router) {
        _pimpl->module_router->setEventCallback(
            [this](uint32_t instance_id, const synth_canvas::host::PluginEvent& ev) {
                _pimpl->handleInternalEvent(instance_id, ev);
            });
    }
#else
    if (log_cb) log_cb("[System] Initializing (Dummy/Windows)...");
#endif
}

void System::terminate() { stopAudio(); }

void System::update(double delta) {
#if defined(__ANDROID__)
    if (_pimpl->module_router) {
        _pimpl->module_router->pollAllMainThreads();
    }
#endif
}

auto System::createPluginInstance(const std::string& path) -> uint32_t {
#if defined(__ANDROID__)
    if (!_pimpl->module_router) return 0;
    uint32_t id = _pimpl->module_router->createPluginInstance(path);
    if (id != 0 && _pimpl->audio_engine && _pimpl->audio_engine->isRunning()) {
        int32_t rate = _pimpl->audio_engine->getSampleRate();
        int32_t frames = _pimpl->audio_engine->getFramesPerBlock();
        _pimpl->module_router->activateNode(id, rate, frames);
    }
    return id;
#else
    return 100;
#endif
}

void System::destroyInstance(uint32_t instance_id) {
#if defined(__ANDROID__)
    if (_pimpl->module_router) {
        _pimpl->module_router->deactivateNode(instance_id);
        _pimpl->module_router->destroyInstance(instance_id);
    }
#endif
}

auto System::registerSpecialNode(const std::string& type) -> uint32_t {
#if defined(__ANDROID__)
    if (_pimpl->module_router) {
        uint32_t id = _pimpl->module_router->registerSpecialNode(type);
        if (id != 0 && _pimpl->audio_engine && _pimpl->audio_engine->isRunning()) {
            int32_t rate = _pimpl->audio_engine->getSampleRate();
            int32_t frames = _pimpl->audio_engine->getFramesPerBlock();
            _pimpl->module_router->activateNode(id, rate, frames);
        }
        return id;
    }
    return 0;
#else
    return (type == "audio_out") ? 0 : 200;
#endif
}

auto System::createCompositeInstance(const CompositeConfig& config) -> uint32_t {
#if defined(__ANDROID__)
    if (!_pimpl->module_router) return 0;

    uint32_t id = _pimpl->module_router->createCompositeInstance(config);

    if (id != 0 && _pimpl->audio_engine && _pimpl->audio_engine->isRunning()) {
        int32_t rate = _pimpl->audio_engine->getSampleRate();
        int32_t frames = _pimpl->audio_engine->getFramesPerBlock();
        _pimpl->module_router->activateNode(id, rate, frames);
    }

    return id;
#else
    return 300;
#endif
}

void System::setParameterValue(uint32_t instance_id, const std::string& param_id, double value) {
#if defined(__ANDROID__)
    if (_pimpl->module_router) {
        if (auto* node = _pimpl->module_router->getProcessingNode(instance_id)) {
            node->setParameterValue(param_id, value);
        }
    }
#endif
}

void System::connectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                          uint32_t to_port, ConnectionType type) {
#if defined(__ANDROID__)
    if (_pimpl->module_router) {
        _pimpl->module_router->connectNodes(from_node, from_port, to_node, to_port, type);
    }
#endif
}

void System::disconnectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                             uint32_t to_port, ConnectionType type) {
#if defined(__ANDROID__)
    if (_pimpl->module_router) {
        _pimpl->module_router->disconnectNodes(from_node, from_port, to_node, to_port, type);
    }
#endif
}

void System::startAudio() {
#if defined(__ANDROID__)
    if (_pimpl->audio_engine) _pimpl->audio_engine->start();
#endif
}

void System::stopAudio() {
#if defined(__ANDROID__)
    if (_pimpl->audio_engine) _pimpl->audio_engine->stop();
#endif
}

void System::playNote(uint32_t instance_id, int note, double velocity, int32_t note_id) {
#if defined(__ANDROID__)
    if (_pimpl->audio_engine) _pimpl->audio_engine->playNote(instance_id, note, velocity, note_id);
#endif
}

void System::stopNote(uint32_t instance_id, int note, double velocity, int32_t note_id) {
#if defined(__ANDROID__)
    if (_pimpl->audio_engine) _pimpl->audio_engine->stopNote(instance_id, note, velocity, note_id);
#endif
}

void System::setParameterValue(uint32_t instance_id, uint32_t param_id, double value) {
#if defined(__ANDROID__)
    if (_pimpl->audio_engine) _pimpl->audio_engine->setParameterValue(instance_id, param_id, value);
#endif
}

auto System::getPluginParameters(uint32_t instance_id) -> ParameterList {
    ParameterList result;
#if defined(__ANDROID__)
    if (!_pimpl->module_router) return result;
    auto* node = _pimpl->module_router->getProcessingNode(instance_id);
    if (!node) return result;

    for (const auto& param_slot : node->getParameters()) {
        result.push_back({param_slot->info.id, param_slot->info.name, param_slot->info.module,
                          param_slot->info.min_value, param_slot->info.max_value,
                          param_slot->info.default_value, param_slot->base_value.load()});
    }
#endif
    return result;
}

void System::playNoteFromNode(uint32_t from_node_id, int note, double velocity) {
#if defined(__ANDROID__)
    if (_pimpl->module_router && _pimpl->audio_engine) {
        for (const auto& conn : _pimpl->module_router->getConnections()) {
            if (conn.from_node == from_node_id) {
                _pimpl->audio_engine->playNote(conn.to_node, note, velocity);
            }
        }
    }
#endif
}

void System::stopNoteFromNode(uint32_t from_node_id, int note) {
#if defined(__ANDROID__)
    if (_pimpl->module_router && _pimpl->audio_engine) {
        for (const auto& conn : _pimpl->module_router->getConnections()) {
            if (conn.from_node == from_node_id) {
                _pimpl->audio_engine->stopNote(conn.to_node, note);
            }
        }
    }
#endif
}

void System::setEventOccuredCallback(EventOccuredCallback cb) { _pimpl->on_event_occured = cb; }

}  // namespace synth_canvas
