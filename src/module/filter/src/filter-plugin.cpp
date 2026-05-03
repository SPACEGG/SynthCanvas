#include "filter-plugin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace synth_canvas::filter_plugin {

static const std::array<const char*, 3> kFeatures = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
                                                     CLAP_PLUGIN_FEATURE_FILTER, nullptr};

static const clap_plugin_descriptor kDesc = {
    .clap_version = CLAP_VERSION,
    .id = "com.synthcanvas.filter-plugin",
    .name = "Filter Plugin",
    .vendor = "SynthCanvas",
    .url = "https://github.com/SPACEGG/SynthCanvas",
    .manual_url = "",
    .support_url = "",
    .version = "0.1.0",
    .description = "A multi-slope State Variable Filter plugin.",
    .features = kFeatures.data()};

auto FilterPlugin::descriptor() -> const clap_plugin_descriptor* { return &kDesc; }

FilterPlugin::FilterPlugin(const std::string& plugin_path, const clap_host* host)
    : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                            clap::helpers::CheckingLevel::Maximal>(&kDesc, host) {}

auto FilterPlugin::activate(double sample_rate, uint32_t min_frames_count,
                            uint32_t max_frames_count) noexcept -> bool {
    if (!Plugin::activate(sample_rate, min_frames_count, max_frames_count)) return false;

    _sample_rate = sample_rate;

    // ~15ms time constant for smoothing
    const double tau = 0.015;
    _smoothing_coeff = 1.0 - std::exp(-1.0 / (_sample_rate * tau));

    _current_cutoff = _cutoff_hz;
    _current_resonance = _resonance;
    _current_mix = _mix;

    _engine.reset();

    return true;
}

