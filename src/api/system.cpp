#include "system.h"

#if defined(__ANDROID__)
#include "host/audio_engine.h"
#include "host/composite_node.h"
#include "host/logger.h"
#include "host/module_router.h"
#include "host/plugin_host.h"
#endif

#include <map>

namespace synth_canvas {

#if defined(__ANDROID__)
struct System::Impl {
    std::unique_ptr<synth_canvas::host::ModuleRouter> module_router;
    std::unique_ptr<synth_canvas::host::AudioEngine> audio_engine;
    ParameterChangedCallback on_parameter_changed;

    Impl() {
        module_router = std::make_unique<synth_canvas::host::ModuleRouter>();
        audio_engine = std::make_unique<synth_canvas::host::AudioEngine>(module_router.get());
    }
};
#else
struct System::Impl {
    ParameterChangedCallback on_parameter_changed;
};
#endif

System::System() : _pimpl(std::make_unique<Impl>()) {}
System::~System() = default;

void System::initialize(LogCallback log_cb) {
#if defined(__ANDROID__)
    synth_canvas::host::setLogCallback(log_cb);
    if (_pimpl->module_router) {
        _pimpl->module_router->on_parameter_changed =
            [this](uint32_t instance_id, uint32_t param_id, double value) -> void {
            if (_pimpl->on_parameter_changed) {
                _pimpl->on_parameter_changed(instance_id, param_id, value);
            }
        };
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
        if (type == "audio_out") {
            return synth_canvas::host::constants::kAudioOutputNoteId;
        }
        return _pimpl->module_router->registerSpecialNode();
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

void System::setCompositeParameter(uint32_t instance_id, const std::string& param_id,
                                   double value) {
#if defined(__ANDROID__)
    if (_pimpl->module_router) {
        auto* node = _pimpl->module_router->getProcessingNode(instance_id);
        if (auto* composite = dynamic_cast<synth_canvas::host::CompositeNode*>(node)) {
            composite->setCompositeParameter(param_id, value);
        }
    }
#endif
}

void System::connectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                          uint32_t to_port, ConnectionType type) {
#if defined(__ANDROID__)
    if (_pimpl->module_router) {
        _pimpl->module_router->connectNodes(from_node, from_port, to_node, to_port,
                                            type);
    }
#endif
}

void System::disconnectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                             uint32_t to_port, ConnectionType type) {
#if defined(__ANDROID__)
    if (_pimpl->module_router) {
        _pimpl->module_router->disconnectNodes(
            from_node, from_port, to_node, to_port,
            type);
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

void System::setParameterChangedCallback(ParameterChangedCallback cb) {
    _pimpl->on_parameter_changed = cb;
}

}  // namespace synth_canvas
