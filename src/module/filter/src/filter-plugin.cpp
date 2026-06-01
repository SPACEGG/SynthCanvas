#include "filter-plugin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <string>

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

    const double tau = 0.015;
    _smoothing_coeff = 1.0 - std::exp(-1.0 / (_sample_rate * tau));

    // Initialize functional targets from normalized defaults
    _current_cutoff_hz = 20.0 * std::pow(1000.0, _cutoff_normalized);
    _current_resonance = _resonance_normalized;
    _current_mix = _mix_normalized;
    _current_drive_linear = std::pow(10.0, (_drive_normalized * 36.0) / 20.0);
    _current_gain_linear = std::pow(10.0, (-60.0 + _gain_normalized * 72.0) / 20.0);

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
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = std::log10(1000.0 / 20.0) / 3.0;  // 1000 Hz
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
            info->max_value = 5.0;
            info->default_value = 0.0;
            break;
        case kParamSlope:
            info->id = kParamSlope;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
            snprintf(info->name, sizeof(info->name), "Slope");
            snprintf(info->module, sizeof(info->module), "Main");
            info->min_value = 0.0;
            info->max_value = 2.0;
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
        case kParamDrive:
            info->id = kParamDrive;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            snprintf(info->name, sizeof(info->name), "Drive");
            snprintf(info->module, sizeof(info->module), "Main");
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.0;
            break;
        case kParamGain:
            info->id = kParamGain;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            snprintf(info->name, sizeof(info->name), "Gain");
            snprintf(info->module, sizeof(info->module), "Main");
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 60.0 / 72.0;  // 0 dB
            break;
        default:
            return false;
    }
    return true;
}

auto FilterPlugin::paramsValue(clap_id param_id, double* value) noexcept -> bool {
    switch (param_id) {
        case kParamCutoff:
            *value = _cutoff_normalized;
            break;
        case kParamResonance:
            *value = _resonance_normalized;
            break;
        case kParamMode:
            *value = _mode;
            break;
        case kParamSlope:
            *value = _slope;
            break;
        case kParamMix:
            *value = _mix_normalized;
            break;
        case kParamDrive:
            *value = _drive_normalized;
            break;
        case kParamGain:
            *value = _gain_normalized;
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
            ss << std::fixed << std::setprecision(1) << (20.0 * std::pow(1000.0, value)) << " Hz";
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
            ss << std::fixed << std::setprecision(1) << value * 100.0 << " %";
            break;
        case kParamDrive:
            ss << std::fixed << std::setprecision(1) << value * 36.0 << " dB";
            break;
        case kParamGain:
            ss << std::fixed << std::setprecision(1) << (-60.0 + value * 72.0) << " dB";
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
    char* end;
    double parsed = strtod(display, &end);
    if (end == display) return false;

    switch (param_id) {
        case kParamCutoff:
            parsed = std::clamp(parsed, 20.0, 20000.0);
            *value = std::log10(parsed / 20.0) / 3.0;
            break;
        case kParamResonance:
        case kParamMix:
            *value = std::clamp(parsed, 0.0, 1.0);
            break;
        case kParamDrive:
            parsed = std::clamp(parsed, 0.0, 36.0);
            *value = parsed / 36.0;
            break;
        case kParamGain:
            parsed = std::clamp(parsed, -60.0, 12.0);
            *value = (parsed + 60.0) / 72.0;
            break;
        default:
            *value = parsed;
            break;
    }
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
                    _cutoff_normalized = ev->value;
                    break;
                case kParamResonance:
                    _resonance_normalized = ev->value;
                    break;
                case kParamMode:
                    _mode = ev->value;
                    break;
                case kParamSlope:
                    _slope = ev->value;
                    break;
                case kParamMix:
                    _mix_normalized = ev->value;
                    break;
                case kParamDrive:
                    _drive_normalized = ev->value;
                    break;
                case kParamGain:
                    _gain_normalized = ev->value;
                    break;
            }
        }
    }
}

auto FilterPlugin::stateSave(const clap_ostream* os) noexcept -> bool {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "cutoff=" << _cutoff_normalized << ";"
        << "res=" << _resonance_normalized << ";"
        << "mode=" << _mode << ";"
        << "slope=" << _slope << ";"
        << "mix=" << _mix_normalized << ";"
        << "drive=" << _drive_normalized << ";"
        << "gain=" << _gain_normalized << ";";

    std::string s = oss.str();
    int64_t result = os->write(os, s.c_str(), s.size());
    return result == static_cast<int64_t>(s.size());
}

