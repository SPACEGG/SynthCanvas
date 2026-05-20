#pragma once

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <string>
#include <vector>
#include <array>
#include <atomic>

#include "osc-voice.h"

namespace synth_canvas::oscillator_plugin {

class OscillatorPlugin : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                                       clap::helpers::CheckingLevel::Maximal> {
public:
    OscillatorPlugin(const std::string& plugin_path, const clap_host* host);

    static auto descriptor() -> const clap_plugin_descriptor*;

    // --- Overrides from clap::helpers::Plugin ---
    auto activate(double sample_rate, uint32_t min_frames_count, uint32_t max_frames_count) noexcept
        -> bool override;

    // --- Ports ---
    [[nodiscard]] auto implementsAudioPorts() const noexcept -> bool override { return true; }
    [[nodiscard]] auto audioPortsCount(bool is_input) const noexcept -> uint32_t override {
        return is_input ? 0 : 1;
    }
    auto audioPortsInfo(uint32_t index, bool is_input, clap_audio_port_info* info) const noexcept
        -> bool override;

    [[nodiscard]] auto implementsNotePorts() const noexcept -> bool override { return true; }
    [[nodiscard]] auto notePortsCount(bool is_input) const noexcept -> uint32_t override {
        return is_input ? 1 : 0;
    }
    auto notePortsInfo(uint32_t index, bool is_input, clap_note_port_info* info) const noexcept
        -> bool override;

    // --- Parameters ---
    [[nodiscard]] auto implementsParams() const noexcept -> bool override { return true; }
    [[nodiscard]] auto paramsCount() const noexcept -> uint32_t override;
    auto paramsInfo(uint32_t index, clap_param_info* info) const noexcept -> bool override;
    auto paramsValue(clap_id param_id, double* value) noexcept -> bool override;
    auto paramsValueToText(clap_id param_id, double value, char* display, uint32_t size) noexcept
        -> bool override;
    auto paramsTextToValue(clap_id param_id, const char* display, double* value) noexcept
        -> bool override;
    void paramsFlush(const clap_input_events* in, const clap_output_events* out) noexcept override;

    // --- State ---
    [[nodiscard]] auto implementsState() const noexcept -> bool override { return true; }
    auto stateSave(const clap_ostream* os) noexcept -> bool override;
    auto stateLoad(const clap_istream* is) noexcept -> bool override;

    // --- Processing ---
    auto process(const clap_process* process) noexcept -> clap_process_status override;

    enum ParamIds {
        kParamWaveform = 0,
        kParamUnisonCount,
        kParamUnisonDetune,
        kParamPulseWidth,
        kParamPitch,
        kParamMode,
        kParamSlideTime,
        kParamAttack,
        kParamRelease,
        kParamCount
    };

private:
    void handleEvents(const clap_input_events* in, uint32_t& event_index,
                      uint32_t sample_index) noexcept;
    void triggerNoteOn(int16_t port, int16_t channel, int16_t key, int32_t note_id, double velocity);
    void triggerNoteOff(int16_t port, int16_t channel, int16_t key, int32_t note_id);
    auto findFreeVoice() -> OscVoice*;

    // Parameters (Normalized 0.0 ~ 1.0 or Stepped)
    std::array<std::atomic<double>, kParamCount> _params;
    
    // Modulation offsets (Normalized)
    std::array<std::atomic<double>, kParamCount> _param_mods;

    // Voice management
    static constexpr int kMaxVoices = 32;
    std::array<OscVoice, kMaxVoices> _voices;
    uint32_t _voice_counter = 0;
    
    double _sample_rate = 44100.0;
    double _pitch_bend = 0.0; // Normalized -1.0 to 1.0

    // Mono mode state
    int16_t _mono_last_key = -1;
    int32_t _mono_last_note_id = -1;
};

} // namespace synth_canvas::oscillator_plugin
