#pragma once

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <string>

namespace synth_canvas::gain_plugin {
class GainPlugin : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                                clap::helpers::CheckingLevel::Maximal> {
   public:
    GainPlugin(const std::string& plugin_path, const clap_host* host);

    static auto descriptor() -> const clap_plugin_descriptor*;

    // Overrides from clap::helpers::Plugin
    auto activate(double sample_rate, uint32_t min_frames_count, uint32_t max_frames_count) noexcept
        -> bool override;

    // Ports
    [[nodiscard]] auto implementsAudioPorts() const noexcept -> bool override { return true; }
    [[nodiscard]] auto audioPortsCount(bool is_input) const noexcept -> uint32_t override {
        return 1;
    }
    auto audioPortsInfo(uint32_t index, bool is_input, clap_audio_port_info* info) const noexcept
        -> bool override;

    [[nodiscard]] auto implementsNotePorts() const noexcept -> bool override { return false; }

    // Parameters
    [[nodiscard]] auto implementsParams() const noexcept -> bool override { return true; }
    [[nodiscard]] auto paramsCount() const noexcept -> uint32_t override;
    auto paramsInfo(uint32_t index, clap_param_info* info) const noexcept -> bool override;
    auto paramsValue(clap_id param_id, double* value) noexcept -> bool override;
    auto paramsValueToText(clap_id param_id, double value, char* display, uint32_t size) noexcept
        -> bool override;
    auto paramsTextToValue(clap_id param_id, const char* display, double* value) noexcept
        -> bool override;
    void paramsFlush(const clap_input_events* in, const clap_output_events* out) noexcept override;

    // Processing
    auto process(const clap_process* process) noexcept -> clap_process_status override;

   private:
    void updateTargetGain() noexcept;

    // Parameters (Normalized 0.0 ~ 1.0)
    double _gain_normalized = 60.0 / 72.0;  // Default 0 dB
    double _modulation_normalized = 0.0;

    // Internal state for DSP
    double _sample_rate = 0.0;
    double _target_gain_linear = 1.0;
    double _current_gain_linear = 1.0;
    double _smoothing_coeff = 0.01;  // Alpha for 1-pole filter

    // Parameter IDs
    enum { kParamGain = 0, kParamCount };
};
}  // namespace synth_canvas::gain_plugin
