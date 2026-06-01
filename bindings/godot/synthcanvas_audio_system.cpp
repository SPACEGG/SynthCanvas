#include "synthcanvas_audio_system.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

void SynthCanvasAudioSystem::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("create_plugin_instance", "path"),
                                &SynthCanvasAudioSystem::createPluginInstance);
    godot::ClassDB::bind_method(godot::D_METHOD("create_composite_instance", "config"),
                                &SynthCanvasAudioSystem::createCompositeInstance);
    godot::ClassDB::bind_method(godot::D_METHOD("destroy_instance", "instance_id"),
                                &SynthCanvasAudioSystem::destroyInstance);
    godot::ClassDB::bind_method(godot::D_METHOD("register_special_node", "type"),
                                &SynthCanvasAudioSystem::registerSpecialNode);
    godot::ClassDB::bind_method(
        godot::D_METHOD("connect_nodes", "from_node", "from_port", "to_node", "to_port", "type"),
        &SynthCanvasAudioSystem::connectNodes, DEFVAL(0));
    godot::ClassDB::bind_method(
        godot::D_METHOD("disconnect_nodes", "from_node", "from_port", "to_node", "to_port", "type"),
        &SynthCanvasAudioSystem::disconnectNodes, DEFVAL(0));
    godot::ClassDB::bind_method(godot::D_METHOD("update_connection", "from_node", "from_port",
                                                "to_node", "to_port", "type", "scale", "bypass"),
                                &SynthCanvasAudioSystem::updateConnection);
    godot::ClassDB::bind_method(godot::D_METHOD("get_connection_properties", "from_node",
                                                "from_port", "to_node", "to_port", "type"),
                                &SynthCanvasAudioSystem::getConnectionProperties);

    godot::ClassDB::bind_method(godot::D_METHOD("start_audio"),
                                &SynthCanvasAudioSystem::startAudio);
    godot::ClassDB::bind_method(godot::D_METHOD("stop_audio"), &SynthCanvasAudioSystem::stopAudio);
    godot::ClassDB::bind_method(godot::D_METHOD("set_tempo", "bpm"),
                                &SynthCanvasAudioSystem::setTempo);
    godot::ClassDB::bind_method(godot::D_METHOD("set_transport_playing", "playing"),
                                &SynthCanvasAudioSystem::setTransportPlaying);
    godot::ClassDB::bind_method(godot::D_METHOD("get_transport_state"),
                                &SynthCanvasAudioSystem::getTransportState);
    godot::ClassDB::bind_method(
        godot::D_METHOD("play_note", "instance_id", "note", "velocity", "note_id"),
        &SynthCanvasAudioSystem::playNote, DEFVAL(-1));
    godot::ClassDB::bind_method(
        godot::D_METHOD("stop_note", "instance_id", "note", "velocity", "note_id"),
        &SynthCanvasAudioSystem::stopNote, DEFVAL(0.0), DEFVAL(-1));
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_parameter_value", "instance_id", "param", "value"),
        &SynthCanvasAudioSystem::setParameterValue);
    godot::ClassDB::bind_method(godot::D_METHOD("get_plugin_parameters", "instance_id"),
                                &SynthCanvasAudioSystem::getPluginParameters);
    godot::ClassDB::bind_method(godot::D_METHOD("get_ports", "instance_id", "is_input"),
                                &SynthCanvasAudioSystem::getPorts);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_parameter_text", "instance_id", "param", "value"),
        &SynthCanvasAudioSystem::getParameterText);

    godot::ClassDB::bind_method(godot::D_METHOD("get_state", "instance_id"),
                                &SynthCanvasAudioSystem::getState);
    godot::ClassDB::bind_method(godot::D_METHOD("set_state", "instance_id", "data"),
                                &SynthCanvasAudioSystem::setState);

    godot::ClassDB::bind_method(godot::D_METHOD("save_project"),
                                &SynthCanvasAudioSystem::saveProject);
    godot::ClassDB::bind_method(godot::D_METHOD("load_project", "json_str"),
                                &SynthCanvasAudioSystem::loadProject);

    godot::ClassDB::bind_method(
        godot::D_METHOD("play_note_from_node", "from_node_id", "note", "velocity"),
        &SynthCanvasAudioSystem::playNoteFromNode);
    godot::ClassDB::bind_method(godot::D_METHOD("stop_note_from_node", "from_node_id", "note"),
                                &SynthCanvasAudioSystem::stopNoteFromNode);

    godot::ClassDB::bind_method(godot::D_METHOD("supports_mini_curve", "instance_id"),
                                &SynthCanvasAudioSystem::supportsMiniCurve);
    godot::ClassDB::bind_method(godot::D_METHOD("get_mini_curve_count", "instance_id"),
                                &SynthCanvasAudioSystem::getMiniCurveCount);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_mini_curve_axis_names", "instance_id", "curve_index"),
        &SynthCanvasAudioSystem::getMiniCurveAxisNames);
    godot::ClassDB::bind_method(
        godot::D_METHOD("render_mini_curve", "instance_id", "curve_index", "resolution"),
        &SynthCanvasAudioSystem::renderMiniCurve);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_mini_curve_observed", "instance_id", "is_observed"),
        &SynthCanvasAudioSystem::setMiniCurveObserved);

    BIND_ENUM_CONSTANT(kConnectionTypeAudio);
    BIND_ENUM_CONSTANT(kConnectionTypeEvent);
    BIND_ENUM_CONSTANT(kConnectionTypeModulation);

    ADD_SIGNAL(godot::MethodInfo("parameter_changed",
                                 godot::PropertyInfo(godot::Variant::INT, "instance_id"),
                                 godot::PropertyInfo(godot::Variant::INT, "param_id"),
                                 godot::PropertyInfo(godot::Variant::FLOAT, "value")));

    ADD_SIGNAL(godot::MethodInfo("note_event_received",
                                 godot::PropertyInfo(godot::Variant::INT, "instance_id"),
                                 godot::PropertyInfo(godot::Variant::INT, "note"),
                                 godot::PropertyInfo(godot::Variant::FLOAT, "velocity"),
                                 godot::PropertyInfo(godot::Variant::BOOL, "is_on")));

    ADD_SIGNAL(godot::MethodInfo(
        "midi_event_received", godot::PropertyInfo(godot::Variant::INT, "instance_id"),
        godot::PropertyInfo(godot::Variant::PACKED_BYTE_ARRAY, "midi_bytes"),
        godot::PropertyInfo(godot::Variant::INT, "port_index")));

    ADD_SIGNAL(godot::MethodInfo("connection_disconnected",
                                 godot::PropertyInfo(godot::Variant::INT, "from_node"),
                                 godot::PropertyInfo(godot::Variant::INT, "from_port"),
                                 godot::PropertyInfo(godot::Variant::INT, "to_node"),
                                 godot::PropertyInfo(godot::Variant::INT, "to_port"),
                                 godot::PropertyInfo(godot::Variant::INT, "type")));

    ADD_SIGNAL(godot::MethodInfo("node_ports_changed",
                                 godot::PropertyInfo(godot::Variant::INT, "instance_id"),
                                 godot::PropertyInfo(godot::Variant::INT, "input_count"),
                                 godot::PropertyInfo(godot::Variant::INT, "output_count")));
}

