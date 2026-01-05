#pragma once

#include <clap/helpers/plugin.hh>

namespace clap {
class SinePlugin : public helpers::Plugin<helpers::MisbehaviourHandler::Terminate,
                                          helpers::CheckingLevel::Maximal> {
    using super =
        helpers::Plugin<helpers::MisbehaviourHandler::Terminate, helpers::CheckingLevel::Maximal>;

   public:
    SinePlugin(const clap_host_t *host);

    static const clap_plugin_descriptor *descriptor();

   protected:
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override;
    clap_process_status process(const clap_process *process) noexcept override;

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool is_input) const noexcept override;
    bool audioPortsInfo(uint32_t index, bool is_input,
                        clap_audio_port_info_t *info) const noexcept override;

    bool implementsNotePorts() const noexcept override { return true; }
    uint32_t notePortsCount(bool is_input) const noexcept override;
    bool notePortsInfo(uint32_t index, bool is_input,
                       clap_note_port_info_t *info) const noexcept override;

   private:
    double _sample_rate = 44100;
    float _phase = 0;
    bool _note_is_active = false;
    int32_t _note_key = 0;
    double _note_freq = 440.0;
    float _note_velocity = 0.0f;
};

}  // namespace clap