auto FilterPlugin::stateLoad(const clap_istream* is) noexcept -> bool {
    std::array<char, 1024> buffer;
    int64_t result = is->read(is, buffer.data(), buffer.size() - 1);
    if (result <= 0) return false;
    buffer[result] = '\0';

    std::string s(buffer.data());
    std::string pair;
    std::stringstream ss(s);
    while (std::getline(ss, pair, ';')) {
        size_t pos = pair.find('=');
        if (pos == std::string::npos) continue;
        std::string key = pair.substr(0, pos);
        std::string val_str = pair.substr(pos + 1);
        try {
            double val = std::stod(val_str);
            if (key == "cutoff") {
                _cutoff_normalized = val;
            } else if (key == "res") {
                _resonance_normalized = val;
            } else if (key == "mode") {
                _mode = val;
            } else if (key == "slope") {
                _slope = val;
            } else if (key == "mix") {
                _mix_normalized = val;
            } else if (key == "drive") {
                _drive_normalized = val;
            } else if (key == "gain") {
                _gain_normalized = val;
            }
        } catch (...) {
        }
    }
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

        // Map normalized + modulation to functional domain
        double total_cutoff_norm = std::clamp(_cutoff_normalized + _cutoff_mod, 0.0, 1.0);
        double target_cutoff_hz = 20.0 * std::pow(1000.0, total_cutoff_norm);

        double target_resonance = std::clamp(_resonance_normalized + _resonance_mod, 0.0, 1.0);
        double target_mix = std::clamp(_mix_normalized + _mix_mod, 0.0, 1.0);

        double total_drive_norm = std::clamp(_drive_normalized + _drive_mod, 0.0, 1.0);
        double target_drive_db = total_drive_norm * 36.0;
        double target_drive_linear = std::pow(10.0, target_drive_db / 20.0);

        double total_gain_norm = std::clamp(_gain_normalized + _gain_mod, 0.0, 1.0);
        double target_gain_db = -60.0 + total_gain_norm * 72.0;
        double target_gain_linear = std::pow(10.0, target_gain_db / 20.0);

        // Smoothing
        _current_cutoff_hz += _smoothing_coeff * (target_cutoff_hz - _current_cutoff_hz);
        _current_resonance += _smoothing_coeff * (target_resonance - _current_resonance);
        _current_mix += _smoothing_coeff * (target_mix - _current_mix);
        _current_drive_linear += _smoothing_coeff * (target_drive_linear - _current_drive_linear);
        _current_gain_linear += _smoothing_coeff * (target_gain_linear - _current_gain_linear);

        int slope_idx = std::clamp(static_cast<int>(_slope + 0.5), 0, 2);
        int num_stages = (slope_idx == 2) ? 4 : (slope_idx == 1 ? 2 : 1);
        _engine.setCoeff(_current_cutoff_hz, _current_resonance, _sample_rate, num_stages);

        float l = in[0][i] * static_cast<float>(_current_drive_linear);
        float r = in[1][i] * static_cast<float>(_current_drive_linear);
        float dry_l = in[0][i];
        float dry_r = in[1][i];

        auto mode = static_cast<SvfEngine::Mode>(std::clamp(static_cast<int>(_mode + 0.5), 0, 5));
        _engine.step(l, r, mode, num_stages);

        out[0][i] = (dry_l + static_cast<float>(_current_mix) * (l - dry_l)) *
                    static_cast<float>(_current_gain_linear);
        out[1][i] = (dry_r + static_cast<float>(_current_mix) * (r - dry_r)) *
                    static_cast<float>(_current_gain_linear);
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
                        _cutoff_normalized = ev->value;
                        break;
                    case kParamResonance:
                        _resonance_normalized = ev->value;
                        break;
                    case kParamMode:
                        _mode = ev->value;
                        break;
                    case kParamSlope:
                        _slope = ev->value;
                        break;
                    case kParamMix:
                        _mix_normalized = ev->value;
                        break;
                    case kParamDrive:
                        _drive_normalized = ev->value;
                        break;
                    case kParamGain:
                        _gain_normalized = ev->value;
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
                    case kParamDrive:
                        _drive_mod = ev->amount;
                        break;
                    case kParamGain:
                        _gain_mod = ev->amount;
                        break;
                }
            }
        }
        event_index++;
    }
}