auto FilterPlugin::audioPortsInfo(uint32_t index, bool is_input,
                                  clap_audio_port_info* info) const noexcept -> bool {
    if (index > 0) return false;

    info->id = is_input ? 0 : 1;
    snprintf(info->name, sizeof(info->name), "%s", is_input ? "Audio In" : "Audio Out");
    info->channel_count = 2;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

auto FilterPlugin::paramsCount() const noexcept -> uint32_t { return kParamCount; }

auto FilterPlugin::paramsInfo(uint32_t index, clap_param_info* info) const noexcept -> bool {
    switch (index) {
        case kParamCutoff:
            info->id = kParamCutoff;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            snprintf(info->name, sizeof(info->name), "Cutoff");
            snprintf(info->module, sizeof(info->module), "Main");
            info->min_value = 20.0;
            info->max_value = 20000.0;
            info->default_value = 1000.0;
            break;
        case kParamResonance:
            info->id = kParamResonance;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            snprintf(info->name, sizeof(info->name), "Resonance");
            snprintf(info->module, sizeof(info->module), "Main");
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.5;
            break;
        case kParamMode:
            info->id = kParamMode;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
            snprintf(info->name, sizeof(info->name), "Mode");
            snprintf(info->module, sizeof(info->module), "Main");
            info->min_value = 0.0;
            info->max_value = 5.0;  // LP, HP, BP, Notch, Peak, All
            info->default_value = 0.0;
            break;
        case kParamSlope:
            info->id = kParamSlope;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
            snprintf(info->name, sizeof(info->name), "Slope");
            snprintf(info->module, sizeof(info->module), "Main");
            info->min_value = 0.0;
            info->max_value = 2.0;  // 0: 12dB, 1: 24dB, 2: 48dB
            info->default_value = 0.0;
            break;
        case kParamMix:
            info->id = kParamMix;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            snprintf(info->name, sizeof(info->name), "Mix");
            snprintf(info->module, sizeof(info->module), "Main");
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 1.0;
            break;
        default:
            return false;
    }
    return true;
}

auto FilterPlugin::paramsValue(clap_id param_id, double* value) noexcept -> bool {
    switch (param_id) {
        case kParamCutoff:
            *value = _cutoff_hz;
            break;
        case kParamResonance:
            *value = _resonance;
            break;
        case kParamMode:
            *value = _mode;
            break;
        case kParamSlope:
            *value = _slope;
            break;
        case kParamMix:
            *value = _mix;
            break;
        default:
            return false;
    }
    return true;
}

auto FilterPlugin::paramsValueToText(clap_id param_id, double value, char* display,
                                     uint32_t size) noexcept -> bool {
    std::stringstream ss;
    switch (param_id) {
        case kParamCutoff:
            ss << std::fixed << std::setprecision(1) << value << " Hz";
            break;
        case kParamResonance:
            ss << std::fixed << std::setprecision(2) << value;
            break;
        case kParamMode: {
            auto modes = std::to_array<const char*>({"LP", "HP", "BP", "Notch", "Peak", "All"});
            int idx = std::clamp(static_cast<int>(value + 0.5), 0, 5);
            ss << modes[idx];
            break;
        }
        case kParamSlope: {
            auto slopes = std::to_array<const char*>({"12dB", "24dB", "48dB"});
            int idx = std::clamp(static_cast<int>(value + 0.5), 0, 2);
            ss << slopes[idx];
            break;
        }
        case kParamMix:
            ss << std::fixed << std::setprecision(2) << value * 100.0 << " %";
            break;
        default:
            return false;
    }
    strncpy(display, ss.str().c_str(), size - 1);
    display[size - 1] = '\0';
    return true;
}

auto FilterPlugin::paramsTextToValue(clap_id param_id, const char* display, double* value) noexcept
    -> bool {
    // Simple implementation for basic parameters
    char* end;
    double parsed = strtod(display, &end);
    if (end == display) return false;
    *value = parsed;
    return true;
}

void FilterPlugin::paramsFlush(const clap_input_events* in,
                               const clap_output_events* out) noexcept {
    uint32_t event_count = in->size(in);
    for (uint32_t i = 0; i < event_count; ++i) {
        const clap_event_header* header = in->get(in, i);
        if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;

        if (header->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* ev = reinterpret_cast<const clap_event_param_value*>(header);
            switch (ev->param_id) {
                case kParamCutoff:
                    _cutoff_hz = ev->value;
                    break;
                case kParamResonance:
                    _resonance = ev->value;
                    break;
                case kParamMode:
                    _mode = ev->value;
                    break;
                case kParamSlope:
                    _slope = ev->value;
                    break;
                case kParamMix:
                    _mix = ev->value;
                    break;
            }
        }
    }
}

auto FilterPlugin::stateSave(const clap_ostream* os) noexcept -> bool {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "cutoff=" << _cutoff_hz << ";"
        << "res=" << _resonance << ";"
        << "mode=" << _mode << ";"
        << "slope=" << _slope << ";"
        << "mix=" << _mix << ";";

    std::string s = oss.str();
    int64_t result = os->write(os, s.c_str(), s.size());
    return result == static_cast<int64_t>(s.size());
}

auto FilterPlugin::stateLoad(const clap_istream* is) noexcept -> bool {
    std::array<char, 1024> buffer;
    int64_t result = is->read(is, buffer.data(), sizeof(buffer.data()) - 1);
    if (result <= 0) return false;
    buffer[result] = '\0';

    std::string s(buffer.data());
    std::string pair;
    std::stringstream ss(s);
    while (std::getline(ss, pair, ';')) {
        size_t pos = pair.find('=');
        if (pos == std::string::npos) continue;
        std::string key = pair.substr(0, pos);
        double val = std::stod(pair.substr(pos + 1));
        if (key == "cutoff") {
            _cutoff_hz = val;
        } else if (key == "res") {
            _resonance = val;
        } else if (key == "mode") {
            _mode = val;
        } else if (key == "slope") {
            _slope = val;
        } else if (key == "mix") {
            _mix = val;
        }
    }

    _current_cutoff = _cutoff_hz;
    _current_resonance = _resonance;
    _current_mix = _mix;

    return true;
}

auto FilterPlugin::process(const clap_process* process) noexcept -> clap_process_status {
    const uint32_t nframes = process->frames_count;
    const uint32_t nevents = process->in_events->size(process->in_events);
    uint32_t event_index = 0;

    float* const* in = process->audio_inputs[0].data32;
    float* const* out = process->audio_outputs[0].data32;

    for (uint32_t i = 0; i < nframes; ++i) {
        handleEvents(process->in_events, event_index, i);

        // Target calculation with modulation
        double target_cutoff = std::clamp(_cutoff_hz + _cutoff_mod, 20.0, 20000.0);
        double target_resonance = std::clamp(_resonance + _resonance_mod, 0.0, 1.0);
        double target_mix = std::clamp(_mix + _mix_mod, 0.0, 1.0);

        // Smoothing
        _current_cutoff += _smoothing_coeff * (target_cutoff - _current_cutoff);
        _current_resonance += _smoothing_coeff * (target_resonance - _current_resonance);
        _current_mix += _smoothing_coeff * (target_mix - _current_mix);

        // Update DSP coefficients
        int slope_idx = std::clamp(static_cast<int>(_slope + 0.5), 0, 2);
        int num_stages = (slope_idx == 2) ? 4 : (slope_idx == 1 ? 2 : 1);
        _engine.setCoeff(_current_cutoff, _current_resonance, _sample_rate, num_stages);

        // DSP processing
        float l = in[0][i];
        float r = in[1][i];
        float dry_l = l;
        float dry_r = r;

        auto mode = static_cast<SvfEngine::Mode>(std::clamp(static_cast<int>(_mode + 0.5), 0, 5));
        _engine.step(l, r, mode, num_stages);

        // Dry/Wet Mix
        out[0][i] = dry_l + static_cast<float>(_current_mix) * (l - dry_l);
        out[1][i] = dry_r + static_cast<float>(_current_mix) * (r - dry_r);
    }

    return CLAP_PROCESS_CONTINUE;
}

void FilterPlugin::handleEvents(const clap_input_events* in, uint32_t& event_index,
                                uint32_t sample_index) noexcept {
    uint32_t event_count = in->size(in);
    while (event_index < event_count) {
        const clap_event_header* header = in->get(in, event_index);
        if (header->time > sample_index) break;

        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID) {
            if (header->type == CLAP_EVENT_PARAM_VALUE) {
                const auto* ev = reinterpret_cast<const clap_event_param_value*>(header);
                switch (ev->param_id) {
                    case kParamCutoff:
                        _cutoff_hz = ev->value;
                        break;
                    case kParamResonance:
                        _resonance = ev->value;
                        break;
                    case kParamMode:
                        _mode = ev->value;
                        break;
                    case kParamSlope:
                        _slope = ev->value;
                        break;
                    case kParamMix:
                        _mix = ev->value;
                        break;
                }
            } else if (header->type == CLAP_EVENT_PARAM_MOD) {
                const auto* ev = reinterpret_cast<const clap_event_param_mod*>(header);
                switch (ev->param_id) {
                    case kParamCutoff:
                        _cutoff_mod = ev->amount;
                        break;
                    case kParamResonance:
                        _resonance_mod = ev->amount;
                        break;
                    case kParamMix:
                        _mix_mod = ev->amount;
                        break;
                }
            }
        }
        event_index++;
    }
}

}  // namespace synth_canvas::filter_plugin
