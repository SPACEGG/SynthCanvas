#pragma once

#include <clap/helpers/host-proxy.hh>
#include <clap/helpers/host-proxy.hxx>
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <string>

namespace synth_canvas::passthrough_plugin {
class PassthroughPlugin
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                   clap::helpers::CheckingLevel::Maximal> {
   public:
    PassthroughPlugin(const std::string &pluginPath, const clap_host *host);

    static const clap_plugin_descriptor *descriptor();

    // --- Ports ---
    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool isInput) const noexcept override { return 1; }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override;

    bool implementsNotePorts() const noexcept override { return false; }
    uint32_t notePortsCount(bool isInput) const noexcept override { return 0; }
    bool notePortsInfo(uint32_t index, bool isInput,
                       clap_note_port_info *info) const noexcept override {
        return false;
    }

    // --- Processing ---
    clap_process_status process(const clap_process *process) noexcept override;
};
}  // namespace synth_canvas::passthrough_plugin
