#include "synthcanvas_audio_system.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// Host headers are only needed for Android build
#if defined(__ANDROID__)
#include "host/audio_engine.h"
#include "host/logger.h"
#include "host/module_router.h"
#include "host/plugin_host.h"
#endif

#include "host/constants.h"

// --- Common Implementation (Binding) ---

void SynthCanvasAudioSystem::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("create_plugin_instance", "path"),
                                &SynthCanvasAudioSystem::createPluginInstance);
    godot::ClassDB::bind_method(godot::D_METHOD("destroy_plugin_instance", "instance_id"),
                                &SynthCanvasAudioSystem::destroyPluginInstance);
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
        &SynthCanvasAudioSystem::playNote, DEFVAL(synth_canvas::host::constants::kClapInvalidId));
    godot::ClassDB::bind_method(
        godot::D_METHOD("stop_note", "instance_id", "note", "velocity", "note_id"),
        &SynthCanvasAudioSystem::stopNote, DEFVAL(0.0),
        DEFVAL(synth_canvas::host::constants::kClapInvalidId));
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

    BIND_ENUM_CONSTANT(CONNECTION_TYPE_AUDIO);
    BIND_ENUM_CONSTANT(CONNECTION_TYPE_EVENT);
    BIND_ENUM_CONSTANT(CONNECTION_TYPE_MODULATION);

    ADD_SIGNAL(godot::MethodInfo("parameter_changed",
                                 godot::PropertyInfo(godot::Variant::INT, "param_id"),
                                 godot::PropertyInfo(godot::Variant::FLOAT, "value")));
}

#if defined(__ANDROID__)

// Implementation

SynthCanvasAudioSystem::SynthCanvasAudioSystem() {
    synth_canvas::host::setLogCallback(
        [](const std::string& msg) { godot::UtilityFunctions::print(godot::String(msg.c_str())); });

    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] Initializing (Android)...");

    _module_router = std::make_unique<synth_canvas::host::ModuleRouter>();

    if (_module_router) {
        _module_router->on_parameter_changed = [this](clap_id param_id, double value) {
            emit_signal("parameter_changed", param_id, value);
        };
    }

    _audio_engine = std::make_unique<synth_canvas::host::AudioEngine>(_module_router.get());
}

SynthCanvasAudioSystem::~SynthCanvasAudioSystem() {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] Cleaning up.");
    _audio_engine.reset();
    _module_router.reset();
}

void SynthCanvasAudioSystem::_ready() {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] Node is ready.");
}

void SynthCanvasAudioSystem::_exit_tree() {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] Node exiting tree. Stopping audio.");
    stopAudio();
}

void SynthCanvasAudioSystem::_process(double delta) {
    if (_module_router) {
        _module_router->pollAllMainThreads();
    }
}

auto SynthCanvasAudioSystem::createPluginInstance(const godot::String& path) -> uint32_t {
    godot::UtilityFunctions::print(
        "[SynthCanvasAudioSystem] Attempting to create plugin instance from path: ", path);

    if (!_module_router) return 0;

    // 1. Load plugin (without activation)
    uint32_t id = _module_router->createPluginInstance(path.utf8().get_data());

    // 2. If audio engine is running, activate immediately with current stream settings
    if (id != 0 && _audio_engine && _audio_engine->isRunning()) {
        int32_t rate = _audio_engine->getSampleRate();
        int32_t frames = _audio_engine->getFramesPerBlock();
        _module_router->activatePlugin(id, rate, frames);
        godot::UtilityFunctions::print(
            "[SynthCanvasAudioSystem] Audio engine running. Activated plugin ",
            static_cast<int>(id), " immediately.");
    }

    return id;
}

void SynthCanvasAudioSystem::destroyPluginInstance(uint32_t instance_id) {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] Destroying plugin instance: ",
                                   static_cast<int>(instance_id));
    if (_module_router) {
        _module_router->deactivatePlugin(instance_id);
        _module_router->destroyPluginInstance(instance_id);
    }
}

auto SynthCanvasAudioSystem::registerSpecialNode(const godot::String& type) -> uint32_t {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] Registering special node of type: ",
                                   type);
    if (_module_router) {
        return _module_router->registerSpecialNode();
    }
    return 0;
}

