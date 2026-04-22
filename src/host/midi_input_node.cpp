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
    } catch (const rt::midi::RtMidiError& error) {
        log("[MidiInputNode] Error creating RtMidiIn: ", error.getMessage());
    }

    // Initialize parameters
    auto port_param = std::make_unique<ParameterSlot>();
    port_param->info.id = 0;
    std::strncpy(port_param->info.name, "Port Index", sizeof(port_param->info.name));
    port_param->info.min_value = 0;
    port_param->info.max_value = 255;
    port_param->info.default_value = 0;
    _parameters.push_back(std::move(port_param));
}

MidiInputNode::~MidiInputNode() { deactivate(); }

void MidiInputNode::activate(int32_t sample_rate, int32_t block_size) {
    _sample_rate = sample_rate;
    _active = true;
    _enabled = true;
    openPort(_port_index);
}

void MidiInputNode::deactivate() {
    closePort();
    _active = false;
}

void MidiInputNode::openPort(uint32_t port_index) {
    if (!_midi_in) return;

    closePort();
    try {
        unsigned int port_count = _midi_in->getPortCount();

        uint32_t target_index = port_index;
        bool found_valid = false;

        for (unsigned int i = 0; i < port_count; ++i) {
            std::string name = _midi_in->getPortName(i);
        }

        if (port_count > 0) {
            std::string final_name = _midi_in->getPortName(target_index);

            _midi_in->openPort(target_index);
            _midi_in->setCallback(&midiCallback, this);
            _port_index = target_index;
        }
    } catch (const rt::midi::RtMidiError& error) {
        log("[MidiInputNode] EXCEPTION during openPort: ", error.getMessage());
    }
}

void MidiInputNode::closePort() {
    if (_midi_in && _midi_in->isPortOpen()) {
        _midi_in->cancelCallback();
        _midi_in->closePort();
    }
}

void MidiInputNode::midiCallback(double time_stamp, std::vector<unsigned char>* message,
                                 void* user_data) {
    auto* node = static_cast<MidiInputNode*>(user_data);
    if (!message || message->empty()) return;

    RawMidiMessage raw;
    raw.time_stamp = time_stamp;
    raw.size = std::min(static_cast<size_t>(4), message->size());
    std::copy(message->begin(), message->begin() + raw.size, raw.data.begin());

    node->_message_queue.enqueue(raw);
}

void MidiInputNode::processBegin(int num_frames) {
    _block_start_time = std::chrono::high_resolution_clock::now();
    _output_events.clear();
    _current_event_idx = 0;
    _current_num_frames = num_frames;
}

void MidiInputNode::process() {
    if (!_active || !_enabled) return;

    RawMidiMessage raw;
    while (_message_queue.try_dequeue(raw)) {
        if (raw.size < 1) continue;

        PluginEvent ev = {};
        ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.event.header.flags = 0;

        auto now = std::chrono::high_resolution_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::microseconds>(now - _block_start_time);
        auto offset = static_cast<int32_t>((duration.count() * _sample_rate) / 1000000);

        if (_current_num_frames > 0) {
            ev.event.header.time = std::clamp(offset, 0, _current_num_frames - 1);
        } else {
            ev.event.header.time = 0;
        }

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
        } else {
            ev.event.header.type = CLAP_EVENT_MIDI;
            ev.event.header.size = sizeof(clap_event_midi);
            ev.event.midi.port_index = 0;
            std::memcpy(ev.event.midi.data, raw.data.data(),
                        std::min(static_cast<size_t>(3), raw.size));
        }

        _output_events.push_back(ev);
        _output_events_to_main.enqueue(ev);
    }
}

void MidiInputNode::processEnd(int num_frames) {}

auto MidiInputNode::popOutputEvent(PluginEvent& out_event) -> bool {
    if (_current_event_idx < _output_events.size()) {
        out_event = _output_events[_current_event_idx++];
        return true;
    }
    return false;
}

void MidiInputNode::pollMainThread() {
    PluginEvent ev;
    while (_output_events_to_main.try_dequeue(ev)) {
        if (on_event_occured) {
            on_event_occured(_instance_id, ev);
        }
    }
}

void MidiInputNode::setParameterValue(clap_id param_id, double value) {
    if (param_id == 0) {
        _port_index = static_cast<uint32_t>(value);
        if (_active) openPort(_port_index);
    }
}

void MidiInputNode::setParameterValue(const std::string& param_id, double value) {
    if (param_id == "port_index") setParameterValue(0, value);
}

auto MidiInputNode::getParameterBaseValue(clap_id param_id) const -> double {
    if (param_id == 0) return static_cast<double>(_port_index);
    return 0.0;
}

auto MidiInputNode::getAudioPorts(bool is_input) const -> const std::vector<AudioPortInfo>& {
    return is_input ? _audio_inputs : _audio_outputs;
}

auto MidiInputNode::getParameters() const -> const std::vector<std::unique_ptr<ParameterSlot>>& {
    return _parameters;
}

auto MidiInputNode::getParameterSlot(clap_id param_id) const -> const ParameterSlot* {
    return nullptr;
}

void MidiInputNode::setPorts(uint32_t num_inputs, clap_audio_buffer* inputs, uint32_t num_outputs,
                             clap_audio_buffer* outputs) {}

}  // namespace synth_canvas::host
