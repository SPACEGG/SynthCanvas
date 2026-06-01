#include "system.h"

#include <thread>

#include "types.h"

#if defined(__ANDROID__)
#include <clap/clap.h>

#include "host/engine/audio_engine.h"
#include "host/engine/module_router.h"
#include "host/graph/graph_types.h"
#include "host/nodes/wrappers/plugin_host.h"
#include "utils/logger.h"

#endif

namespace synth_canvas {

#if defined(__ANDROID__)
struct System::Impl {
    std::unique_ptr<synth_canvas::host::ModuleRouter> module_router;
    std::unique_ptr<synth_canvas::host::AudioEngine> audio_engine;
    EventOccuredCallback on_event_occured;
    std::function<void(uint32_t)> on_params_rescan;
    TransportState transport_state;

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
    TransportState transport_state;
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

        _pimpl->module_router->on_connection_pruned =
            [this](const synth_canvas::host::PortConnection& conn) {
                if (_pimpl->on_event_occured) {
                    SystemEvent sev;
                    sev.type = SystemEventType::kConnectionDisconnected;
                    sev.instance_id = 0;
                    sev.data.connection.from_node = conn.from_node;
                    sev.data.connection.from_port = conn.from_port;
                    sev.data.connection.to_node = conn.to_node;
                    sev.data.connection.to_port = conn.to_port;
                    sev.data.connection.type = static_cast<int32_t>(conn.type);
                    _pimpl->on_event_occured(sev);
                }
            };

