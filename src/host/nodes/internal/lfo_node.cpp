#include "host/nodes/internal/lfo_node.h"

#include <array>
#include <cmath>
#include <numbers>

namespace synth_canvas::host {

static constexpr double kPi = std::numbers::pi;

struct SyncOption {
    const char* label;
    double multiplier;
};

static constexpr std::array<SyncOption, 10> kSyncOptions = {
    {{.label = "4/1", .multiplier = 1.0 / 16.0},
     {.label = "2/1", .multiplier = 1.0 / 8.0},
     {.label = "1/1", .multiplier = 1.0 / 4.0},
     {.label = "1/2", .multiplier = 1.0 / 2.0},
     {.label = "1/4", .multiplier = 1.0},
     {.label = "1/8", .multiplier = 2.0},
     {.label = "1/8T", .multiplier = 3.0},
     {.label = "1/16", .multiplier = 4.0},
     {.label = "1/16T", .multiplier = 6.0},
     {.label = "1/32", .multiplier = 8.0}}};

static constexpr double kMinFreq = 0.01;
static constexpr double kMaxFreq = 20.0;

static auto getSyncIndex(double normalized) -> int {
    auto index = static_cast<int>(normalized * static_cast<double>(kSyncOptions.size()));
    if (index >= static_cast<int>(kSyncOptions.size())) {
        index = static_cast<int>(kSyncOptions.size()) - 1;
    }
    if (index < 0) index = 0;
    return index;
}

LFONode::LFONode() : _rng(std::random_device{}()) {
    _node_type_name = "lfo";

    ParameterConfig freq_config{.type = MappingType::kLogarithmic,
                                .min_functional = kMinFreq,
                                .max_functional = kMaxFreq,
                                .unit_suffix = " Hz"};
    ParameterConfig amp_config{.type = MappingType::kLinear,
                               .min_functional = 0.0,
                               .max_functional = 1.0,
                               .unit_suffix = ""};
    ParameterConfig offset_config{.type = MappingType::kLinear,
                                  .min_functional = -1.0,
                                  .max_functional = 1.0,
                                  .unit_suffix = ""};
    ParameterConfig smooth_config{.type = MappingType::kLinear,
                                  .min_functional = 0.0,
                                  .max_functional = 100.0,
                                  .unit_suffix = " ms"};

    addParameter(kFreq, "Frequency", "lfo", freq_config.toNormalized(1.0), freq_config);
    addSteppedParameter(kWaveform, "Waveform", "lfo", 0.0, 4.0, 0.0);
    addSteppedParameter(kSync, "Sync", "lfo", 0.0, 1.0, 0.0);
    addSteppedParameter(kRetrigger, "Retrigger", "lfo", 0.0, 1.0, 0.0);
    addParameter(kAmplitude, "Amplitude", "lfo", 1.0, amp_config);
    addParameter(kOffset, "Offset", "lfo", 0.0, offset_config);
    addParameter(kSmoothing, "Smoothing", "lfo", smooth_config.toNormalized(5.0), smooth_config);

    // Setup ports
    addAudioPort("Note In", true, 0, false);
    addAudioPort("Freq Mod", true, 1, true, kFreq);
    addAudioPort("Amp Mod", true, 1, true, kAmplitude);
    addAudioPort("Offset Mod", true, 1, true, kOffset);
    addAudioPort("Smoothing Mod", true, 1, true, kSmoothing);
    addAudioPort("Signal Out", false, 1, true);
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

    for (int i = 0; i < frames; ++i) {
        // 1. Sample-accurate parameter updates
        updateParametersForSample(i);

        // 2. Refresh parameters for this sample
        double freq_normalized = getParameterCurrentValue(kFreq);
        double freq_functional = getFunctionalValue(kFreq);
        int waveform = static_cast<int>(getParameterCurrentValue(kWaveform));
        bool sync = getParameterCurrentValue(kSync) > 0.5;
        bool retrigger_enabled = getParameterCurrentValue(kRetrigger) > 0.5;
        auto amp = static_cast<float>(getFunctionalValue(kAmplitude));
        auto offset = static_cast<float>(getFunctionalValue(kOffset));
        auto smoothing_ms = static_cast<float>(getFunctionalValue(kSmoothing));

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

        double current_sample_phase = _phase;

        if (sync && _transport) {
            // In sync mode, phase is absolute based on song position
            double actual_freq = kSyncOptions[getSyncIndex(freq_normalized)].multiplier;
            current_sample_phase = std::fmod(_transport->song_pos_beats * actual_freq, 1.0);
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
            double phase_inc = freq_functional / _current_sample_rate;
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
            if (sync) {
                return kSyncOptions[getSyncIndex(value)].label;
            } else {
                // Use base class mapping for Hz mode (Logarithmic)
                return InternalNodeBase::getParameterText(param_id, value);
            }
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
        case kRetrigger:
            return value > 0.5 ? "On" : "Off";
        default:
            // Use InternalNodeBase mapping for Amplitude, Offset, and Smoothing
            return InternalNodeBase::getParameterText(param_id, value);
    }
}

}  // namespace synth_canvas::host
