#include "simple_delay_plugin.h"

#include <algorithm>  // for std::copy
#include <cmath>      // For std::fmax
#include <cstring>    // for memcpy
#include <iomanip>    // For std::fixed, std::setprecision

namespace synth_canvas::simple_delay_plugin {
// Define the plugin descriptor
static const char *features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY,
                                 nullptr};
static const clap_plugin_descriptor desc = {CLAP_VERSION,
                                            "com.synthcanvas.simple-delay-plugin",
                                            "Simple Delay Plugin",
                                            "SynthCanvas",
                                            "https://github.com/SPACEGG/SynthCanvas",
                                            "",
                                            "",
                                            "0.1.0",
                                            "A simple audio delay plugin for demonstration.",
                                            features};

// Maximum delay time in seconds
static const double MAX_DELAY_TIME_SEC = 2.0;

const clap_plugin_descriptor *SimpleDelayPlugin::descriptor() { return &desc; }

SimpleDelayPlugin::SimpleDelayPlugin(const std::string &pluginPath, const clap_host *host)
    : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                            clap::helpers::CheckingLevel::Maximal>(&desc, host) {
    _writeHead = 0;
}

bool SimpleDelayPlugin::activate(double sampleRate, uint32_t minFramesCount,
                                 uint32_t maxFramesCount) noexcept {
    if (!Plugin::activate(sampleRate, minFramesCount, maxFramesCount)) return false;

    _sampleRate = sampleRate;
    _bufferSize = static_cast<uint32_t>(MAX_DELAY_TIME_SEC * _sampleRate);

    // Ensure buffer size is at least 1 to avoid division by zero or issues with very small buffers
    if (_bufferSize == 0) {
        _bufferSize = 1;
    }

    // Resize buffers for 2 channels (stereo)
    _delayBuffer.resize(2);
    for (auto &channelBuffer : _delayBuffer) {
        channelBuffer.assign(_bufferSize, 0.0f);  // Initialize with zeros
    }

    _writeHead = 0;  // Reset write head on activation

    return true;
}

void SimpleDelayPlugin::deactivate() noexcept {
    Plugin::deactivate();
    for (auto &channelBuffer : _delayBuffer) {
        channelBuffer.clear();  // Clear buffers
    }
    _delayBuffer.clear();  // Clear the vector of vectors
    _bufferSize = 0;
    _writeHead = 0;
    _sampleRate = 0.0;
}

