#include "passthrough_plugin.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace synth_canvas::passthrough_plugin {
// Define the plugin descriptor
static constexpr std::array<const char*, 3> kFeatures = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
                                                         CLAP_PLUGIN_FEATURE_UTILITY, nullptr};
static const clap_plugin_descriptor kDesc = {
    .clap_version = CLAP_VERSION,
    .id = "com.synthcanvas.passthrough-plugin",
    .name = "Passthrough Plugin",
    .vendor = "SynthCanvas",
    .url = "https://github.com/SPACEGG/SynthCanvas",
    .manual_url = "",
    .support_url = "",
    .version = "0.1.0",
    .description = "A simple audio passthrough plugin for testing.",
    .features = kFeatures.data()};

auto PassthroughPlugin::descriptor() -> const clap_plugin_descriptor* { return &kDesc; }

PassthroughPlugin::PassthroughPlugin(const std::string& plugin_path, const clap_host* host)
    : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                            clap::helpers::CheckingLevel::Maximal>(&kDesc, host) {}

auto PassthroughPlugin::audioPortsInfo(uint32_t index, bool is_input,
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

auto PassthroughPlugin::process(const clap_process* process) noexcept -> clap_process_status {
    const uint32_t kNframes = process->frames_count;
    const uint32_t kInCount = process->audio_inputs_count;
    const uint32_t kOutCount = process->audio_outputs_count;

    // If no output, nothing to do
    if (kOutCount == 0) return CLAP_PROCESS_CONTINUE;

    // Get output buffer
    float** outputs = process->audio_outputs[0].data32;
    uint32_t out_channels = process->audio_outputs[0].channel_count;

    // If we have input, copy it to output
    if (kInCount > 0) {
        float** inputs = process->audio_inputs[0].data32;
        uint32_t in_channels = process->audio_inputs[0].channel_count;

        // Copy channels that exist in both input and output
        uint32_t common_channels = std::min(in_channels, out_channels);
        for (uint32_t c = 0; c < common_channels; ++c) {
            if (inputs[c] && outputs[c]) {
                std::copy(inputs[c], inputs[c] + kNframes, outputs[c]);
            }
        }

        // Silence remaining output channels if output has more channels than input
        for (uint32_t c = common_channels; c < out_channels; ++c) {
            if (outputs[c]) {
                std::fill(outputs[c], outputs[c] + kNframes, 0.0f);
            }
        }
    } else {
        // No input, output silence
        for (uint32_t c = 0; c < out_channels; ++c) {
            if (outputs[c]) {
                std::fill(outputs[c], outputs[c] + kNframes, 0.0f);
            }
        }
    }

    return CLAP_PROCESS_CONTINUE;
}

}  // namespace synth_canvas::passthrough_plugin
