#include "sequencer_node.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "logger.h"

namespace synth_canvas::host {

SequencerNode::SequencerNode() {
    _node_type_name = "sequencer";
    addParameter(kParamSteps, "Steps", "Logic", 1.0, 32.0, 8.0,
                 CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED);
    addParameter(kParamTime, "Time", "Logic", 0.0, 5.0, 1.0,
                 CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED);
    addParameter(kParamSwing, "Swing", "Logic", 0.0, 0.75, 0.0, CLAP_PARAM_IS_AUTOMATABLE);
    addParameter(kParamRestart, "Restart", "Logic", 0.0, 1.0, 1.0,
                 CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED);
    addParameter(kParamCurrentStep, "Current Step", "Logic", -1.0, 31.0, -1.0,
                 CLAP_PARAM_IS_READONLY | CLAP_PARAM_IS_STEPPED);

    addEventPort("Trigger IN", true);
    addEventPort("Note OUT", false);     // Port 0
    addEventPort("Trigger OUT", false);  // Port 1

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
        instance.last_processed_relative_beat = 0.0;
    }
    _last_block_end_beat = -1.0;
    _was_playing = false;
}

void SequencerNode::processBegin(int num_frames) {
    InternalNodeBase::processBegin(num_frames);

    if (_pending_update.load(std::memory_order_acquire)) {
        _active_pattern = _pending_pattern;
        _pending_update.store(false, std::memory_order_release);
    }
}

void SequencerNode::process() {
    auto send_step_gui_update = [&](int32_t step, uint32_t offset) {
        PluginEvent gui_ev;
        gui_ev.event.header.size = sizeof(clap_event_param_value);
        gui_ev.event.header.time = offset;
        gui_ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        gui_ev.event.header.type = CLAP_EVENT_PARAM_VALUE;
        gui_ev.event.header.flags = 0;
        gui_ev.event.param_value.param_id = kParamCurrentStep;
        gui_ev.event.param_value.value = static_cast<double>(step);
        gui_ev.event.param_value.note_id = -1;
        gui_ev.event.param_value.port_index = -1;
        gui_ev.event.param_value.key = -1;
        gui_ev.event.param_value.channel = -1;
        _output_events_to_main.try_enqueue(gui_ev);
    };

    if (!_transport || !_transport->is_playing) {
        if (_was_playing) {
            send_step_gui_update(-1, 0);
            _was_playing = false;
        }
        return;
    }
    _was_playing = true;

    double tempo = _transport->tempo;
    double samples_per_beat = (_current_sample_rate * 60.0) / tempo;
    double block_start_beat = _transport->song_pos_beats;
    uint32_t num_frames = _output_buffer.frames;
    double block_beats = static_cast<double>(num_frames) / samples_per_beat;

    double step_duration = getStepDurationBeats();
    double swing_factor = _swing.load(std::memory_order_relaxed);
    int32_t active_steps = _active_steps.load(std::memory_order_relaxed);

    uint32_t first_active_idx = 0xFFFFFFFF;
    for (uint32_t i = 0; i < kMaxInstances; ++i) {
        if (_instances[i].is_active) {
            if (first_active_idx == 0xFFFFFFFF) first_active_idx = i;
            break;
        }
    }

    constexpr double epsilon = 1e-10;

    for (uint32_t i = 0; i < kMaxInstances; ++i) {
        auto& instance = _instances[i];
        if (!instance.is_active) continue;

        // Progress Calculation (Delta Timing) - Idempotent
        double progress_start = instance.last_processed_relative_beat;
        double progress_end = (block_start_beat + block_beats) - instance.start_beat;

        // If the instance started exactly at the block end or in the future, wait for next block.
        if (progress_end <= progress_start) continue;

        double sequence_end_relative_beat = active_steps * step_duration;
        bool sequence_finished = false;
        if (sequence_end_relative_beat >= progress_start - epsilon &&
            sequence_end_relative_beat < progress_end - epsilon) {
            double relative_in_block_beat = sequence_end_relative_beat - progress_start;
            auto offset =
                static_cast<uint32_t>(std::round(relative_in_block_beat * samples_per_beat));
            offset = std::min(offset, num_frames - 1);

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
            sequence_finished = true;

            if (i == first_active_idx) {
                send_step_gui_update(-1, offset);
            }
        }

        // 2. GUI Step Updates
        if (i == first_active_idx) {
            for (int32_t s = 0; s < active_steps; ++s) {
                double step_beat_relative = s * step_duration;
                if (step_beat_relative >= progress_start - epsilon &&
                    step_beat_relative < progress_end - epsilon) {
                    double relative_in_block_beat = step_beat_relative - progress_start;
                    auto offset = static_cast<uint32_t>(
                        std::round(relative_in_block_beat * samples_per_beat));
                    offset = std::min(offset, num_frames - 1);
                    send_step_gui_update(s, offset);
                }
            }
        }

        // 3. NOTE_ON from Pattern
        for (const auto& note : _active_pattern) {
            double note_start_relative = note.step * step_duration;
            if (note.step % 2 == 1) {
                note_start_relative += swing_factor * step_duration;
            }

            if (note_start_relative >= progress_start - epsilon &&
                note_start_relative < progress_end - epsilon) {
                double relative_in_block_beat = note_start_relative - progress_start;
                auto offset =
                    static_cast<uint32_t>(std::round(relative_in_block_beat * samples_per_beat));
                offset = std::min(offset, num_frames - 1);

                double global_note_start = block_start_beat + relative_in_block_beat;
                triggerNoteOn(i, note, global_note_start, offset);
            }
        }

        // 4. NOTE_OFF from Active Notes
        for (uint32_t n = 0; n < kMaxActiveNotesPerInstance; ++n) {
            auto& active = instance.active_notes[n];
            if (!active.is_active) continue;

            double off_relative_beat = active.off_beat - instance.start_beat;
            if (off_relative_beat >= progress_start - epsilon &&
                off_relative_beat < progress_end - epsilon) {
                double relative_in_block_beat = off_relative_beat - progress_start;
                auto offset =
                    static_cast<uint32_t>(std::round(relative_in_block_beat * samples_per_beat));
                offset = std::min(offset, num_frames - 1);
                triggerNoteOff(i, n, offset);
            } else if (sequence_finished) {
                triggerNoteOff(i, n, num_frames - 1);
            }
        }

        instance.last_processed_relative_beat = progress_end;
        if (sequence_finished) {
            instance.is_active = false;
        }
    }
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
        double tempo = _transport ? _transport->tempo : 120.0;
        double samples_per_beat = (_current_sample_rate * 60.0) / tempo;

        double trigger_beat = (_transport ? _transport->song_pos_beats : 0.0) +
                              (static_cast<double>(event.event.header.time) / samples_per_beat);

        auto setup_instance = [&](PlaybackInstance& inst) {
            inst.is_active = true;
            inst.start_beat = trigger_beat;
            // CRITICAL FIX: Reset the internal timeline to match the offset within the block.
            inst.last_processed_relative_beat =
                -(static_cast<double>(event.event.header.time) / samples_per_beat);
            for (auto& n : inst.active_notes) n.is_active = false;
        };

        if (_restart_mode.load(std::memory_order_relaxed)) {
            for (uint32_t i = 0; i < kMaxInstances; ++i) {
                auto& inst = _instances[i];
                if (inst.is_active) {
                    for (uint32_t n = 0; n < kMaxActiveNotesPerInstance; ++n) {
                        if (inst.active_notes[n].is_active) {
                            triggerNoteOff(i, n, event.event.header.time);
                        }
                    }
                }
                inst.is_active = false;
            }
            setup_instance(_instances[0]);
        } else {
            for (auto& inst : _instances) {
                if (!inst.is_active) {
                    setup_instance(inst);
                    break;
                }
            }
        }
    }
}

