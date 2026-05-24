#include "oscillator-plugin.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace synth_canvas::oscillator_plugin {

static const std::array<const char*, 3> kFeatures = {CLAP_PLUGIN_FEATURE_INSTRUMENT,
                                                     CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr};

static const clap_plugin_descriptor kDesc = {
    .clap_version = CLAP_VERSION,
    .id = "com.synthcanvas.oscillator-plugin",
    .name = "Oscillator Plugin",
    .vendor = "SynthCanvas",
    .url = "https://github.com/SPACEGG/SynthCanvas",
    .manual_url = "",
    .support_url = "",
    .version = "0.1.0",
    .description = "A monophonic/polyphonic oscillator with unison and PWM.",
    .features = kFeatures.data()};

auto OscillatorPlugin::descriptor() -> const clap_plugin_descriptor* { return &kDesc; }

OscillatorPlugin::OscillatorPlugin(const std::string& plugin_path, const clap_host* host)
    : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                            clap::helpers::CheckingLevel::Maximal>(&kDesc, host) {
    for (auto& p : _params) p.store(0.0);
    for (auto& m : _param_mods) m.store(0.0);

    // Initial Defaults
    _params[kParamWaveform].store(2.0);  // Saw
    _params[kParamUnisonCount].store(1.0);
    _params[kParamUnisonDetune].store(0.1);
    _params[kParamPulseWidth].store(0.5);
    _params[kParamPitch].store(0.5);  // 0 semitones
    _params[kParamMode].store(1.0);   // Poly
    _params[kParamSlideTime].store(0.1);
    _params[kParamAttack].store(0.01);
    _params[kParamRelease].store(0.1);
    _params[kParamGain].store(60.0 / 72.0);  // 0 dB
}

auto OscillatorPlugin::activate(double sample_rate, uint32_t min_frames_count,
                                uint32_t max_frames_count) noexcept -> bool {
    _sample_rate = sample_rate;
    for (auto& voice : _voices) {
        voice.active = false;
        voice.env_stage = EnvelopeStage::kIdle;
    }
    return true;
}

