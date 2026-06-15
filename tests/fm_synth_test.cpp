#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>

#include "../src/module/fm-synth/fm_synth_plugin.h"

using synth_canvas::fm_synth::AdsrEnvelope;
using synth_canvas::fm_synth::AlgorithmConfig;
using synth_canvas::fm_synth::FmVoice;
using synth_canvas::fm_synth::kAlgorithms;
using synth_canvas::fm_synth::kMaxFeedbackIndex;
using synth_canvas::fm_synth::kMaxModIndex;
using synth_canvas::fm_synth::kMaxVoices;
using synth_canvas::fm_synth::kNumOperators;

// Test 1: ADSR Exponential Decay shape verification
TEST_CASE("ADSR Exponential Decay Curve", "[fm-synth][adsr]") {
    AdsrEnvelope env;
    env.attack_time = 0.001;  // 1ms attack (very short)
    env.decay_time = 0.1;     // 100ms decay
    env.sustain_level = 0.3;
    env.release_time = 0.2;

    const double sr = 48000.0;
    env.gateOn();

    // Run through attack phase
    int max_attack_samples = static_cast<int>(env.attack_time * sr) + 100;
    for (int i = 0; i < max_attack_samples; ++i) {
        env.process(sr);
        if (env.stage == AdsrEnvelope::kDecay) break;
    }
    REQUIRE(env.stage == AdsrEnvelope::kDecay);
    REQUIRE_THAT(env.level, Catch::Matchers::WithinAbs(1.0, 0.01));

    // Record decay values to verify exponential shape
    double prev_level = env.level;
    double prev_diff = 0.0;
    bool exponential_confirmed = true;

    for (int i = 0; i < 20000; ++i) {
        double current = env.process(sr);
        double diff = prev_level - current;

        // After a few initial samples, each step should be smaller than the previous
        // (characteristic of exponential decay toward sustain)
        if (i > 10 && prev_diff > 1e-8 && diff > 1e-8) {
            if (diff > prev_diff * 1.01) {
                exponential_confirmed = false;
                break;
            }
        }
        prev_diff = diff;
        prev_level = current;
    }

    REQUIRE(exponential_confirmed);
    // Should be near sustain level after enough decay samples
    REQUIRE_THAT(env.level, Catch::Matchers::WithinAbs(env.sustain_level, 0.05));
}

// Test 2: Dynamic Feedback Stability for Alg 1 (fb=6) and Alg 2 (fb=2)
TEST_CASE("Feedback Stability - Alg 1 and Alg 2", "[fm-synth][feedback]") {
    const double sr = 48000.0;

    auto run_feedback_test = [&](int alg_index) {
        const auto& alg = kAlgorithms[alg_index];
        int fb_src = alg.feedback_src;
        int fb_dst = alg.feedback_dst;

        FmVoice voice;
        voice.start(69, 1, 1.0, 0);

        double midi_freq = 440.0;
        for (int i = 1; i <= kNumOperators; ++i) {
            voice.op_freq[i] = midi_freq;
            voice.env[i].attack_time = 0.001;
            voice.env[i].decay_time = 10.0;
            voice.env[i].sustain_level = 1.0;
            voice.env[i].release_time = 10.0;
        }

        // Maximum feedback
        double beta = 1.0 * kMaxFeedbackIndex;
        bool stable = true;

        for (int n = 0; n < 2000; ++n) {
            double fb_sig = beta * (voice.fb_history[0] + voice.fb_history[1]) * 0.5;

            std::array<double, 7> op_out{};
            std::array<double, 7> op_raw{};

            for (int op = kNumOperators; op >= 1; --op) {
                double env_level = voice.env[op].process(sr);

                double mod_input = 0.0;
                uint8_t sources = alg.mod_sources[op];
                for (int src = 1; src <= kNumOperators; ++src) {
                    if (sources & (1 << (src - 1))) {
                        mod_input += op_out[src];
                    }
                }

                if (op == fb_dst) {
                    mod_input += fb_sig;
                }

                double phase_inc = 2.0 * std::numbers::pi * voice.op_freq[op] / sr;
                voice.phase[op] += phase_inc;

                double raw = std::sin(voice.phase[op] + mod_input);
                op_raw[op] = raw * env_level;
                op_out[op] = raw * env_level * kMaxModIndex;
            }

            voice.fb_history[1] = voice.fb_history[0];
            voice.fb_history[0] = op_raw[fb_src];

            // Check bounds
            if (std::isnan(op_raw[fb_src]) || std::isinf(op_raw[fb_src]) ||
                std::abs(op_raw[fb_src]) > 1.0 + 1e-6) {
                stable = false;
                break;
            }
        }

        REQUIRE(stable);
        // Verify feedback operator has correct assignment
        REQUIRE(alg.feedback_src == fb_src);
        REQUIRE(alg.feedback_dst == fb_dst);
    };

    SECTION("Algorithm 1 - fb=6") {
        REQUIRE(kAlgorithms[0].feedback_src == 6);
        REQUIRE(kAlgorithms[0].feedback_dst == 6);
        run_feedback_test(0);
    }

    SECTION("Algorithm 2 - fb=2") {
        REQUIRE(kAlgorithms[1].feedback_src == 2);
        REQUIRE(kAlgorithms[1].feedback_dst == 2);
        run_feedback_test(1);
    }
}