void SynthCanvasAudioSystem::connectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                                          uint32_t to_port, int type) {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] Connecting ",
                                   static_cast<int>(from_node), ":", static_cast<int>(from_port),
                                   " -> ", static_cast<int>(to_node), ":",
                                   static_cast<int>(to_port), " Type: ", type);
    if (_module_router) {
        _module_router->connectNodes(from_node, from_port, to_node, to_port,
                                     static_cast<synth_canvas::host::ConnectionType>(type));
    }
}

void SynthCanvasAudioSystem::disconnectNodes(uint32_t from_node, uint32_t from_port,
                                             uint32_t to_node, uint32_t to_port, int type) {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] Disconnecting ",
                                   static_cast<int>(from_node), ":", static_cast<int>(from_port),
                                   " -> ", static_cast<int>(to_node), ":",
                                   static_cast<int>(to_port), " Type: ", type);
    if (_module_router) {
        _module_router->disconnectNodes(from_node, from_port, to_node, to_port,
                                        static_cast<synth_canvas::host::ConnectionType>(type));
    }
}

void SynthCanvasAudioSystem::startAudio() {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] Starting audio stream.");
    if (_audio_engine) {
        _audio_engine->start();
    }
}

void SynthCanvasAudioSystem::stopAudio() {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] Stopping audio stream.");
    if (_audio_engine) {
        _audio_engine->stop();
    }
}

void SynthCanvasAudioSystem::playNote(uint32_t instance_id, int note, double velocity,
                                      int32_t note_id) {
    if (_audio_engine) {
        _audio_engine->playNote(instance_id, note, velocity, note_id);
    }
}

void SynthCanvasAudioSystem::stopNote(uint32_t instance_id, int note, double velocity,
                                      int32_t note_id) {
    if (_audio_engine) {
        _audio_engine->stopNote(instance_id, note, velocity, note_id);
    }
}

void SynthCanvasAudioSystem::setParameterValue(uint32_t instance_id, clap_id param_id,
                                               double value) {
    if (_audio_engine) {
        _audio_engine->setParameterValue(instance_id, param_id, value);
    }
}

godot::Dictionary SynthCanvasAudioSystem::getPluginParameters(uint32_t instance_id) {
    godot::Dictionary result;
    if (!_module_router) return result;

    auto* host = _module_router->getPluginInstance(instance_id);
    if (!host) return result;

    const auto& params = host->getParameters();
    for (const auto& param_slot : params) {
        godot::Dictionary param_info;
        param_info["name"] = godot::String(param_slot->info.name);
        param_info["module"] = godot::String(param_slot->info.module);
        param_info["min_value"] = param_slot->info.min_value;
        param_info["max_value"] = param_slot->info.max_value;
        param_info["default_value"] = param_slot->info.default_value;
        param_info["current_value"] = param_slot->base_value.load();

        result[param_slot->info.id] = param_info;
    }

    return result;
}

void SynthCanvasAudioSystem::playNoteFromNode(uint32_t from_node_id, int note, double velocity) {
    if (_module_router && _audio_engine) {
        for (const auto& conn : _module_router->getConnections()) {
            if (conn.from_node == from_node_id) {
                _audio_engine->playNote(conn.to_node, note, velocity);
            }
        }
    }
}

void SynthCanvasAudioSystem::stopNoteFromNode(uint32_t from_node_id, int note) {
    if (_module_router && _audio_engine) {
        for (const auto& conn : _module_router->getConnections()) {
            if (conn.from_node == from_node_id) {
                _audio_engine->stopNote(conn.to_node, note);
            }
        }
    }
}

#else

// Windows Dummy

SynthCanvasAudioSystem::SynthCanvasAudioSystem() {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] Initializing (Dummy/Windows)...");
}

SynthCanvasAudioSystem::~SynthCanvasAudioSystem() {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] Cleaning up (Dummy/Windows).");
}

void SynthCanvasAudioSystem::_ready() {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] Node is ready (Dummy/Windows).");
}