auto OscillatorPlugin::audioPortsInfo(uint32_t index, bool is_input,
                                      clap_audio_port_info* info) const noexcept -> bool {
    if (is_input || index != 0) return false;
    info->id = 0;
    snprintf(info->name, sizeof(info->name), "Main Output");
    info->channel_count = 2;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

auto OscillatorPlugin::notePortsInfo(uint32_t index, bool is_input,
                                     clap_note_port_info* info) const noexcept -> bool {
    if (!is_input || index != 0) return false;
    info->id = 0;
    snprintf(info->name, sizeof(info->name), "Note Input");
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    return true;
}

auto OscillatorPlugin::paramsCount() const noexcept -> uint32_t { return kParamCount; }

auto OscillatorPlugin::paramsInfo(uint32_t index, clap_param_info* info) const noexcept -> bool {
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    auto mod = CLAP_PARAM_IS_MODULATABLE;

    switch (index) {
        case kParamWaveform:
            info->id = kParamWaveform;
            snprintf(info->name, sizeof(info->name), "Waveform");
            info->min_value = 0;
            info->max_value = 4;
            info->default_value = 2;
            info->flags |= CLAP_PARAM_IS_STEPPED;
            break;
        case kParamUnisonCount:
            info->id = kParamUnisonCount;
            snprintf(info->name, sizeof(info->name), "Unison Count");
            info->min_value = 1;
            info->max_value = 8;
            info->default_value = 1;
            info->flags |= CLAP_PARAM_IS_STEPPED;
            break;
        case kParamUnisonDetune:
            info->id = kParamUnisonDetune;
            snprintf(info->name, sizeof(info->name), "Unison Detune");
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.1;
            info->flags |= mod;
            break;
        case kParamPulseWidth:
            info->id = kParamPulseWidth;
            snprintf(info->name, sizeof(info->name), "Pulse Width");
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.5;
            info->flags |= mod;
            break;
        case kParamPitch:
            info->id = kParamPitch;
            snprintf(info->name, sizeof(info->name), "Pitch");
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.5;
            info->flags |= mod;
            break;
        case kParamMode:
            info->id = kParamMode;
            snprintf(info->name, sizeof(info->name), "Mode");
            info->min_value = 0;
            info->max_value = 1;
            info->default_value = 1;
            info->flags |= CLAP_PARAM_IS_STEPPED;
            break;
        case kParamSlideTime:
            info->id = kParamSlideTime;
            snprintf(info->name, sizeof(info->name), "Slide Time");
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.1;
            info->flags |= mod;
            break;
        case kParamAttack:
            info->id = kParamAttack;
            snprintf(info->name, sizeof(info->name), "Attack");
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.01;
            info->flags |= mod;
            break;
        case kParamRelease:
            info->id = kParamRelease;
            snprintf(info->name, sizeof(info->name), "Release");
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.1;
            info->flags |= mod;
            break;
        case kParamGain:
            info->id = kParamGain;
            snprintf(info->name, sizeof(info->name), "Gain");
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 60.0 / 72.0;  // 0 dB
            info->flags |= mod;
            break;
        default:
            return false;
    }
    return true;
}

auto OscillatorPlugin::paramsValue(clap_id param_id, double* value) noexcept -> bool {
    if (param_id >= kParamCount) return false;
    *value = _params[param_id].load();
    return true;
}

auto OscillatorPlugin::paramsValueToText(clap_id param_id, double value, char* display,
                                         uint32_t size) noexcept -> bool {
    std::stringstream ss;
    switch (param_id) {
        case kParamWaveform: {
            std::array<const char*, 5> names = {"Sine", "Triangle", "Saw", "Square", "Noise"};
            int idx = std::clamp(static_cast<int>(value), 0, 4);
            ss << names[idx];
            break;
        }
        case kParamUnisonCount:
            ss << static_cast<int>(value);
            break;
        case kParamUnisonDetune:
            ss << std::fixed << std::setprecision(1) << (value * 100.0) << " cents";
            break;
        case kParamPulseWidth:
            ss << std::fixed << std::setprecision(1) << (value * 100.0) << " %";
            break;
        case kParamPitch:
            ss << std::fixed << std::setprecision(1) << (-24.0 + value * 48.0) << " st";
            break;
        case kParamMode:
            ss << (value > 0.5 ? "Poly" : "Mono");
            break;
        case kParamSlideTime:
            ss << std::fixed << std::setprecision(1) << (0.1 * std::pow(20000.0, value)) << " ms";
            break;
        case kParamAttack:
        case kParamRelease:
            ss << std::fixed << std::setprecision(1) << (0.1 * std::pow(100000.0, value)) << " ms";
            break;
        case kParamGain:
            ss << std::fixed << std::setprecision(1) << (-60.0 + value * 72.0) << " dB";
            break;
        default:
            return false;
    }
    strncpy(display, ss.str().c_str(), size - 1);
    display[size - 1] = '\0';
    return true;
}

auto OscillatorPlugin::paramsTextToValue(clap_id param_id, const char* display,
                                         double* value) noexcept -> bool {
    char* end;
    double parsed = std::strtod(display, &end);
    if (end == display) return false;

    switch (param_id) {
        case kParamUnisonDetune:
            *value = std::clamp(parsed / 100.0, 0.0, 1.0);
            break;
        case kParamPulseWidth:
            *value = std::clamp(parsed / 100.0, 0.0, 1.0);
            break;
        case kParamPitch:
            *value = std::clamp((parsed + 24.0) / 48.0, 0.0, 1.0);
            break;
        case kParamSlideTime:
            *value = std::clamp(std::log10(parsed / 0.1) / std::log10(20000.0), 0.0, 1.0);
            break;
        case kParamAttack:
        case kParamRelease:
            *value = std::clamp(std::log10(parsed / 0.1) / std::log10(100000.0), 0.0, 1.0);
            break;
        case kParamGain:
            *value = std::clamp((parsed + 60.0) / 72.0, 0.0, 1.0);
            break;
        default:
            *value = parsed;
            break;
    }
    return true;
}

void OscillatorPlugin::paramsFlush(const clap_input_events* in,
                                   const clap_output_events* out) noexcept {
    uint32_t size = in->size(in);
    for (uint32_t i = 0; i < size; ++i) {
        const clap_event_header_t* hdr = in->get(in, i);
        if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
            auto* ev = reinterpret_cast<const clap_event_param_value*>(hdr);
            if (ev->param_id < kParamCount) _params[ev->param_id].store(ev->value);
        } else if (hdr->type == CLAP_EVENT_PARAM_MOD) {
            auto* ev = reinterpret_cast<const clap_event_param_mod*>(hdr);
            if (ev->param_id < kParamCount) _param_mods[ev->param_id].store(ev->amount);
        }
    }
}

