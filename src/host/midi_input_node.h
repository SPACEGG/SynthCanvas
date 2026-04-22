#ifndef SYNTH_CANVAS_HOST_MIDI_INPUT_NODE_H
#define SYNTH_CANVAS_HOST_MIDI_INPUT_NODE_H

#include <RtMidi.h>
#include <readerwriterqueue.h>

#include <array>
#include <chrono>
#include <memory>
#include <vector>

#include "constants.h"
#include "processing_node.h"

namespace synth_canvas::host {

class MidiInputNode : public ProcessingNode {
   public:
    MidiInputNode();
    ~MidiInputNode() override;

    void activate(int32_t sample_rate, int32_t block_size) override;
    void deactivate() override;
    void setProcessingEnabled(bool enabled) override { _enabled = enabled; }

    void setPorts(uint32_t num_inputs, clap_audio_buffer* inputs, uint32_t num_outputs,
                  clap_audio_buffer* outputs) override;
    void processBegin(int num_frames) override;
    void process() override;
    void processEnd(int num_frames) override;

    auto getOutputBuffer(uint32_t port_idx) -> AudioBuffer* override { return nullptr; }
    void reserveOutputBuffers(uint32_t count) override {}

    void setParameterValue(clap_id param_id, double value) override;
    void setParameterValue(const std::string& param_id, double value) override;

    void applyModulation(clap_id param_id, double value, uint32_t sample_offset) override {}

    [[nodiscard]] auto getParameterBaseValue(clap_id param_id) const -> double override;
    [[nodiscard]] auto getParameterModulationOffset(clap_id param_id) const -> double override {
        return 0.0;
    }

    void queueEvent(const PluginEvent& event) override {}
    auto popOutputEvent(PluginEvent& out_event) -> bool override;
    void pollMainThread() override;

    void setInstanceId(uint32_t id) override { _instance_id = id; }
    [[nodiscard]] auto getInstanceId() const -> uint32_t override { return _instance_id; }
    [[nodiscard]] auto getAudioPorts(bool is_input) const
        -> const std::vector<AudioPortInfo>& override;
    [[nodiscard]] auto getParameters() const
        -> const std::vector<std::unique_ptr<ParameterSlot>>& override;
    [[nodiscard]] auto getParameterSlot(clap_id param_id) const
        -> const ParameterSlot* override;

    [[nodiscard]] auto isActive() const -> bool override { return _active; }

   private:
    static void midiCallback(double time_stamp, std::vector<unsigned char>* message,
                             void* user_data);

    void openPort(uint32_t port_index);
    void closePort();

    uint32_t _instance_id = 0;
    bool _active = false;
    bool _enabled = false;
    int32_t _sample_rate = constants::kDefaultSampleRate;
    int32_t _current_num_frames = 0;

    std::unique_ptr<rt::midi::RtMidiIn> _midi_in;

    struct RawMidiMessage {
        double time_stamp;
        std::array<uint8_t, 4> data;
        size_t size;
    };

    // Queue for passing MIDI messages from system thread to audio thread
    moodycamel::ReaderWriterQueue<RawMidiMessage> _message_queue{constants::kEventQueueSize};

    // Queue for passing events from audio thread to main thread (GUI feedback)
    moodycamel::ReaderWriterQueue<PluginEvent> _output_events_to_main{constants::kEventQueueSize};

    // Converted events for the current audio block
    std::vector<PluginEvent> _output_events;
    size_t _current_event_idx = 0;

    std::chrono::high_resolution_clock::time_point _block_start_time;

    std::vector<AudioPortInfo> _audio_inputs;
    std::vector<AudioPortInfo> _audio_outputs;
    std::vector<std::unique_ptr<ParameterSlot>> _parameters;

    uint32_t _port_index = 0;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_MIDI_INPUT_NODE_H
