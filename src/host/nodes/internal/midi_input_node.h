#ifndef SYNTH_CANVAS_HOST_MIDI_INPUT_NODE_H
#define SYNTH_CANVAS_HOST_MIDI_INPUT_NODE_H

#include <RtMidi.h>
#include <readerwriterqueue.h>

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

#include "host/nodes/base/internal_node_base.h"
#include "utils/constants.h"

namespace synth_canvas::host {

class MidiInputNode : public InternalNodeBase {
   public:
    MidiInputNode();
    ~MidiInputNode() override;

    void activate(int32_t sample_rate, int32_t block_size) override;
    void deactivate() override;
    void processBegin(int num_frames) override;
    void process() override;

    void setParameterValue(clap_id param_id, double value) override;
    void setParameterValue(const std::string& param_id, double value) override;

    void pollMainThread() override;

   private:
    static void midiCallback(double time_stamp, std::vector<unsigned char>* message,
                             void* user_data);
    static void errorCallback(rt::midi::RtMidiError::Type type, const std::string& error_text,
                              void* user_data);

    void openPort(uint32_t port_index);
    void closePort();
    void deactivateInternal();

    bool _active = false;
    std::atomic<bool> _pending_close{false};
    int32_t _current_num_frames = 0;

    struct RawMidiMessage {
        std::chrono::high_resolution_clock::time_point arrival_time;
        std::array<uint8_t, 4> data;
        size_t size;
    };

    // Queue for passing MIDI messages from system thread to audio thread
    moodycamel::ReaderWriterQueue<RawMidiMessage> _message_queue{constants::kEventQueueSize};

    // Queue for passing events from audio thread to main thread (GUI feedback)
    moodycamel::ReaderWriterQueue<PluginEvent> _output_events_to_main{constants::kEventQueueSize};

    std::chrono::high_resolution_clock::time_point _block_start_time;

    uint32_t _port_index = 0;

    std::unique_ptr<rt::midi::RtMidiIn> _midi_in;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_MIDI_INPUT_NODE_H
