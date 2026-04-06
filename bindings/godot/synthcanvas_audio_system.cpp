#include "synthcanvas_audio_system.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

void SynthCanvasAudioSystem::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("create_plugin_instance", "path"),
                                &SynthCanvasAudioSystem::createPluginInstance);
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
        godot::D_METHOD("set_parameter_value", "instance_id", "param_id", "value"),
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
}

SynthCanvasAudioSystem::SynthCanvasAudioSystem() {
    _system = std::make_unique<synth_canvas::System>();

    _system->initialize([](const std::string& msg) -> void {
        godot::UtilityFunctions::print(godot::String(msg.c_str()));
    });

    _system->setParameterChangedCallback([this](uint32_t instance_id, uint32_t param_id, double value) -> void {
        emit_signal("parameter_changed", instance_id, param_id, value);
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

void SynthCanvasAudioSystem::setParameterValue(uint32_t instance_id, uint32_t param_id,
                                               double value) {
    if (_system) {
        _system->setParameterValue(instance_id, param_id, value);
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
