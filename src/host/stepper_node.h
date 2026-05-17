#ifndef SYNTH_CANVAS_HOST_STEPPER_NODE_H
#define SYNTH_CANVAS_HOST_STEPPER_NODE_H

#include <atomic>
#include <array>
#include <vector>

#include "internal_node_base.h"

namespace synth_canvas::host {

class StepperNode final : public InternalNodeBase {
public:
    static constexpr uint32_t kMaxSteps = 32;
    static constexpr uint32_t kMaxPorts = 16;

    static constexpr uint32_t kParamSteps = 0;
    static constexpr uint32_t kParamPorts = 1;
    static constexpr uint32_t kParamCurrentStep = 2;

    StepperNode();
    ~StepperNode() override = default;

    void activate(int32_t sample_rate, int32_t block_size) override;
    void processBegin(int num_frames) override;
    void setParameterValue(clap_id param_id, double value) override;
    void queueEvent(const PluginEvent& event) override;
    void process() override;

    auto saveState(std::vector<uint8_t>& data) -> bool override;
    auto loadState(const std::vector<uint8_t>& data) -> bool override;

    [[nodiscard]] auto getAudioPorts(bool is_input) const -> const std::vector<AudioPortInfo>& override;

private:
    void updateActivePortsMetadata();

    std::array<std::atomic<uint16_t>, kMaxSteps> _matrix;
    
    // Buffer for pending updates from UI or Project Load
    std::array<uint16_t, kMaxSteps> _pending_matrix;
    std::atomic<bool> _pending_update{false};

    std::atomic<int32_t> _active_steps{4};
    std::atomic<int32_t> _active_ports{2};
    std::atomic<int32_t> _current_index{-1};

    std::vector<AudioPortInfo> _active_output_ports;
};

} // namespace synth_canvas::host

#endif // SYNTH_CANVAS_HOST_STEPPER_NODE_H
