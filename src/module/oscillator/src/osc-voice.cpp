#include "osc-voice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace synth_canvas::oscillator_plugin {

static constexpr double kPi = std::numbers::pi;

// PolyBLEP for antialiasing discontinuities
inline auto polyBlep(double t, double dt) -> double {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    } else if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

void OscVoice::start(int16_t k, int32_t nid, double freq, double attack_ms, double sample_rate) {
    key = k;
    note_id = nid;
    active = true;
    current_freq_key = freq;
    target_freq_key = freq;

    // Randomize initial phases for unison
    for (int i = 0; i < 8; ++i) {
        phase[i] = dist(rng);
    }

    // Start Attack
    env_stage = EnvelopeStage::kAttack;
    env_value = 0.0;
    env_phase_inc = 1.0 / (sample_rate * (std::max(0.1, attack_ms) * 0.001));
}

void OscVoice::release(double release_ms, double sample_rate) {
    if (env_stage != EnvelopeStage::kIdle) {
        env_stage = EnvelopeStage::kRelease;
        env_phase_inc = 1.0 / (sample_rate * (std::max(0.1, release_ms) * 0.001));
    }
}

void OscVoice::processSample(int waveform, int uni_count, double uni_detune_cents, double pw,
                             double freq_hz, double sample_rate, float& out_l, float& out_r) {
    if (env_stage == EnvelopeStage::kIdle) {
        out_l = 0.0f;
        out_r = 0.0f;
        return;
    }

    // 1. Update Envelope
    if (env_stage == EnvelopeStage::kAttack) {
        env_value += env_phase_inc;
        if (env_value >= 1.0) {
            env_value = 1.0;
        }
    } else if (env_stage == EnvelopeStage::kRelease) {
        env_value -= env_phase_inc;
        if (env_value <= 0.0) {
            env_value = 0.0;
            env_stage = EnvelopeStage::kIdle;
            active = false;
            return;
        }
    }

    double sum_l = 0.0;
    double sum_r = 0.0;

    for (int i = 0; i < uni_count; ++i) {
        // Calculate per-unison frequency
        double detune_factor = 0.0;
        if (uni_count > 1) {
            detune_factor = (2.0 * i / (uni_count - 1)) - 1.0;  // -1.0 to 1.0
        }
        double detune_hz =
            freq_hz * (std::pow(2.0, (detune_factor * uni_detune_cents) / 1200.0) - 1.0);
        double actual_freq = freq_hz + detune_hz;
        double dt = actual_freq / sample_rate;

        // Phase Warping (PWM)
        double t = phase[i];
        double warped_t = t;
        if (waveform != 4) {  // Not for Noise
            if (pw < 0.01) pw = 0.01;
            if (pw > 0.99) pw = 0.99;
            if (t < pw) {
                warped_t = 0.5 * t / pw;
            } else {
                warped_t = 0.5 + 0.5 * (t - pw) / (1.0 - pw);
            }
        }

        double val = 0.0;
        switch (waveform) {
            case 0:  // Sine
                val = std::sin(2.0 * kPi * warped_t);
                break;
            case 1:  // Triangle
                val = 2.0 * std::abs(2.0 * warped_t - 1.0) - 1.0;
                // Triangle is relatively clean, but could use PolyBLEP if warped heavily
                break;
            case 2:  // Saw
                val = 2.0 * warped_t - 1.0;
                val -= polyBlep(t, dt);
                break;
            case 3:  // Square
                val = (t < pw) ? 1.0 : -1.0;
                val += polyBlep(t, dt);
                val -= polyBlep(std::fmod(t + (1.0 - pw), 1.0), dt);
                break;
            case 4:  // Noise
                val = dist(rng) * 2.0 - 1.0;
                break;
        }

        // Stereo pan for unison
        double pan = 0.5;
        if (uni_count > 1) {
            pan = 0.5 + 0.5 * detune_factor;  // 0.0 to 1.0
        }

        sum_l += val * (1.0 - pan);
        sum_r += val * pan;

        // Advance phase
        phase[i] += dt;
        if (phase[i] >= 1.0) phase[i] -= 1.0;
    }

    double normalization = (uni_count > 1) ? 1.0 / std::sqrt(static_cast<double>(uni_count)) : 1.0;
    out_l = static_cast<float>(sum_l * normalization * env_value);
    out_r = static_cast<float>(sum_r * normalization * env_value);
}

}  // namespace synth_canvas::oscillator_plugin
