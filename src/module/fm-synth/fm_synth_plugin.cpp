#include "fm_synth_plugin.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <string>
#include <utility>

namespace synth_canvas::fm_synth {

static const std::array<const char*, 3> kFeatures = {CLAP_PLUGIN_FEATURE_INSTRUMENT, "synthesizer",
                                                     nullptr};

static const clap_plugin_descriptor kDesc = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "com.synthcanvas.fm-synth",
    .name = "FM Synth",
    .vendor = "SynthCanvas",
    .url = "https://github.com/SPACEGG/SynthCanvas",
    .manual_url = "",
    .support_url = "",
    .version = "0.1.0",
    .description = "A 6-operator FM synthesizer with 32 DX7 algorithms.",
    .features = kFeatures.data()};

auto FmSynthPlugin::descriptor() -> const clap_plugin_descriptor* { return &kDesc; }

FmSynthPlugin::FmSynthPlugin(const std::string& plugin_path, const clap_host* host)
    : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                            clap::helpers::CheckingLevel::Maximal>(&kDesc, host) {}

auto FmSynthPlugin::activate(double sample_rate, uint32_t min_frames_count,
                             uint32_t max_frames_count) noexcept -> bool {
    if (!Plugin::activate(sample_rate, min_frames_count, max_frames_count)) return false;
    _sample_rate = sample_rate;
    for (auto& v : _voices) {
        v.active = false;
        for (int i = 1; i <= kNumOperators; ++i) {
            v.env[i].reset();
        }
    }
    _voice_counter = 0;
    return true;
}

// --- Audio Ports ---
auto FmSynthPlugin::audioPortsCount(bool is_input) const noexcept -> uint32_t {
    return is_input ? 0 : 1;
}

