#ifndef SYNTH_CANVAS_HOST_ENVELOPE_NODE_H
#define SYNTH_CANVAS_HOST_ENVELOPE_NODE_H

#include <array>
#include <cstdint>

#include "internal_node_base.h"

namespace synth_canvas::host {

/**
 * Polyphonic Envelope Node (ADSR)
 * Emits CLAP_EVENT_PARAM_MOD events instead of audio.
 * Features exponential curves, soft-takeover, and velocity mapping.
 */
class EnvelopeNode : public InternalNodeBase {
   public:
    enum ParamId : clap_id {
        kAttack = 0,       // ms
        kDecay = 1,        // ms
        kSustain = 2,      // 0.0 ~ 1.0
        kRelease = 3,      // ms
        kAttackCurve = 4,  // -1.0 (Log) to 1.0 (Exp)
        kDecayCurve = 5,
        kReleaseCurve = 6,
        kVelocityAmp = 7,   // 0.0 ~ 1.0 (Amount of velocity affecting peak amplitude)
        kVelocityTime = 8,  // 0.0 ~ 1.0 (Amount of velocity reducing attack time)
        kAmount = 9,        // -1.0 ~ 1.0
        kBypass = 10        // Gate mode
    };

    struct ADSRState {
        enum Stage { kIdle, kAttack, kDecay, kSustain, kRelease };
        Stage stage = kIdle;

        double current_value = 0.0;
        double target_value = 0.0;
        double coeff = 0.0;  // 1-pole filter coefficient for current stage

        // Calculated targets based on velocity
        double peak_amplitude = 1.0;
    };

    struct VoiceState {
        int32_t note_id = -1;
        int16_t key = -1;
        int16_t channel = -1;
        uint32_t last_active_time = 0;  // For oldest-stealing
        bool active = false;

        ADSRState adsr;
    };

    EnvelopeNode();
    ~EnvelopeNode() override = default;

    void activate(int32_t sample_rate, int32_t block_size) override;
    void process() override;
    void queueEvent(const PluginEvent& event) override;

    [[nodiscard]] auto getParameterText(clap_id param_id, double value) const
        -> std::string override;

   private:
    void triggerNoteOn(int16_t key, int16_t channel, int32_t note_id, double velocity);
    void triggerNoteOff(int16_t key, int32_t note_id);
    auto findFreeVoice() -> VoiceState*;
    auto findVoice(int16_t key, int32_t note_id) -> VoiceState*;
    auto getOldestVoice() -> VoiceState*;

    [[nodiscard]] auto calculateCoeff(double time_ms, double curve) const -> double;
    void processVoice(VoiceState& voice, uint32_t frame_index);
    void pushModulationEvent(const VoiceState& voice, uint32_t frame_index);
    void pushNoteEndEvent(const VoiceState& voice, uint32_t frame_index);

    std::array<VoiceState, constants::kMaxInternalPolyphony> _voices;
    uint32_t _voice_counter = 0;  // Incremented on each Note-ON for oldest-stealing

    // Cached parameter values (read once per block for performance)
    double _cached_attack = 0.0;
    double _cached_decay = 0.0;
    double _cached_sustain = 0.0;
    double _cached_release = 0.0;
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
