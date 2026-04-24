#include "envelope_node.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace synth_canvas::host {

EnvelopeNode::EnvelopeNode() {
    addParameter(kAttack, "Attack", "env", 0.1, 10000.0, 10.0);
    addParameter(kDecay, "Decay", "env", 0.1, 10000.0, 100.0);
    addParameter(kSustain, "Sustain", "env", 0.0, 1.0, 0.5);
    addParameter(kRelease, "Release", "env", 0.1, 10000.0, 500.0);
    addParameter(kAttackCurve, "A Curve", "env", -1.0, 1.0, 0.0);
    addParameter(kDecayCurve, "D Curve", "env", -1.0, 1.0, 0.0);
    addParameter(kReleaseCurve, "R Curve", "env", -1.0, 1.0, 0.0);
    addParameter(kVelocityAmp, "Vel->Amp", "env", 0.0, 1.0, 1.0);
    addParameter(kVelocityTime, "Vel->Atk", "env", 0.0, 1.0, 0.0);
    addParameter(kAmount, "Amount", "env", -1.0, 1.0, 1.0);
    addParameter(kBypass, "Bypass", "env", 0.0, 1.0, 0.0, CLAP_PARAM_IS_STEPPED);

    addAudioPort("Note In", true, 0, false);
    addAudioPort("Signal Out", false, 0, true);  // No audio, just events
}

void EnvelopeNode::activate(int32_t sample_rate, int32_t block_size) {
    InternalNodeBase::activate(sample_rate, block_size);
    for (auto& voice : _voices) {
        voice.active = false;
        voice.adsr.stage = ADSRState::kIdle;
        voice.adsr.current_value = 0.0;
    }
}

void EnvelopeNode::process() {
    if (!_processing_enabled || !isActive()) return;

    // 1. Update cached parameters
    _cached_attack = getParameterBaseValue(kAttack);
    _cached_decay = getParameterBaseValue(kDecay);
    _cached_sustain = getParameterBaseValue(kSustain);
    _cached_release = getParameterBaseValue(kRelease);
    _cached_a_curve = getParameterBaseValue(kAttackCurve);
    _cached_d_curve = getParameterBaseValue(kDecayCurve);
    _cached_r_curve = getParameterBaseValue(kReleaseCurve);
    _cached_vel_amp = getParameterBaseValue(kVelocityAmp);
    _cached_vel_time = getParameterBaseValue(kVelocityTime);
    _cached_amount = getParameterBaseValue(kAmount);
    _cached_bypass = getParameterBaseValue(kBypass) > 0.5;

    // 2. Process active voices
    for (auto& voice : _voices) {
        if (!voice.active) continue;

        // We iterate through frames, but update modulation every StepSize
        for (int i = 0; i < _output_buffer.frames; ++i) {
            processVoice(voice, i);

            if (i % constants::kModulationStepSize == 0) {
                pushModulationEvent(voice, i);
            }
        }
    }
}

void EnvelopeNode::queueEvent(const PluginEvent& event) {
    if (event.event.header.type == CLAP_EVENT_NOTE_ON) {
        triggerNoteOn(event.event.note.key, event.event.note.channel, event.event.note.note_id,
                      event.event.note.velocity);
    } else if (event.event.header.type == CLAP_EVENT_NOTE_OFF) {
        triggerNoteOff(event.event.note.key, event.event.note.note_id);
    }
}

void EnvelopeNode::triggerNoteOn(int16_t key, int16_t channel, int32_t note_id, double velocity) {
    // Soft-takeover: try to find existing voice for same note
    VoiceState* voice = findVoice(key, note_id);
    if (!voice) {
        voice = findFreeVoice();
    }
    if (!voice) {
        voice = getOldestVoice();
    }

    if (voice) {
        voice->key = key;
        voice->channel = channel;
        voice->note_id = note_id;
        voice->active = true;
        voice->last_active_time = ++_voice_counter;

        // Velocity mapping
        voice->adsr.peak_amplitude = 1.0 - _cached_vel_amp + (_cached_vel_amp * velocity);

        double vel_time_factor = 1.0 - (velocity * _cached_vel_time);
        double actual_attack = std::max(0.1, _cached_attack * vel_time_factor);

        // Transition to ATTACK
        voice->adsr.stage = ADSRState::kAttack;
        voice->adsr.target_value = 1.0;  // Normalized target
        voice->adsr.coeff = calculateCoeff(actual_attack, _cached_a_curve);
    }
}

void EnvelopeNode::triggerNoteOff(int16_t key, int32_t note_id) {
    VoiceState* voice = findVoice(key, note_id);
    if (voice && voice->adsr.stage != ADSRState::kIdle) {
        voice->adsr.stage = ADSRState::kRelease;
        voice->adsr.target_value = 0.0;
        voice->adsr.coeff = calculateCoeff(_cached_release, _cached_r_curve);
    }
}

auto EnvelopeNode::findFreeVoice() -> VoiceState* {
    for (auto& voice : _voices) {
        if (!voice.active) return &voice;
    }
    return nullptr;
}

