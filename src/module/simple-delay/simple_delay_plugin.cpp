#include "simple_delay_plugin.h"

#include <algorithm>  // for std::copy
#include <cstring>    // for memcpy
#include <iomanip>    // For std::fixed, std::setprecision
#include <sstream>

namespace synth_canvas::simple_delay_plugin {
// Define the plugin descriptor
static const std::array<const char*, 3> kFeatures = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
                                                     CLAP_PLUGIN_FEATURE_UTILITY, nullptr};
static const clap_plugin_descriptor kDesc = {
    .clap_version = CLAP_VERSION,
    .id = "com.synthcanvas.simple-delay-plugin",
    .name = "Simple Delay Plugin",
    .vendor = "SynthCanvas",
    .url = "https://github.com/SPACEGG/SynthCanvas",
    .manual_url = "",
    .support_url = "",
    .version = "0.1.0",
    .description = "A simple audio delay plugin for demonstration.",
    .features = kFeatures.data()};

// Maximum delay time in seconds
static const double kMaxDelayTimeSec = 2.0;

auto SimpleDelayPlugin::descriptor() -> const clap_plugin_descriptor* { return &kDesc; }

SimpleDelayPlugin::SimpleDelayPlugin(const std::string& plugin_path, const clap_host* host)
    : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                            clap::helpers::CheckingLevel::Maximal>(&kDesc, host) {
    _write_head = 0;
}

auto SimpleDelayPlugin::activate(double sample_rate, uint32_t min_frames_count,
                                 uint32_t max_frames_count) noexcept -> bool {
    if (!Plugin::activate(sample_rate, min_frames_count, max_frames_count)) return false;

    _sample_rate = sample_rate;
    _buffer_size = static_cast<uint32_t>(kMaxDelayTimeSec * _sample_rate);

    if (_buffer_size == 0) {
        _buffer_size = 1;
    }

    _delay_buffer.resize(2);
    for (auto& channel_buffer : _delay_buffer) {
        channel_buffer.assign(_buffer_size, 0.0f);
    }

    _write_head = 0;

    return true;
}

void SimpleDelayPlugin::deactivate() noexcept {
    Plugin::deactivate();
    for (auto& channel_buffer : _delay_buffer) {
        channel_buffer.clear();
    }
    _delay_buffer.clear();
    _buffer_size = 0;
    _write_head = 0;
    _sample_rate = 0.0;
}

auto SimpleDelayPlugin::audioPortsInfo(uint32_t index, bool is_input,
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

auto SimpleDelayPlugin::paramsCount() const noexcept -> uint32_t { return kParamCount; }

auto SimpleDelayPlugin::paramsInfo(uint32_t index, clap_param_info* info) const noexcept -> bool {
    switch (index) {
        case kParamDelayTime:
            info->id = kParamDelayTime;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            snprintf(info->name, sizeof(info->name), "Delay Time");
            snprintf(info->module, sizeof(info->module), "Delay");
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.5 / kMaxDelayTimeSec;
            break;
        case kParamFeedback:
            info->id = kParamFeedback;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            snprintf(info->name, sizeof(info->name), "Feedback");
            snprintf(info->module, sizeof(info->module), "Delay");
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.5;
            break;
        case kParamMix:
            info->id = kParamMix;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            snprintf(info->name, sizeof(info->name), "Mix");
            snprintf(info->module, sizeof(info->module), "Delay");
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.5;
            break;
        default:
            return false;
    }
    return true;
}

auto SimpleDelayPlugin::paramsValue(clap_id param_id, double* value) noexcept -> bool {
    switch (param_id) {
        case kParamDelayTime:
            *value = _delay_time_normalized;
            break;
        case kParamFeedback:
            *value = _feedback_normalized;
            break;
        case kParamMix:
            *value = _mix_normalized;
            break;
        default:
            return false;
    }
    return true;
}

auto SimpleDelayPlugin::paramsValueToText(clap_id param_id, double value, char* display,
                                          uint32_t size) noexcept -> bool {
    std::stringstream ss;
    switch (param_id) {
        case kParamDelayTime:
            ss << std::fixed << std::setprecision(2) << (value * kMaxDelayTimeSec) << " s";
            break;
        case kParamFeedback:
        case kParamMix:
            ss << std::fixed << std::setprecision(0) << value * 100.0 << " %";
            break;
        default:
            return false;
    }
    strncpy(display, ss.str().c_str(), size - 1);
    display[size - 1] = '\0';
    return true;
}

auto SimpleDelayPlugin::paramsTextToValue(clap_id param_id, const char* display,
                                          double* value) noexcept -> bool {
    char* end;
    double parsed_value = strtod(display, &end);
    if (end == display) return false;

    std::string s_display(display);
    std::string unit_str;
    size_t unit_pos = s_display.find_first_not_of("0123456789.-", end - display);
    if (unit_pos != std::string::npos) {
        unit_str = s_display.substr(unit_pos);
        size_t first = unit_str.find_first_not_of(' ');
        if (std::string::npos == first) {
            unit_str.clear();
        } else {
            size_t last = unit_str.find_last_not_of(' ');
            unit_str = unit_str.substr(first, (last - first + 1));
        }
    }

    switch (param_id) {
        case kParamDelayTime:
            *value = std::clamp(parsed_value / kMaxDelayTimeSec, 0.0, 1.0);
            break;
        case kParamFeedback:
        case kParamMix:
            if (unit_str == "%") {
                *value = std::clamp(parsed_value / 100.0, 0.0, 1.0);
            } else {
                *value = std::clamp(parsed_value, 0.0, 1.0);
            }
            break;
        default:
            return false;
    }
    return true;
}

void SimpleDelayPlugin::handleParamValueEvent(const clap_event_param_value* param_value) noexcept {
    switch (param_value->param_id) {
        case kParamDelayTime:
            _delay_time_normalized = param_value->value;
            break;
        case kParamFeedback:
            _feedback_normalized = param_value->value;
            break;
        case kParamMix:
            _mix_normalized = param_value->value;
            break;
    }
}

void SimpleDelayPlugin::paramsFlush(const clap_input_events* in,
                                    const clap_output_events* out) noexcept {
    if (!in) return;
    uint32_t size = in->size(in);
    for (uint32_t i = 0; i < size; ++i) {
        const clap_event_header_t* hdr = in->get(in, i);
        if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
            handleParamValueEvent(reinterpret_cast<const clap_event_param_value*>(hdr));
        } else if (hdr->type == CLAP_EVENT_PARAM_MOD) {
            auto* ev = reinterpret_cast<const clap_event_param_mod*>(hdr);
            switch (ev->param_id) {
                case kParamDelayTime:
                    _delay_time_mod = ev->amount;
                    break;
                case kParamFeedback:
                    _feedback_mod = ev->amount;
                    break;
                case kParamMix:
                    _mix_mod = ev->amount;
                    break;
            }
        }
    }
}

