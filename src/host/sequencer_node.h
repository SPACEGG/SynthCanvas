#ifndef SYNTH_CANVAS_HOST_SEQUENCER_NODE_H
#define SYNTH_CANVAS_HOST_SEQUENCER_NODE_H

#include <array>
#include <atomic>
#include <vector>

#include "internal_node_base.h"

namespace synth_canvas::host {

class SequencerNode final : public InternalNodeBase {
   public:
    static constexpr uint32_t kMaxInstances = 32;
    static constexpr uint32_t kMaxPatternSize = 256;
    static constexpr uint32_t kMaxActiveNotesPerInstance = 256;

    static constexpr uint32_t kParamSteps = 0;
    static constexpr uint32_t kParamTime = 1;
    static constexpr uint32_t kParamSwing = 2;
    static constexpr uint32_t kParamRestart = 3;
    static constexpr uint32_t kParamCurrentStep = 4;

    struct NoteData {
        uint8_t step;
        uint8_t length;
        uint8_t pitch;
        uint8_t velocity;
    };

    struct ActiveNote {
        bool is_active = false;
        uint8_t pitch = 0;
        int32_t note_id = -1;
        double off_beat = 0.0;
    };

    struct PlaybackInstance {
        bool is_active = false;
        double start_beat = 0.0;
        double last_processed_relative_beat = 0.0;
        std::array<ActiveNote, kMaxActiveNotesPerInstance> active_notes;
    };

    SequencerNode();
    ~SequencerNode() override = default;

    void activate(int32_t sample_rate, int32_t block_size) override;
    void processBegin(int num_frames) override;
    void process() override;

    void setParameterValue(clap_id param_id, double value) override;
    void queueEvent(const PluginEvent& event) override;

    auto saveState(std::vector<uint8_t>& data) -> bool override;
    auto loadState(const std::vector<uint8_t>& data) -> bool override;

   private:
    [[nodiscard]] auto getStepDurationBeats() const -> double;
    void triggerNoteOn(uint32_t instance_idx, const NoteData& note, double start_beat,
                       uint32_t sample_offset);
    void triggerNoteOff(uint32_t instance_idx, uint32_t note_idx, uint32_t sample_offset);

    std::vector<NoteData> _active_pattern;
    std::vector<NoteData> _pending_pattern;
    std::atomic<bool> _pending_update{false};

    std::array<PlaybackInstance, kMaxInstances> _instances;

    std::atomic<int32_t> _active_steps{8};
    std::atomic<int32_t> _time_enum{1};  // 1/8
    std::atomic<double> _swing{0.0};
    std::atomic<bool> _restart_mode{true};

    int32_t _next_note_id = 0;
    double _last_block_end_beat = -1.0;
    bool _was_playing = false;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_SEQUENCER_NODE_H
