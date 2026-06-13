#include "simple_fm_plugin.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace synth_canvas::fm_plugin {

static const std::array<const char*, 3> kFeatures = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, "stereo",
                                                     nullptr};

static const clap_plugin_descriptor kDesc = {
    .clap_version = CLAP_VERSION,
    .id = "com.synthcanvas.simple-fm",
    .name = "Simple FM",
    .vendor = "SynthCanvas",
    .url = "https://github.com/SPACEGG/SynthCanvas",
    .manual_url = "",
    .support_url = "",
    .version = "0.1.0",
    .description = "A Simple FM effect using phase modulation with integrated modulator.",
    .features = kFeatures.data()};

auto SimpleFmPlugin::descriptor() -> const clap_plugin_descriptor* { return &kDesc; }

SimpleFmPlugin::SimpleFmPlugin(const std::string& plugin_path, const clap_host* host)
    : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                            clap::helpers::CheckingLevel::Maximal>(&kDesc, host) {}

auto SimpleFmPlugin::activate(double sample_rate, uint32_t min_frames_count,
                              uint32_t max_frames_count) noexcept -> bool {
    if (!Plugin::activate(sample_rate, min_frames_count, max_frames_count)) return false;
    _sample_rate = sample_rate;
    _delay_line_l.reset();
    _delay_line_r.reset();
    _integrator_l.reset();
    _integrator_r.reset();
    return true;
}

auto SimpleFmPlugin::audioPortsCount(bool is_input) const noexcept -> uint32_t {
    return is_input ? 2 : 1;
}

auto SimpleFmPlugin::audioPortsInfo(uint32_t index, bool is_input,
                                    clap_audio_port_info* info) const noexcept -> bool {
    if (is_input) {
        if (index == 0) {
            info->id = 0;
            snprintf(info->name, sizeof(info->name), "carrier_in");
            info->channel_count = 2;
            info->flags = CLAP_AUDIO_PORT_IS_MAIN;
            info->port_type = CLAP_PORT_STEREO;
            info->in_place_pair = CLAP_INVALID_ID;
            return true;
        } else if (index == 1) {
            info->id = 1;
            snprintf(info->name, sizeof(info->name), "modulator_in");
            info->channel_count = 2;
            info->flags = 0;
            info->port_type = CLAP_PORT_STEREO;
            info->in_place_pair = CLAP_INVALID_ID;
            return true;
        }
    } else {
        if (index == 0) {
            info->id = 2;
            snprintf(info->name, sizeof(info->name), "audio_out");
            info->channel_count = 2;
            info->flags = CLAP_AUDIO_PORT_IS_MAIN;
            info->port_type = CLAP_PORT_STEREO;
            info->in_place_pair = CLAP_INVALID_ID;
            return true;
        }
    }
    return false;
}

auto SimpleFmPlugin::paramsCount() const noexcept -> uint32_t { return kParamCount; }

auto SimpleFmPlugin::paramsInfo(uint32_t index, clap_param_info* info) const noexcept -> bool {
    switch (index) {
        case kParamModDepth:
            info->id = kParamModDepth;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            snprintf(info->name, sizeof(info->name), "Depth");
            snprintf(info->module, sizeof(info->module), "Main");
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.2;
            break;
        case kParamDryWet:
            info->id = kParamDryWet;
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

auto SimpleFmPlugin::paramsValue(clap_id param_id, double* value) noexcept -> bool {
    switch (param_id) {
        case kParamModDepth:
            *value = _mod_depth_normalized;
            break;
        case kParamDryWet:
            *value = _dry_wet_normalized;
            break;
        default:
            return false;
    }
    return true;
}

auto SimpleFmPlugin::paramsValueToText(clap_id param_id, double value, char* display,
                                       uint32_t size) noexcept -> bool {
    std::stringstream ss;
    switch (param_id) {
        case kParamModDepth:
            ss << std::fixed << std::setprecision(1) << value * 100.0 << "%";
            break;
        case kParamDryWet:
            ss << std::fixed << std::setprecision(0) << value * 100.0 << "%";
            break;
        default:
            return false;
    }
    strncpy(display, ss.str().c_str(), size - 1);
    display[size - 1] = '\0';
    return true;
}

auto SimpleFmPlugin::paramsTextToValue(clap_id param_id, const char* display,
                                       double* value) noexcept -> bool {
    char* end;
    double parsed = strtod(display, &end);
    if (end == display) return false;

    switch (param_id) {
        case kParamModDepth:
        case kParamDryWet:
            *value = std::clamp(parsed / 100.0, 0.0, 1.0);
            break;
        default:
            *value = parsed;
            break;
    }
    return true;
}

void SimpleFmPlugin::paramsFlush(const clap_input_events* in,
                                 const clap_output_events* out) noexcept {
    uint32_t event_count = in->size(in);
    for (uint32_t i = 0; i < event_count; ++i) {
        const clap_event_header* header = in->get(in, i);
        if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;

        if (header->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* ev = reinterpret_cast<const clap_event_param_value*>(header);
            switch (ev->param_id) {
                case kParamModDepth:
                    _mod_depth_normalized = ev->value;
                    break;
                case kParamDryWet:
                    _dry_wet_normalized = ev->value;
                    break;
            }
        }
    }
}

auto SimpleFmPlugin::stateSave(const clap_ostream* os) noexcept -> bool {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "mod_depth=" << _mod_depth_normalized << ";"
        << "dry_wet=" << _dry_wet_normalized << ";";

    std::string s = oss.str();
    int64_t result = os->write(os, s.c_str(), s.size());
    return std::cmp_equal(result, s.size());
    // return std::cmp_equal(result, static_cast<int64_t>(s.size()));
}

auto SimpleFmPlugin::stateLoad(const clap_istream* is) noexcept -> bool {
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
            if (key == "mod_depth") {
                _mod_depth_normalized = val;
            } else if (key == "dry_wet") {
                _dry_wet_normalized = val;
            }
        } catch (...) {
        }
    }
    return true;
}

