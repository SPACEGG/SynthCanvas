#include "midi_input_node.h"

#include <algorithm>
#include <clap/helpers/host.hxx>
#include <cstring>

#include "logger.h"

namespace synth_canvas::host {

MidiInputNode::MidiInputNode() {
    try {
        _midi_in = std::make_unique<rt::midi::RtMidiIn>();
        // Ignore sysex, timing, or active sensing messages by default.
        _midi_in->ignoreTypes(true, true, true);
        _midi_in->setErrorCallback(&errorCallback, this);
    } catch (const rt::midi::RtMidiError& error) {
        log("[MidiInputNode] Error creating RtMidiIn: ", error.getMessage());
    }

    // Initialize parameters
    addParameter(0, "Port Index", "midi", 0.0, 255.0, 0.0, CLAP_PARAM_IS_STEPPED);
    
    // Setup ports
    addEventPort("MIDI Out", false);
}

MidiInputNode::~MidiInputNode() { deactivateInternal(); }

void MidiInputNode::activate(int32_t sample_rate, int32_t block_size) {
    InternalNodeBase::activate(sample_rate, block_size);
    _active = true;
    openPort(_port_index);
}

void MidiInputNode::deactivate() { deactivateInternal(); }

void MidiInputNode::deactivateInternal() {
    closePort();
    _active = false;
    _is_active = false;
}

void MidiInputNode::openPort(uint32_t port_index) {
    if (!_midi_in) return;

    closePort();
    try {
        unsigned int port_count = _midi_in->getPortCount();
        if (port_index < port_count) {
            _midi_in->openPort(port_index);
            _midi_in->setCallback(&midiCallback, this);
            _port_index = port_index;
        }
    } catch (const rt::midi::RtMidiError& error) {
        log("[MidiInputNode] EXCEPTION during openPort: ", error.getMessage());
    }
}

void MidiInputNode::closePort() {
    if (_midi_in && _midi_in->isPortOpen()) {
        try {
            _midi_in->cancelCallback();
            _midi_in->closePort();
        } catch (const rt::midi::RtMidiError& error) {
            log("[MidiInputNode] EXCEPTION during closePort: ", error.getMessage());
        }
    }
}

void MidiInputNode::midiCallback(double time_stamp, std::vector<unsigned char>* message,
                                 void* user_data) {
    auto* node = static_cast<MidiInputNode*>(user_data);
    if (!message || message->empty()) return;

    RawMidiMessage raw;
    raw.arrival_time = std::chrono::high_resolution_clock::now();
    raw.size = std::min(static_cast<size_t>(4), message->size());
    std::copy(message->begin(), message->begin() + raw.size, raw.data.begin());

    node->_message_queue.try_enqueue(raw);
}

void MidiInputNode::errorCallback(rt::midi::RtMidiError::Type type, const std::string& error_text,
                                  void* user_data) {
    log("[MidiInputNode] RtMidi ERROR [Type: ", static_cast<int>(type), "]: ", error_text);
    auto* node = static_cast<MidiInputNode*>(user_data);
    node->_pending_close.store(true, std::memory_order_relaxed);
}

void MidiInputNode::processBegin(int num_frames) {
    InternalNodeBase::processBegin(num_frames);
    _block_start_time = std::chrono::high_resolution_clock::now();
    _current_num_frames = num_frames;
}

void MidiInputNode::process() {
    if (!_active || !_processing_enabled) return;

    RawMidiMessage raw;
    int32_t last_offset = -1;

    while (_message_queue.try_dequeue(raw)) {
        if (raw.size < 1) continue;

        PluginEvent ev = {};
        ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.event.header.flags = 0;

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(raw.arrival_time -
                                                                              _block_start_time);
        auto offset = static_cast<int32_t>((duration.count() * _current_sample_rate) / 1000000);

        if (_current_num_frames > 0) {
            offset = std::clamp(offset, 0, _current_num_frames - 1);
        } else {
            offset = 0;
        }

        if (offset < last_offset) {
            offset = last_offset;
        }

        ev.event.header.time = offset;
        last_offset = offset;

        uint8_t status = raw.data[0];
        auto type = static_cast<uint8_t>(status & 0xF0);
        auto channel = static_cast<uint8_t>(status & 0x0F);

        if (type == constants::midi_status::kNoteOn || type == constants::midi_status::kNoteOff) {
            uint8_t key = (raw.size > 1) ? raw.data[1] : 0;
            uint8_t vel = (raw.size > 2) ? raw.data[2] : 0;

            bool is_note_on = (type == constants::midi_status::kNoteOn && vel > 0);

            ev.event.header.type = is_note_on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF;
            ev.event.header.size = sizeof(clap_event_note);
            ev.event.note.port_index = 0;
            ev.event.note.channel = channel;
            ev.event.note.key = static_cast<int16_t>(key);
            ev.event.note.velocity = static_cast<double>(vel) / 127.0;
            ev.event.note.note_id = -1;
        } else {
            ev.event.header.type = CLAP_EVENT_MIDI;
            ev.event.header.size = sizeof(clap_event_midi);
            ev.event.midi.port_index = 0;
            std::memcpy(ev.event.midi.data, raw.data.data(),
                        std::min(static_cast<size_t>(3), raw.size));
        }

        if (!_output_event_queues.empty()) {
            _output_event_queues[0]->try_enqueue(ev);
        }
        _output_events_to_main.try_enqueue(ev);
    }
}

void MidiInputNode::pollMainThread() {
    if (_pending_close.load(std::memory_order_relaxed)) {
        deactivateInternal();
        _pending_close.store(false, std::memory_order_relaxed);
    }

    PluginEvent ev;
    while (_output_events_to_main.try_dequeue(ev)) {
        if (on_event_occured) {
            on_event_occured(_instance_id, ev);
        }
    }
}

void MidiInputNode::setParameterValue(clap_id param_id, double value) {
    InternalNodeBase::setParameterValue(param_id, value);
    if (param_id == 0) {
        _port_index = static_cast<uint32_t>(value);
        if (_active) openPort(_port_index);
    }
}

void MidiInputNode::setParameterValue(const std::string& param_id, double value) {
    if (param_id == "0") setParameterValue(0, value);
}

}  // namespace synth_canvas::host