auto SequencerNode::saveState(std::vector<uint8_t>& data) -> bool {
    // 1. Save base class state (parameters) FIRST to align with Godot UI expectations
    if (!InternalNodeBase::saveState(data)) return false;

    // 2. Append Sequencer-specific pattern state afterwards
    // CRITICAL: If a pending update exists (just loaded but not yet processed by audio thread),
    // we must return the pending pattern to prevent UI data loss.
    bool has_pending = _pending_update.load(std::memory_order_acquire);
    const auto& pattern_to_save = has_pending ? _pending_pattern : _active_pattern;

    auto count = static_cast<uint32_t>(pattern_to_save.size());
    size_t start = data.size();
    data.resize(start + sizeof(uint32_t) + (count * sizeof(NoteData)));

    std::memcpy(data.data() + start, &count, sizeof(uint32_t));
    if (count > 0) {
        std::memcpy(data.data() + start + sizeof(uint32_t), pattern_to_save.data(),
                    count * sizeof(NoteData));
    }

    return true;
}

auto SequencerNode::loadState(const std::vector<uint8_t>& data) -> bool {
    if (data.size() < sizeof(uint32_t)) {
        return false;
    }

    // 1. Locate the start of Sequencer-specific data using the parent's block size header
    uint32_t base_block_size = 0;
    std::memcpy(&base_block_size, data.data(), sizeof(uint32_t));

    // Pass the whole data to InternalNodeBase; it will only read up to base_block_size
    if (data.size() >= base_block_size && base_block_size > 0) {
        InternalNodeBase::loadState(data);
    }

    size_t offset = base_block_size;
    if (data.size() < offset + sizeof(uint32_t)) {
        return true; 
    }

    uint32_t count;
    std::memcpy(&count, data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    if (data.size() < offset + (count * sizeof(NoteData))) {
        return false;
    }

    _pending_pattern.clear();
    if (count > 0) {
        const auto* src = reinterpret_cast<const NoteData*>(data.data() + offset);
        _pending_pattern.insert(_pending_pattern.end(), src, src + count);
    }
    _pending_update.store(true, std::memory_order_release);
    
    return true;
}

auto SequencerNode::getStepDurationBeats() const -> double {
    switch (_time_enum.load(std::memory_order_relaxed)) {
        case 0:
            return 1.0;
        case 1:
            return 0.5;
        case 2:
            return 1.0 / 3.0;
        case 3:
            return 0.25;
        case 4:
            return 1.0 / 6.0;
        case 5:
            return 0.125;
        default:
            return 0.5;
    }
}

void SequencerNode::triggerNoteOn(uint32_t instance_idx, const NoteData& note, double start_beat,
                                  uint32_t sample_offset) {
    auto& instance = _instances[instance_idx];
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
            ev.event.note.port_index = 0;
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