auto EnvelopeNode::findVoice(int16_t key, int32_t note_id) -> VoiceState* {
    for (auto& voice : _voices) {
        if (voice.active && voice.key == key && (note_id == -1 || voice.note_id == note_id)) {
            return &voice;
        }
    }
    return nullptr;
}

auto EnvelopeNode::getOldestVoice() -> VoiceState* {
    VoiceState* oldest = nullptr;
    uint32_t min_time = 0xFFFFFFFF;
    for (auto& voice : _voices) {
        if (voice.last_active_time < min_time) {
            min_time = voice.last_active_time;
            oldest = &voice;
        }
    }
    return oldest;
}

auto EnvelopeNode::calculateCoeff(double time_ms, double curve) const -> double {
    double tau = time_ms * 0.001;
    if (curve > 0.01) {
        tau *= (1.0 + curve * 5.0);
    } else if (curve < -0.01) {
        tau /= (1.0 - curve * 5.0);
    }

    return 1.0 - std::exp(-1.0 / (_current_sample_rate * std::max(0.0001, tau)));
}

void EnvelopeNode::processVoice(VoiceState& voice, uint32_t frame_index) {
    auto& adsr = voice.adsr;

    if (_cached_bypass) {
        if (adsr.stage == ADSRState::kRelease) {
            adsr.current_value = 0.0;
            adsr.stage = ADSRState::kIdle;
            voice.active = false;
            pushNoteEndEvent(voice, frame_index);
        } else if (adsr.stage != ADSRState::kIdle) {
            adsr.current_value = 1.0;
        }
        return;
    }

    const double kThreshold = constants::kEnvelopeSilenceThreshold;

    // 1-pole filter style transition
    adsr.current_value += (adsr.target_value - adsr.current_value) * adsr.coeff;

    // Stage transitions
    switch (adsr.stage) {
        case ADSRState::kAttack:
            if (adsr.current_value >= 1.0 - kThreshold) {
                adsr.current_value = 1.0;
                adsr.stage = ADSRState::kDecay;
                adsr.target_value = _cached_sustain;
                adsr.coeff = calculateCoeff(_cached_decay, _cached_d_curve);
            }
            break;
        case ADSRState::kDecay:
            if (std::abs(adsr.current_value - _cached_sustain) < kThreshold) {
                adsr.current_value = _cached_sustain;
                adsr.stage = ADSRState::kSustain;
                adsr.target_value = _cached_sustain;
                adsr.coeff = 0.0;
            }
            break;
        case ADSRState::kSustain:
            adsr.current_value = _cached_sustain;
            break;
        case ADSRState::kRelease:
            if (adsr.current_value < kThreshold) {
                adsr.current_value = 0.0;
                adsr.stage = ADSRState::kIdle;
                voice.active = false;
                pushNoteEndEvent(voice, frame_index);
            }
            break;
        default:
            break;
    }
}

void EnvelopeNode::pushModulationEvent(const VoiceState& voice, uint32_t frame_index) {
    PluginEvent ev;
    ev.event.header.size = sizeof(clap_event_param_mod);
    ev.event.header.time = frame_index;
    ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.event.header.type = CLAP_EVENT_PARAM_MOD;
    ev.event.header.flags = 0;

    ev.event.param_mod.port_index = 0;
    ev.event.param_mod.key = voice.key;
    ev.event.param_mod.channel = voice.channel;
    ev.event.param_mod.note_id = voice.note_id;
    ev.event.param_mod.param_id = 0;
    ev.event.param_mod.amount =
        voice.adsr.current_value * voice.adsr.peak_amplitude * _cached_amount;

    _output_events.enqueue(ev);
}

void EnvelopeNode::pushNoteEndEvent(const VoiceState& voice, uint32_t frame_index) {
    PluginEvent ev;
    ev.event.header.size = sizeof(clap_event_note);
    ev.event.header.time = frame_index;
    ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.event.header.type = CLAP_EVENT_NOTE_END;
    ev.event.header.flags = 0;

    ev.event.note.port_index = 0;
    ev.event.note.key = voice.key;
    ev.event.note.channel = voice.channel;
    ev.event.note.note_id = voice.note_id;
    ev.event.note.velocity = 0.0;

    _output_events.enqueue(ev);
}

auto EnvelopeNode::getParameterText(clap_id param_id, double value) const -> std::string {
    std::array<char, 32> buf{};
    switch (param_id) {
        case kAttack:
        case kDecay:
        case kRelease:
            snprintf(buf.data(), buf.size(), "%.1f ms", value);
            break;
        case kSustain:
        case kVelocityAmp:
        case kVelocityTime:
            snprintf(buf.data(), buf.size(), "%.1f%%", value * 100.0);
            break;
        case kAttackCurve:
        case kDecayCurve:
        case kReleaseCurve:
            snprintf(buf.data(), buf.size(), "%.2f", value);
            break;
        default:
            return std::to_string(value);
    }
    return {buf.data()};
}

}  // namespace synth_canvas::host
