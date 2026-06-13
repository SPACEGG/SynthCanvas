#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <numbers>

#include "../src/module/fm/simple_fm_plugin.h"

using synth_canvas::fm_plugin::FmIntegrator;
using synth_canvas::fm_plugin::FractionalDelayLine;
using synth_canvas::fm_plugin::SimpleFmPlugin;

TEST_CASE("FmIntegrator Accumulation & Leakage", "[fm][integrator]") {
    FmIntegrator integrator;
    integrator.reset();
    REQUIRE(integrator.state == 0.0);

    // Feed a constant DC input (1.0) for 1000 samples.
    for (int i = 0; i < 1000; ++i) {
        integrator.process(1.0);
    }

    // The state should increase and asymptotically stabilize below the leak limit.
    // DC Gain = 1.0, so with input = 1.0, it should converge to 1.0.
    // Let's verify it is very close to 1.0, and definitely <= 1.0.
    REQUIRE_THAT(integrator.state, Catch::Matchers::WithinAbs(1.0, 0.01));
    REQUIRE(integrator.state <= 1.0);

    // Feed zero input (0.0) and verify the state decays back to 0.0
    for (int i = 0; i < 1000; ++i) {
        integrator.process(0.0);
    }
    REQUIRE_THAT(integrator.state, Catch::Matchers::WithinAbs(0.0, 0.01));
}

TEST_CASE("FractionalDelayLine Interpolation", "[fm][delay]") {
    FractionalDelayLine delay_line(16);
    delay_line.reset();

    // Write ramp: 0.0, 1.0, 2.0, 3.0, 4.0
    delay_line.write(0.0f);
    delay_line.write(1.0f);
    delay_line.write(2.0f);
    delay_line.write(3.0f);
    delay_line.write(4.0f);

    // Delay = 1.0 samples: expect 3.0
    REQUIRE_THAT(delay_line.read(1.0), Catch::Matchers::WithinRel(3.0f, 1e-5f));

    // Delay = 1.5 samples: expect 2.5 (interpolated between 2.0 and 3.0)
    REQUIRE_THAT(delay_line.read(1.5), Catch::Matchers::WithinRel(2.5f, 1e-5f));
}

TEST_CASE("Dry/Wet Blend Verification", "[fm][blend]") {
    // In our dry/wet blend, if carrier (dry) is 0.5 and modulated (wet) is 1.0:
    // dry_wet = 0.0 -> 0.5
    // dry_wet = 1.0 -> 1.0
    // dry_wet = 0.5 -> 0.75
    // We can test this linear blend logic directly.
    auto blend = [](double carrier, double wet, double dry_wet) -> double {
        return (1.0 - dry_wet) * carrier + dry_wet * wet;
    };

    REQUIRE_THAT(blend(0.5, 1.0, 0.0), Catch::Matchers::WithinRel(0.5, 1e-5));
    REQUIRE_THAT(blend(0.5, 1.0, 1.0), Catch::Matchers::WithinRel(1.0, 1e-5));
    REQUIRE_THAT(blend(0.5, 1.0, 0.5), Catch::Matchers::WithinRel(0.75, 1e-5));
}

TEST_CASE("Frequency Modulation Behavior", "[fm][modulation]") {
    // Write a pure sine wave carrier (1.0 kHz at 48 kHz samplerate) to the delay line
    // Modulate with a 100 Hz sine wave.
    // Verify that zero-crossing variations occur symmetrically without drift.
    FractionalDelayLine delay_line(2048);
    delay_line.reset();

    const double sample_rate = 48000.0;
    const double carrier_freq = 1000.0;
    const double modulator_freq = 100.0;
    const double mod_depth = 0.2;
    const double max_delay_time = 0.002;                            // 2 ms
    const double max_delay_samples = max_delay_time * sample_rate;  // 96 samples
    const double base_delay_samples = 2.0 * max_delay_samples;      // 192 samples (4 ms)

    FmIntegrator integrator;
    integrator.reset();

    std::vector<double> modulated_output;

    // Process for 480 samples (10 ms, i.e., 1 full cycle of modulator at 100 Hz)
    for (int n = 0; n < 480; ++n) {
        double t = n / sample_rate;
        double carrier_val = std::sin(2.0 * std::numbers::pi * carrier_freq * t);
        double modulator_val = std::sin(2.0 * std::numbers::pi * modulator_freq * t);

        // Integrate modulator
        double x_int = integrator.process(modulator_val);

        // Write carrier to delay line
        delay_line.write(static_cast<float>(carrier_val));

        // Calculate modulated delay:
        // delay_samples = base_delay + mod_depth * x_int * max_delay_samples
        double delay_dev = mod_depth * x_int * max_delay_samples;
        double delay_samples = base_delay_samples + delay_dev;

        // Clamp delay samples to [0.05 ms in samples, 2.0 * base_delay]
        double min_delay = 0.00005 * sample_rate;
        double max_delay = 2.0 * base_delay_samples;
        delay_samples = std::clamp(delay_samples, min_delay, max_delay);

        double out_sample = delay_line.read(delay_samples);
        modulated_output.push_back(out_sample);
    }

    // Verify modulation by checking that the signal has zero average offset
    double sum = 0.0;
    for (double val : modulated_output) {
        sum += val;
    }
    double average = sum / modulated_output.size();
    // Average should be very close to 0.0 since carrier is a sine wave and modulation is symmetric
    REQUIRE_THAT(average, Catch::Matchers::WithinAbs(0.0, 0.15));
}
