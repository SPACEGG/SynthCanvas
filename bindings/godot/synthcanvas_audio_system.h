#ifndef SYNTHCANVAS_AUDIO_SYSTEM_H
#define SYNTHCANVAS_AUDIO_SYSTEM_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "api/system.h"  // synth_canvas::System

class SynthCanvasAudioSystem : public godot::Node {
    // NOLINTNEXTLINE(modernize-use-auto)
    GDCLASS(SynthCanvasAudioSystem, godot::Node);

   public:
    enum ConnectionType {
        kConnectionTypeAudio = 0,
        kConnectionTypeEvent = 1,
        kConnectionTypeModulation = 2,
    };

   private:
    std::unique_ptr<synth_canvas::System> _system;

   protected:
    static void _bind_methods();

   public:
    SynthCanvasAudioSystem();
    ~SynthCanvasAudioSystem() override;

    void _ready() override;
    void _exit_tree() override;
    void _process(double delta) override;

    auto createPluginInstance(const godot::String& path) -> uint32_t;
    auto createCompositeInstance(const godot::Dictionary& config) -> uint32_t;
    void destroyInstance(uint32_t instance_id);
    auto registerSpecialNode(const godot::String& type) -> uint32_t;
    void connectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port,
                      int type = 0);
    void disconnectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port,
                         int type = 0);

    void startAudio();
    void stopAudio();
    void playNote(uint32_t instance_id, int note, double velocity, int32_t note_id = -1);
    void stopNote(uint32_t instance_id, int note, double velocity = 0.0, int32_t note_id = -1);
    void setParameterValue(uint32_t instance_id, const godot::Variant& p_param, double value);
    auto getPluginParameters(uint32_t instance_id) -> godot::Dictionary;

    void playNoteFromNode(uint32_t from_node_id, int note, double velocity);
    void stopNoteFromNode(uint32_t from_node_id, int note);
};

VARIANT_ENUM_CAST(SynthCanvasAudioSystem::ConnectionType);

#endif  // SYNTHCANVAS_AUDIO_SYSTEM_H