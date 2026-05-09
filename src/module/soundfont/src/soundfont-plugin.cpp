#include "soundfont-plugin.h"

#include <algorithm>
#include <clap/helpers/host-proxy.hxx>
#include <clap/helpers/plugin.hxx>
#include <cmath>
#include <cstring>
#include <sstream>


namespace synth_canvas::soundfont_plugin {

SoundfontPlugin::SoundfontPlugin(const std::string& plugin_path, const clap_host* host)
    : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                            clap::helpers::CheckingLevel::Maximal>(descriptor(), host) {}

auto SoundfontPlugin::descriptor() -> const clap_plugin_descriptor* {
    static const std::array<const char*, 3> features = {CLAP_PLUGIN_FEATURE_INSTRUMENT,
                                                        CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr};

    static const clap_plugin_descriptor desc = {
        .clap_version = CLAP_VERSION_INIT,
        .id = "com.synthcanvas.soundfont-plugin",
        .name = "Soundfont Player",
        .vendor = "SynthCanvas",
        .url = "https://github.com/SPACEGG/SynthCanvas",
        .manual_url = "",
        .support_url = "",
        .version = "1.0.0",
        .description = "A lightweight Soundfont (.sf2) player using TinySoundFont.",
        .features = features.data(),
    };
    return &desc;
}

auto SoundfontPlugin::activate(double sample_rate, uint32_t min_frames_count,
                               uint32_t max_frames_count) noexcept -> bool {
    _engine.setSampleRate(static_cast<float>(sample_rate));
    _current_gain = static_cast<float>(_gain_db);
    _current_pan = static_cast<float>(_pan);
    _current_midi_channel = static_cast<int>(_midi_channel);
    return true;
}

//--- Ports
auto SoundfontPlugin::audioPortsCount(bool is_input) const noexcept -> uint32_t {
    return is_input ? 0 : 1;
}

auto SoundfontPlugin::audioPortsInfo(uint32_t index, bool is_input,
                                     clap_audio_port_info* info) const noexcept -> bool {
    if (is_input || index != 0) return false;

    info->id = 0;
    std::strncpy(info->name, "Main Output", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

auto SoundfontPlugin::notePortsCount(bool is_input) const noexcept -> uint32_t {
    return is_input ? 1 : 0;
}

auto SoundfontPlugin::notePortsInfo(uint32_t index, bool is_input,
                                    clap_note_port_info* info) const noexcept -> bool {
    if (!is_input || index != 0) return false;

    info->id = 0;
    std::strncpy(info->name, "Note Input", sizeof(info->name));
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    return true;
}

//--- Parameters
auto SoundfontPlugin::paramsCount() const noexcept -> uint32_t { return kParamCount; }

auto SoundfontPlugin::paramsInfo(uint32_t index, clap_param_info* info) const noexcept -> bool {
    switch (index) {
        case kParamPreset:
            info->id = kParamPreset;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
            info->min_value = 0;
            info->max_value = std::max(0, _engine.getPresetCount() - 1);
            info->default_value = 0;
            std::strncpy(info->name, "Preset", sizeof(info->name));
            std::strncpy(info->module, "", sizeof(info->module));
            return true;
        case kParamGain:
            info->id = kParamGain;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            info->min_value = -60.0;
            info->max_value = 12.0;
            info->default_value = 0.0;
            std::strncpy(info->name, "Gain (dB)", sizeof(info->name));
            std::strncpy(info->module, "", sizeof(info->module));
            return true;
        case kParamPan:
            info->id = kParamPan;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            info->min_value = -1.0;
            info->max_value = 1.0;
            info->default_value = 0.0;
            std::strncpy(info->name, "Pan", sizeof(info->name));
            std::strncpy(info->module, "", sizeof(info->module));
            return true;
        case kParamMidiChannel:
            info->id = kParamMidiChannel;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
            info->min_value = 0;
            info->max_value = 15;
            info->default_value = 0;
            std::strncpy(info->name, "MIDI Channel", sizeof(info->name));
            std::strncpy(info->module, "", sizeof(info->module));
            return true;
    }
    return false;
}

auto SoundfontPlugin::paramsValue(clap_id param_id, double* value) noexcept -> bool {
    switch (param_id) {
        case kParamPreset:
            *value = _preset_index;
            return true;
        case kParamGain:
            *value = _gain_db;
            return true;
        case kParamPan:
            *value = _pan;
            return true;
        case kParamMidiChannel:
            *value = _midi_channel;
            return true;
    }
    return false;
}

auto SoundfontPlugin::paramsValueToText(clap_id param_id, double value, char* display,
                                        uint32_t size) noexcept -> bool {
    if (param_id == kParamPreset) {
        int idx = static_cast<int>(value);
        const char* name = _engine.getPresetName(idx);
        if (name) {
            std::strncpy(display, name, size);
        } else {
            std::snprintf(display, size, "%d", idx);
        }
        return true;
    } else if (param_id == kParamMidiChannel) {
        std::snprintf(display, size, "%d", static_cast<int>(value) + 1);
        return true;
    }
    return false;
}

auto SoundfontPlugin::paramsTextToValue(clap_id param_id, const char* display,
                                        double* value) noexcept -> bool {
    if (param_id == kParamPreset) {
        *value = std::atof(display);
        return true;
    }
    return false;
}

void SoundfontPlugin::paramsFlush(const clap_input_events* in,
                                  const clap_output_events* out) noexcept {
    uint32_t event_index = 0;
    handleEvents(in, event_index, 0);
}

//--- State
auto SoundfontPlugin::stateSave(const clap_ostream* os) noexcept -> bool {
    std::ostringstream ss;
    ss << "path=" << _sf2_path << ";"
       << "preset=" << static_cast<int>(_preset_index) << ";"
       << "gain=" << _gain_db << ";"
       << "pan=" << _pan << ";"
       << "channel=" << static_cast<int>(_midi_channel) << ";";

    std::string state = ss.str();
    int64_t written = os->write(os, state.c_str(), state.size());
    return written == static_cast<int64_t>(state.size());
}

auto SoundfontPlugin::stateLoad(const clap_istream* is) noexcept -> bool {
    std::array<char, 1024> buffer;
    std::string state;
    int64_t read;
    while ((read = is->read(is, buffer.data(), sizeof(buffer.data()))) > 0) {
        state.append(buffer.data(), static_cast<size_t>(read));
    }

    std::string key, value;
    std::istringstream ss(state);
    std::string pair;
    while (std::getline(ss, pair, ';')) {
        size_t pos = pair.find('=');
        if (pos != std::string::npos) {
            key = pair.substr(0, pos);
            value = pair.substr(pos + 1);
            if (key == "path") {
                _sf2_path = value;
                if (_engine.load(_sf2_path)) {
                    _host.paramsRescan(CLAP_PARAM_RESCAN_ALL);
                }
            } else if (key == "preset") {
                _preset_index = std::atof(value.c_str());
                _engine.setPreset(_current_midi_channel, static_cast<int>(_preset_index));
            } else if (key == "gain") {
                _gain_db = std::atof(value.c_str());
            } else if (key == "pan") {
                _pan = std::atof(value.c_str());
            } else if (key == "channel") {
                _midi_channel = std::atof(value.c_str());
                _current_midi_channel = static_cast<int>(_midi_channel);
            }
        }
    }
    _current_gain = static_cast<float>(_gain_db);
    _current_pan = static_cast<float>(_pan);
    return true;
}

//--- Processing
auto SoundfontPlugin::process(const clap_process* process) noexcept -> clap_process_status {
    const uint32_t nframes = process->frames_count;
    const uint32_t nevents = process->in_events->size(process->in_events);
    uint32_t event_index = 0;
    uint32_t current_frame = 0;

    float* out_l = process->audio_outputs[0].data32[0];
    float* out_r = process->audio_outputs[0].data32[1];

    while (current_frame < nframes) {
        handleEvents(process->in_events, event_index, current_frame);

        uint32_t frames_to_render = nframes - current_frame;
        if (event_index < nevents) {
            const clap_event_header_t* next_event =
                process->in_events->get(process->in_events, event_index);
            if (next_event->time > current_frame) {
                frames_to_render = std::min(
                    frames_to_render, static_cast<uint32_t>(next_event->time - current_frame));
            }
        }

        frames_to_render = std::min(frames_to_render, 32u);

        std::array<float*, 2> block_outputs = {&out_l[current_frame], &out_r[current_frame]};
        _engine.process(block_outputs.data(), frames_to_render);

        for (uint32_t i = 0; i < frames_to_render; ++i) {
            uint32_t f = current_frame + i;
            const float smoothing_coeff = 0.005f;

            auto target_gain = static_cast<float>(_gain_db + _gain_mod * 60.0);
            float target_pan = std::clamp(static_cast<float>(_pan + _pan_mod), -1.0f, 1.0f);

            _current_gain += (target_gain - _current_gain) * smoothing_coeff;
            _current_pan += (target_pan - _current_pan) * smoothing_coeff;

            float gain_lin = std::pow(10.0f, _current_gain / 20.0f);
            float pan_l = std::min(1.0f, 1.0f - _current_pan);
            float pan_r = std::min(1.0f, 1.0f + _current_pan);

            out_l[f] *= gain_lin * pan_l;
            out_r[f] *= gain_lin * pan_r;
        }

        current_frame += frames_to_render;
    }

    return CLAP_PROCESS_CONTINUE;
}

void SoundfontPlugin::handleEvents(const clap_input_events* in, uint32_t& event_index,
                                   uint32_t sample_index) noexcept {
    const uint32_t nevents = in->size(in);
    while (event_index < nevents) {
        const clap_event_header_t* hdr = in->get(in, event_index);
        if (hdr->time > sample_index) break;

        if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID) {
            switch (hdr->type) {
                case CLAP_EVENT_NOTE_ON: {
                    const auto* ev = reinterpret_cast<const clap_event_note_t*>(hdr);
                    if (ev->channel == _current_midi_channel) {
                        _engine.noteOn(ev->channel, ev->key, static_cast<float>(ev->velocity));
                    }
                    break;
                }
                case CLAP_EVENT_NOTE_OFF: {
                    const auto* ev = reinterpret_cast<const clap_event_note_t*>(hdr);
                    if (ev->channel == _current_midi_channel) {
                        _engine.noteOff(ev->channel, ev->key);
                    }
                    break;
                }
                case CLAP_EVENT_PARAM_VALUE: {
                    const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(hdr);
                    switch (ev->param_id) {
                        case kParamPreset:
                            _preset_index = ev->value;
                            _engine.setPreset(_current_midi_channel,
                                              static_cast<int>(_preset_index));
                            break;
                        case kParamGain:
                            _gain_db = ev->value;
                            break;
                        case kParamPan:
                            _pan = ev->value;
                            break;
                        case kParamMidiChannel:
                            _midi_channel = ev->value;
                            _current_midi_channel = static_cast<int>(_midi_channel);
                            // Re-apply preset to the new channel
                            _engine.setPreset(_current_midi_channel,
                                              static_cast<int>(_preset_index));
                            break;
                    }
                    break;
                }
                case CLAP_EVENT_PARAM_MOD: {
                    const auto* ev = reinterpret_cast<const clap_event_param_mod_t*>(hdr);
                    switch (ev->param_id) {
                        case kParamGain:
                            _gain_mod = ev->amount;
                            break;
                        case kParamPan:
                            _pan_mod = ev->amount;
                            break;
                    }
                    break;
                }
                case CLAP_EVENT_MIDI: {
                    const auto* ev = reinterpret_cast<const clap_event_midi_t*>(hdr);
                    uint8_t msg = ev->data[0] & 0xF0;
                    uint8_t chan = ev->data[0] & 0x0F;
                    if (chan == _current_midi_channel) {
                        if (msg == 0xE0) {  // Pitch Bend
                            int pb = ev->data[1] + (ev->data[2] << 7);
                            _engine.setPitchBend(chan, pb);
                        } else if (msg == 0x90) {  // Note On fallback
                            uint8_t key = ev->data[1];
                            uint8_t vel = ev->data[2];
                            if (vel > 0) {
                                _engine.noteOn(chan, key, vel / 127.0f);
                            } else {
                                _engine.noteOff(chan, key);
                            }
                        } else if (msg == 0x80) {  // Note Off fallback
                            _engine.noteOff(chan, ev->data[1]);
                        }
                    }
                    break;
                }
            }
        }
        ++event_index;
    }
}

}  // namespace synth_canvas::soundfont_plugin