auto FmSynthPlugin::audioPortsInfo(uint32_t index, bool is_input,
                                   clap_audio_port_info* info) const noexcept -> bool {
    if (is_input || index != 0) return false;
    info->id = 0;
    snprintf(info->name, sizeof(info->name), "audio_out");
    info->channel_count = 2;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

// --- Note Ports ---
auto FmSynthPlugin::notePortsCount(bool is_input) const noexcept -> uint32_t {
    return is_input ? 1 : 0;
}

auto FmSynthPlugin::notePortsInfo(uint32_t index, bool is_input,
                                  clap_note_port_info* info) const noexcept -> bool {
    if (!is_input || index != 0) return false;
    info->id = 0;
    strncpy(info->name, "Main", sizeof(info->name));
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    return true;
}

// --- Parameter helpers ---
static auto coarseToMultiplier(double coarse_val) -> double {
    int c = static_cast<int>(std::round(coarse_val));
    if (c <= 1) return 0.5;
    return static_cast<double>(c - 1);
}

static auto fineToOffset(double fine_val) -> double { return 0.99 * fine_val; }

static auto detuneToHz(double detune_val) -> double {
    return std::round(detune_val) * kDetuneConstantHz;
}

static auto envTimeToSeconds(double normalized) -> double {
    return normalized * kMaxEnvTimeSeconds;
}

// --- Parameters ---
auto FmSynthPlugin::paramsCount() const noexcept -> uint32_t { return kTotalParams; }

auto FmSynthPlugin::paramsInfo(uint32_t index, clap_param_info* info) const noexcept -> bool {
    if (index >= static_cast<uint32_t>(kTotalParams)) return false;

    // Global params
    if (index == 0) {
        info->id = kParamAlgorithm;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
        snprintf(info->name, sizeof(info->name), "Algorithm");
        snprintf(info->module, sizeof(info->module), "Global");
        info->min_value = 1.0;
        info->max_value = 32.0;
        info->default_value = 1.0;
        return true;
    }
    if (index == 1) {
        info->id = kParamFeedback;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        snprintf(info->name, sizeof(info->name), "Feedback");
        snprintf(info->module, sizeof(info->module), "Global");
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    }
    if (index == 2) {
        info->id = kParamVolume;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        snprintf(info->name, sizeof(info->name), "Volume");
        snprintf(info->module, sizeof(info->module), "Global");
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.8;
        return true;
    }

    // Per-operator params
    uint32_t op_param_index = index - 3;
    int op_num = static_cast<int>(op_param_index / kParamsPerOp) + 1;
    int offset = static_cast<int>(op_param_index % kParamsPerOp);

    if (op_num < 1 || op_num > kNumOperators) return false;

    info->id = opParamId(op_num, static_cast<OpParamOffset>(offset));

    snprintf(info->module, sizeof(info->module), "Op %d", op_num);

    switch (static_cast<OpParamOffset>(offset)) {
        case kOpLevel:
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            snprintf(info->name, sizeof(info->name), "Op%d Level", op_num);
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 1.0;
            break;
        case kOpCoarse:
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
            snprintf(info->name, sizeof(info->name), "Op%d Coarse", op_num);
            info->min_value = 1.0;
            info->max_value = 32.0;
            info->default_value = 2.0;
            break;
        case kOpFine:
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            snprintf(info->name, sizeof(info->name), "Op%d Fine", op_num);
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.0;
            break;
        case kOpDetune:
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
            snprintf(info->name, sizeof(info->name), "Op%d Detune", op_num);
            info->min_value = -7.0;
            info->max_value = 7.0;
            info->default_value = 0.0;
            break;
        case kOpAttack:
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            snprintf(info->name, sizeof(info->name), "Op%d Attack", op_num);
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.01;
            break;
        case kOpDecay:
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            snprintf(info->name, sizeof(info->name), "Op%d Decay", op_num);
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.3;
            break;
        case kOpSustain:
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            snprintf(info->name, sizeof(info->name), "Op%d Sustain", op_num);
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.7;
            break;
        case kOpRelease:
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            snprintf(info->name, sizeof(info->name), "Op%d Release", op_num);
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.3;
            break;
    }
    return true;
}

auto FmSynthPlugin::getParamValue(clap_id param_id) const -> double {
    if (param_id == kParamAlgorithm) return _algorithm;
    if (param_id == kParamFeedback) return _feedback;
    if (param_id == kParamVolume) return _volume;

    if (param_id >= kOpParamBase && param_id < kOpParamBase + kNumOperators * kParamsPerOp) {
        int relative = static_cast<int>(param_id - kOpParamBase);
        int op_idx = relative / kParamsPerOp;
        int offset = relative % kParamsPerOp;
        const auto& op = _op_params[op_idx];
        switch (static_cast<OpParamOffset>(offset)) {
            case kOpLevel:
                return op.level;
            case kOpCoarse:
                return op.coarse;
            case kOpFine:
                return op.fine;
            case kOpDetune:
                return op.detune;
            case kOpAttack:
                return op.attack;
            case kOpDecay:
                return op.decay;
            case kOpSustain:
                return op.sustain;
            case kOpRelease:
                return op.release;
        }
    }
    return 0.0;
}

void FmSynthPlugin::setParamValue(clap_id param_id, double value) noexcept {
    if (param_id == kParamAlgorithm) {
        _algorithm = std::clamp(value, 1.0, 32.0);
        return;
    }
    if (param_id == kParamFeedback) {
        _feedback = std::clamp(value, 0.0, 1.0);
        return;
    }
    if (param_id == kParamVolume) {
        _volume = std::clamp(value, 0.0, 1.0);
        return;
    }

    if (param_id >= kOpParamBase && param_id < kOpParamBase + kNumOperators * kParamsPerOp) {
        int relative = static_cast<int>(param_id - kOpParamBase);
        int op_idx = relative / kParamsPerOp;
        int offset = relative % kParamsPerOp;
        auto& op = _op_params[op_idx];
        switch (static_cast<OpParamOffset>(offset)) {
            case kOpLevel:
                op.level = std::clamp(value, 0.0, 1.0);
                break;
            case kOpCoarse:
                op.coarse = std::clamp(std::round(value), 1.0, 32.0);
                break;
            case kOpFine:
                op.fine = std::clamp(value, 0.0, 1.0);
                break;
            case kOpDetune:
                op.detune = std::clamp(std::round(value), -7.0, 7.0);
                break;
            case kOpAttack:
                op.attack = std::clamp(value, 0.0, 1.0);
                break;
            case kOpDecay:
                op.decay = std::clamp(value, 0.0, 1.0);
                break;
            case kOpSustain:
                op.sustain = std::clamp(value, 0.0, 1.0);
                break;
            case kOpRelease:
                op.release = std::clamp(value, 0.0, 1.0);
                break;
        }
    }
}

auto FmSynthPlugin::paramsValue(clap_id param_id, double* value) noexcept -> bool {
    *value = getParamValue(param_id);
    return true;
}

auto FmSynthPlugin::paramsValueToText(clap_id param_id, double value, char* display,
                                      uint32_t size) noexcept -> bool {
    std::stringstream ss;
    if (param_id == kParamAlgorithm) {
        ss << static_cast<int>(std::round(value));
    } else if (param_id == kParamFeedback || param_id == kParamVolume) {
        ss << std::fixed << std::setprecision(1) << value * 100.0 << "%";
    } else if (param_id >= kOpParamBase) {
        int relative = static_cast<int>(param_id - kOpParamBase);
        int offset = relative % kParamsPerOp;
        switch (static_cast<OpParamOffset>(offset)) {
            case kOpLevel:
            case kOpSustain:
                ss << std::fixed << std::setprecision(1) << value * 100.0 << "%";
                break;
            case kOpCoarse:
                ss << std::fixed << std::setprecision(1) << coarseToMultiplier(value) << "x";
                break;
            case kOpFine:
                ss << "+" << std::fixed << std::setprecision(2) << fineToOffset(value);
                break;
            case kOpDetune:
                ss << std::showpos << static_cast<int>(std::round(value));
                break;
            case kOpAttack:
            case kOpDecay:
            case kOpRelease: {
                double seconds = envTimeToSeconds(value);
                if (seconds < 1.0) {
                    ss << std::fixed << std::setprecision(0) << seconds * 1000.0 << " ms";
                } else {
                    ss << std::fixed << std::setprecision(2) << seconds << " s";
                }
                break;
            }
        }
    } else {
        return false;
    }
    strncpy(display, ss.str().c_str(), size - 1);
    display[size - 1] = '\0';
    return true;
}

auto FmSynthPlugin::paramsTextToValue(clap_id param_id, const char* display, double* value) noexcept
    -> bool {
    char* end;
    double parsed = strtod(display, &end);
    if (end == display) return false;
    *value = parsed;
    return true;
}

void FmSynthPlugin::paramsFlush(const clap_input_events* in,
                                const clap_output_events* out) noexcept {
    uint32_t event_count = in->size(in);
    for (uint32_t i = 0; i < event_count; ++i) {
        const clap_event_header* header = in->get(in, i);
        handleEvent(header);
    }
}

// --- State ---
auto FmSynthPlugin::stateSave(const clap_ostream* os) noexcept -> bool {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "algorithm=" << _algorithm << ";";
    oss << "feedback=" << _feedback << ";";
    oss << "volume=" << _volume << ";";

    for (int i = 0; i < kNumOperators; ++i) {
        const auto& op = _op_params[i];
        std::string prefix = "op" + std::to_string(i + 1) + "_";
        oss << prefix << "level=" << op.level << ";";
        oss << prefix << "coarse=" << op.coarse << ";";
        oss << prefix << "fine=" << op.fine << ";";
        oss << prefix << "detune=" << op.detune << ";";
        oss << prefix << "attack=" << op.attack << ";";
        oss << prefix << "decay=" << op.decay << ";";
        oss << prefix << "sustain=" << op.sustain << ";";
        oss << prefix << "release=" << op.release << ";";
    }

    std::string s = oss.str();
    int64_t result = os->write(os, s.c_str(), s.size());
    return std::cmp_equal(result, s.size());
}

auto FmSynthPlugin::stateLoad(const clap_istream* is) noexcept -> bool {
    std::array<char, 8192> buffer;
    int64_t result = is->read(is, buffer.data(), buffer.size() - 1);
    if (result <= 0) return false;
    buffer[result] = '\0';

    std::string s(buffer.data());
    std::string pair;
    std::stringstream ss(s);
    while (std::getline(ss, pair, ';')) {
        size_t pos = pair.find('=');
        if (pos == std::string::npos) continue;
        std::string key = pair.substr(0, pos);
        std::string val_str = pair.substr(pos + 1);
        try {
            double val = std::stod(val_str);
            if (key == "algorithm") {
                _algorithm = val;
            } else if (key == "feedback") {
                _feedback = val;
            } else if (key == "volume") {
                _volume = val;
            } else {
                for (int i = 0; i < kNumOperators; ++i) {
                    std::string prefix = "op" + std::to_string(i + 1) + "_";
                    auto& op = _op_params[i];
                    if (key == prefix + "level") {
                        op.level = val;
                    } else if (key == prefix + "coarse") {
                        op.coarse = val;
                    } else if (key == prefix + "fine") {
                        op.fine = val;
                    } else if (key == prefix + "detune") {
                        op.detune = val;
                    } else if (key == prefix + "attack") {
                        op.attack = val;
                    } else if (key == prefix + "decay") {
                        op.decay = val;
                    } else if (key == prefix + "sustain") {
                        op.sustain = val;
                    } else if (key == prefix + "release") {
                        op.release = val;
                    }
                }
            }
        } catch (...) {
        }
    }
    return true;
}

// --- Event handling ---
void FmSynthPlugin::handleEvent(const clap_event_header* header) noexcept {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) return;

    if (header->type == CLAP_EVENT_NOTE_ON) {
        const auto* ev = reinterpret_cast<const clap_event_note*>(header);
        auto* voice = allocateVoice();
        if (voice) {
            double midi_freq = 440.0 * std::pow(2.0, (ev->key - 69.0) / 12.0);

            // Configure per-op ADSR from params
            for (int i = 1; i <= kNumOperators; ++i) {
                const auto& op = _op_params[i - 1];
                voice->env[i].attack_time = envTimeToSeconds(op.attack);
                voice->env[i].decay_time = envTimeToSeconds(op.decay);
                voice->env[i].sustain_level = op.sustain;
                voice->env[i].release_time = envTimeToSeconds(op.release);

                double coarse_mult = coarseToMultiplier(op.coarse);
                double fine_off = fineToOffset(op.fine);
                double detune_hz = detuneToHz(op.detune);
                voice->op_freq[i] = midi_freq * (coarse_mult + fine_off) + detune_hz;
            }

            voice->start(ev->key, ev->note_id, ev->velocity, _voice_counter++);
        }
    } else if (header->type == CLAP_EVENT_NOTE_OFF) {
        const auto* ev = reinterpret_cast<const clap_event_note*>(header);
        for (auto& v : _voices) {
            if (v.active && v.key == ev->key && (ev->note_id == -1 || v.note_id == ev->note_id)) {
                v.release();
            }
        }
    } else if (header->type == CLAP_EVENT_PARAM_VALUE) {
        const auto* ev = reinterpret_cast<const clap_event_param_value*>(header);
        handleParamValue(ev);
    }
}

