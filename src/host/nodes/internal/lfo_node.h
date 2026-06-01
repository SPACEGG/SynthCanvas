#ifndef SYNTH_CANVAS_HOST_LFO_NODE_H
#define SYNTH_CANVAS_HOST_LFO_NODE_H

#include <random>

#include "host/nodes/base/internal_node_base.h"

namespace synth_canvas::host {

/**
 * LFO Node for low-frequency modulation.
 * Supports Sine, Triangle, Square, Saw, and Random (S&H) waveforms.
 * Features BPM sync, retriggering, and smoothing.
 */
class LFONode : public InternalNodeBase {
   public:
    enum ParamId : clap_id {
        kFreq = 0,       // Hz or Division (beats)
        kWaveform = 1,   // Sine, Tri, Square, Saw, Random
        kSync = 2,       // 0: Hz, 1: Sync
        kRetrigger = 3,  // 0: Off, 1: On
        kAmplitude = 4,  // Scale
        kOffset = 5,     // Bias
        kSmoothing = 6   // Filter time (ms)
    };

    LFONode();
    ~LFONode() override = default;

    void activate(int32_t sample_rate, int32_t block_size) override;
    void process() override;
    void queueEvent(const PluginEvent& event) override;

    [[nodiscard]] auto getParameterText(clap_id param_id, double value) const
        -> std::string override;

    [[nodiscard]] auto supportsMiniCurve() const -> bool override;
    [[nodiscard]] auto getMiniCurveCount() const -> uint32_t override;
    auto getMiniCurveAxisNames(uint32_t curve_index, std::string& out_x, std::string& out_y) const
        -> bool override;
    auto renderMiniCurve(uint32_t curve_index, std::vector<float>& out_values, uint32_t resolution)
        -> uint32_t override;

   private:
    void updateSmoothingCoeff();
    auto generateWaveform(double phase, int waveform) -> float;

    double _phase = 0.0;
    float _last_output = 0.0;
    bool _retrigger_queued = false;

    // Smoothing state
    float _smooth_alpha = 1.0f;
    float _current_smoothing_ms = -1.0f;

    // Random state
    std::mt19937 _rng;
    std::uniform_real_distribution<float> _dist{-1.0f, 1.0f};
    float _last_random_val = 0.0f;
    double _last_random_phase = -1.0;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_LFO_NODE_H
