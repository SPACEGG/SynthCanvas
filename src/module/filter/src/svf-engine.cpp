#include "svf-engine.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace synth_canvas::filter_plugin {

void SvfEngine::reset() noexcept {
    for (auto& stage : _stages) {
        stage.reset();
    }
}

void SvfEngine::setCoeff(double cutoff_hz, double resonance, double sample_rate,
                         int num_stages) noexcept {
    cutoff_hz = std::clamp(cutoff_hz, 20.0, 20000.0);
    resonance = std::clamp(resonance, 0.0, 1.0);

    _g = std::tan(kPi * cutoff_hz / sample_rate);

    // Resonance compensation: k_total = k_stage^N => k_stage = k_total^(1/N)
    // k = 2.0 - 2.0 * resonance
    double k_target = 2.0 - 2.0 * resonance;
    _k = std::pow(k_target, 1.0 / static_cast<double>(num_stages));

    _gk = _g + _k;
    _a1 = 1.0 / (1.0 + _g * _gk);
    _a2 = _g * _a1;
    _a3 = _g * _a2;
    _ak = _gk * _a1;
}

void SvfEngine::step(float& l, float& r, Mode mode, int num_stages) noexcept {
    std::array<float, 2> vin = {l, r};

    for (int s = 0; s < num_stages; ++s) {
        auto& stage = _stages[s];
        std::array<float, 2> stage_out;

        for (int c = 0; c < 2; ++c) {
            double v3 = static_cast<double>(vin[c]) - stage.ic2eq[c];
            double v0 = _a1 * v3 - _ak * stage.ic1eq[c];

            // Non-linearity
            v0 = std::tanh(v0);

            double v1 = _a2 * v3 + _a1 * stage.ic1eq[c];
            double v2 = _a3 * v3 + _a2 * stage.ic1eq[c] + stage.ic2eq[c];

            stage.ic1eq[c] = 2.0 * v1 - stage.ic1eq[c];
            stage.ic2eq[c] = 2.0 * v2 - stage.ic2eq[c];

            switch (mode) {
                case kLP:
                    stage_out[c] = static_cast<float>(v2);
                    break;
                case kHP:
                    stage_out[c] = static_cast<float>(v0);
                    break;
                case kBP:
                    stage_out[c] = static_cast<float>(v1);
                    break;
                case kNotch:
                    stage_out[c] = static_cast<float>(v2 + v0);
                    break;
                case kPeak:
                    stage_out[c] = static_cast<float>(v2 - v0);
                    break;
                case kAll:
                    stage_out[c] = static_cast<float>(v2 + v0 - _k * v1);
                    break;
                default:
                    stage_out[c] = vin[c];
                    break;
            }
        }
        vin[0] = stage_out[0];
        vin[1] = stage_out[1];
    }

    l = vin[0];
    r = vin[1];
}

}  // namespace synth_canvas::filter_plugin
