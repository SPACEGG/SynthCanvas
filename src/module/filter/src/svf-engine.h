#include <array>
#include <numbers>

#pragma once

namespace synth_canvas::filter_plugin {

class SvfEngine {
   public:
    enum Mode { kLP = 0, kHP, kBP, kNotch, kPeak, kAll };

    SvfEngine() = default;

    void reset() noexcept;
    void setCoeff(double cutoff_hz, double resonance, double sample_rate, int num_stages) noexcept;
    void step(float& l, float& r, Mode mode, int num_stages) noexcept;

   private:
    struct Stage {
        std::array<double, 2> ic1eq{0.0, 0.0};
        std::array<double, 2> ic2eq{0.0, 0.0};

        void reset() {
            ic1eq[0] = ic1eq[1] = 0.0;
            ic2eq[0] = ic2eq[1] = 0.0;
        }
    };

    std::array<Stage, 4> _stages;

    // Coefficients (shared by all stages)
    double _g{0.0};
    double _k{0.0};
    double _gk{0.0};
    double _a1{0.0};
    double _a2{0.0};
    double _a3{0.0};
    double _ak{0.0};

    static constexpr double kPi = std::numbers::pi;
};

}  // namespace synth_canvas::filter_plugin