SynthCanvasAudioSystem::SynthCanvasAudioSystem() {
    _system = std::make_unique<synth_canvas::System>();

    _system->initialize([](const std::string& msg) -> void {
        godot::UtilityFunctions::print(godot::String(msg.c_str()));
    });

    _system->setEventOccuredCallback([this](const synth_canvas::SystemEvent& ev) -> void {
        using synth_canvas::SystemEventType;
        if (ev.type == SystemEventType::kParameterValue) {
            emit_signal("parameter_changed", ev.instance_id, ev.data.parameter.param_id,
                        ev.data.parameter.value);
        } else if (ev.type == SystemEventType::kNoteOn || ev.type == SystemEventType::kNoteOff) {
            bool is_on = (ev.type == SystemEventType::kNoteOn);
            emit_signal("note_event_received", ev.instance_id, ev.data.note.key,
                        ev.data.note.velocity, is_on);
        } else if (ev.type == SystemEventType::kMidi) {
            godot::PackedByteArray bytes;
            bytes.push_back(ev.data.midi.data[0]);
            bytes.push_back(ev.data.midi.data[1]);
            bytes.push_back(ev.data.midi.data[2]);
            emit_signal("midi_event_received", ev.instance_id, bytes, ev.data.midi.port_index);
        } else if (ev.type == SystemEventType::kConnectionDisconnected) {
            emit_signal("connection_disconnected", ev.data.connection.from_node,
                        ev.data.connection.from_port, ev.data.connection.to_node,
                        ev.data.connection.to_port, ev.data.connection.type);
        } else if (ev.type == SystemEventType::kNodePortsChanged) {
            emit_signal("node_ports_changed", ev.instance_id, ev.data.port_change.input_count,
                        ev.data.port_change.output_count);
        }
    });

    _system->setParamsRescanCallback([this](uint32_t instance_id) -> void {
        // Use param_id = -1 as a special signal for "all parameters rescanned"
        emit_signal("parameter_changed", instance_id, -1, 1.0);
    });
}

