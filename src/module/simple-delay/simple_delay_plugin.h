#pragma once

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <numeric>  // For std::iota (used for parameter IDs)
#include <string>
#include <vector>

namespace synth_canvas::simple_delay_plugin {
class SimpleDelayPlugin
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                   clap::helpers::CheckingLevel::Maximal> {
   public:
    SimpleDelayPlugin(const std::string &pluginPath, const clap_host *host);

    static const clap_plugin_descriptor *descriptor();

    // --- Overrides from clap::helpers::Plugin ---
    bool activate(double sampleRate, uint32_t minFramesCount,
                  uint32_t maxFramesCount) noexcept override;
    void deactivate() noexcept override;

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

    // --- Parameters ---
    bool implementsParams() const noexcept override { return true; }
    uint32_t paramsCount() const noexcept override;
    bool paramsInfo(uint32_t index, clap_param_info *info) const noexcept override;
    bool paramsValue(clap_id paramId, double *value) noexcept override;
    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override;
    bool paramsTextToValue(clap_id paramId, const char *display, double *value) noexcept override;
    void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept override;

    // --- Processing ---
    clap_process_status process(const clap_process *process) noexcept override;

   private:
    // Helper to handle parameter changes
    void handleParamValueEvent(const clap_event_param_value *paramValue) noexcept;

    // Delay parameters
    double _delayTime = 0.5;  // seconds
    double _feedback = 0.5;   // 0.0 to 1.0
    double _mix = 0.5;        // 0.0 (dry) to 1.0 (wet)

    // Internal state
    double _sampleRate = 0.0;
    std::vector<std::vector<float>> _delayBuffer;  // [channel][samples]
    uint32_t _writeHead = 0;                       // Current write position in the buffer
    uint32_t _bufferSize = 0;                      // Size of the delay buffer in samples

    // Parameter IDs
    enum { PARAM_DELAY_TIME = 0, PARAM_FEEDBACK, PARAM_MIX, PARAM_COUNT };

    // Helper to convert time to samples
    uint32_t secondsToSamples(double seconds) const {
        return static_cast<uint32_t>(seconds * _sampleRate);
    }
};
}  // namespace synth_canvas::simple_delay_plugin
