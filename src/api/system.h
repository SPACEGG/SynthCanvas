#ifndef SYNTH_CANVAS_API_SYSTEM_H
#define SYNTH_CANVAS_API_SYSTEM_H

#include <functional>
#include <memory>
#include <string>

#include "types.h"

namespace synth_canvas {

class System {
   public:
    using ParameterChangedCallback = std::function<void(uint32_t param_id, double value)>;
    using LogCallback = std::function<void(const std::string& msg)>;

    System();
    ~System();

    // Lifecycle
    void initialize(LogCallback log_cb);
    void terminate();
    void update(double delta);

    // Plugin & Node Management
    auto createPluginInstance(const std::string& path) -> uint32_t;
    void destroyPluginInstance(uint32_t instance_id);
    auto registerSpecialNode(const std::string& type) -> uint32_t;

    void connectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port,
                      ConnectionType type);
    void disconnectNodes(uint32_t from_node, uint32_t from_port, uint32_t to_node, uint32_t to_port,
                         ConnectionType type);

    // Audio Control
    void startAudio();
    void stopAudio();

    // MIDI / Parameter Control
    void playNote(uint32_t instance_id, int note, double velocity, int32_t note_id = -1);
    void stopNote(uint32_t instance_id, int note, double velocity = 0.0, int32_t note_id = -1);
    void setParameterValue(uint32_t instance_id, uint32_t param_id, double value);

    auto getPluginParameters(uint32_t instance_id) -> ParameterList;

    // Node-based Control
    void playNoteFromNode(uint32_t from_node_id, int note, double velocity);
    void stopNoteFromNode(uint32_t from_node_id, int note);

    // Parameter Callbacks
    void setParameterChangedCallback(ParameterChangedCallback cb);

   private:
    struct Impl;
    std::unique_ptr<Impl> _pimpl;
};

}  // namespace synth_canvas

#endif  // SYNTH_CANVAS_API_SYSTEM_H