        _pimpl->module_router->on_node_ports_changed = [this](uint32_t id, uint32_t inputs,
                                                              uint32_t outputs) {
            if (_pimpl->on_event_occured) {
                SystemEvent sev;
                sev.type = SystemEventType::kNodePortsChanged;
                sev.instance_id = id;
                sev.data.port_change.input_count = inputs;
                sev.data.port_change.output_count = outputs;
                _pimpl->on_event_occured(sev);
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
    if (id != 0) {
        if (auto* node = _pimpl->module_router->getProcessingNode(id)) {
            node->on_params_rescan = [this](uint32_t instance_id) {
                if (_pimpl->on_params_rescan) _pimpl->on_params_rescan(instance_id);
            };
            node->on_ports_changed = [this](uint32_t instance_id) {
                if (_pimpl->module_router) _pimpl->module_router->updateNodePorts(instance_id);
            };
        }

        if (_pimpl->audio_engine && _pimpl->audio_engine->isRunning()) {
            int32_t rate = _pimpl->audio_engine->getSampleRate();
            int32_t frames = _pimpl->audio_engine->getFramesPerBlock();
            _pimpl->module_router->activateNode(id, rate, frames);
        }
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
        if (id != 0) {
            if (auto* node = _pimpl->module_router->getProcessingNode(id)) {
                node->on_params_rescan = [this](uint32_t instance_id) {
                    if (_pimpl->on_params_rescan) _pimpl->on_params_rescan(instance_id);
                };
                node->on_ports_changed = [this](uint32_t instance_id) {
                    if (_pimpl->module_router) _pimpl->module_router->updateNodePorts(instance_id);
                };
            }

            if (_pimpl->audio_engine && _pimpl->audio_engine->isRunning()) {
                int32_t rate = _pimpl->audio_engine->getSampleRate();
                int32_t frames = _pimpl->audio_engine->getFramesPerBlock();
                _pimpl->module_router->activateNode(id, rate, frames);
            }
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

    if (id != 0) {
        if (auto* node = _pimpl->module_router->getProcessingNode(id)) {
            node->on_params_rescan = [this](uint32_t instance_id) {
                if (_pimpl->on_params_rescan) _pimpl->on_params_rescan(instance_id);
            };
            node->on_ports_changed = [this](uint32_t instance_id) {
                if (_pimpl->module_router) _pimpl->module_router->updateNodePorts(instance_id);
            };
        }

        if (_pimpl->audio_engine && _pimpl->audio_engine->isRunning()) {
            int32_t rate = _pimpl->audio_engine->getSampleRate();
            int32_t frames = _pimpl->audio_engine->getFramesPerBlock();
            _pimpl->module_router->activateNode(id, rate, frames);
        }
    }

    return id;
#else
    return 300;
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

void System::updateConnection(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                              uint32_t to_port, ConnectionType type, float scale, bool bypass) {
#if defined(__ANDROID__)
    if (_pimpl->module_router) {
        _pimpl->module_router->updateConnection(from_node, from_port, to_node, to_port, type, scale,
                                                bypass);
    }
#endif
}

auto System::getConnectionProperties(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                                     uint32_t to_port, ConnectionType type) const
    -> ConnectionProperties {
#if defined(__ANDROID__)
    if (_pimpl->module_router) {
        float scale = 1.0f;
        bool bypass = false;
        if (_pimpl->module_router->getConnectionProperties(from_node, from_port, to_node, to_port,
                                                           type, scale, bypass)) {
            return {.scale = scale, .bypass = bypass};
        }
    }
#endif
    return {.scale = 1.0f, .bypass = false};
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

void System::setParameterValue(uint32_t instance_id, const std::string& param_id, double value) {
#if defined(__ANDROID__)
    if (!_pimpl->module_router) return;
    auto* node = _pimpl->module_router->getProcessingNode(instance_id);
    if (node) node->setParameterValue(param_id, value);
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
                          param_slot->info.default_value,
                          node->getParameterBaseValue(param_slot->info.id),
                          node->getParameterCurrentValue(param_slot->info.id)});
    }
#endif
    return result;
}

auto System::getPorts(uint32_t instance_id, bool is_input) -> PortList {
    PortList result;
#if defined(__ANDROID__)
    if (!_pimpl->module_router) return result;
    auto* node = _pimpl->module_router->getProcessingNode(instance_id);
    if (!node) return result;

    const auto& ports = node->getAudioPorts(is_input);
    for (const auto& port : ports) {
        ConnectionType type = ConnectionType::kAudio;
        if (port.is_modulation) {
            type = ConnectionType::kModulation;
        } else if (port.clap_info.port_type && std::string(port.clap_info.port_type) == "event") {
            type = ConnectionType::kEvent;
        }

        result.push_back({port.index, port.clap_info.name, port.is_input, type});
    }
#endif
    return result;
}

auto System::getParameterText(uint32_t instance_id, uint32_t param_id, double value) const
    -> std::string {
#if defined(__ANDROID__)
    if (!_pimpl->module_router) return std::to_string(value);
    auto* node = _pimpl->module_router->getProcessingNode(instance_id);
    if (node) return node->getParameterText(param_id, value);
#endif
    return std::to_string(value);
}

auto System::getParameterText(uint32_t instance_id, const std::string& param_id, double value) const
    -> std::string {
#if defined(__ANDROID__)
    if (!_pimpl->module_router) return std::to_string(value);
    auto* node = _pimpl->module_router->getProcessingNode(instance_id);
    if (node) return node->getParameterText(param_id, value);
#endif
    return std::to_string(value);
}

auto System::saveState(uint32_t instance_id) -> std::vector<uint8_t> {
    std::vector<uint8_t> data;
#if defined(__ANDROID__)
    if (_pimpl->module_router) {
        auto* node = _pimpl->module_router->getProcessingNode(instance_id);
        if (node) {
            node->saveState(data);
        }
    }
#endif
    return data;
}

auto System::loadState(uint32_t instance_id, const std::vector<uint8_t>& data) -> bool {
#if defined(__ANDROID__)
    if (_pimpl->module_router) {
        auto* node = _pimpl->module_router->getProcessingNode(instance_id);
        if (node) {
            return node->loadState(data);
        }
    }
#endif
    return false;
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

auto System::getTransportState() const -> const TransportState& {
#if defined(__ANDROID__)
    if (_pimpl->module_router) return _pimpl->module_router->getTransportState();
#endif
    return _pimpl->transport_state;
}

auto System::saveProject() -> std::string {
#if defined(__ANDROID__)
    if (_pimpl->module_router) return _pimpl->module_router->serializeGraph();
#endif
    return "{}";
}

auto System::loadProject(const std::string& json_str) -> bool {
#if defined(__ANDROID__)
    if (!_pimpl->module_router || !_pimpl->audio_engine) return false;

    stopAudio();
    // Wait for audio engine to stop
    while (_pimpl->audio_engine->isRunning()) {
        std::this_thread::yield();
    }

    bool success = _pimpl->module_router->deserializeNodes(json_str);

    // 1. Re-attach callbacks and activate all nodes FIRST to finalize ports
    for (uint32_t id : _pimpl->module_router->getProcessOrder()) {
        if (auto* node = _pimpl->module_router->getProcessingNode(id)) {
            node->on_params_rescan = [this](uint32_t instance_id) {
                if (_pimpl->on_params_rescan) _pimpl->on_params_rescan(instance_id);
            };
            node->on_ports_changed = [this](uint32_t instance_id) {
                if (_pimpl->module_router) _pimpl->module_router->updateNodePorts(instance_id);
            };

            int32_t rate = _pimpl->audio_engine->getSampleRate();
            int32_t frames = _pimpl->audio_engine->getFramesPerBlock();
            _pimpl->module_router->activateNode(id, rate, frames);
        }
    }

    // 2. Restore connections ONLY AFTER nodes are activated and ports are ready
    if (success) {
        success = _pimpl->module_router->deserializeConnections(json_str);
    }

    startAudio();
    return success;
#else
    return false;
#endif
}

void System::setTempo(double bpm) {
    _pimpl->transport_state.tempo = bpm;
#if defined(__ANDROID__)
    if (_pimpl->module_router) _pimpl->module_router->setTempo(bpm);
#endif
}

void System::setTransportPlaying(bool playing) {
    _pimpl->transport_state.is_playing = playing;
#if defined(__ANDROID__)
    if (_pimpl->module_router) _pimpl->module_router->setTransportPlaying(playing);
#endif
}

void System::setEventOccuredCallback(EventOccuredCallback cb) { _pimpl->on_event_occured = cb; }

void System::setParamsRescanCallback(std::function<void(uint32_t)> cb) {
#if defined(__ANDROID__)
    _pimpl->on_params_rescan = cb;
#endif
}

#if defined(__ANDROID__)
auto System::supportsMiniCurve(uint32_t instance_id) const -> bool {
    if (!_pimpl->module_router) return false;
    auto* node = _pimpl->module_router->getProcessingNode(instance_id);
    return node ? node->supportsMiniCurve() : false;
}

auto System::getMiniCurveCount(uint32_t instance_id) const -> uint32_t {
    if (!_pimpl->module_router) return 0;
    auto* node = _pimpl->module_router->getProcessingNode(instance_id);
    return node ? node->getMiniCurveCount() : 0;
}

auto System::getMiniCurveAxisNames(uint32_t instance_id, uint32_t curve_index, std::string& out_x,
                                   std::string& out_y) const -> bool {
    if (!_pimpl->module_router) return false;
    auto* node = _pimpl->module_router->getProcessingNode(instance_id);
    return node ? node->getMiniCurveAxisNames(curve_index, out_x, out_y) : false;
}

auto System::renderMiniCurve(uint32_t instance_id, uint32_t curve_index,
                             std::vector<float>& out_values, uint32_t resolution) -> uint32_t {
    if (!_pimpl->module_router) return 0;
    auto* node = _pimpl->module_router->getProcessingNode(instance_id);
    return node ? node->renderMiniCurve(curve_index, out_values, resolution) : 0;
}

void System::setMiniCurveObserved(uint32_t instance_id, bool is_observed) {
    if (!_pimpl->module_router) return;
    auto* node = _pimpl->module_router->getProcessingNode(instance_id);
    if (node) node->setMiniCurveObserved(is_observed);
}
#endif

}  // namespace synth_canvas
