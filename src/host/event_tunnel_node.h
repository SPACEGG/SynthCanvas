#ifndef SYNTH_CANVAS_HOST_EVENT_TUNNEL_NODE_H
#define SYNTH_CANVAS_HOST_EVENT_TUNNEL_NODE_H

#include <vector>
#include <array>

#include "internal_node_base.h"

namespace synth_canvas::host {

class EventTunnelNode final : public InternalNodeBase {
   public:
    EventTunnelNode();
    ~EventTunnelNode() override = default;

    void activate(int32_t sample_rate, int32_t block_size) override;
    void deactivate() override;
    void process() override;

   private:
    std::array<std::array<int, 128>, 16> _note_active_counts{};
    std::vector<PluginEvent> _event_buffer;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_EVENT_TUNNEL_NODE_H