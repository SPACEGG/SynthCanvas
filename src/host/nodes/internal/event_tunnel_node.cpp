#include "host/nodes/internal/event_tunnel_node.h"

#include <algorithm>
#include <cstring>

namespace synth_canvas::host {

EventTunnelNode::EventTunnelNode() {
    _node_type_name = "event_tunnel";

    // 1 Event Input, 1 Event Output. No audio ports.
    addEventPort("Event IN", true);
    addEventPort("Event OUT", false);  // Port 0

    // Initialize counts to 0
    for (auto& channel_counts : _note_active_counts) {
        channel_counts.fill(0);
    }
}

void EventTunnelNode::activate(int32_t sample_rate, int32_t block_size) {
    InternalNodeBase::activate(sample_rate, block_size);
    for (auto& channel_counts : _note_active_counts) {
        channel_counts.fill(0);
    }
    _event_buffer.reserve(256);  // Pre-allocate some capacity
}

void EventTunnelNode::deactivate() {
    InternalNodeBase::deactivate();
    for (auto& channel_counts : _note_active_counts) {
        channel_counts.fill(0);
    }
}

void EventTunnelNode::queueEvent(const PluginEvent& event) { _event_buffer.push_back(event); }

void EventTunnelNode::process() {
    if (_event_buffer.empty()) {
        return;
    }

    // 2. Sort events by time
    std::ranges::sort(_event_buffer, [](const PluginEvent& a, const PluginEvent& b) {
        return a.event.header.time < b.event.header.time;
    });

    // 3. Filter and process events
    for (auto& ev : _event_buffer) {
        if (ev.event.header.type == CLAP_EVENT_NOTE_ON ||
            ev.event.header.type == CLAP_EVENT_NOTE_OFF) {
            // TODO(): Proper ID mapping for MPE/advanced articulation later.
            // For now, reset to -1 to prevent ID collisions.
            ev.event.note.note_id = -1;

            int channel = std::clamp(static_cast<int>(ev.event.note.channel), 0, 15);
            int key = std::clamp(static_cast<int>(ev.event.note.key), 0, 127);

            if (ev.event.header.type == CLAP_EVENT_NOTE_ON) {
                if (_note_active_counts[channel][key] == 0) {
                    _output_event_queues[0]->try_enqueue(ev);
                }
                _note_active_counts[channel][key]++;
            } else if (ev.event.header.type == CLAP_EVENT_NOTE_OFF) {
                if (_note_active_counts[channel][key] > 0) {
                    _note_active_counts[channel][key]--;
                    if (_note_active_counts[channel][key] == 0) {
                        _output_event_queues[0]->try_enqueue(ev);
                    }
                }
            }
        } else {
            // Forward any other events directly (e.g., parameter value, MIDI)
            _output_event_queues[0]->try_enqueue(ev);
        }
    }

    _event_buffer.clear();
}

}  // namespace synth_canvas::host