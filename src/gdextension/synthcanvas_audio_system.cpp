#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "host/audio_engine.h"
#include "host/module_router.h"
#include "host/plugin_host.h" // For get_plugin_instance() if needed
#include "synthcanvas_audio_system.h"

using namespace godot;

void SynthCanvasAudioSystem::_bind_methods()
{
    // --- Bind methods to be called from Godot scripts ---
    ClassDB::bind_method(D_METHOD("create_plugin_instance", "path"), &SynthCanvasAudioSystem::create_plugin_instance);
    ClassDB::bind_method(D_METHOD("destroy_plugin_instance", "instance_id"), &SynthCanvasAudioSystem::destroy_plugin_instance);
    ClassDB::bind_method(D_METHOD("register_special_node", "type"), &SynthCanvasAudioSystem::register_special_node);
    ClassDB::bind_method(D_METHOD("connect_nodes", "from_node", "from_port", "to_node", "to_port"), &SynthCanvasAudioSystem::connect_nodes);
    ClassDB::bind_method(D_METHOD("disconnect_nodes", "from_node", "from_port", "to_node", "to_port"), &SynthCanvasAudioSystem::disconnect_nodes);

    ClassDB::bind_method(D_METHOD("start_audio"), &SynthCanvasAudioSystem::start_audio);
    ClassDB::bind_method(D_METHOD("stop_audio"), &SynthCanvasAudioSystem::stop_audio);
    ClassDB::bind_method(D_METHOD("play_note", "instance_id", "note", "velocity"), &SynthCanvasAudioSystem::play_note);
    ClassDB::bind_method(D_METHOD("stop_note", "instance_id", "note"), &SynthCanvasAudioSystem::stop_note);
    ClassDB::bind_method(D_METHOD("set_parameter_value", "instance_id", "param_id", "value"), &SynthCanvasAudioSystem::set_parameter_value);

    ClassDB::bind_method(D_METHOD("play_note_from_node", "from_node_id", "note", "velocity"), &SynthCanvasAudioSystem::play_note_from_node);
    ClassDB::bind_method(D_METHOD("stop_note_from_node", "from_node_id", "note"), &SynthCanvasAudioSystem::stop_note_from_node);

    // --- Define signals to be emitted from C++ to Godot ---
    // Used to notify Godot that a plugin parameter has changed.
    ADD_SIGNAL(MethodInfo("parameter_changed", PropertyInfo(Variant::INT, "param_id"), PropertyInfo(Variant::FLOAT, "value")));
}

SynthCanvasAudioSystem::SynthCanvasAudioSystem()
{
    UtilityFunctions::print("[SynthCanvasAudioSystem] Initializing...");
    
    // Create ModuleRouter first
    module_router = std::make_unique<synth_canvas::host::ModuleRouter>();
    
    // Connect ModuleRouter callbacks
    if (module_router) {
        module_router->on_parameter_changed = [this](clap_id param_id, double value)
        {
            emit_signal("parameter_changed", param_id, value);
        };
    }

    // Create AudioEngine, injecting ModuleRouter
    audio_engine = std::make_unique<synth_canvas::host::AudioEngine>(module_router.get());
}

SynthCanvasAudioSystem::~SynthCanvasAudioSystem()
{
    UtilityFunctions::print("[SynthCanvasAudioSystem] Cleaning up.");
    // Explicitly reset audio_engine before module_router because audio_engine depends on module_router
    audio_engine.reset(); 
    module_router.reset();
}

void SynthCanvasAudioSystem::_ready()
{
    UtilityFunctions::print("[SynthCanvasAudioSystem] Node is ready.");
}

void SynthCanvasAudioSystem::_exit_tree()
{
    UtilityFunctions::print("[SynthCanvasAudioSystem] Node exiting tree. Stopping audio.");
    stop_audio();
}

void SynthCanvasAudioSystem::_process(double delta)
{
    if (module_router)
    {
        module_router->poll_all_main_threads();
    }
}

// --- Implementation of methods exposed to Godot ---