auto OscillatorPlugin::stateSave(const clap_ostream* os) noexcept -> bool {
    std::ostringstream ss;
    for (int i = 0; i < kParamCount; ++i) {
        ss << i << "=" << _params[i].load() << ";";
    }
    std::string s = ss.str();
    return os->write(os, s.c_str(), s.size()) == static_cast<int64_t>(s.size());
}

auto OscillatorPlugin::stateLoad(const clap_istream* is) noexcept -> bool {
    std::array<char, 1024> buf;
    int64_t rd = is->read(is, buf.data(), buf.size() - 1);
    if (rd <= 0) return false;
    buf[rd] = '\0';
    std::string s(buf.data());
    std::stringstream ss(s);
    std::string pair;
    while (std::getline(ss, pair, ';')) {
        size_t pos = pair.find('=');
        if (pos == std::string::npos) continue;
        int id = std::stoi(pair.substr(0, pos));
        double val = std::stod(pair.substr(pos + 1));
        if (id < kParamCount) _params[id].store(val);
    }
    return true;
}

auto OscillatorPlugin::process(const clap_process* process) noexcept -> clap_process_status {
    const uint32_t nframes = process->frames_count;
    uint32_t ev_idx = 0;

    float* out_l = process->audio_outputs[0].data32[0];
    float* out_r = process->audio_outputs[0].data32[1];

    for (uint32_t i = 0; i < nframes; ++i) {
        handleEvents(process->in_events, ev_idx, i);

        out_l[i] = 0.0f;
        out_r[i] = 0.0f;

        int waveform = static_cast<int>(_params[kParamWaveform].load());
        int uni_count = static_cast<int>(_params[kParamUnisonCount].load());
        double uni_detune =
            std::clamp(_params[kParamUnisonDetune].load() + _param_mods[kParamUnisonDetune].load(),
                       0.0, 1.0) *
            100.0;
        double pw = std::clamp(
            _params[kParamPulseWidth].load() + _param_mods[kParamPulseWidth].load(), 0.0, 1.0);
        double pitch_offset =
            (-24.0 +
             std::clamp(_params[kParamPitch].load() + _param_mods[kParamPitch].load(), 0.0, 1.0) *
                 48.0);
        bool poly = _params[kParamMode].load() > 0.5;
        double slide_time_ms =
            0.1 * std::pow(20000.0, std::clamp(_params[kParamSlideTime].load() +
                                                   _param_mods[kParamSlideTime].load(),
                                               0.0, 1.0));

        double total_pitch_offset = pitch_offset + (_pitch_bend * 2.0);

        // Calculate output gain multiplier
        double gain_norm =
            std::clamp(_params[kParamGain].load() + _param_mods[kParamGain].load(), 0.0, 1.0);
        double gain_db = -60.0 + gain_norm * 72.0;
        auto gain_multiplier = static_cast<float>(std::pow(10.0, gain_db / 20.0));

        for (auto& voice : _voices) {
            if (!voice.active) continue;

            double target_key = voice.target_freq_key + total_pitch_offset;
            if (!poly) {
                double slide_alpha =
                    1.0 - std::exp(-1.0 / (_sample_rate * (std::max(0.1, slide_time_ms) * 0.001)));
                voice.current_freq_key += (target_key - voice.current_freq_key) * slide_alpha;
            } else {
                voice.current_freq_key = target_key;
            }

            double hz = 440.0 * std::pow(2.0, (voice.current_freq_key - 69.0) / 12.0);
            float v_l, v_r;
            voice.processSample(waveform, uni_count, uni_detune, pw, hz, _sample_rate, v_l, v_r);

            // Sum voices and apply final gain
            out_l[i] += v_l * gain_multiplier;
            out_r[i] += v_r * gain_multiplier;
        }
    }

    return CLAP_PROCESS_CONTINUE;
}

