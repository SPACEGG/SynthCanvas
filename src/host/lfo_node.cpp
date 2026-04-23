#include "lfo_node.h"

#include <array>
#include <cmath>
#include <numbers>

namespace synth_canvas::host {

static constexpr double kPi = std::numbers::pi;

LFONode::LFONode() : _rng(std::random_device{}()) {
    // Add parameters with default values and ranges
    addParameter(kFreq, "Frequency", "lfo", 0.01, 20.0, 1.0);
    addParameter(kWaveform, "Waveform", "lfo", 0.0, 4.0, 0.0);
    addParameter(kSync, "Sync", "lfo", 0.0, 1.0, 0.0);
    addParameter(kRetrigger, "Retrigger", "lfo", 0.0, 1.0, 0.0);
    addParameter(kAmplitude, "Amplitude", "lfo", 0.0, 1.0, 1.0);
    addParameter(kOffset, "Offset", "lfo", -1.0, 1.0, 0.0);
    addParameter(kSmoothing, "Smoothing", "lfo", 0.0, 100.0, 5.0);

    // Setup ports
    addAudioPort("Note In", true, 0, false);     // Note input (0 channels)
    addAudioPort("Signal Out", false, 1, true);  // Mono signal output
}

void LFONode::activate(int32_t sample_rate, int32_t block_size) {
    InternalNodeBase::activate(sample_rate, block_size);
    _last_output = 0.0f;
    _current_smoothing_ms = -1.0f;  // Force update
    updateSmoothingCoeff();
}

void LFONode::process() {
    if (!_processing_enabled || !isActive()) return;

    auto* out_buf = _output_buffer.data32[0];
    int frames = _output_buffer.frames;

    // Get current parameter values (relaxed atomics from base class)
    double freq = getParameterBaseValue(kFreq);
    int waveform = static_cast<int>(getParameterBaseValue(kWaveform));
    bool sync = getParameterBaseValue(kSync) > 0.5;
    bool retrigger_enabled = getParameterBaseValue(kRetrigger) > 0.5;
    auto amp = static_cast<float>(getParameterBaseValue(kAmplitude));
    auto offset = static_cast<float>(getParameterBaseValue(kOffset));
    auto smoothing_ms = static_cast<float>(getParameterBaseValue(kSmoothing));

    // Update smoothing coefficient if parameter changed
    if (std::abs(smoothing_ms - _current_smoothing_ms) > 0.001f) {
        _current_smoothing_ms = smoothing_ms;
        updateSmoothingCoeff();
    }

    // Handle retrigger
    if (_retrigger_queued) {
        if (retrigger_enabled) {
            _phase = 0.0;
            _last_random_phase = -1.0;
        }
        _retrigger_queued = false;
    }

    double phase_inc = freq / _current_sample_rate;

    for (int i = 0; i < frames; ++i) {
        double current_sample_phase = _phase;

        if (sync && _transport) {
            // In sync mode, phase is absolute based on song position
            // song_pos_beats * division (freq acts as division/multiplier here)
            current_sample_phase = std::fmod(_transport->song_pos_beats * freq, 1.0);
            if (current_sample_phase < 0) current_sample_phase += 1.0;
        }

        float raw_val = generateWaveform(current_sample_phase, waveform);
        float target_val = (raw_val * amp) + offset;

        // Apply 1-pole LPF smoothing
        float out_val = _last_output + (target_val - _last_output) * _smooth_alpha;
        out_buf[i] = out_val;
        _last_output = out_val;

        // Increment phase for next sample (only used in Hz mode)
        if (!sync) {
            _phase += phase_inc;
            if (_phase >= 1.0) _phase -= 1.0;
        }
    }
}

void LFONode::queueEvent(const PluginEvent& event) {
    if (event.event.header.type == CLAP_EVENT_NOTE_ON) {
        _retrigger_queued = true;
    }
}

auto LFONode::generateWaveform(double phase, int waveform) -> float {
    switch (waveform) {
        case 0:  // Sine
            return std::sin(2.0 * kPi * phase);
        case 1:  // Triangle
            return 2.0 * std::abs(2.0 * (phase - std::floor(phase + 0.5))) - 1.0;
        case 2:  // Square
            return (phase < 0.5) ? 1.0f : -1.0f;
        case 3:  // Saw
            return 2.0 * (phase - std::floor(phase)) - 1.0;
        case 4:  // Random (Sample & Hold)
        {
            if (phase < _last_random_phase || _last_random_phase < 0.0) {
                _last_random_val = _dist(_rng);
            }
            _last_random_phase = phase;
            return _last_random_val;
        }
        default:
            return 0.0f;
    }
}

void LFONode::updateSmoothingCoeff() {
    if (_current_smoothing_ms <= 0.001f) {
        _smooth_alpha = 1.0f;
    } else {
        // alpha = 1 - exp(-2 * pi * fc / fs)
        // fc = 1 / (2 * pi * tau)
        // alpha = 1 - exp(-1 / (fs * tau))
        float tau = _current_smoothing_ms * 0.001f;
        _smooth_alpha = 1.0f - std::exp(-1.0f / (_current_sample_rate * tau));
    }
}

auto LFONode::getParameterText(clap_id param_id, double value) const -> std::string {
    switch (param_id) {
        case kFreq: {
            bool sync = getParameterBaseValue(kSync) > 0.5;
            std::array<char, 32> buf{};
            if (sync) {
                snprintf(buf.data(), buf.size(), "%.2fx", value);
            } else {
                snprintf(buf.data(), buf.size(), "%.2f Hz", value);
            }
            return {buf.data()};
        }
        case kWaveform: {
            int wave = static_cast<int>(value);
            switch (wave) {
                case 0:
                    return "Sine";
                case 1:
                    return "Triangle";
                case 2:
                    return "Square";
                case 3:
                    return "Saw";
                case 4:
                    return "Random";
                default:
                    return "Unknown";
            }
        }
        case kSync:
            return value > 0.5 ? "On" : "Off";
        case kRetrigger:
            return value > 0.5 ? "On" : "Off";
        case kAmplitude: {
            std::array<char, 32> buf{};
            snprintf(buf.data(), buf.size(), "%.1f%%", value * 100.0);
            return {buf.data()};
        }
        case kSmoothing: {
            std::array<char, 32> buf{};
            snprintf(buf.data(), buf.size(), "%.1f ms", value);
            return {buf.data()};
        }
        default:
            return std::to_string(value);
    }
}

}  // namespace synth_canvas::host
