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

    godot::ClassDB::bind_method(godot::D_METHOD("start_audio"),
                                &SynthCanvasAudioSystem::startAudio);
    godot::ClassDB::bind_method(godot::D_METHOD("stop_audio"), &SynthCanvasAudioSystem::stopAudio);
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

    godot::ClassDB::bind_method(
        godot::D_METHOD("play_note_from_node", "from_node_id", "note", "velocity"),
        &SynthCanvasAudioSystem::playNoteFromNode);
    godot::ClassDB::bind_method(godot::D_METHOD("stop_note_from_node", "from_node_id", "note"),
                                &SynthCanvasAudioSystem::stopNoteFromNode);

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
        }
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
            cpp_config.input_proxies.push_back(
                {.external_port_index =
                     static_cast<uint32_t>(static_cast<int>(p["external_port_index"])),
                 .internal_node = std::string(godot::String(p["internal_node"]).utf8().get_data()),
                 .internal_port_index =
                     static_cast<uint32_t>(static_cast<int>(p["internal_port_index"])),
                 .type = static_cast<synth_canvas::ConnectionType>(static_cast<int>(p["type"]))});
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
        param_info["current_value"] = info.current_value;

        result[info.id] = param_info;
    }

    return result;
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