SynthCanvasAudioSystem::~SynthCanvasAudioSystem() {
    if (_system) {
        _system->terminate();
    }
}

void SynthCanvasAudioSystem::_ready() {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] Node is ready.");
}

void SynthCanvasAudioSystem::_exit_tree() {
    if (_system) {
        _system->stopAudio();
    }
}

void SynthCanvasAudioSystem::_process(double delta) {
    if (_system) {
        _system->update(delta);
    }
}

auto SynthCanvasAudioSystem::createPluginInstance(const godot::String& path) -> uint32_t {
    if (!_system) return 0;
    return _system->createPluginInstance(path.utf8().get_data());
}

auto SynthCanvasAudioSystem::createCompositeInstance(const godot::Dictionary& config) -> uint32_t {
    if (!_system) return 0;

    synth_canvas::CompositeConfig cpp_config;

    // 1. Plugins
    if (config.has("plugins")) {
        godot::Array plugins = config["plugins"];
        for (const auto& plugin : plugins) {
            godot::Dictionary p = plugin;
            cpp_config.plugins.push_back(
                {.alias = std::string(godot::String(p["alias"]).utf8().get_data()),
                 .plugin_path = std::string(godot::String(p["plugin_path"]).utf8().get_data())});
        }
    }

    // 2. Routings
    if (config.has("routings")) {
        godot::Array routings = config["routings"];
        for (const auto& routing : routings) {
            godot::Dictionary r = routing;
            cpp_config.routings.push_back(
                {.from_node = std::string(godot::String(r["from_node"]).utf8().get_data()),
                 .from_port = static_cast<uint32_t>(static_cast<int>(r["from_port"])),
                 .to_node = std::string(godot::String(r["to_node"]).utf8().get_data()),
                 .to_port = static_cast<uint32_t>(static_cast<int>(r["to_port"])),
                 .type = static_cast<synth_canvas::ConnectionType>(static_cast<int>(r["type"]))});
        }
    }

    // 3. Parameter Mappings
    if (config.has("parameter_mappings")) {
        godot::Array mappings = config["parameter_mappings"];
        for (const auto& mapping : mappings) {
            godot::Dictionary m = mapping;
            cpp_config.parameter_mappings.push_back(
                {.param_id = std::string(godot::String(m["param_id"]).utf8().get_data()),
                 .target_node = std::string(godot::String(m["target_node"]).utf8().get_data()),
                 .target_param_index =
                     static_cast<uint32_t>(static_cast<int>(m["target_param_index"]))});
        }
    }

    // 4. Input Proxies
    if (config.has("input_proxies")) {
        godot::Array proxies = config["input_proxies"];
        for (const auto& proxie : proxies) {
            godot::Dictionary p = proxie;
            synth_canvas::PortProxyConfig p_config;
            p_config.external_port_index =
                static_cast<uint32_t>(static_cast<int>(p["external_port_index"]));
            p_config.internal_node =
                std::string(godot::String(p["internal_node"]).utf8().get_data());
            p_config.type = static_cast<synth_canvas::ConnectionType>(static_cast<int>(p["type"]));

            if (p.has("internal_port_index")) {
                p_config.internal_port_index =
                    static_cast<uint32_t>(static_cast<int>(p["internal_port_index"]));
            } else {
                p_config.internal_port_index = 0;
            }

            if (p.has("target_param_id")) {
                p_config.target_param_id =
                    static_cast<uint32_t>(static_cast<int>(p["target_param_id"]));
            }

            cpp_config.input_proxies.push_back(p_config);
        }
    }

    // 5. Output Proxies
    if (config.has("output_proxies")) {
        godot::Array proxies = config["output_proxies"];
        for (const auto& proxie : proxies) {
            godot::Dictionary p = proxie;
            cpp_config.output_proxies.push_back(
                {.external_port_index =
                     static_cast<uint32_t>(static_cast<int>(p["external_port_index"])),
                 .internal_node = std::string(godot::String(p["internal_node"]).utf8().get_data()),
                 .internal_port_index =
                     static_cast<uint32_t>(static_cast<int>(p["internal_port_index"])),
                 .type = static_cast<synth_canvas::ConnectionType>(static_cast<int>(p["type"]))});
        }
    }

    // 6. Display target alias
    if (config.has("display")) {
        cpp_config.display = std::string(godot::String(config["display"]).utf8().get_data());
    }

    return _system->createCompositeInstance(cpp_config);
}

