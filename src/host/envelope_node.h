#ifndef SYNTH_CANVAS_HOST_ENVELOPE_NODE_H
#define SYNTH_CANVAS_HOST_ENVELOPE_NODE_H

#include <array>
#include <cstdint>

#include "internal_node_base.h"

namespace synth_canvas::host {

enum class ADSRState { kIdle, kAttack, kDecay, kSustain, kRelease };

struct ADSRInstance {
    ADSRState stage = ADSRState::kIdle;
    double current_value = 0.0;
    double start_value = 0.0;
    double target_value = 0.0;
    double phase = 0.0;
    double phase_inc = 0.0;
    double curve = 0.0;
    double peak_amplitude = 1.0;
};

/**
 * Monophonic Envelope Node (ADSR).
 * Outputs a 0.0 to 1.0 modulation signal via an audio buffer.
 */
class EnvelopeNode final : public InternalNodeBase {
   public:
    enum ParameterId {
        kAttack = 0,
        kDecay = 1,
        kSustain = 2,
        kRelease = 3,
        kAttackCurve = 4,
        kDecayCurve = 5,
        kReleaseCurve = 6,
        kVelocityAmp = 7,
        kVelocityTime = 8,
        kAmount = 9,
        kBypass = 10
    };

    EnvelopeNode();
    ~EnvelopeNode() override = default;

    void activate(int32_t sample_rate, int32_t block_size) override;
    void process() override;
    void queueEvent(const PluginEvent& event) override;

    [[nodiscard]] auto getParameterText(clap_id param_id, double value) const
        -> std::string override;

   private:
    void triggerNoteOn(int16_t key, int16_t channel, int32_t note_id, double velocity, uint32_t offset);
    void triggerNoteOff(int16_t key, int32_t note_id, uint32_t offset);

    // Monophonic State
    ADSRInstance _adsr;
    int16_t _current_key = -1;

    // Cached Parameters (Functional values)
    double _cached_attack = 10.0;
    double _cached_decay = 100.0;
    double _cached_sustain = 0.5;
    double _cached_release = 500.0;
    double _cached_a_curve = 0.0;
    double _cached_d_curve = 0.0;
    double _cached_r_curve = 0.0;
    double _cached_vel_amp = 0.0;
    double _cached_vel_time = 0.0;
    double _cached_amount = 1.0;
    bool _cached_bypass = false;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_ENVELOPE_NODE_H
