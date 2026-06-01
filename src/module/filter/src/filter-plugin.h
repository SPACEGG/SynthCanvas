#pragma once

#include <clap/ext/draft/mini-curve-display.h>

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <string>

#include "svf-engine.h"

namespace synth_canvas::filter_plugin {

class FilterPlugin : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                                  clap::helpers::CheckingLevel::Maximal> {
   public:
    FilterPlugin(const std::string& plugin_path, const clap_host* host);

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

    // State
    [[nodiscard]] auto implementsState() const noexcept -> bool override { return true; }
    auto stateSave(const clap_ostream* os) noexcept -> bool override;
    auto stateLoad(const clap_istream* is) noexcept -> bool override;
    auto extension(const char* id) noexcept -> const void* override;

    // Processing
    auto process(const clap_process* process) noexcept -> clap_process_status override;

    enum ParamIds {
        kParamCutoff = 0,
        kParamResonance,
        kParamMode,
        kParamSlope,
        kParamMix,
        kParamDrive,
        kParamGain,
        kParamCount
    };

   private:
    void handleEvents(const clap_input_events* in, uint32_t& event_index,
                      uint32_t sample_index) noexcept;

    // Parameters (Normalized 0.0 ~ 1.0)
    double _cutoff_normalized{0.5};
    double _resonance_normalized{0.5};
    double _mix_normalized{1.0};
    double _drive_normalized{0.0};
    double _gain_normalized{60.0 / 72.0};  // 0 dB

    // Stepped Parameters
    double _mode{0.0};
    double _slope{0.0};

    // Modulation (Normalized)
    double _cutoff_mod{0.0};
    double _resonance_mod{0.0};
    double _mix_mod{0.0};
    double _drive_mod{0.0};
    double _gain_mod{0.0};

    // Smoothing (Functional domain)
    double _current_cutoff_hz{1000.0};
    double _current_resonance{0.5};
    double _current_mix{1.0};
    double _current_drive_linear{1.0};
    double _current_gain_linear{1.0};
    double _smoothing_coeff{0.0};

    double _sample_rate{44100.0};

    SvfEngine _engine;

    static const clap_plugin_mini_curve_display_t s_mini_curve_display;

    [[nodiscard]] auto get_magnitude(float f, float fc, float res, int m) const -> float;
};

}  // namespace synth_canvas::filter_plugin