void SynthCanvasAudioSystem::destroyInstance(uint32_t instance_id) {
    if (_system) {
        _system->destroyInstance(instance_id);
    }
}

auto SynthCanvasAudioSystem::registerSpecialNode(const godot::String& type) -> uint32_t {
    if (!_system) return 0;
    return _system->registerSpecialNode(type.utf8().get_data());
}

void SynthCanvasAudioSystem::connectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                                          uint32_t to_port, int type) {
    if (_system) {
        _system->connectNodes(from_node, from_port, to_node, to_port,
                              static_cast<synth_canvas::ConnectionType>(type));
    }
}

void SynthCanvasAudioSystem::disconnectNodes(uint32_t from_node, uint32_t from_port,
                                             uint32_t to_node, uint32_t to_port, int type) {
    if (_system) {
        _system->disconnectNodes(from_node, from_port, to_node, to_port,
                                 static_cast<synth_canvas::ConnectionType>(type));
    }
}

void SynthCanvasAudioSystem::updateConnection(uint32_t from_node, uint32_t from_port,
                                              uint32_t to_node, uint32_t to_port, int type,
                                              float scale, bool bypass) {
    if (_system) {
        _system->updateConnection(from_node, from_port, to_node, to_port,
                                  static_cast<synth_canvas::ConnectionType>(type), scale, bypass);
    }
}

auto SynthCanvasAudioSystem::getConnectionProperties(uint32_t from_node, uint32_t from_port,
                                                     uint32_t to_node, uint32_t to_port, int type)
    -> godot::Dictionary {
    godot::Dictionary res;
    if (_system) {
        auto props =
            _system->getConnectionProperties(from_node, from_port, to_node, to_port,
                                             static_cast<synth_canvas::ConnectionType>(type));
        res["scale"] = props.scale;
        res["bypass"] = props.bypass;
    }
    return res;
}

