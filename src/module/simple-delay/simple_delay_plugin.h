#pragma once

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <string>
#include <vector>

namespace synth_canvas::simple_delay_plugin {
class SimpleDelayPlugin
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                   clap::helpers::CheckingLevel::Maximal> {
   public:
    SimpleDelayPlugin(const std::string& plugin_path, const clap_host* host);

    static auto descriptor() -> const clap_plugin_descriptor*;

    // --- Overrides from clap::helpers::Plugin ---
    auto activate(double sample_rate, uint32_t min_frames_count, uint32_t max_frames_count) noexcept
        -> bool override;
    void deactivate() noexcept override;

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

   private:
    // Helper to handle parameter changes
    void handleParamValueEvent(const clap_event_param_value* param_value) noexcept;

    // Delay parameters
    double _delay_time = 0.5;  // seconds
    double _feedback = 0.5;    // 0.0 to 1.0
    double _mix = 0.5;         // 0.0 (dry) to 1.0 (wet)

    // Internal state
    double _sample_rate = 0.0;
    std::vector<std::vector<float>> _delay_buffer;  // [channel][samples]
    uint32_t _write_head = 0;                       // Current write position in the buffer
    uint32_t _buffer_size = 0;                      // Size of the delay buffer in samples

    // Parameter IDs
    enum { kParamDelayTime = 0, kParamFeedback, kParamMix, kParamCount };

    // Helper to convert time to samples
    [[nodiscard]] auto secondsToSamples(double seconds) const -> uint32_t {
        return static_cast<uint32_t>(seconds * _sample_rate);
    }
};
}  // namespace synth_canvas::simple_delay_plugin