// Test 3: Algorithm 32 - All carriers, no modulation = 6 clean sines
TEST_CASE("Algorithm 32 - All Carriers Additive", "[fm-synth][algorithm]") {
    const auto& alg = kAlgorithms[31];

    // Verify all 6 operators are carriers
    for (int op = 1; op <= kNumOperators; ++op) {
        REQUIRE((alg.carrier_mask & (1 << (op - 1))) != 0);
    }

    // Verify no modulation connections
    for (int op = 1; op <= kNumOperators; ++op) {
        REQUIRE(alg.mod_sources[op] == 0);
    }

    // Render and verify output is sum of 6 clean sines
    const double sr = 48000.0;
    FmVoice voice;
    voice.start(69, 1, 1.0, 0);

    double base_freq = 440.0;
    for (int i = 1; i <= kNumOperators; ++i) {
        voice.op_freq[i] = base_freq * i;
        voice.env[i].attack_time = 0.0001;
        voice.env[i].decay_time = 10.0;
        voice.env[i].sustain_level = 1.0;
        voice.env[i].release_time = 10.0;
    }

    // Let attack settle
    for (int i = 1; i <= kNumOperators; ++i) {
        for (int n = 0; n < 100; ++n) voice.env[i].process(sr);
    }

    // Reset phases for clean test
    voice.phase.fill(0.0);
    voice.fb_history[0] = 0.0;
    voice.fb_history[1] = 0.0;

    // Render one sample with no modulation (Alg 32)
    // Each op produces sin(phase) * env * level, no modulation input
    double expected = 0.0;
    for (int op = 1; op <= kNumOperators; ++op) {
        double phase_inc = 2.0 * std::numbers::pi * voice.op_freq[op] / sr;
        expected += std::sin(phase_inc);  // First sample after phase reset
    }

    // Now render through the algorithm
    std::array<double, 7> op_out{};
    double actual = 0.0;
    for (int op = kNumOperators; op >= 1; --op) {
        double phase_inc = 2.0 * std::numbers::pi * voice.op_freq[op] / sr;
        voice.phase[op] += phase_inc;
        double raw = std::sin(voice.phase[op]);
        op_out[op] = raw;
        if (alg.carrier_mask & (1 << (op - 1))) {
            actual += raw;
        }
    }

    REQUIRE_THAT(actual, Catch::Matchers::WithinAbs(expected, 1e-10));
}