auto FilterPlugin::extension(const char* id) noexcept -> const void* {
    if (std::strcmp(id, CLAP_EXT_MINI_CURVE_DISPLAY) == 0) {
        return &s_mini_curve_display;
    }
    return Plugin::extension(id);
}

auto FilterPlugin::get_magnitude(float f, float fc, float res, int m) const -> float {
    int slope_idx = std::clamp(static_cast<int>(_slope + 0.5), 0, 2);
    int num_stages = (slope_idx == 2) ? 4 : (slope_idx == 1 ? 2 : 1);
    auto k = static_cast<float>(std::pow(2.0 - 2.0 * res, 1.0 / num_stages));

    float f_val = std::tan(std::numbers::pi_v<float> * f / static_cast<float>(_sample_rate));
    float g = std::tan(std::numbers::pi_v<float> * fc / static_cast<float>(_sample_rate));

    if (g < 1e-6f) g = 1e-6f;
    float x = f_val / g;

    float denom_real = 1.0f - x * x;
    float denom_imag = k * x;
    float denom_sq = denom_real * denom_real + denom_imag * denom_imag;
    if (denom_sq < 1e-12f) denom_sq = 1e-12f;
    float denom = std::sqrt(denom_sq);

    float num = 0.0f;
    switch (m) {
        case SvfEngine::kLP:
            num = 1.0f;
            break;
        case SvfEngine::kHP:
            num = x * x;
            break;
        case SvfEngine::kBP:
            num = x;
            break;
        case SvfEngine::kNotch:
            num = std::abs(1.0f - x * x);
            break;
        case SvfEngine::kPeak:
            num = 1.0f + x * x;
            break;
        case SvfEngine::kAll:
            num = denom;
            break;
        default:
            return 1.0f;
    }

    float single_stage_mag = num / denom;
    return std::pow(single_stage_mag, static_cast<float>(num_stages));
}

const clap_plugin_mini_curve_display_t FilterPlugin::s_mini_curve_display = {
    // get_curve_count
    [](const clap_plugin_t* plugin) -> uint32_t { return 1; },
    // render
    [](const clap_plugin_t* plugin, clap_mini_curve_display_curve_data_t* curves,
       uint32_t curves_size) -> uint32_t {
        if (!plugin || !curves || curves_size == 0) return 0;
        auto* self = static_cast<FilterPlugin*>(plugin->plugin_data);
        if (!self) return 0;

        double total_cutoff_norm =
            std::clamp(self->_cutoff_normalized + self->_cutoff_mod, 0.0, 1.0);
        auto fc = static_cast<float>(20.0 * std::pow(1000.0, total_cutoff_norm));

        double total_res_norm =
            std::clamp(self->_resonance_normalized + self->_resonance_mod, 0.0, 1.0);
        auto res = static_cast<float>(total_res_norm);

        int mode = std::clamp(static_cast<int>(self->_mode + 0.5), 0, 5);

        for (uint32_t i = 0; i < curves[0].values_count; ++i) {
            float norm_x = static_cast<float>(i) / static_cast<float>(curves[0].values_count - 1);
            float f = 20.0f * std::pow(1000.0f, norm_x);

            float mag = self->get_magnitude(f, fc, res, mode);
            float db = 20.0f * std::log10(std::max(mag, 1e-5f));

            float curve_val = std::clamp(0.5f + db / 96.0f, 0.0f, 1.0f);
            uint16_t raw_val = 1 + static_cast<uint16_t>(curve_val * 65533.0f);
            curves[0].values[i] = raw_val;
        }

        curves[0].curve_kind = CLAP_MINI_CURVE_DISPLAY_CURVE_KIND_GAIN_RESPONSE;
        return 1;
    },
    // set_observed
    [](const clap_plugin_t* plugin, bool is_observed) {},
    // get_axis_name
    [](const clap_plugin_t* plugin, uint32_t curve_index, char* x_name, char* y_name,
       uint32_t name_capacity) -> bool {
        if (curve_index != 0 || name_capacity < 8) return false;
        std::strncpy(x_name, "Hz", name_capacity - 1);
        std::strncpy(y_name, "dB", name_capacity - 1);
        x_name[name_capacity - 1] = '\0';
        y_name[name_capacity - 1] = '\0';
        return true;
    }};

}  // namespace synth_canvas::filter_plugin
