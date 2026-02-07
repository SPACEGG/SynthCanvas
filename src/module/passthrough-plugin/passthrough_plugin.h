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
    PassthroughPlugin(const std::string& plugin_path, const clap_host* host);

    static auto descriptor() -> const clap_plugin_descriptor*;

    // --- Ports ---
    [[nodiscard]] auto implementsAudioPorts() const noexcept -> bool override { return true; }
    [[nodiscard]] auto audioPortsCount(bool is_input) const noexcept -> uint32_t override {
        return 1;
    }
    auto audioPortsInfo(uint32_t index, bool is_input, clap_audio_port_info* info) const noexcept
        -> bool override;

    [[nodiscard]] auto implementsNotePorts() const noexcept -> bool override { return false; }
    [[nodiscard]] auto notePortsCount(bool is_input) const noexcept -> uint32_t override {
        return 0;
    }
    auto notePortsInfo(uint32_t index, bool is_input, clap_note_port_info* info) const noexcept
        -> bool override {
        return false;
    }

    // --- Processing ---
    auto process(const clap_process* process) noexcept -> clap_process_status override;
};
}  // namespace synth_canvas::passthrough_plugin
