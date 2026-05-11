#include "sequencer_node.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace synth_canvas::host {

SequencerNode::SequencerNode() {
    // 1. Register Parameters
    addParameter(kParamSteps, "Steps", "Logic", 1.0, 32.0, 8.0,
                 CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED);
    addParameter(kParamTime, "Time", "Logic", 0.0, 5.0, 1.0,
                 CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED);
    addParameter(kParamSwing, "Swing", "Logic", 0.0, 0.75, 0.0, CLAP_PARAM_IS_AUTOMATABLE);
    addParameter(kParamRestart, "Restart", "Logic", 0.0, 1.0, 1.0,
                 CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED);
    addParameter(kParamCurrentStep, "Current Step", "Logic", 0.0, 31.0, 0.0,
                 CLAP_PARAM_IS_READONLY | CLAP_PARAM_IS_STEPPED);

    // 2. Ports
    addEventPort("Trigger IN", true);
    addEventPort("Note OUT", false);     // Port 0
    addEventPort("Trigger OUT", false);  // Port 1

    // 3. Pre-allocate
    _active_pattern.reserve(kMaxPatternSize);
    _pending_pattern.reserve(kMaxPatternSize);

    for (auto& instance : _instances) {
        instance.is_active = false;
        for (auto& note : instance.active_notes) {
            note.is_active = false;
        }
    }
}

void SequencerNode::activate(int32_t sample_rate, int32_t block_size) {
    InternalNodeBase::activate(sample_rate, block_size);
    for (auto& instance : _instances) {
        instance.is_active = false;
    }
    _last_block_end_beat = -1.0;
}

void SequencerNode::processBegin(int num_frames) {
    InternalNodeBase::processBegin(num_frames);

    if (_pending_update.load(std::memory_order_acquire)) {
        _active_pattern = _pending_pattern;
        _pending_update.store(false, std::memory_order_release);
    }
}

void SequencerNode::process() {
    if (!_transport || !_transport->is_playing) return;

    double tempo = _transport->tempo;
    double samples_per_beat = (_current_sample_rate * 60.0) / tempo;
    double block_start_beat = _transport->song_pos_beats;
    double block_end_beat =
        block_start_beat + (static_cast<double>(_output_buffer.frames) / samples_per_beat);

    double step_duration = getStepDurationBeats();
    double swing_factor = _swing.load(std::memory_order_relaxed);
    int32_t active_steps = _active_steps.load(std::memory_order_relaxed);

    for (uint32_t i = 0; i < kMaxInstances; ++i) {
        auto& instance = _instances[i];
        if (!instance.is_active) continue;

        // 1. Handle Trigger OUT (Sequence End)
        double sequence_end_beat = instance.start_beat + (active_steps * step_duration);
        if (sequence_end_beat >= block_start_beat && sequence_end_beat < block_end_beat) {
            auto offset = static_cast<uint32_t>(
                std::round((sequence_end_beat - block_start_beat) * samples_per_beat));
            offset = std::min(offset, static_cast<uint32_t>(_output_buffer.frames - 1));

            PluginEvent trigger_ev;
            trigger_ev.event.header.size = sizeof(clap_event_note);
            trigger_ev.event.header.time = offset;
            trigger_ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            trigger_ev.event.header.type = CLAP_EVENT_NOTE_ON;
            trigger_ev.event.header.flags = 0;
            trigger_ev.event.note.port_index = 1;  // Trigger OUT
            trigger_ev.event.note.key = 60;
            trigger_ev.event.note.channel = 0;
            trigger_ev.event.note.velocity = 1.0;
            trigger_ev.event.note.note_id = -1;

            _output_event_queues[1]->try_enqueue(trigger_ev);
            instance.is_active = false;
        }

        // 2. Generate NOTE_ON from Pattern
        for (const auto& note : _active_pattern) {
            double note_start_relative = note.step * step_duration;
            // Apply swing to even-numbered steps (1, 3, 5...)
            if (note.step % 2 == 1) {
                note_start_relative += swing_factor * step_duration;
            }

            double global_note_start = instance.start_beat + note_start_relative;
            if (global_note_start >= block_start_beat && global_note_start < block_end_beat) {
                auto offset = static_cast<uint32_t>(
                    std::round((global_note_start - block_start_beat) * samples_per_beat));
                offset = std::min(offset, static_cast<uint32_t>(_output_buffer.frames - 1));
                triggerNoteOn(i, note, global_note_start, offset);

                // GUI Sync: Send Current Step update
                PluginEvent gui_ev;
                gui_ev.event.header.size = sizeof(clap_event_param_value);
                gui_ev.event.header.time = offset;
                gui_ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                gui_ev.event.header.type = CLAP_EVENT_PARAM_VALUE;
                gui_ev.event.header.flags = 0;
                gui_ev.event.param_value.param_id = kParamCurrentStep;
                gui_ev.event.param_value.value = static_cast<double>(note.step);
                gui_ev.event.param_value.note_id = -1;
                gui_ev.event.param_value.port_index = -1;
                gui_ev.event.param_value.key = -1;
                gui_ev.event.param_value.channel = -1;
                _output_events_to_main.try_enqueue(gui_ev);
            }
        }

        // 3. Generate NOTE_OFF from Active Notes
        for (uint32_t n = 0; n < kMaxActiveNotesPerInstance; ++n) {
            auto& active = instance.active_notes[n];
            if (!active.is_active) continue;

            if (active.off_beat >= block_start_beat && active.off_beat < block_end_beat) {
                auto offset = static_cast<uint32_t>(
                    std::round((active.off_beat - block_start_beat) * samples_per_beat));
                offset = std::min(offset, static_cast<uint32_t>(_output_buffer.frames - 1));
                triggerNoteOff(i, n, offset);
            } else if (!instance.is_active) {
                // Sequence ended, force kill remaining notes
                triggerNoteOff(i, n, 0);
            }
        }
    }

    _last_block_end_beat = block_end_beat;
}

