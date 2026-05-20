#pragma once

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <string>

#include "soundfont-engine.h"

namespace synth_canvas::soundfont_plugin {

class SoundfontPlugin : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                                     clap::helpers::CheckingLevel::Maximal> {
   public:
    SoundfontPlugin(const std::string& plugin_path, const clap_host* host);

    static auto descriptor() -> const clap_plugin_descriptor*;

    // Overrides from clap::helpers::Plugin
    auto activate(double sample_rate, uint32_t min_frames_count, uint32_t max_frames_count) noexcept
        -> bool override;

    // Ports
    [[nodiscard]] auto implementsAudioPorts() const noexcept -> bool override { return true; }
    [[nodiscard]] auto audioPortsCount(bool is_input) const noexcept -> uint32_t override;
    auto audioPortsInfo(uint32_t index, bool is_input, clap_audio_port_info* info) const noexcept
        -> bool override;

    [[nodiscard]] auto implementsNotePorts() const noexcept -> bool override { return true; }
    [[nodiscard]] auto notePortsCount(bool is_input) const noexcept -> uint32_t override;
    auto notePortsInfo(uint32_t index, bool is_input, clap_note_port_info* info) const noexcept
        -> bool override;

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

    // Processing
    auto process(const clap_process* process) noexcept -> clap_process_status override;

    enum ParamIds { kParamPreset = 0, kParamGain, kParamPan, kParamMidiChannel, kParamCount };

   private:
    void handleEvents(const clap_input_events* in, uint32_t& event_index,
                      uint32_t sample_index) noexcept;

    // Parameters
    double _preset_index{0.0};             // Stepped
    double _gain_normalized{60.0 / 72.0};  // 0 dB
    double _pan_normalized{0.5};           // Center
    double _midi_channel{0.0};             // Stepped

    // Modulation (Normalized)
    double _gain_mod{0.0};
    double _pan_mod{0.0};

    // Current values for smoothing (Functional domain)
    float _current_gain_db{0.0f};
    float _current_pan{-0.0f};

    // Current values for engine updates
    int _current_preset{-1};
    int _current_midi_channel{0};
    std::string _sf2_path;

    SoundfontEngine _engine;
};

}  // namespace synth_canvas::soundfont_plugin
