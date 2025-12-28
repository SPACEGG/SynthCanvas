#ifndef SYNTHCANVAS_AUDIO_SYSTEM_H
#define SYNTHCANVAS_AUDIO_SYSTEM_H

#include <godot_cpp/classes/node.hpp>
#include <memory>

#include <clap/clap.h> 

namespace synth_canvas::host {
    class AudioEngine;
    class ModuleRouter;
}

class SynthCanvasAudioSystem : public godot::Node {
    GDCLASS(SynthCanvasAudioSystem, godot::Node);

private:
    std::unique_ptr<synth_canvas::host::ModuleRouter> module_router;
    std::unique_ptr<synth_canvas::host::AudioEngine> audio_engine;

protected:
    static void _bind_methods();

public:
    SynthCanvasAudioSystem();
    ~SynthCanvasAudioSystem();

    void _ready() override;
    void _exit_tree() override;
    void _process(double delta) override;

    uint32_t create_plugin_instance(const godot::String& path);
    void destroy_plugin_instance(uint32_t instance_id);
    uint32_t register_special_node(const godot::String& type);
    void connect_nodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port);
    void disconnect_nodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port);

    void start_audio();
    void stop_audio();
    void play_note(uint32_t instance_id, int note, double velocity);
    void stop_note(uint32_t instance_id, int note);
    void set_parameter_value(uint32_t instance_id, clap_id param_id, double value);

    void play_note_from_node(uint32_t from_node_id, int note, double velocity);
    void stop_note_from_node(uint32_t from_node_id, int note);
};

#endif // SYNTHCANVAS_AUDIO_SYSTEM_H