uint32_t SynthCanvasAudioSystem::create_plugin_instance(const String &path)
{
    UtilityFunctions::print("[SynthCanvasAudioSystem] Attempting to create plugin instance from path: ", path);
    
    if (!module_router) return 0;

    // 1. Load plugin (without activation)
    uint32_t id = module_router->create_plugin_instance(path.utf8().get_data());
    
    // 2. If audio engine is running, activate immediately with current stream settings
    if (id != 0 && audio_engine && audio_engine->isRunning()) {
        int32_t rate = audio_engine->getSampleRate();
        int32_t frames = audio_engine->getFramesPerBlock();
        module_router->activate_plugin(id, rate, frames);
        UtilityFunctions::print("[SynthCanvasAudioSystem] Audio engine running. Activated plugin ", (int)id, " immediately.");
    }

    return id;
}

void SynthCanvasAudioSystem::destroy_plugin_instance(uint32_t instance_id)
{
    UtilityFunctions::print("[SynthCanvasAudioSystem] Destroying plugin instance: ", (int)instance_id);
    if (module_router)
    {
        module_router->deactivate_plugin(instance_id);
        module_router->destroy_plugin_instance(instance_id);
    }
}

uint32_t SynthCanvasAudioSystem::register_special_node(const String &type)
{
    UtilityFunctions::print("[SynthCanvasAudioSystem] Registering special node of type: ", type);
    if (module_router)
    {
        return module_router->register_special_node();
    }
    return 0;
}

void SynthCanvasAudioSystem::connect_nodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port)
{
    UtilityFunctions::print("[SynthCanvasAudioSystem] Connecting ", (int)from_node, ":", (int)from_port, " -> ", (int)to_node, ":", (int)to_port);
    if (module_router) {
        module_router->connect_nodes(from_node, from_port, to_node, to_port);
    }
}

void SynthCanvasAudioSystem::disconnect_nodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port)
{
    UtilityFunctions::print("[SynthCanvasAudioSystem] Disconnecting ", (int)from_node, ":", (int)from_port, " -> ", (int)to_node, ":", (int)to_port);
    if (module_router) {
        module_router->disconnect_nodes(from_node, from_port, to_node, to_port);
    }
}


void SynthCanvasAudioSystem::start_audio()
{
    UtilityFunctions::print("[SynthCanvasAudioSystem] Starting audio stream.");
    if (audio_engine)
    {
        audio_engine->start();
    }
}

void SynthCanvasAudioSystem::stop_audio()
{
    UtilityFunctions::print("[SynthCanvasAudioSystem] Stopping audio stream.");
    if (audio_engine)
    {
        audio_engine->stop();
    }
}

void SynthCanvasAudioSystem::play_note(uint32_t instance_id, int note, double velocity)
{
    if (audio_engine)
    {
        audio_engine->playNote(instance_id, note, velocity);
    }
}

void SynthCanvasAudioSystem::stop_note(uint32_t instance_id, int note)
{
    if (audio_engine)
    {
        audio_engine->stopNote(instance_id, note);
    }
}

void SynthCanvasAudioSystem::set_parameter_value(uint32_t instance_id, clap_id param_id, double value)
{
    if (audio_engine)
    {
        audio_engine->setParameterValue(instance_id, param_id, value);
    }
}

void SynthCanvasAudioSystem::play_note_from_node(uint32_t from_node_id, int note, double velocity)
{
    if (module_router && audio_engine)
    {
        for (const auto &conn : module_router->get_connections())
        {
            if (conn.from_node == from_node_id)
            {
                audio_engine->playNote(conn.to_node, note, velocity);
            }
        }
    }
}

void SynthCanvasAudioSystem::stop_note_from_node(uint32_t from_node_id, int note)
{
    if (module_router && audio_engine)
    {
        for (const auto &conn : module_router->get_connections())
        {
            if (conn.from_node == from_node_id)
            {
                audio_engine->stopNote(conn.to_node, note);
            }
        }
    }
}