void SynthCanvasAudioSystem::startAudio() {
    if (_system) {
        _system->startAudio();
    }
}

void SynthCanvasAudioSystem::stopAudio() {
    if (_system) {
        _system->stopAudio();
    }
}

void SynthCanvasAudioSystem::playNote(uint32_t instance_id, int note, double velocity,
                                      int32_t note_id) {
    if (_system) {
        _system->playNote(instance_id, note, velocity, note_id);
    }
}

void SynthCanvasAudioSystem::stopNote(uint32_t instance_id, int note, double velocity,
                                      int32_t note_id) {
    if (_system) {
        _system->stopNote(instance_id, note, velocity, note_id);
    }
}

void SynthCanvasAudioSystem::setParameterValue(uint32_t instance_id, const godot::Variant& p_param,
                                               double value) {
    if (!_system) return;

    if (p_param.get_type() == godot::Variant::INT) {
        uint32_t param_id = p_param;
        _system->setParameterValue(instance_id, param_id, value);
    } else if (p_param.get_type() == godot::Variant::STRING) {
        godot::String s = p_param;
        _system->setParameterValue(instance_id, s.utf8().get_data(), value);
    }
}

auto SynthCanvasAudioSystem::getPluginParameters(uint32_t instance_id) -> godot::Dictionary {
    godot::Dictionary result;
    if (!_system) return result;

    auto params = _system->getPluginParameters(instance_id);
    for (const auto& info : params) {
        godot::Dictionary param_info;
        param_info["name"] = godot::String(info.name.c_str());
        param_info["module"] = godot::String(info.module.c_str());
        param_info["min_value"] = info.min_value;
        param_info["max_value"] = info.max_value;
        param_info["default_value"] = info.default_value;
        param_info["base_value"] = info.base_value;
        param_info["current_value"] = info.current_value;

        result[info.id] = param_info;
    }

    return result;
}

auto SynthCanvasAudioSystem::getPorts(uint32_t instance_id, bool is_input) -> godot::Array {
    godot::Array result;
    if (!_system) return result;

    auto ports = _system->getPorts(instance_id, is_input);
    for (const auto& info : ports) {
        godot::Dictionary port_info;
        port_info["index"] = info.index;
        port_info["name"] = godot::String(info.name.c_str());
        port_info["is_input"] = info.is_input;
        port_info["type"] = static_cast<int>(info.type);
        result.append(port_info);
    }

    return result;
}

auto SynthCanvasAudioSystem::getParameterText(uint32_t instance_id, const godot::Variant& p_param,
                                              double value) -> godot::String {
    if (!_system) return {std::to_string(value).c_str()};

    if (p_param.get_type() == godot::Variant::INT) {
        uint32_t param_id = p_param;
        return {_system->getParameterText(instance_id, param_id, value).c_str()};
    } else if (p_param.get_type() == godot::Variant::STRING) {
        godot::String s = p_param;
        return {_system->getParameterText(instance_id, s.utf8().get_data(), value).c_str()};
    }
    return {std::to_string(value).c_str()};
}

auto SynthCanvasAudioSystem::saveProject() -> godot::String {
    if (!_system) return "{}";
    return {_system->saveProject().c_str()};
}

auto SynthCanvasAudioSystem::loadProject(const godot::String& json_str) -> bool {
    if (!_system) return false;
    return _system->loadProject(json_str.utf8().get_data());
}

auto SynthCanvasAudioSystem::getState(uint32_t instance_id) -> godot::PackedByteArray {
    godot::PackedByteArray res;
    if (_system) {
        std::vector<uint8_t> data = _system->saveState(instance_id);
        if (!data.empty()) {
            res.resize(static_cast<int64_t>(data.size()));
            std::memcpy(res.ptrw(), data.data(), data.size());
        }
    }
    return res;
}

