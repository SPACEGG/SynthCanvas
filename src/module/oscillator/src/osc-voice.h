#pragma once

#include <cstdint>
#include <random>

namespace synth_canvas::oscillator_plugin {

enum class EnvelopeStage { kIdle, kAttack, kRelease };

struct OscVoice {
    // Basic state
    bool active = false;
    int16_t key = -1;
    int32_t note_id = -1;
    uint32_t last_active_time = 0;

    // Oscillator state
    std::array<double, 8> phase = {0.0};  // Max 8 unison sub-oscillators
    double current_freq_key = 69.0;
    double target_freq_key = 69.0;

    // Envelope state
    EnvelopeStage env_stage = EnvelopeStage::kIdle;
    double env_value = 0.0;
    double env_phase = 0.0;
    double env_phase_inc = 0.0;

    // Random generator for Noise and Unison phase randomization
    std::mt19937 rng;
    std::uniform_real_distribution<float> dist{0.0f, 1.0f};

    OscVoice() : rng(std::random_device{}()) {}

    void start(int16_t k, int32_t nid, double freq, double attack_ms, double sample_rate);
    void release(double release_ms, double sample_rate);
    void processSample(int waveform, int uni_count, double uni_detune_cents, double pw,
                       double freq_hz, double sample_rate, float& out_l, float& out_r);
};

}  // namespace synth_canvas::oscillator_plugin
