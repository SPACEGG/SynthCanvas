#pragma once

#include <clap/helpers/plugin.hh>

namespace clap {
class SinePlugin : public helpers::Plugin<helpers::MisbehaviourHandler::Terminate,
                                          helpers::CheckingLevel::Maximal> {
    using super =
        helpers::Plugin<helpers::MisbehaviourHandler::Terminate, helpers::CheckingLevel::Maximal>;

   public:
    explicit SinePlugin(const clap_host_t* host);

    static auto descriptor() -> const clap_plugin_descriptor*;

   protected:
    auto activate(double sample_rate, uint32_t min_frame_count, uint32_t max_frame_count) noexcept
        -> bool override;
    auto process(const clap_process* process) noexcept -> clap_process_status override;

    [[nodiscard]] auto implementsAudioPorts() const noexcept -> bool override { return true; }
    [[nodiscard]] auto audioPortsCount(bool is_input) const noexcept -> uint32_t override;
    auto audioPortsInfo(uint32_t index, bool is_input, clap_audio_port_info_t* info) const noexcept
        -> bool override;

    [[nodiscard]] auto implementsNotePorts() const noexcept -> bool override { return true; }
    [[nodiscard]] auto notePortsCount(bool is_input) const noexcept -> uint32_t override;
    auto notePortsInfo(uint32_t index, bool is_input, clap_note_port_info_t* info) const noexcept
        -> bool override;

   private:
    double _sample_rate = 44100;
    float _phase = 0;
    bool _note_is_active = false;
    int32_t _note_key = 0;
    double _note_freq = 440.0;
    float _note_velocity = 0.0f;
};

}  // namespace clap