auto SimpleFmPlugin::process(const clap_process* process) noexcept -> clap_process_status {
    const uint32_t nframes = process->frames_count;
    uint32_t event_index = 0;

    float* const* in_carrier = process->audio_inputs[0].data32;
    float* const* out = process->audio_outputs[0].data32;

    const bool has_modulator =
        (process->audio_inputs_count > 1 && process->audio_inputs[1].data32 != nullptr);
    const uint32_t mod_channels = has_modulator ? process->audio_inputs[1].channel_count : 0;
    float* const* in_modulator = has_modulator ? process->audio_inputs[1].data32 : nullptr;

    const double max_delay_samples = 0.004 * _sample_rate;
    const double base_delay_samples = 0.005 * _sample_rate;
    const double min_delay = 0.00005 * _sample_rate;
    const double max_delay = 2.0 * base_delay_samples;

    for (uint32_t i = 0; i < nframes; ++i) {
        handleEvents(process->in_events, event_index, i);

        double mod_depth_total = std::clamp(_mod_depth_normalized + _mod_depth_mod, 0.0, 1.0);
        double dry_wet_total = std::clamp(_dry_wet_normalized + _dry_wet_mod, 0.0, 1.0);

        float mod_l = (has_modulator && mod_channels > 0) ? in_modulator[0][i] : 0.0f;
        float mod_r = (has_modulator && mod_channels > 1)
                          ? in_modulator[1][i]
                          : ((has_modulator && mod_channels > 0) ? in_modulator[0][i] : 0.0f);

        double x_int_l = _integrator_l.process(mod_l);
        double x_int_r = _integrator_r.process(mod_r);

        _delay_line_l.write(in_carrier[0][i]);
        _delay_line_r.write(in_carrier[1][i]);

        double delay_l =
            std::clamp(base_delay_samples + mod_depth_total * x_int_l * max_delay_samples,
                       min_delay, max_delay);
        double delay_r =
            std::clamp(base_delay_samples + mod_depth_total * x_int_r * max_delay_samples,
                       min_delay, max_delay);

        float w_l = _delay_line_l.read(delay_l);
        float w_r = _delay_line_r.read(delay_r);

        out[0][i] =
            static_cast<float>((1.0 - dry_wet_total) * in_carrier[0][i] + dry_wet_total * w_l);
        out[1][i] =
            static_cast<float>((1.0 - dry_wet_total) * in_carrier[1][i] + dry_wet_total * w_r);
    }

    return CLAP_PROCESS_CONTINUE;
}

void SimpleFmPlugin::handleEvents(const clap_input_events* in, uint32_t& event_index,
                                  uint32_t sample_index) noexcept {
    uint32_t event_count = in->size(in);
    while (event_index < event_count) {
        const clap_event_header* header = in->get(in, event_index);
        if (header->time > sample_index) break;

        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID) {
            if (header->type == CLAP_EVENT_PARAM_VALUE) {
                const auto* ev = reinterpret_cast<const clap_event_param_value*>(header);
                switch (ev->param_id) {
                    case kParamModDepth:
                        _mod_depth_normalized = ev->value;
                        break;
                    case kParamDryWet:
                        _dry_wet_normalized = ev->value;
                        break;
                }
            } else if (header->type == CLAP_EVENT_PARAM_MOD) {
                const auto* ev = reinterpret_cast<const clap_event_param_mod*>(header);
                switch (ev->param_id) {
                    case kParamModDepth:
                        _mod_depth_mod = ev->amount;
                        break;
                    case kParamDryWet:
                        _dry_wet_mod = ev->amount;
                        break;
                }
            }
        }
        event_index++;
    }
}

}  // namespace synth_canvas::fm_plugin
