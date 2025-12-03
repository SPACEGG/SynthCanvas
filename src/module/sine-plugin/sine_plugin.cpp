#define _USE_MATH_DEFINES
#include "sine_plugin.h"
#include <clap/helpers/plugin.hxx>
#include <cmath>
#include <cstring>

namespace clap
{
    const clap_plugin_descriptor *SinePlugin::descriptor()
    {
        static const char *features[] = {
            CLAP_PLUGIN_FEATURE_INSTRUMENT, "synthesizer", nullptr};

        static const clap_plugin_descriptor desc = {
            CLAP_VERSION_INIT,
            "com.synthcanvas.sine-plugin",
            "Sine Synth",
            "SynthCanvas",
            "https://github.com/SPACEGG/SynthCanvas",
            "",
            "",
            "1.0.0",
            "A simple sine wave synthesizer from a tutorial, refactored with clap-helpers.",
            features,
        };
        return &desc;
    }

    SinePlugin::SinePlugin(const clap_host_t *host) : super(descriptor(), host) {}

    bool SinePlugin::activate(double sampleRate, uint32_t minFrameCount, uint32_t maxFrameCount) noexcept
    {
        _sample_rate = sampleRate;
        return true;
    }

    clap_process_status SinePlugin::process(const clap_process *process) noexcept
    {
        const uint32_t nframes = process->frames_count;
        const uint32_t nev = process->in_events->size(process->in_events);
        uint32_t ev_index = 0;

        for (uint32_t i = 0; i < nframes;)
        {
            while (ev_index < nev)
            {
                const clap_event_header_t *hdr = process->in_events->get(process->in_events, ev_index);
                if (hdr->time > i)
                    break;

                if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID)
                {
                    if (hdr->type == CLAP_EVENT_NOTE_ON)
                    {
                        const auto *ev = reinterpret_cast<const clap_event_note_t *>(hdr);
                        _note_key = ev->key;
                        _note_freq = 440.0 * pow(2.0, (_note_key - 69.0) / 12.0);
                        _note_velocity = (float)ev->velocity;
                        _note_is_active = true;
                    }
                    else if (hdr->type == CLAP_EVENT_NOTE_OFF)
                    {
                        const auto *ev = reinterpret_cast<const clap_event_note_t *>(hdr);
                        if (ev->key == _note_key)
                        {
                            _note_is_active = false;
                        }
                    }
                }
                ++ev_index;
            }

            float *out_l = process->audio_outputs[0].data32[0];
            float *out_r = process->audio_outputs[0].data32[1];

            if (_note_is_active)
            {
                out_l[i] = sinf(_phase * 2 * M_PI) * (_note_velocity * 0.2f);
                _phase += _note_freq / _sample_rate;
                if (_phase > 1)
                    _phase -= 1;
            }
            else
            {
                out_l[i] = 0;
            }
            out_r[i] = out_l[i];
            ++i;
        }

        return CLAP_PROCESS_CONTINUE;
    }

    //--- Audio Ports
    uint32_t SinePlugin::audioPortsCount(bool is_input) const noexcept
    {
        return is_input ? 0 : 1;
    }

    bool SinePlugin::audioPortsInfo(uint32_t index, bool is_input, clap_audio_port_info_t *info) const noexcept
    {
        if (is_input || index != 0)
            return false;

        info->id = 0;
        strncpy(info->name, "Main", sizeof(info->name));
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }

    //--- Note Ports
    uint32_t SinePlugin::notePortsCount(bool is_input) const noexcept
    {
        return is_input ? 1 : 0;
    }

    bool SinePlugin::notePortsInfo(uint32_t index, bool is_input, clap_note_port_info_t *info) const noexcept
    {
        if (!is_input || index != 0)
            return false;

        info->id = 0;
        strncpy(info->name, "Main", sizeof(info->name));
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        return true;
    }
}