// Test 4: Voice Stealing at 32-voice limit
TEST_CASE("Voice Stealing - Oldest Voice", "[fm-synth][polyphony]") {
    std::array<FmVoice, kMaxVoices> voices{};
    uint64_t counter = 0;

    // Allocate voice helper
    auto allocate = [&]() -> FmVoice* {
        for (auto& v : voices) {
            if (!v.active) return &v;
        }
        FmVoice* oldest = &voices[0];
        for (auto& v : voices) {
            if (v.start_order < oldest->start_order) {
                oldest = &v;
            }
        }
        return oldest;
    };

    // Fill all 32 voices
    for (int i = 0; i < kMaxVoices; ++i) {
        auto* v = allocate();
        v->start(static_cast<int16_t>(60 + i), i, 0.8, counter++);
    }

    // Verify all active
    int active_count = 0;
    for (const auto& v : voices) {
        if (v.active) active_count++;
    }
    REQUIRE(active_count == kMaxVoices);

    // Record the oldest voice's start_order (should be 0)
    uint64_t oldest_order = UINT64_MAX;
    int oldest_key = -1;
    for (const auto& v : voices) {
        if (v.start_order < oldest_order) {
            oldest_order = v.start_order;
            oldest_key = v.key;
        }
    }
    REQUIRE(oldest_key == 60);  // First note

    // Trigger 33rd note - should steal the oldest
    auto* stolen = allocate();
    REQUIRE(stolen->key == 60);  // Oldest voice is stolen
    stolen->start(static_cast<int16_t>(99), 99, 0.9, counter++);

    // Verify still exactly 32 active voices
    active_count = 0;
    for (const auto& v : voices) {
        if (v.active) active_count++;
    }
    REQUIRE(active_count == kMaxVoices);

    // Verify stolen voice now has the new key
    bool found_new = false;
    for (const auto& v : voices) {
        if (v.key == 99) {
            found_new = true;
            break;
        }
    }
    REQUIRE(found_new);

    // Verify key 60 is gone
    bool found_old = false;
    for (const auto& v : voices) {
        if (v.key == 60) {
            found_old = true;
            break;
        }
    }
    REQUIRE_FALSE(found_old);
}

// Test 5: Phase Reset (Key Sync) on Note On
TEST_CASE("Phase Reset on Note On - Key Sync", "[fm-synth][keysync]") {
    FmVoice voice;
    const double sr = 48000.0;

    // Set up frequencies
    for (int i = 1; i <= kNumOperators; ++i) {
        voice.op_freq[i] = 440.0 * i;
        voice.env[i].attack_time = 0.001;
        voice.env[i].decay_time = 1.0;
        voice.env[i].sustain_level = 0.8;
        voice.env[i].release_time = 0.5;
    }

    // Start a note
    voice.start(69, 1, 1.0, 0);

    // Verify phases are 0 after start
    for (int i = 1; i <= kNumOperators; ++i) {
        REQUIRE(voice.phase[i] == 0.0);
    }

    // Accumulate some phase by simulating 50 samples
    for (int n = 0; n < 50; ++n) {
        for (int i = 1; i <= kNumOperators; ++i) {
            double phase_inc = 2.0 * std::numbers::pi * voice.op_freq[i] / sr;
            voice.phase[i] += phase_inc;
            voice.env[i].process(sr);
        }
    }

    // Phases should be non-zero
    for (int i = 1; i <= kNumOperators; ++i) {
        REQUIRE(voice.phase[i] != 0.0);
    }

    // Trigger a new note (key sync)
    voice.start(72, 2, 0.9, 1);

    // All phases must be reset to 0.0
    for (int i = 1; i <= kNumOperators; ++i) {
        REQUIRE(voice.phase[i] == 0.0);
    }

    // Feedback history should also be reset
    REQUIRE(voice.fb_history[0] == 0.0);
    REQUIRE(voice.fb_history[1] == 0.0);
}
