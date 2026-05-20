#include "gain.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace synth_canvas::gain_plugin {

static const std::array<const char*, 3> kFeatures = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
                                                     CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

static const clap_plugin_descriptor kDesc = {
    .clap_version = CLAP_VERSION,
    .id = "com.synthcanvas.gain-plugin",
    .name = "Gain Plugin",
    .vendor = "SynthCanvas",
    .url = "https://github.com/SPACEGG/SynthCanvas",
    .manual_url = "",
    .support_url = "",
    .version = "0.1.0",
    .description = "A lightweight gain plugin with smoothing.",
    .features = kFeatures.data()};

auto GainPlugin::descriptor() -> const clap_plugin_descriptor* { return &kDesc; }

GainPlugin::GainPlugin(const std::string& plugin_path, const clap_host* host)
    : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                            clap::helpers::CheckingLevel::Maximal>(&kDesc, host) {}

auto GainPlugin::activate(double sample_rate, uint32_t min_frames_count,
                          uint32_t max_frames_count) noexcept -> bool {
    if (!Plugin::activate(sample_rate, min_frames_count, max_frames_count)) return false;

    _sample_rate = sample_rate;

    // Calculate smoothing coefficient for ~15ms time constant
    // alpha = 1 - exp(-1 / (fs * tau))
    const double tau = 0.015;
    _smoothing_coeff = 1.0 - std::exp(-1.0 / (_sample_rate * tau));

    // Initialize gain states
    updateTargetGain();
    _current_gain_linear = _target_gain_linear;

    return true;
}

auto GainPlugin::audioPortsInfo(uint32_t index, bool is_input,
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

auto GainPlugin::paramsCount() const noexcept -> uint32_t { return kParamCount; }

auto GainPlugin::paramsInfo(uint32_t index, clap_param_info* info) const noexcept -> bool {
    if (index != kParamGain) return false;

    info->id = kParamGain;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
    snprintf(info->name, sizeof(info->name), "Gain");
    snprintf(info->module, sizeof(info->module), "Main");
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = 60.0 / 72.0;  // 0 dB
    return true;
}

auto GainPlugin::paramsValue(clap_id param_id, double* value) noexcept -> bool {
    if (param_id != kParamGain) return false;
    *value = _gain_normalized;
    return true;
}

auto GainPlugin::paramsValueToText(clap_id param_id, double value, char* display,
                                   uint32_t size) noexcept -> bool {
    if (param_id != kParamGain) return false;

    double functional_db = -60.0 + value * 72.0;
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << functional_db << " dB";
    strncpy(display, ss.str().c_str(), size - 1);
    display[size - 1] = '\0';
    return true;
}

auto GainPlugin::paramsTextToValue(clap_id param_id, const char* display, double* value) noexcept
    -> bool {
    if (param_id != kParamGain) return false;

    char* end;
    double parsed = strtod(display, &end);
    if (end == display) return false;

    double clamped_db = std::clamp(parsed, -60.0, 12.0);
    *value = (clamped_db + 60.0) / 72.0;
    return true;
}

void GainPlugin::paramsFlush(const clap_input_events* in, const clap_output_events* out) noexcept {
    if (!in) return;

    uint32_t size = in->size(in);
    for (uint32_t i = 0; i < size; ++i) {
        const clap_event_header_t* hdr = in->get(in, i);
        if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
            auto* ev = reinterpret_cast<const clap_event_param_value*>(hdr);
            if (ev->param_id == kParamGain) {
                _gain_normalized = ev->value;
            }
        } else if (hdr->type == CLAP_EVENT_PARAM_MOD) {
            auto* ev = reinterpret_cast<const clap_event_param_mod*>(hdr);
            if (ev->param_id == kParamGain) {
                _modulation_normalized = ev->amount;
            }
        }
    }
    updateTargetGain();
}

auto GainPlugin::process(const clap_process* process) noexcept -> clap_process_status {
    const uint32_t frames = process->frames_count;
    const uint32_t in_ports = process->audio_inputs_count;
    const uint32_t out_ports = process->audio_outputs_count;

    if (out_ports == 0) return CLAP_PROCESS_CONTINUE;

    float** inputs = (in_ports > 0) ? process->audio_inputs[0].data32 : nullptr;
    float** outputs = process->audio_outputs[0].data32;
    uint32_t channels = process->audio_outputs[0].channel_count;

    const clap_input_events* in_events = process->in_events;
    uint32_t ev_idx = 0;
    uint32_t num_events = in_events ? in_events->size(in_events) : 0;

    for (uint32_t i = 0; i < frames; ++i) {
        // Handle sample-accurate parameter events
        while (ev_idx < num_events) {
            const clap_event_header_t* hdr = in_events->get(in_events, ev_idx);
            if (hdr->time > i) break;

            if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
                auto* ev = reinterpret_cast<const clap_event_param_value*>(hdr);
                if (ev->param_id == kParamGain) {
                    _gain_normalized = ev->value;
                    updateTargetGain();
                }
            } else if (hdr->type == CLAP_EVENT_PARAM_MOD) {
                auto* ev = reinterpret_cast<const clap_event_param_mod*>(hdr);
                if (ev->param_id == kParamGain) {
                    _modulation_normalized = ev->amount;
                    updateTargetGain();
                }
            }
            ev_idx++;
        }

        // Apply smoothing: 1-pole filter
        _current_gain_linear += _smoothing_coeff * (_target_gain_linear - _current_gain_linear);

        // Process audio
        for (uint32_t c = 0; c < channels; ++c) {
            float in_sample = (inputs && inputs[c]) ? inputs[c][i] : 0.0f;
            outputs[c][i] = in_sample * static_cast<float>(_current_gain_linear);
        }
    }

    return CLAP_PROCESS_CONTINUE;
}

void GainPlugin::updateTargetGain() noexcept {
    double total_normalized = std::clamp(_gain_normalized + _modulation_normalized, 0.0, 1.0);
    double total_db = -60.0 + total_normalized * 72.0;
    _target_gain_linear = std::pow(10.0, total_db / 20.0);
}

}  // namespace synth_canvas::gain_plugin