auto SimpleDelayPlugin::stateSave(const clap_ostream* os) noexcept -> bool {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "delay_time=" << _delay_time_normalized << ";"
        << "feedback=" << _feedback_normalized << ";"
        << "mix=" << _mix_normalized << ";";

    std::string s = oss.str();
    int64_t result = os->write(os, s.c_str(), s.size());
    return result == static_cast<int64_t>(s.size());
}

auto SimpleDelayPlugin::stateLoad(const clap_istream* is) noexcept -> bool {
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
            if (key == "delay_time") {
                _delay_time_normalized = val;
            } else if (key == "feedback") {
                _feedback_normalized = val;
            } else if (key == "mix") {
                _mix_normalized = val;
            }
        } catch (...) {
        }
    }
    return true;
}

auto SimpleDelayPlugin::process(const clap_process* process) noexcept -> clap_process_status {
    const uint32_t frames = process->frames_count;
    const uint32_t in_count = process->audio_inputs_count;
    const uint32_t out_count = process->audio_outputs_count;

    const clap_input_events* in_events = process->in_events;
    uint32_t ev_idx = 0;
    uint32_t num_events = in_events ? in_events->size(in_events) : 0;

    if (_sample_rate == 0 || _delay_buffer.empty() || out_count == 0 || _buffer_size == 0) {
        return CLAP_PROCESS_CONTINUE;
    }

    float** outputs = process->audio_outputs[0].data32;
    uint32_t out_channels = process->audio_outputs[0].channel_count;
    uint32_t num_channels = std::min(out_channels, static_cast<uint32_t>(_delay_buffer.size()));

    float** inputs = nullptr;
    if (in_count > 0) {
        inputs = process->audio_inputs[0].data32;
        num_channels = std::min({num_channels, process->audio_inputs[0].channel_count,
                                 static_cast<uint32_t>(_delay_buffer.size())});
    }

    for (uint32_t i = 0; i < frames; ++i) {
        while (ev_idx < num_events) {
            const clap_event_header_t* hdr = in_events->get(in_events, ev_idx);
            if (hdr->time > i) break;
            if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
                handleParamValueEvent(reinterpret_cast<const clap_event_param_value*>(hdr));
            } else if (hdr->type == CLAP_EVENT_PARAM_MOD) {
                auto* ev = reinterpret_cast<const clap_event_param_mod*>(hdr);
                switch (ev->param_id) {
                    case kParamDelayTime:
                        _delay_time_mod = ev->amount;
                        break;
                    case kParamFeedback:
                        _feedback_mod = ev->amount;
                        break;
                    case kParamMix:
                        _mix_mod = ev->amount;
                        break;
                }
            }
            ev_idx++;
        }

        double total_delay_norm = std::clamp(_delay_time_normalized + _delay_time_mod, 0.0, 1.0);
        double functional_delay = total_delay_norm * kMaxDelayTimeSec;
        uint32_t delay_samples = secondsToSamples(functional_delay);
        delay_samples = std::min(delay_samples, _buffer_size - 1);

        float feedback =
            static_cast<float>(std::clamp(_feedback_normalized + _feedback_mod, 0.0, 1.0));
        float mix = static_cast<float>(std::clamp(_mix_normalized + _mix_mod, 0.0, 1.0));

        for (uint32_t c = 0; c < num_channels; ++c) {
            float dry_sample = (inputs && inputs[c]) ? inputs[c][i] : 0.0f;
            uint32_t read_head = (_write_head + _buffer_size - delay_samples) % _buffer_size;
            float delayed_sample = _delay_buffer[c][read_head];

            float output_sample = (dry_sample * (1.0f - mix)) + (delayed_sample * mix);

            if (outputs[c]) outputs[c][i] = output_sample;
            _delay_buffer[c][_write_head] = dry_sample + (delayed_sample * feedback);
        }
        _write_head = (_write_head + 1) % _buffer_size;
    }

    for (uint32_t c = num_channels; c < out_channels; ++c) {
        if (outputs[c]) std::fill(outputs[c], outputs[c] + frames, 0.0f);
    }
    return CLAP_PROCESS_CONTINUE;
}

}  // namespace synth_canvas::simple_delay_plugin