void OscillatorPlugin::handleEvents(const clap_input_events* in, uint32_t& event_index,
                                    uint32_t sample_index) noexcept {
    uint32_t count = in->size(in);
    while (event_index < count) {
        const clap_event_header_t* hdr = in->get(in, event_index);
        if (hdr->time > sample_index) break;

        if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID) {
            switch (hdr->type) {
                case CLAP_EVENT_NOTE_ON: {
                    auto* ev = reinterpret_cast<const clap_event_note*>(hdr);
                    triggerNoteOn(ev->port_index, ev->channel, ev->key, ev->note_id, ev->velocity);
                    break;
                }
                case CLAP_EVENT_NOTE_OFF: {
                    auto* ev = reinterpret_cast<const clap_event_note*>(hdr);
                    triggerNoteOff(ev->port_index, ev->channel, ev->key, ev->note_id);
                    break;
                }
                case CLAP_EVENT_PARAM_VALUE: {
                    auto* ev = reinterpret_cast<const clap_event_param_value*>(hdr);
                    if (ev->param_id < kParamCount) _params[ev->param_id].store(ev->value);
                    break;
                }
                case CLAP_EVENT_PARAM_MOD: {
                    auto* ev = reinterpret_cast<const clap_event_param_mod*>(hdr);
                    if (ev->param_id < kParamCount) _param_mods[ev->param_id].store(ev->amount);
                    break;
                }
                case CLAP_EVENT_MIDI: {
                    auto* ev = reinterpret_cast<const clap_event_midi*>(hdr);
                    if ((ev->data[0] & 0xF0) == 0xE0) {
                        _pitch_bend =
                            (static_cast<double>(ev->data[1] | (ev->data[2] << 7)) - 8192.0) /
                            8192.0;
                    }
                    break;
                }
            }
        }
        event_index++;
    }
}

void OscillatorPlugin::triggerNoteOn(int16_t port, int16_t channel, int16_t key, int32_t note_id,
                                     double velocity) {
    bool poly = _params[kParamMode].load() > 0.5;
    double attack_ms = 0.1 * std::pow(100000.0, _params[kParamAttack].load());

    if (poly) {
        auto* voice = findFreeVoice();
        if (voice) {
            voice->start(key, note_id, static_cast<double>(key), attack_ms, _sample_rate);
            voice->last_active_time = ++_voice_counter;
        }
    } else {
        auto& voice = _voices[0];
        if (!voice.active) {
            voice.start(key, note_id, static_cast<double>(key), attack_ms, _sample_rate);
        } else {
            voice.target_freq_key = static_cast<double>(key);
            voice.key = key;
            voice.note_id = note_id;
        }
        _mono_last_key = key;
        _mono_last_note_id = note_id;
    }
}

void OscillatorPlugin::triggerNoteOff(int16_t port, int16_t channel, int16_t key, int32_t note_id) {
    bool poly = _params[kParamMode].load() > 0.5;
    double release_ms = 0.1 * std::pow(100000.0, _params[kParamRelease].load());

    if (poly) {
        for (auto& voice : _voices) {
            if (voice.active && voice.key == key && (note_id == -1 || voice.note_id == note_id)) {
                voice.release(release_ms, _sample_rate);
            }
        }
    } else {
        if (_mono_last_key == key) {
            _voices[0].release(release_ms, _sample_rate);
            _mono_last_key = -1;
        }
    }
}

auto OscillatorPlugin::findFreeVoice() -> OscVoice* {
    for (auto& voice : _voices) {
        if (!voice.active) return &voice;
    }
    OscVoice* oldest = nullptr;
    uint32_t min_time = 0xFFFFFFFF;
    for (auto& voice : _voices) {
        if (voice.last_active_time < min_time) {
            min_time = voice.last_active_time;
            oldest = &voice;
        }
    }
    return oldest;
}

}  // namespace synth_canvas::oscillator_plugin
