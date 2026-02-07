#include "simple_delay_plugin.h"

#include <algorithm>  // for std::copy
#include <cstring>    // for memcpy
#include <iomanip>    // For std::fixed, std::setprecision

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

    // Ensure buffer size is at least 1 to avoid division by zero or issues with very small buffers
    if (_buffer_size == 0) {
        _buffer_size = 1;
    }

    // Resize buffers for 2 channels (stereo)
    _delay_buffer.resize(2);
    for (auto& channel_buffer : _delay_buffer) {
        channel_buffer.assign(_buffer_size, 0.0f);  // Initialize with zeros
    }

    _write_head = 0;  // Reset write head on activation

    return true;
}

void SimpleDelayPlugin::deactivate() noexcept {
    Plugin::deactivate();
    for (auto& channel_buffer : _delay_buffer) {
        channel_buffer.clear();  // Clear buffers
    }
    _delay_buffer.clear();  // Clear the vector of vectors
    _buffer_size = 0;
    _write_head = 0;
    _sample_rate = 0.0;
}

auto SimpleDelayPlugin::audioPortsInfo(uint32_t index, bool is_input,
                                       clap_audio_port_info* info) const noexcept -> bool {
    if (index > 0) return false;

    info->id = is_input ? 0 : 1;  // Different IDs for input and output just in case
    snprintf(info->name, sizeof(info->name), "%s", is_input ? "Audio In" : "Audio Out");
    info->channel_count = 2;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

// --- Parameter Implementations ---
auto SimpleDelayPlugin::paramsCount() const noexcept -> uint32_t { return kParamCount; }

auto SimpleDelayPlugin::paramsInfo(uint32_t index, clap_param_info* info) const noexcept -> bool {
    switch (index) {
        case kParamDelayTime:
            info->id = kParamDelayTime;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            snprintf(info->name, sizeof(info->name), "Delay Time");
            snprintf(info->module, sizeof(info->module), "Delay");
            info->min_value = 0.0;
            info->max_value = kMaxDelayTimeSec;
            info->default_value = 0.5;
            break;
        case kParamFeedback:
            info->id = kParamFeedback;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            snprintf(info->name, sizeof(info->name), "Feedback");
            snprintf(info->module, sizeof(info->module), "Delay");
            info->min_value = 0.0;
            info->max_value = 0.95;  // Avoid 1.0 to prevent infinite feedback
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
            *value = _delay_time;
            break;
        case kParamFeedback:
            *value = _feedback;
            break;
        case kParamMix:
            *value = _mix;
            break;
        default:
            return false;
    }
    return true;
}

auto SimpleDelayPlugin::paramsValueToText(clap_id param_id, double value, char* display,
                                          uint32_t size) noexcept -> bool {
    // Using stringstream for flexible formatting, then copy to char array
    std::stringstream ss;
    switch (param_id) {
        case kParamDelayTime:
            ss << std::fixed << std::setprecision(2) << value << " s";
            break;
        case kParamFeedback:
        case kParamMix:
            ss << std::fixed << std::setprecision(0) << value * 100.0 << " %";
            break;
        default:
            return false;
    }
    strncpy(display, ss.str().c_str(), size - 1);
    display[size - 1] = '\0';  // Ensure null-termination
    return true;
}

auto SimpleDelayPlugin::paramsTextToValue(clap_id param_id, const char* display,
                                          double* value) noexcept -> bool {
    // Simple implementation, could be improved with robust parsing
    char* end;
    double parsed_value = strtod(display, &end);

    // Check if parsing failed or if there are unparsed characters other than a space or unit
    if (end == display) {  // No characters parsed
        return false;
    }

    // Try to handle common unit suffixes
    std::string s_display(display);
    std::string unit_str;
    size_t unit_pos = s_display.find_first_not_of("0123456789.-", end - display);
    if (unit_pos != std::string::npos) {
        unit_str = s_display.substr(unit_pos);
        // Trim leading/trailing whitespace from unit_str
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
            // No specific unit conversion needed for seconds, just value clamping
            *value = std::clamp(parsed_value, 0.0, kMaxDelayTimeSec);
            break;
        case kParamFeedback:
        case kParamMix:
            // If unit is '%', then divide by 100.0
            if (unit_str == "%") {
                *value = std::clamp(parsed_value / 100.0, 0.0,
                                    (param_id == kParamFeedback ? 0.95 : 1.0));
            } else {  // Assume it's already 0.0-1.0 range if no '%'
                *value = std::clamp(parsed_value, 0.0, (param_id == kParamFeedback ? 0.95 : 1.0));
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
            _delay_time = param_value->value;
            break;
        case kParamFeedback:
            _feedback = param_value->value;
            break;
        case kParamMix:
            _mix = param_value->value;
            break;
        default:
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
        }
    }
}

auto SimpleDelayPlugin::process(const clap_process* process) noexcept -> clap_process_status {
    const uint32_t kNframes = process->frames_count;
    const uint32_t kInCount = process->audio_inputs_count;
    const uint32_t kOutCount = process->audio_outputs_count;

    // Process parameter events
    const clap_input_events* in_events = process->in_events;
    if (in_events) {
        uint32_t num_events = in_events->size(in_events);
        for (uint32_t i = 0; i < num_events; ++i) {
            const clap_event_header_t* hdr = in_events->get(in_events, i);
            if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
                handleParamValueEvent(reinterpret_cast<const clap_event_param_value*>(hdr));
            }
        }
    }

    // If plugin is not activated or no output, nothing to do
    if (_sample_rate == 0 || _delay_buffer.empty() || kOutCount == 0 || _buffer_size == 0) {
        return CLAP_PROCESS_CONTINUE;
    }

    // Get output buffer (stereo)
    float** outputs = process->audio_outputs[0].data32;
    uint32_t out_channels = process->audio_outputs[0].channel_count;

    // Ensure we handle at least stereo, and cap to _delayBuffer channels
    uint32_t num_channels = std::min(out_channels, static_cast<uint32_t>(_delay_buffer.size()));

    // Get input buffer (stereo)
    float** inputs = nullptr;
    if (kInCount > 0) {
        inputs = process->audio_inputs[0].data32;
        num_channels = std::min({num_channels, process->audio_inputs[0].channel_count,
                                 static_cast<uint32_t>(_delay_buffer.size())});
    }

    // Calculate read head for delay. Clamp to avoid reading beyond allocated buffer which can
    // happen if delay_samples is too large
    uint32_t delay_samples = secondsToSamples(_delay_time);
    delay_samples = std::min(
        delay_samples,
        _buffer_size - 1);  // Ensure it doesn't exceed buffer boundary. Minimal delay is 1 sample.

    for (uint32_t i = 0; i < kNframes; ++i)  // Iterate through each frame
    {
        for (uint32_t c = 0; c < num_channels; ++c)  // Iterate through each channel
        {
            float dry_sample = (inputs && inputs[c]) ? inputs[c][i] : 0.0f;

            // Calculate read head
            uint32_t read_head = (_write_head + _buffer_size - delay_samples) % _buffer_size;
            float delayed_sample = _delay_buffer[c][read_head];

            // Calculate output sample: dry + wet (delayed signal)
            float output_sample = (dry_sample * (1.0 - _mix)) + (delayed_sample * _mix);

            // Write to output
            if (outputs[c]) {
                outputs[c][i] = output_sample;
            }

            // Update delay buffer with input + feedback
            // Apply feedback to the delayed sample before mixing with input
            _delay_buffer[c][_write_head] = dry_sample + (delayed_sample * _feedback);
        }

        // Move write head
        _write_head = (_write_head + 1) % _buffer_size;
    }

    // Silence any remaining output channels if out_channels > num_channels
    for (uint32_t c = num_channels; c < out_channels; ++c) {
        if (outputs[c]) {
            std::fill(outputs[c], outputs[c] + kNframes, 0.0f);
        }
    }

    return CLAP_PROCESS_CONTINUE;
}

}  // namespace synth_canvas::simple_delay_plugin
