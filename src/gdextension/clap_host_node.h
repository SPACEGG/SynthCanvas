#ifndef CLAP_HOST_NODE_H
#define CLAP_HOST_NODE_H

#include <godot_cpp/classes/node.hpp>
#include <memory>

// Forward declare the AudioEngine class to avoid circular dependencies.
// It's assumed to be in the synth_canvas::host namespace.
namespace synth_canvas::host {
    class AudioEngine;
}

class ClapHostNode : public godot::Node {
    GDCLASS(ClapHostNode, godot::Node);

private:
    std::unique_ptr<synth_canvas::host::AudioEngine> audio_engine;

protected:
    static void _bind_methods();

public:
    ClapHostNode();
    ~ClapHostNode();

    void _ready() override;
    void _exit_tree() override;

    // --- Methods exposed to Godot ---
    void load_plugin(const godot::String& path);
    void start_audio();
    void stop_audio();
    void play_note(int note, double velocity);
    void stop_note(int note);
};

#endif // CLAP_HOST_NODE_H