void SynthCanvasAudioSystem::setState(uint32_t instance_id, const godot::PackedByteArray& data) {
    if (_system) {
        std::vector<uint8_t> vec(data.size());
        if (!vec.empty()) {
            std::memcpy(vec.data(), data.ptr(), data.size());
        }
        _system->loadState(instance_id, vec);
    }
}

void SynthCanvasAudioSystem::playNoteFromNode(uint32_t from_node_id, int note, double velocity) {
    if (_system) {
        _system->playNoteFromNode(from_node_id, note, velocity);
    }
}

void SynthCanvasAudioSystem::stopNoteFromNode(uint32_t from_node_id, int note) {
    if (_system) {
        _system->stopNoteFromNode(from_node_id, note);
    }
}

auto SynthCanvasAudioSystem::getTransportState() -> godot::Dictionary {
    godot::Dictionary result;
    if (!_system) return result;

    auto transport = _system->getTransportState();
    result["is_playing"] = transport.is_playing;
    result["song_pos_beats"] = transport.song_pos_beats;
    result["tempo"] = transport.tempo;
    result["ts_denom"] = transport.ts_denom;
    result["ts_num"] = transport.ts_num;

    return result;
}

void SynthCanvasAudioSystem::setTempo(double bpm) {
    if (_system) {
        _system->setTempo(bpm);
    }
}

void SynthCanvasAudioSystem::setTransportPlaying(bool playing) {
    if (_system) {
        _system->setTransportPlaying(playing);
    }
}

auto SynthCanvasAudioSystem::supportsMiniCurve(uint32_t instance_id) -> bool {
#if defined(__ANDROID__)
    if (_system) {
        return _system->supportsMiniCurve(instance_id);
    }
    return false;
#else
    return true; // Dummy engine on Windows supports curves for UI testing
#endif
}

auto SynthCanvasAudioSystem::getMiniCurveCount(uint32_t instance_id) -> int {
#if defined(__ANDROID__)
    if (_system) {
        return static_cast<int>(_system->getMiniCurveCount(instance_id));
    }
#endif
    return 1; // Dummy count on Windows
}

auto SynthCanvasAudioSystem::getMiniCurveAxisNames(uint32_t instance_id, uint32_t curve_index)
    -> godot::Dictionary {
    godot::Dictionary dict;
    dict["x"] = "";
    dict["y"] = "";
#if defined(__ANDROID__)
    if (_system) {
        std::string out_x, out_y;
        if (_system->getMiniCurveAxisNames(instance_id, curve_index, out_x, out_y)) {
            dict["x"] = godot::String(out_x.c_str());
            dict["y"] = godot::String(out_y.c_str());
        }
    }
#endif
    return dict;
}

auto SynthCanvasAudioSystem::renderMiniCurve(uint32_t instance_id, uint32_t curve_index,
                                             uint32_t resolution) -> godot::PackedFloat32Array {
    godot::PackedFloat32Array arr;
#if defined(__ANDROID__)
    if (_system) {
        std::vector<float> values;
        uint32_t count = _system->renderMiniCurve(instance_id, curve_index, values, resolution);
        if (count > 0 && !values.empty()) {
            arr.resize(static_cast<int64_t>(values.size()));
            for (size_t i = 0; i < values.size(); ++i) {
                arr[static_cast<int64_t>(i)] = values[i];
            }
        }
    }
#else
    arr.resize(resolution);
    for (uint32_t i = 0; i < resolution; ++i) {
        float x = static_cast<float>(i) / static_cast<float>(resolution - 1);
        arr[i] = (std::sin(x * 3.14159265f * 4.0f) + 1.0f) * 0.5f; // Dummy sine wave
    }
#endif
    return arr;
}

void SynthCanvasAudioSystem::setMiniCurveObserved(uint32_t instance_id, bool is_observed) {
#if defined(__ANDROID__)
    if (_system) {
        _system->setMiniCurveObserved(instance_id, is_observed);
    }
#endif
}
