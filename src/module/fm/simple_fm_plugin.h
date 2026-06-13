#pragma once

#include <algorithm>
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <cmath>
#include <string>
#include <vector>

namespace synth_canvas::fm_plugin {

struct FmIntegrator {
    double state = 0.0;
    double coeff = 0.995;

    auto process(double in) -> double {
        state = coeff * state + (1.0 - coeff) * in;
        return state;
    }

    void reset() { state = 0.0; }
};

class FractionalDelayLine {
   public:
    explicit FractionalDelayLine(size_t max_size) : _max_size(max_size), _buffer(max_size, 0.0f) {}

    void write(float sample) {
        _buffer[_write_idx] = sample;
        _write_idx = (_write_idx + 1) % _max_size;
    }

    auto read(double delay_samples) -> float {
        double target_idx = static_cast<double>(_write_idx) - 1.0 - delay_samples;
        while (target_idx < 0.0) {
            target_idx += _max_size;
        }
        while (target_idx >= _max_size) {
            target_idx -= _max_size;
        }

        auto idx_floor = static_cast<size_t>(std::floor(target_idx));
        double frac = target_idx - idx_floor;
        size_t idx_ceil = (idx_floor + 1) % _max_size;

        return static_cast<float>((1.0 - frac) * _buffer[idx_floor] + frac * _buffer[idx_ceil]);
    }

    void reset() {
        std::ranges::fill(_buffer, 0.0f);
        _write_idx = 0;
    }

   private:
    std::vector<float> _buffer;
    size_t _write_idx = 0;
    size_t _max_size = 0;
};

class SimpleFmPlugin : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                                    clap::helpers::CheckingLevel::Maximal> {
   public:
    SimpleFmPlugin(const std::string& plugin_path, const clap_host* host);

    static auto descriptor() -> const clap_plugin_descriptor*;

    auto activate(double sample_rate, uint32_t min_frames_count, uint32_t max_frames_count) noexcept
        -> bool override;

    [[nodiscard]] auto implementsAudioPorts() const noexcept -> bool override { return true; }
    [[nodiscard]] auto audioPortsCount(bool is_input) const noexcept -> uint32_t override;
    auto audioPortsInfo(uint32_t index, bool is_input, clap_audio_port_info* info) const noexcept
        -> bool override;

    [[nodiscard]] auto implementsNotePorts() const noexcept -> bool override { return false; }

    [[nodiscard]] auto implementsParams() const noexcept -> bool override { return true; }
    [[nodiscard]] auto paramsCount() const noexcept -> uint32_t override;
    auto paramsInfo(uint32_t index, clap_param_info* info) const noexcept -> bool override;
    auto paramsValue(clap_id param_id, double* value) noexcept -> bool override;
    auto paramsValueToText(clap_id param_id, double value, char* display, uint32_t size) noexcept
        -> bool override;
    auto paramsTextToValue(clap_id param_id, const char* display, double* value) noexcept
        -> bool override;
    void paramsFlush(const clap_input_events* in, const clap_output_events* out) noexcept override;

    [[nodiscard]] auto implementsState() const noexcept -> bool override { return true; }
    auto stateSave(const clap_ostream* os) noexcept -> bool override;
    auto stateLoad(const clap_istream* is) noexcept -> bool override;

    auto process(const clap_process* process) noexcept -> clap_process_status override;

    enum ParamIds { kParamModDepth = 0, kParamDryWet, kParamCount };

   private:
    void handleEvents(const clap_input_events* in, uint32_t& event_index,
                      uint32_t sample_index) noexcept;

    double _mod_depth_normalized{0.2};
    double _dry_wet_normalized{1.0};

    double _mod_depth_mod{0.0};
    double _dry_wet_mod{0.0};

    double _sample_rate{48000.0};

    FractionalDelayLine _delay_line_l{2048};
    FractionalDelayLine _delay_line_r{2048};
    FmIntegrator _integrator_l;
    FmIntegrator _integrator_r;
};

}  // namespace synth_canvas::fm_plugin
