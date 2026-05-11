#ifndef SYNTH_CANVAS_HOST_TRANSPORT_NODE_H
#define SYNTH_CANVAS_HOST_TRANSPORT_NODE_H

#include <atomic>
#include <functional>

#include "internal_node_base.h"

namespace synth_canvas::host {

/**
 * Node that allows controlling and visualizing global transport state.
 */
class TransportNode final : public InternalNodeBase {
   public:
    enum ParameterId {
        kParamTempo = 0,
        kParamPlaying = 1,
    };

    TransportNode();
    ~TransportNode() override = default;

    void processBegin(int num_frames) override;
    void process() override;
    void pollMainThread() override;
    void setParameterValue(clap_id param_id, double value) override;

    // Callback used to notify the main thread of transport changes
    std::function<void(double tempo, bool playing)> on_transport_change_requested;

   private:
    std::atomic<bool> _needs_update_to_main{false};
    std::atomic<bool> _is_syncing{false};

    double _last_tempo = 120.0;
    bool _last_playing = false;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_TRANSPORT_NODE_H