bool SimpleDelayPlugin::audioPortsInfo(uint32_t index, bool isInput,
                                       clap_audio_port_info *info) const noexcept {
    if (index > 0) return false;

    info->id = isInput ? 0 : 1;  // Different IDs for input and output just in case
    snprintf(info->name, sizeof(info->name), "%s", isInput ? "Audio In" : "Audio Out");
    info->channel_count = 2;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

// --- Parameter Implementations ---
uint32_t SimpleDelayPlugin::paramsCount() const noexcept { return PARAM_COUNT; }

bool SimpleDelayPlugin::paramsInfo(uint32_t index, clap_param_info *info) const noexcept {
    switch (index) {
        case PARAM_DELAY_TIME:
            info->id = PARAM_DELAY_TIME;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            snprintf(info->name, sizeof(info->name), "Delay Time");
            snprintf(info->module, sizeof(info->module), "Delay");
            info->min_value = 0.0;
            info->max_value = MAX_DELAY_TIME_SEC;
            info->default_value = 0.5;
            break;
        case PARAM_FEEDBACK:
            info->id = PARAM_FEEDBACK;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            snprintf(info->name, sizeof(info->name), "Feedback");
            snprintf(info->module, sizeof(info->module), "Delay");
            info->min_value = 0.0;
            info->max_value = 0.95;  // Avoid 1.0 to prevent infinite feedback
            info->default_value = 0.5;
            break;
        case PARAM_MIX:
            info->id = PARAM_MIX;
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

bool SimpleDelayPlugin::paramsValue(clap_id paramId, double *value) noexcept {
    switch (paramId) {
        case PARAM_DELAY_TIME:
            *value = _delayTime;
            break;
        case PARAM_FEEDBACK:
            *value = _feedback;
            break;
        case PARAM_MIX:
            *value = _mix;
            break;
        default:
            return false;
    }
    return true;
}

bool SimpleDelayPlugin::paramsValueToText(clap_id paramId, double value, char *display,
                                          uint32_t size) noexcept {
    // Using stringstream for flexible formatting, then copy to char array
    std::stringstream ss;
    switch (paramId) {
        case PARAM_DELAY_TIME:
            ss << std::fixed << std::setprecision(2) << value << " s";
            break;
        case PARAM_FEEDBACK:
        case PARAM_MIX:
            ss << std::fixed << std::setprecision(0) << value * 100.0 << " %";
            break;
        default:
            return false;
    }
    strncpy(display, ss.str().c_str(), size - 1);
    display[size - 1] = '\0';  // Ensure null-termination
    return true;
}

bool SimpleDelayPlugin::paramsTextToValue(clap_id paramId, const char *display,
                                          double *value) noexcept {
    // Simple implementation, could be improved with robust parsing
    char *end;
    double parsedValue = strtod(display, &end);

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

    switch (paramId) {
        case PARAM_DELAY_TIME:
            // No specific unit conversion needed for seconds, just value clamping
            *value = std::clamp(parsedValue, 0.0, MAX_DELAY_TIME_SEC);
            break;
        case PARAM_FEEDBACK:
        case PARAM_MIX:
            // If unit is '%', then divide by 100.0
            if (unit_str == "%") {
                *value =
                    std::clamp(parsedValue / 100.0, 0.0, (paramId == PARAM_FEEDBACK ? 0.95 : 1.0));
            } else {  // Assume it's already 0.0-1.0 range if no '%'
                *value = std::clamp(parsedValue, 0.0, (paramId == PARAM_FEEDBACK ? 0.95 : 1.0));
            }
            break;
        default:
            return false;
    }
    return true;
}

void SimpleDelayPlugin::handleParamValueEvent(const clap_event_param_value *paramValue) noexcept {
    switch (paramValue->param_id) {
        case PARAM_DELAY_TIME:
            _delayTime = paramValue->value;
            break;
        case PARAM_FEEDBACK:
            _feedback = paramValue->value;
            break;
        case PARAM_MIX:
            _mix = paramValue->value;
            break;
        default:
            break;
    }
}

void SimpleDelayPlugin::paramsFlush(const clap_input_events *in,
                                    const clap_output_events *out) noexcept {
    if (!in) return;

    uint32_t size = in->size(in);
    for (uint32_t i = 0; i < size; ++i) {
        const clap_event_header_t *hdr = in->get(in, i);
        if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
            handleParamValueEvent(reinterpret_cast<const clap_event_param_value *>(hdr));
        }
    }
}

clap_process_status SimpleDelayPlugin::process(const clap_process *process) noexcept {
    const uint32_t nframes = process->frames_count;
    const uint32_t in_count = process->audio_inputs_count;
    const uint32_t out_count = process->audio_outputs_count;

    // Process parameter events
    const clap_input_events *in_events = process->in_events;
    if (in_events) {
        uint32_t num_events = in_events->size(in_events);
        for (uint32_t i = 0; i < num_events; ++i) {
            const clap_event_header_t *hdr = in_events->get(in_events, i);
            if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
                handleParamValueEvent(reinterpret_cast<const clap_event_param_value *>(hdr));
            }
        }
    }

    // If plugin is not activated or no output, nothing to do
    if (_sampleRate == 0 || _delayBuffer.empty() || out_count == 0 || _bufferSize == 0)
        return CLAP_PROCESS_CONTINUE;

    // Get output buffer (stereo)
    float **outputs = process->audio_outputs[0].data32;
    uint32_t out_channels = process->audio_outputs[0].channel_count;

    // Ensure we handle at least stereo, and cap to _delayBuffer channels
    uint32_t num_channels = std::min(out_channels, (uint32_t)_delayBuffer.size());

    // Get input buffer (stereo)
    float **inputs = nullptr;
    if (in_count > 0) {
        inputs = process->audio_inputs[0].data32;
        num_channels = std::min(
            {num_channels, process->audio_inputs[0].channel_count, (uint32_t)_delayBuffer.size()});
    }

    // Calculate read head for delay. Clamp to avoid reading beyond allocated buffer which can
    // happen if delay_samples is too large
    uint32_t delay_samples = secondsToSamples(_delayTime);
    delay_samples = std::min(
        delay_samples,
        _bufferSize - 1);  // Ensure it doesn't exceed buffer boundary. Minimal delay is 1 sample.

    for (uint32_t i = 0; i < nframes; ++i)  // Iterate through each frame
    {
        for (uint32_t c = 0; c < num_channels; ++c)  // Iterate through each channel
        {
            float dry_sample = (inputs && inputs[c]) ? inputs[c][i] : 0.0f;

            // Calculate read head
            uint32_t read_head = (_writeHead + _bufferSize - delay_samples) % _bufferSize;
            float delayed_sample = _delayBuffer[c][read_head];

            // Calculate output sample: dry + wet (delayed signal)
            float output_sample = (dry_sample * (1.0 - _mix)) + (delayed_sample * _mix);

            // Write to output
            if (outputs[c]) {
                outputs[c][i] = output_sample;
            }

            // Update delay buffer with input + feedback
            // Apply feedback to the delayed sample before mixing with input
            _delayBuffer[c][_writeHead] = dry_sample + (delayed_sample * _feedback);
        }

        // Move write head
        _writeHead = (_writeHead + 1) % _bufferSize;
    }

    // Silence any remaining output channels if out_channels > num_channels
    for (uint32_t c = num_channels; c < out_channels; ++c) {
        if (outputs[c]) {
            std::fill(outputs[c], outputs[c] + nframes, 0.0f);
        }
    }

    return CLAP_PROCESS_CONTINUE;
}

}  // namespace synth_canvas::simple_delay_plugin
