#include <gdextension_interface.h>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "synthcanvas_audio_system.h"

static SynthCanvasAudioSystem* synth_canvas_audio_system_singleton;

void initializeSynthCanvasModule(godot::ModuleInitializationLevel p_level) {
    if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    godot::ClassDB::register_class<SynthCanvasAudioSystem>();

    synth_canvas_audio_system_singleton = memnew(SynthCanvasAudioSystem);
    godot::Engine::get_singleton()->register_singleton("SynthCanvasAudioSystem",
                                                       synth_canvas_audio_system_singleton);
}

void uninitializeSynthCanvasModule(godot::ModuleInitializationLevel p_level) {
    if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    godot::Engine::get_singleton()->unregister_singleton("SynthCanvasAudioSystem");
    godot::memdelete(synth_canvas_audio_system_singleton);
}

extern "C" {
// Initialization.
auto GDE_EXPORT synth_canvas_gdextension_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                                              const GDExtensionClassLibraryPtr kPLibrary,
                                              GDExtensionInitialization* r_initialization)
    -> GDExtensionBool {
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, kPLibrary, r_initialization);

    init_obj.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);

    init_obj.register_initializer(initializeSynthCanvasModule);
    init_obj.register_terminator(uninitializeSynthCanvasModule);

    return init_obj.init();
}
}