void SequencerNode::setParameterValue(clap_id param_id, double value) {
    InternalNodeBase::setParameterValue(param_id, value);

    switch (param_id) {
        case kParamSteps:
            _active_steps.store(static_cast<int32_t>(value), std::memory_order_relaxed);
            break;
        case kParamTime:
            _time_enum.store(static_cast<int32_t>(value), std::memory_order_relaxed);
            break;
        case kParamSwing:
            _swing.store(value, std::memory_order_relaxed);
            break;
        case kParamRestart:
            _restart_mode.store(value > 0.5, std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

void SequencerNode::queueEvent(const PluginEvent& event) {
    if (event.event.header.type == CLAP_EVENT_NOTE_ON) {
        // Find trigger beat relative to current block
        double tempo = _transport ? _transport->tempo : 120.0;
        double samples_per_beat = (_current_sample_rate * 60.0) / tempo;
        double trigger_beat = (_transport ? _transport->song_pos_beats : 0.0) +
                              (event.event.header.time / samples_per_beat);

        if (_restart_mode.load(std::memory_order_relaxed)) {
            // Mono mode: reset all and start instance 0
            for (auto& inst : _instances) {
                if (inst.is_active) {
                    // Kill all active notes
                    for (uint32_t n = 0; n < kMaxActiveNotesPerInstance; ++n) {
                        if (inst.active_notes[n].is_active) {
                            triggerNoteOff(0, n, event.event.header.time);
                        }
                    }
                }
                inst.is_active = false;
            }
            _instances[0].is_active = true;
            _instances[0].start_beat = trigger_beat;
        } else {
            // Poly mode: find free instance
            for (auto& inst : _instances) {
                if (!inst.is_active) {
                    inst.is_active = true;
                    inst.start_beat = trigger_beat;
                    break;
                }
            }
        }
    }
}

auto SequencerNode::saveState(std::vector<uint8_t>& data) -> bool {
    auto count = static_cast<uint32_t>(_active_pattern.size());
    size_t start = data.size();
    data.resize(start + sizeof(uint32_t) + (count * sizeof(NoteData)));

    std::memcpy(data.data() + start, &count, sizeof(uint32_t));
    if (count > 0) {
        std::memcpy(data.data() + start + sizeof(uint32_t), _active_pattern.data(),
                    count * sizeof(NoteData));
    }
    return true;
}

auto SequencerNode::loadState(const std::vector<uint8_t>& data) -> bool {
    if (data.size() < sizeof(uint32_t)) return false;

    uint32_t count;
    std::memcpy(&count, data.data(), sizeof(uint32_t));

    if (data.size() < sizeof(uint32_t) + (count * sizeof(NoteData))) return false;

    _pending_pattern.clear();
    if (count > 0) {
        const auto* src = reinterpret_cast<const NoteData*>(data.data() + sizeof(uint32_t));
        _pending_pattern.insert(_pending_pattern.end(), src, src + count);
    }
    _pending_update.store(true, std::memory_order_release);
    return true;
}

auto SequencerNode::getStepDurationBeats() const -> double {
    switch (_time_enum.load(std::memory_order_relaxed)) {
        case 0:
            return 1.0;  // 1/4
        case 1:
            return 0.5;  // 1/8
        case 2:
            return 1.0 / 3.0;  // 1/8T
        case 3:
            return 0.25;  // 1/16
        case 4:
            return 1.0 / 6.0;  // 1/16T
        case 5:
            return 0.125;  // 1/32
        default:
            return 0.5;
    }
}

void SequencerNode::triggerNoteOn(uint32_t instance_idx, const NoteData& note, double start_beat,
                                  uint32_t sample_offset) {
    auto& instance = _instances[instance_idx];

    // Find free active note slot
    for (uint32_t i = 0; i < kMaxActiveNotesPerInstance; ++i) {
        if (!instance.active_notes[i].is_active) {
            auto& active = instance.active_notes[i];
            active.is_active = true;
            active.pitch = note.pitch;
            active.note_id = _next_note_id++;
            active.off_beat = start_beat + (note.length * getStepDurationBeats());

            PluginEvent ev;
            ev.event.header.size = sizeof(clap_event_note);
            ev.event.header.time = sample_offset;
            ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev.event.header.type = CLAP_EVENT_NOTE_ON;
            ev.event.header.flags = 0;
            ev.event.note.port_index = 0;  // Note OUT
            ev.event.note.key = note.pitch;
            ev.event.note.channel = 0;
            ev.event.note.velocity = note.velocity / 255.0;
            ev.event.note.note_id = active.note_id;

            _output_event_queues[0]->try_enqueue(ev);
            return;
        }
    }
}

void SequencerNode::triggerNoteOff(uint32_t instance_idx, uint32_t note_idx,
                                   uint32_t sample_offset) {
    auto& instance = _instances[instance_idx];
    auto& active = instance.active_notes[note_idx];

    PluginEvent ev;
    ev.event.header.size = sizeof(clap_event_note);
    ev.event.header.time = sample_offset;
    ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.event.header.type = CLAP_EVENT_NOTE_OFF;
    ev.event.header.flags = 0;
    ev.event.note.port_index = 0;
    ev.event.note.key = active.pitch;
    ev.event.note.channel = 0;
    ev.event.note.velocity = 0.0;
    ev.event.note.note_id = active.note_id;

    _output_event_queues[0]->try_enqueue(ev);
    active.is_active = false;
}

}  // namespace synth_canvas::host
