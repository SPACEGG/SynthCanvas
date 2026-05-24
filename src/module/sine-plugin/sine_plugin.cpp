#include "sine_plugin.h"

#include <clap/helpers/plugin.hxx>
#include <cmath>
#include <cstring>
#include <numbers>

namespace clap {
auto SinePlugin::descriptor() -> const clap_plugin_descriptor* {
    static const std::array<const char*, 3> features = {CLAP_PLUGIN_FEATURE_INSTRUMENT,
                                                        "synthesizer", nullptr};

    static const clap_plugin_descriptor desc = {
        .clap_version = CLAP_VERSION_INIT,
        .id = "com.synthcanvas.sine-plugin",
        .name = "Sine Synth",
        .vendor = "SynthCanvas",
        .url = "https://github.com/SPACEGG/SynthCanvas",
        .manual_url = "",
        .support_url = "",
        .version = "1.0.0",
        .description =
            "A simple sine wave synthesizer from a tutorial, refactored with clap-helpers.",
        .features = features.data(),
    };
    return &desc;
}

SinePlugin::SinePlugin(const clap_host_t* host) : super(descriptor(), host) {}

auto SinePlugin::activate(double sample_rate, uint32_t min_frame_count,
                          uint32_t max_frame_count) noexcept -> bool {
    _sample_rate = sample_rate;
    return true;
}

auto SinePlugin::process(const clap_process* process) noexcept -> clap_process_status {
    const uint32_t nframes = process->frames_count;
    const uint32_t nev = process->in_events->size(process->in_events);
    uint32_t ev_index = 0;

    for (uint32_t i = 0; i < nframes;) {
        while (ev_index < nev) {
            const clap_event_header_t* hdr = process->in_events->get(process->in_events, ev_index);
            if (hdr->time > i) break;

            if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID) {
                if (hdr->type == CLAP_EVENT_NOTE_ON) {
                    const auto* ev = reinterpret_cast<const clap_event_note_t*>(hdr);
                    _note_key = ev->key;
                    _note_freq = 440.0 * pow(2.0, (_note_key - 69.0) / 12.0);
                    _note_velocity = static_cast<float>(ev->velocity);
                    _note_is_active = true;
                } else if (hdr->type == CLAP_EVENT_NOTE_OFF) {
                    const auto* ev = reinterpret_cast<const clap_event_note_t*>(hdr);
                    if (ev->key == _note_key) {
                        _note_is_active = false;
                    }
                }
            }
            ++ev_index;
        }

        float* out_l = process->audio_outputs[0].data32[0];
        float* out_r = process->audio_outputs[0].data32[1];

        if (_note_is_active) {
            out_l[i] = sinf(_phase * 2 * std::numbers::pi) * (_note_velocity * 0.2f);
            _phase += _note_freq / _sample_rate;
            if (_phase > 1) _phase -= 1;
        } else {
            out_l[i] = 0;
        }
        out_r[i] = out_l[i];
        ++i;
    }

    return CLAP_PROCESS_CONTINUE;
}

//--- Audio Ports
auto SinePlugin::audioPortsCount(bool is_input) const noexcept -> uint32_t {
    return is_input ? 0 : 1;
}

auto SinePlugin::audioPortsInfo(uint32_t index, bool is_input,
                                clap_audio_port_info_t* info) const noexcept -> bool {
    if (is_input || index != 0) return false;

    info->id = 0;
    strncpy(info->name, "Main", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

//--- Note Ports
auto SinePlugin::notePortsCount(bool is_input) const noexcept -> uint32_t {
    return is_input ? 1 : 0;
}

auto SinePlugin::notePortsInfo(uint32_t index, bool is_input,
                               clap_note_port_info_t* info) const noexcept -> bool {
    if (!is_input || index != 0) return false;

    info->id = 0;
    strncpy(info->name, "Main", sizeof(info->name));
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    return true;
}
}  // namespace clap
