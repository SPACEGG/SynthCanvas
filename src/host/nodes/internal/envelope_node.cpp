#include "host/nodes/internal/envelope_node.h"

#include <algorithm>
#include <cmath>

namespace synth_canvas::host {

EnvelopeNode::EnvelopeNode() {
    _node_type_name = "envelope";

    ParameterConfig time_config{.type = MappingType::kLogarithmic,
                                .min_functional = 0.1,
                                .max_functional = 10000.0,
                                .unit_suffix = " ms"};
    ParameterConfig lin01_config{.type = MappingType::kLinear,
                                 .min_functional = 0.0,
                                 .max_functional = 1.0,
                                 .unit_suffix = ""};
    ParameterConfig curve_config{.type = MappingType::kLinear,
                                 .min_functional = -1.0,
                                 .max_functional = 1.0,
                                 .unit_suffix = ""};
    ParameterConfig amount_config{.type = MappingType::kLinear,
                                  .min_functional = -1.0,
                                  .max_functional = 1.0,
                                  .unit_suffix = ""};

    addParameter(kAttack, "Attack", "env", time_config.toNormalized(10.0), time_config);
    addParameter(kDecay, "Decay", "env", time_config.toNormalized(100.0), time_config);
    addParameter(kSustain, "Sustain", "env", 0.5, lin01_config);
    addParameter(kRelease, "Release", "env", time_config.toNormalized(500.0), time_config);
    addParameter(kAttackCurve, "A Curve", "env", 0.5, curve_config);
    addParameter(kDecayCurve, "D Curve", "env", 0.5, curve_config);
    addParameter(kReleaseCurve, "R Curve", "env", 0.5, curve_config);
    addParameter(kVelocityAmp, "Vel->Amp", "env", 1.0, lin01_config);
    addParameter(kVelocityTime, "Vel->Atk", "env", 0.0, lin01_config);
    addParameter(kAmount, "Amount", "env", 1.0, amount_config);

    addSteppedParameter(kBypass, "Bypass", "env", 0.0, 1.0, 0.0);

    addAudioPort("Note In", true, 0, false);
    addAudioPort("Attack Mod", true, 1, true, kAttack);
    addAudioPort("Decay Mod", true, 1, true, kDecay);
    addAudioPort("Sustain Mod", true, 1, true, kSustain);
    addAudioPort("Release Mod", true, 1, true, kRelease);
    addAudioPort("Amount Mod", true, 1, true, kAmount);
    addAudioPort("Signal Out", false, 1, true);  // Port 0: Modulation Output
}

void EnvelopeNode::activate(int32_t sample_rate, int32_t block_size) {
    InternalNodeBase::activate(sample_rate, block_size);
    _adsr.stage = ADSRState::kIdle;
    _adsr.current_value = 0.0;
    _adsr.phase = 0.0;
    _current_key = -1;
}

void EnvelopeNode::process() {
    if (!_processing_enabled || !isActive()) return;

    auto* out_buf = _output_buffer.data32[0];
    int num_frames = _output_buffer.frames;

    for (int i = 0; i < num_frames; ++i) {
        // 1. Sample-accurate parameter updates
        updateParametersForSample(i);

        // 2. Cache functional values for this sample
        _cached_attack = getFunctionalValue(kAttack);
        _cached_decay = getFunctionalValue(kDecay);
        _cached_sustain = getFunctionalValue(kSustain);
        _cached_release = getFunctionalValue(kRelease);
        _cached_a_curve = getFunctionalValue(kAttackCurve);
        _cached_d_curve = getFunctionalValue(kDecayCurve);
        _cached_r_curve = getFunctionalValue(kReleaseCurve);
        _cached_vel_amp = getFunctionalValue(kVelocityAmp);
        _cached_vel_time = getFunctionalValue(kVelocityTime);
        _cached_amount = getFunctionalValue(kAmount);
        _cached_bypass = getFunctionalValue(kBypass) > 0.5;

        // 3. ADSR Logic
        if (_cached_bypass) {
            if (_adsr.stage == ADSRState::kRelease) {
                _adsr.current_value = 0.0;
                _adsr.stage = ADSRState::kIdle;
                _current_key = -1;
            } else if (_adsr.stage != ADSRState::kIdle) {
                _adsr.current_value = 1.0;
            }
        } else if (_adsr.stage != ADSRState::kIdle) {
            if (_adsr.stage == ADSRState::kSustain) {
                _adsr.current_value = _cached_sustain;
            } else {
                // Increment phase
                _adsr.phase = std::min(1.0, _adsr.phase + _adsr.phase_inc);

                // Curve shaping
                double curve_val = _adsr.curve;
                double x = _adsr.phase;
                double shaped_x = x;
                if (std::abs(curve_val) > 0.01) {
                    double f = 1.0 + std::abs(curve_val) * 9.0;
                    shaped_x = (curve_val > 0) ? std::pow(x, f) : 1.0 - std::pow(1.0 - x, f);
                }

                _adsr.current_value =
                    _adsr.start_value + (_adsr.target_value - _adsr.start_value) * shaped_x;

                // Stage transitions
                if (_adsr.phase >= 1.0) {
                    switch (_adsr.stage) {
                        case ADSRState::kAttack:
                            _adsr.stage = ADSRState::kDecay;
                            _adsr.start_value = 1.0;
                            _adsr.target_value = _cached_sustain;
                            _adsr.phase = 0.0;
                            _adsr.phase_inc = 1.0 / (_current_sample_rate *
                                                     (std::max(0.1, _cached_decay) * 0.001));
                            _adsr.curve = _cached_d_curve;
                            break;
                        case ADSRState::kDecay:
                            _adsr.stage = ADSRState::kSustain;
                            _adsr.current_value = _cached_sustain;
                            break;
                        case ADSRState::kRelease:
                            _adsr.current_value = 0.0;
                            _adsr.stage = ADSRState::kIdle;
                            _current_key = -1;
                            break;
                        default:
                            break;
                    }
                }
            }
        }

        // 4. Final output
        out_buf[i] =
            static_cast<float>(_adsr.current_value * _adsr.peak_amplitude * _cached_amount);
    }
}

void EnvelopeNode::queueEvent(const PluginEvent& event) {
    if (event.event.header.type == CLAP_EVENT_NOTE_ON) {
        triggerNoteOn(event.event.note.key, event.event.note.channel, event.event.note.note_id,
                      event.event.note.velocity, event.event.header.time);
    } else if (event.event.header.type == CLAP_EVENT_NOTE_OFF) {
        triggerNoteOff(event.event.note.key, event.event.note.note_id, event.event.header.time);
    }
}

void EnvelopeNode::triggerNoteOn(int16_t key, int16_t channel, int32_t note_id, double velocity,
                                 uint32_t offset) {
    // Sync parameters for the exact sample this event occurred at
    updateParametersForSample(offset);

    _current_key = key;

    // Velocity mapping
    _adsr.peak_amplitude = 1.0 - _cached_vel_amp + (_cached_vel_amp * velocity);
    double vel_time_factor = 1.0 - (velocity * _cached_vel_time);
    double actual_attack = std::max(0.1, _cached_attack * vel_time_factor);

    // Restart ADSR from current amplitude for smooth takeover
    _adsr.stage = ADSRState::kAttack;
    _adsr.start_value = _adsr.current_value;
    _adsr.target_value = 1.0;
    _adsr.phase = 0.0;
    _adsr.phase_inc = 1.0 / (_current_sample_rate * (actual_attack * 0.001));
    _adsr.curve = _cached_a_curve;
}

void EnvelopeNode::triggerNoteOff(int16_t key, int32_t note_id, uint32_t offset) {
    if (_current_key == key || key == -1) {
        // Sync parameters for release
        updateParametersForSample(offset);

        if (_adsr.stage != ADSRState::kIdle) {
            _adsr.stage = ADSRState::kRelease;
            _adsr.start_value = _adsr.current_value;
            _adsr.target_value = 0.0;
            _adsr.phase = 0.0;
            _adsr.phase_inc =
                1.0 / (_current_sample_rate * (std::max(0.1, _cached_release) * 0.001));
            _adsr.curve = _cached_r_curve;
        }
    }
}

auto EnvelopeNode::getParameterText(clap_id param_id, double value) const -> std::string {
    switch (param_id) {
        case kBypass:
            return value > 0.5 ? "On" : "Off";
        default:
            return InternalNodeBase::getParameterText(param_id, value);
    }
}

auto EnvelopeNode::supportsMiniCurve() const -> bool { return true; }

auto EnvelopeNode::getMiniCurveCount() const -> uint32_t { return 1; }

auto EnvelopeNode::getMiniCurveAxisNames(uint32_t curve_index, std::string& out_x,
                                         std::string& out_y) const -> bool {
    if (curve_index != 0) return false;
    out_x = "Time";
    out_y = "Level";
    return true;
}

auto EnvelopeNode::renderMiniCurve(uint32_t curve_index, std::vector<float>& out_values,
                                   uint32_t resolution) -> uint32_t {
    if (curve_index != 0 || resolution == 0) return 0;

    out_values.resize(resolution);

    auto c_a = static_cast<float>(getFunctionalValue(kAttackCurve));
    auto c_d = static_cast<float>(getFunctionalValue(kDecayCurve));
    auto c_r = static_cast<float>(getFunctionalValue(kReleaseCurve));
    auto s = static_cast<float>(getFunctionalValue(kSustain));
    auto amt = static_cast<float>(getFunctionalValue(kAmount));

    auto a = static_cast<float>(getParameterCurrentValue(kAttack));
    auto d = static_cast<float>(getParameterCurrentValue(kDecay));
    auto r = static_cast<float>(getParameterCurrentValue(kRelease));

    float p_a = a <= 0.01f ? 0.0f : std::log10(a + 1.0f);
    float p_d = d <= 0.01f ? 0.0f : std::log10(d + 1.0f);
    float p_r = r <= 0.01f ? 0.0f : std::log10(r + 1.0f);
    float p_total = p_a + p_d + p_r;

    float w_a = 0.0f;
    float w_d = 0.0f;
    float w_s = 0.20f;
    float w_r = 0.0f;

    float available_w = 1.0f - w_s;
    if (p_total > 0.001f) {
        w_a = (p_a / p_total) * available_w;
        w_d = (p_d / p_total) * available_w;
        w_r = (p_r / p_total) * available_w;
    } else {
        w_s = 1.0f;
    }

    auto get_shaped_val = [](float x, float curve) -> float {
        if (std::abs(curve) < 0.01f) return x;
        float f = 1.0f + std::abs(curve) * 9.0f;
        if (curve > 0.0f) return std::pow(x, f);
        return 1.0f - std::pow(1.0f - x, f);
    };

    for (uint32_t i = 0; i < resolution; ++i) {
        float x = static_cast<float>(i) / static_cast<float>(resolution - 1);
        float target_y = 0.0f;

        if (x < w_a) {
            float x_rel = x / w_a;
            target_y = get_shaped_val(x_rel, c_a);
        } else if (x < w_a + w_d) {
            float x_rel = (x - w_a) / w_d;
            target_y = 1.0f - (1.0f - s) * get_shaped_val(x_rel, c_d);
        } else if (x < w_a + w_d + w_s) {
            target_y = s;
        } else {
            float x_rel = std::min(1.0f, (x - (w_a + w_d + w_s)) / w_r);
            target_y = s - s * get_shaped_val(x_rel, c_r);
        }

        if (amt < 0.0f) {
            target_y = 1.0f - target_y;
        }
        target_y *= std::abs(amt);

        out_values[i] = target_y;
    }
    return 1;
}

}  // namespace synth_canvas::host
