#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "host/audio_engine.h"
#include "host/plugin_host.h" // For getPluginHost()
#include "clap_host_node.h"

using namespace godot;

void ClapHostNode::_bind_methods()
{
    // --- Bind methods to be called from Godot scripts ---
    ClassDB::bind_method(D_METHOD("load_plugin", "path"), &ClapHostNode::load_plugin);
    ClassDB::bind_method(D_METHOD("start_audio"), &ClapHostNode::start_audio);
    ClassDB::bind_method(D_METHOD("stop_audio"), &ClapHostNode::stop_audio);
    ClassDB::bind_method(D_METHOD("play_note", "note", "velocity"), &ClapHostNode::play_note);
    ClassDB::bind_method(D_METHOD("stop_note", "note"), &ClapHostNode::stop_note);
    ClassDB::bind_method(D_METHOD("set_parameter_value", "param_id", "value"), &ClapHostNode::set_parameter_value);

    // --- Define signals to be emitted from C++ to Godot ---
    // Used to notify Godot that a plugin parameter has changed.
    ADD_SIGNAL(MethodInfo("parameter_changed", PropertyInfo(Variant::INT, "param_id"), PropertyInfo(Variant::FLOAT, "value")));

    // Used to notify Godot that the plugin's GUI wants to resize.

}

ClapHostNode::ClapHostNode()
{
    UtilityFunctions::print("[ClapHostNode] Initializing...");
    audio_engine = std::make_unique<synth_canvas::host::AudioEngine>();

    // --- Connect signals from the audio engine to Godot signals ---
    if (audio_engine)
    {
        audio_engine->on_parameter_changed = [this](clap_id param_id, double value)
        {
            emit_signal("parameter_changed", param_id, value);
        };
    }
}

ClapHostNode::~ClapHostNode()
{
    UtilityFunctions::print("[ClapHostNode] Cleaning up.");
    // The unique_ptr will automatically handle the deletion of audio_engine.
}

void ClapHostNode::_ready()
{
    UtilityFunctions::print("[ClapHostNode] Node is ready.");
}

void ClapHostNode::_exit_tree()
{
    UtilityFunctions::print("[ClapHostNode] Node exiting tree. Stopping audio.");
    stop_audio();
}

void ClapHostNode::_process(double delta)
{
    if (audio_engine && audio_engine->getPluginHost())
    {
        audio_engine->getPluginHost()->pollMainThread();
    }
}

// --- Implementation of methods exposed to Godot ---

void ClapHostNode::load_plugin(const String &path)
{
    UtilityFunctions::print("[ClapHostNode] Attempting to load plugin at path: ", path);
    if (audio_engine)
    {
        bool success = audio_engine->loadPlugin(path.utf8().get_data());
        if (success)
        {
            auto *plugin_host = audio_engine->getPluginHost();
            if (plugin_host)
            {
                // GUI-related code removed for headless plugin support.
            }
        }
    }
}

void ClapHostNode::start_audio()
{
    UtilityFunctions::print("[ClapHostNode] Starting audio stream.");
    if (audio_engine)
    {
        audio_engine->start();
    }
}

void ClapHostNode::stop_audio()
{
    UtilityFunctions::print("[ClapHostNode] Stopping audio stream.");
    if (audio_engine)
    {
        audio_engine->stop();
    }
}

void ClapHostNode::play_note(int note, double velocity)
{
    // UtilityFunctions::print("[ClapHostNode] Play note: ", note, " velocity: ", velocity);
    if (audio_engine)
    {
        audio_engine->playNote(note, velocity);
    }
}

void ClapHostNode::stop_note(int note)
{
    // UtilityFunctions::print("[ClapHostNode] Stop note: ", note);
    if (audio_engine)
    {
        audio_engine->stopNote(note);
    }
}

void ClapHostNode::set_parameter_value(clap_id param_id, double value)
{
    if (audio_engine)
    {
        audio_engine->setParameterValue(param_id, value);
    }
}
