#ifndef SYNTHCANVAS_AUDIO_SYSTEM_H
#define SYNTHCANVAS_AUDIO_SYSTEM_H

#include <clap/clap.h>

#include <godot_cpp/classes/node.hpp>
#include <memory>

#include "host/constants.h"

#if defined(__ANDROID__)
namespace synth_canvas::host {
class AudioEngine;
class ModuleRouter;
}  // namespace synth_canvas::host
#endif

class SynthCanvasAudioSystem : public godot::Node {
    GDCLASS(SynthCanvasAudioSystem, godot::Node);

   public:
    enum ConnectionType {
        CONNECTION_TYPE_AUDIO = 0,
        CONNECTION_TYPE_EVENT = 1,
        CONNECTION_TYPE_MODULATION = 2,
    };

   private:
#if defined(__ANDROID__)
    std::unique_ptr<synth_canvas::host::ModuleRouter> _module_router;
    std::unique_ptr<synth_canvas::host::AudioEngine> _audio_engine;
#endif

   protected:
    static void _bind_methods();

   public:
    SynthCanvasAudioSystem();
    ~SynthCanvasAudioSystem();

    void _ready() override;
    void _exit_tree() override;
    void _process(double delta) override;

    auto createPluginInstance(const godot::String& path) -> uint32_t;
    void destroyPluginInstance(uint32_t instance_id);
    auto registerSpecialNode(const godot::String& type) -> uint32_t;
    void connectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port,
                      int type = 0);
    void disconnectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port,
                         int type = 0);

    void startAudio();
    void stopAudio();
    void playNote(uint32_t instance_id, int note, double velocity,
                  int32_t note_id = synth_canvas::host::constants::kClapInvalidId);
    void stopNote(uint32_t instance_id, int note, double velocity = 0.0,
                  int32_t note_id = synth_canvas::host::constants::kClapInvalidId);
    void setParameterValue(uint32_t instance_id, clap_id param_id, double value);
    godot::Dictionary getPluginParameters(uint32_t instance_id);

    void playNoteFromNode(uint32_t from_node_id, int note, double velocity);
    void stopNoteFromNode(uint32_t from_node_id, int note);
};

VARIANT_ENUM_CAST(SynthCanvasAudioSystem::ConnectionType);

#endif  // SYNTHCANVAS_AUDIO_SYSTEM_H