void FmSynthPlugin::handleParamValue(const clap_event_param_value* ev) noexcept {
    setParamValue(ev->param_id, ev->value);
}

auto FmSynthPlugin::allocateVoice() noexcept -> FmVoice* {
    // Find idle voice
    for (auto& v : _voices) {
        if (!v.active) return &v;
    }
    // Steal oldest
    FmVoice* oldest = &_voices[0];
    for (auto& v : _voices) {
        if (v.start_order < oldest->start_order) {
            oldest = &v;
        }
    }
    return oldest;
}

// --- Render ---
auto FmSynthPlugin::renderVoice(FmVoice& voice) noexcept -> double {
    int alg_index = static_cast<int>(std::round(_algorithm)) - 1;
    alg_index = std::clamp(alg_index, 0, kNumAlgorithms - 1);
    const auto& alg = kAlgorithms[alg_index];

    // Compute feedback signal for the designated feedback operator
    double beta = _feedback * kMaxFeedbackIndex;
    double fb_sig = beta * (voice.fb_history[0] + voice.fb_history[1]) * 0.5;

    // Render operators in reverse order (6→1) to resolve dependencies
    // op_out stores scaled outputs used for modulation
    std::array<double, 7> op_out{};
    std::array<double, 7> op_raw{};

    for (int op = kNumOperators; op >= 1; --op) {
        int op_idx = op - 1;
        const auto& params = _op_params[op_idx];

        double env_level = voice.env[op].process(_sample_rate);

        // Sum modulation input from other operators
        double mod_input = 0.0;
        uint8_t sources = alg.mod_sources[op];
        for (int src = 1; src <= kNumOperators; ++src) {
            if (sources & (1 << (src - 1))) {
                mod_input += op_out[src];
            }
        }

        // Add feedback if this is the feedback destination operator
        if (static_cast<uint8_t>(op) == alg.feedback_dst) {
            mod_input += fb_sig;
        }

        // Phase accumulation
        double phase_inc = 2.0 * std::numbers::pi * voice.op_freq[op] / _sample_rate;
        voice.phase[op] += phase_inc;

        // Compute operator output
        double raw = std::sin(voice.phase[op] + mod_input);
        op_raw[op] = raw * env_level;

        // Non-linear modulation index scaling for modulator output
        // Level maps quadratically to modulation index
        double mod_index = params.level * params.level * kMaxModIndex;
        op_out[op] = raw * env_level * mod_index;
    }

    // Update feedback history for the feedback source operator
    voice.fb_history[1] = voice.fb_history[0];
    voice.fb_history[0] = op_raw[alg.feedback_src];

    // Sum carrier outputs
    double output = 0.0;
    for (int op = 1; op <= kNumOperators; ++op) {
        if (alg.carrier_mask & (1 << (op - 1))) {
            int op_idx = op - 1;
            output += op_raw[op] * _op_params[op_idx].level;
        }
    }

    return output;
}

// --- Process ---
auto FmSynthPlugin::process(const clap_process* process) noexcept -> clap_process_status {
    const uint32_t nframes = process->frames_count;
    uint32_t ev_index = 0;
    const uint32_t nev = process->in_events->size(process->in_events);

    float* out_l = process->audio_outputs[0].data32[0];
    float* out_r = process->audio_outputs[0].data32[1];

    for (uint32_t i = 0; i < nframes; ++i) {
        // Handle events at this sample
        while (ev_index < nev) {
            const clap_event_header* hdr = process->in_events->get(process->in_events, ev_index);
            if (hdr->time > i) break;
            handleEvent(hdr);
            ++ev_index;
        }

        // Render all active voices
        double mix = 0.0;
        for (auto& voice : _voices) {
            if (!voice.active) continue;

            mix += renderVoice(voice) * voice.velocity;

            if (voice.isFinished()) {
                voice.active = false;
            }
        }

        // Apply volume and write output
        auto sample = static_cast<float>(mix * _volume);
        out_l[i] = sample;
        out_r[i] = sample;
    }

    return CLAP_PROCESS_CONTINUE;
}

}  // namespace synth_canvas::fm_synth
