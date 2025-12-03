#include "synthcanvas_audio_system.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/engine.hpp>

using namespace godot;

static SynthCanvasAudioSystem *synth_canvas_audio_system_singleton;

void initialize_synth_canvas_module(ModuleInitializationLevel p_level)
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
    {
        return;
    }

    ClassDB::register_class<SynthCanvasAudioSystem>();

    synth_canvas_audio_system_singleton = memnew(SynthCanvasAudioSystem);
    Engine::get_singleton()->register_singleton("SynthCanvasAudioSystem", synth_canvas_audio_system_singleton);
}

void uninitialize_synth_canvas_module(ModuleInitializationLevel p_level)
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
    {
        return;
    }

    Engine::get_singleton()->unregister_singleton("SynthCanvasAudioSystem");
    memdelete(synth_canvas_audio_system_singleton);
}

extern "C"
{
    // Initialization.
    GDExtensionBool GDE_EXPORT synth_canvas_gdextension_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization)
    {
        godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

        init_obj.register_initializer(initialize_synth_canvas_module);
        init_obj.register_terminator(uninitialize_synth_canvas_module);

        return init_obj.init();
    }
}