void SynthCanvasAudioSystem::_exit_tree() {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] Node exiting tree (Dummy/Windows).");
}

void SynthCanvasAudioSystem::_process(double delta) {
    // No-op
}

auto SynthCanvasAudioSystem::createPluginInstance(const godot::String& path) -> uint32_t {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] (Dummy) createPluginInstance: ", path);
    return 100;  // Return a dummy ID for UI testing
}

void SynthCanvasAudioSystem::destroyPluginInstance(uint32_t instance_id) {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] (Dummy) destroyPluginInstance: ",
                                   instance_id);
}

auto SynthCanvasAudioSystem::registerSpecialNode(const godot::String& type) -> uint32_t {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] (Dummy) registerSpecialNode: ", type);
    return 200;  // Return a dummy ID
}

void SynthCanvasAudioSystem::connectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node,
                                          uint32_t to_port, int type) {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] (Dummy) connectNodes: ", from_node,
                                   ":", from_port, " -> ", to_node, ":", to_port, " Type: ", type);
}

void SynthCanvasAudioSystem::disconnectNodes(uint32_t from_node, uint32_t from_port,
                                             uint32_t to_node, uint32_t to_port, int type) {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] (Dummy) disconnectNodes: ", from_node,
                                   ":", from_port, " -> ", to_node, ":", to_port, " Type: ", type);
}

void SynthCanvasAudioSystem::startAudio() {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] (Dummy) startAudio");
}

void SynthCanvasAudioSystem::stopAudio() {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] (Dummy) stopAudio");
}

void SynthCanvasAudioSystem::playNote(uint32_t instance_id, int note, double velocity,
                                      int32_t note_id) {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] (Dummy) playNote: ", instance_id,
                                   " Note: ", note, " Vel: ", velocity, " NoteID: ", note_id);
}

void SynthCanvasAudioSystem::stopNote(uint32_t instance_id, int note, double velocity,
                                      int32_t note_id) {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] (Dummy) stopNote: ", instance_id,
                                   " Note: ", note);
}

void SynthCanvasAudioSystem::setParameterValue(uint32_t instance_id, clap_id param_id,
                                               double value) {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] (Dummy) setParameterValue: ",
                                   instance_id, " ParamID: ", param_id, " Value: ", value);
}

godot::Dictionary SynthCanvasAudioSystem::getPluginParameters(uint32_t instance_id) {
    godot::UtilityFunctions::print("[SynthCanvasAudioSystem] (Dummy) getPluginParameters: ",
                                   instance_id);

    // Return dummy data for UI testing
    godot::Dictionary result;

    // Dummy Param 1: Cutoff
    godot::Dictionary p1;
    p1["name"] = "Cutoff";
    p1["module"] = "Filter";
    p1["min_value"] = 20.0;
    p1["max_value"] = 20000.0;
    p1["default_value"] = 1000.0;
    p1["current_value"] = 1000.0;
    result[101] = p1;

    // Dummy Param 2: Resonance
    godot::Dictionary p2;
    p2["name"] = "Resonance";
    p2["module"] = "Filter";
    p2["min_value"] = 0.0;
    p2["max_value"] = 1.0;
    p2["default_value"] = 0.5;
    p2["current_value"] = 0.5;
    result[102] = p2;

    // Dummy Param 3: Volume
    godot::Dictionary p3;
    p3["name"] = "Volume";
    p3["module"] = "Output";
    p3["min_value"] = 0.0;
    p3["max_value"] = 1.0;
    p3["default_value"] = 0.8;
    p3["current_value"] = 0.8;
    result[103] = p3;

    return result;
}

void SynthCanvasAudioSystem::playNoteFromNode(uint32_t from_node_id, int note, double velocity) {
    godot::UtilityFunctions::print(
        "[SynthCanvasAudioSystem] (Dummy) playNoteFromNode: ", from_node_id, " Note: ", note);
}

void SynthCanvasAudioSystem::stopNoteFromNode(uint32_t from_node_id, int note) {
    godot::UtilityFunctions::print(
        "[SynthCanvasAudioSystem] (Dummy) stopNoteFromNode: ", from_node_id, " Note: ", note);
}

#endif