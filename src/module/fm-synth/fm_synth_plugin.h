#pragma once

#include <array>
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>

namespace synth_canvas::fm_synth {

static constexpr int kNumOperators = 6;
static constexpr int kMaxVoices = 32;
static constexpr int kNumAlgorithms = 32;
static constexpr double kMaxModIndex = 8.0 * std::numbers::pi;
static constexpr double kMaxFeedbackIndex = 8.0 * std::numbers::pi;
static constexpr double kMaxEnvTimeSeconds = 10.0;
static constexpr double kDetuneConstantHz = 0.05;

// ADSR envelope with linear attack, exponential decay/release
struct AdsrEnvelope {
    enum Stage { kIdle, kAttack, kDecay, kSustain, kRelease };

    Stage stage = kIdle;
    double level = 0.0;

    double attack_time = 0.01;
    double decay_time = 0.01;
    double sustain_level = 0.75;
    double release_time = 0.01;

    void gateOn() {
        stage = kAttack;
        level = 0.0;
    }

    void gateOff() {
        if (stage != kIdle) stage = kRelease;
    }

    auto process(double sample_rate) -> double {
        switch (stage) {
            case kAttack: {
                double attack_samples = attack_time * sample_rate;
                if (attack_samples < 1.0) attack_samples = 1.0;
                level += 1.0 / attack_samples;
                if (level >= 1.0) {
                    level = 1.0;
                    stage = kDecay;
                }
                break;
            }
            case kDecay: {
                double decay_samples = decay_time * sample_rate;
                if (decay_samples < 1.0) decay_samples = 1.0;
                double coeff = std::exp(-4.0 / decay_samples);
                level = sustain_level + (level - sustain_level) * coeff;
                if (std::abs(level - sustain_level) < 1e-6) {
                    level = sustain_level;
                    stage = kSustain;
                }
                break;
            }
            case kSustain:
                level = sustain_level;
                break;
            case kRelease: {
                double release_samples = release_time * sample_rate;
                if (release_samples < 1.0) release_samples = 1.0;
                double coeff = std::exp(-4.0 / release_samples);
                level *= coeff;
                if (level < 1e-6) {
                    level = 0.0;
                    stage = kIdle;
                }
                break;
            }
            case kIdle:
                level = 0.0;
                break;
        }
        return level;
    }

    void reset() {
        stage = kIdle;
        level = 0.0;
    }
};

// Routing config for one DX7 algorithm
struct AlgorithmConfig {
    uint8_t feedback_src;
    uint8_t feedback_dst;
    uint8_t carrier_mask;
    // mod_sources[i] = bitmask of operators modulating op i (1-indexed, index 0 unused)
    // bit 0 = op1, bit 1 = op2, ..., bit 5 = op6
    std::array<uint8_t, 7> mod_sources;
};

// DX7 algorithm table (1-indexed via kAlgorithms[alg-1])
// Bitmask helper: op N sets bit (N-1)
static constexpr uint8_t kOP1 = 1 << 0;
static constexpr uint8_t kOP2 = 1 << 1;
static constexpr uint8_t kOP3 = 1 << 2;
static constexpr uint8_t kOP4 = 1 << 3;
static constexpr uint8_t kOP5 = 1 << 4;
static constexpr uint8_t kOP6 = 1 << 5;

// clang-format off
static constexpr std::array<AlgorithmConfig, kNumAlgorithms> kAlgorithms = {{
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP3, .mod_sources={0, kOP2, 0, kOP4, kOP5, kOP6, 0}},
    {.feedback_src=2, .feedback_dst=2, .carrier_mask=kOP1|kOP3, .mod_sources={0, kOP2, 0, kOP4, kOP5, kOP6, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP4, .mod_sources={0, kOP2, kOP3, 0, kOP5, kOP6, 0}},
    {.feedback_src=4, .feedback_dst=6, .carrier_mask=kOP1|kOP4, .mod_sources={0, kOP2, kOP3, 0, kOP5, kOP6, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP3|kOP5, .mod_sources={0, kOP2, 0, kOP4, 0, kOP6, 0}},
    {.feedback_src=5, .feedback_dst=6, .carrier_mask=kOP1|kOP3|kOP5, .mod_sources={0, kOP2, 0, kOP4, 0, kOP6, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP3, .mod_sources={0, kOP2, 0, kOP4|kOP5, 0, kOP6, 0}},
    {.feedback_src=4, .feedback_dst=4, .carrier_mask=kOP1|kOP3, .mod_sources={0, kOP2, 0, kOP4|kOP5, 0, kOP6, 0}},
    {.feedback_src=2, .feedback_dst=2, .carrier_mask=kOP1|kOP3, .mod_sources={0, kOP2, 0, kOP4|kOP5, 0, kOP6, 0}},
    {.feedback_src=3, .feedback_dst=3, .carrier_mask=kOP1|kOP4, .mod_sources={0, kOP2, kOP3, 0, kOP5|kOP6, 0, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP4, .mod_sources={0, kOP2, kOP3, 0, kOP5|kOP6, 0, 0}},
    {.feedback_src=2, .feedback_dst=2, .carrier_mask=kOP1|kOP3, .mod_sources={0, kOP2, 0, kOP4|kOP5|kOP6, 0, 0, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP3, .mod_sources={0, kOP2, 0, kOP4|kOP5|kOP6, 0, 0, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP3, .mod_sources={0, kOP2, 0, kOP4, kOP5|kOP6, 0, 0}},
    {.feedback_src=2, .feedback_dst=2, .carrier_mask=kOP1|kOP3, .mod_sources={0, kOP2, 0, kOP4, kOP5|kOP6, 0, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1, .mod_sources={0, kOP2|kOP3|kOP5, 0, kOP4, 0, kOP6, 0}},
    {.feedback_src=2, .feedback_dst=2, .carrier_mask=kOP1, .mod_sources={0, kOP2|kOP3|kOP5, 0, kOP4, 0, kOP6, 0}},
    {.feedback_src=3, .feedback_dst=3, .carrier_mask=kOP1, .mod_sources={0, kOP2|kOP3|kOP4, 0, 0, kOP5, kOP6, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP4|kOP5, .mod_sources={0, kOP2, kOP3, 0, kOP6, kOP6, 0}},
    {.feedback_src=3, .feedback_dst=3, .carrier_mask=kOP1|kOP2|kOP4, .mod_sources={0, kOP3, kOP3, 0, kOP5|kOP6, 0, 0}},
    {.feedback_src=3, .feedback_dst=3, .carrier_mask=kOP1|kOP2|kOP4|kOP5, .mod_sources={0, kOP3, kOP3, 0, kOP6, kOP6, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP3|kOP4|kOP5, .mod_sources={0, kOP2, 0, kOP6, kOP6, kOP6, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP2|kOP4|kOP5, .mod_sources={0, 0, kOP3, 0, kOP6, kOP6, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP2|kOP3|kOP4|kOP5, .mod_sources={0, 0, 0, kOP6, kOP6, kOP6, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP2|kOP3|kOP4|kOP5, .mod_sources={0, 0, 0, 0, kOP6, kOP6, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP2|kOP4, .mod_sources={0, 0, kOP3, 0, kOP5|kOP6, 0, 0}},
    {.feedback_src=3, .feedback_dst=3, .carrier_mask=kOP1|kOP2|kOP4, .mod_sources={0, 0, kOP3, 0, kOP5|kOP6, 0, 0}},
    {.feedback_src=5, .feedback_dst=5, .carrier_mask=kOP1|kOP3|kOP6, .mod_sources={0, kOP2, 0, kOP4, kOP5, 0, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP2|kOP3|kOP5, .mod_sources={0, 0, 0, kOP4, 0, kOP6, 0}},
    {.feedback_src=5, .feedback_dst=5, .carrier_mask=kOP1|kOP2|kOP3|kOP6, .mod_sources={0, 0, 0, kOP4, kOP5, 0, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP2|kOP3|kOP4|kOP5, .mod_sources={0, 0, 0, 0, 0, kOP6, 0}},
    {.feedback_src=6, .feedback_dst=6, .carrier_mask=kOP1|kOP2|kOP3|kOP4|kOP5|kOP6, .mod_sources={0, 0, 0, 0, 0, 0, 0}},
}};
// clang-format on

struct FmVoice {
    bool active = false;
    int16_t key = -1;
    int32_t note_id = -1;
    double velocity = 0.0;
    uint64_t start_order = 0;

    std::array<double, 7> phase{};      // 1-indexed, [0] unused
    std::array<AdsrEnvelope, 7> env{};  // 1-indexed, [0] unused
    std::array<double, 7> op_freq{};    // 1-indexed
    std::array<double, 2> fb_history = {0.0, 0.0};

    void start(int16_t k, int32_t nid, double vel, uint64_t order) {
        active = true;
        key = k;
        note_id = nid;
        velocity = vel;
        start_order = order;
        phase.fill(0.0);
        fb_history[0] = 0.0;
        fb_history[1] = 0.0;
        for (int i = 1; i <= kNumOperators; ++i) {
            env[i].gateOn();
        }
    }

    void release() {
        for (int i = 1; i <= kNumOperators; ++i) {
            env[i].gateOff();
        }
    }

    [[nodiscard]] auto isFinished() const -> bool {
        if (!active) return true;
        for (int i = 1; i <= kNumOperators; ++i) {
            if (env[i].stage != AdsrEnvelope::kIdle) return false;
        }
        return true;
    }
};

// Parameter IDs
enum ParamId : clap_id {
    kParamAlgorithm = 0,
    kParamFeedback = 1,
    kParamVolume = 2,

    // Per-operator params: base + op_index * 8 + offset
    // op_index: 0..5 (for op 1..6)
    kParamOp1Level = 100,
    kParamOp1Coarse = 101,
    kParamOp1Fine = 102,
    kParamOp1Detune = 103,
    kParamOp1Attack = 104,
    kParamOp1Decay = 105,
    kParamOp1Sustain = 106,
    kParamOp1Release = 107,
    // Op2: 108..115, Op3: 116..123, etc.
};

static constexpr clap_id kOpParamBase = 100;
static constexpr int kParamsPerOp = 8;
static constexpr int kTotalParams = 3 + kNumOperators * kParamsPerOp;  // 51

enum OpParamOffset {
    kOpLevel = 0,
    kOpCoarse = 1,
    kOpFine = 2,
    kOpDetune = 3,
    kOpAttack = 4,
    kOpDecay = 5,
    kOpSustain = 6,
    kOpRelease = 7,
};

inline auto opParamId(int op_1based, OpParamOffset offset) -> clap_id {
    return kOpParamBase + static_cast<clap_id>((op_1based - 1) * kParamsPerOp + offset);
}

class FmSynthPlugin : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                                   clap::helpers::CheckingLevel::Maximal> {
   public:
    FmSynthPlugin(const std::string& plugin_path, const clap_host* host);

    static auto descriptor() -> const clap_plugin_descriptor*;

    auto activate(double sample_rate, uint32_t min_frames_count, uint32_t max_frames_count) noexcept
        -> bool override;

    [[nodiscard]] auto implementsAudioPorts() const noexcept -> bool override { return true; }
    [[nodiscard]] auto audioPortsCount(bool is_input) const noexcept -> uint32_t override;
    auto audioPortsInfo(uint32_t index, bool is_input, clap_audio_port_info* info) const noexcept
        -> bool override;

    [[nodiscard]] auto implementsNotePorts() const noexcept -> bool override { return true; }
    [[nodiscard]] auto notePortsCount(bool is_input) const noexcept -> uint32_t override;
    auto notePortsInfo(uint32_t index, bool is_input, clap_note_port_info* info) const noexcept
        -> bool override;

    [[nodiscard]] auto implementsParams() const noexcept -> bool override { return true; }
    [[nodiscard]] auto paramsCount() const noexcept -> uint32_t override;
    auto paramsInfo(uint32_t index, clap_param_info* info) const noexcept -> bool override;
    auto paramsValue(clap_id param_id, double* value) noexcept -> bool override;
    auto paramsValueToText(clap_id param_id, double value, char* display, uint32_t size) noexcept
        -> bool override;
    auto paramsTextToValue(clap_id param_id, const char* display, double* value) noexcept
        -> bool override;
    void paramsFlush(const clap_input_events* in, const clap_output_events* out) noexcept override;

    [[nodiscard]] auto implementsState() const noexcept -> bool override { return true; }
    auto stateSave(const clap_ostream* os) noexcept -> bool override;
    auto stateLoad(const clap_istream* is) noexcept -> bool override;

    auto process(const clap_process* process) noexcept -> clap_process_status override;

    // Public accessors for testing
    auto voices() -> std::array<FmVoice, kMaxVoices>& { return _voices; }
    [[nodiscard]] auto voices() const -> const std::array<FmVoice, kMaxVoices>& { return _voices; }

   private:
    void handleEvent(const clap_event_header* header) noexcept;
    void handleParamValue(const clap_event_param_value* ev) noexcept;
    auto renderVoice(FmVoice& voice) noexcept -> double;
    auto allocateVoice() noexcept -> FmVoice*;

    [[nodiscard]] auto getParamValue(clap_id param_id) const -> double;
    void setParamValue(clap_id param_id, double value) noexcept;

    double _sample_rate{48000.0};
    uint64_t _voice_counter{0};

    // Global params
    double _algorithm{1.0};
    double _feedback{0.0};
    double _volume{0.8};

    // Per-operator params [op_index 0..5]
    struct OpParams {
        double level = 1.0;
        double coarse = 2.0;  // stepped 1-32, default 2 (=1x)
        double fine = 0.0;
        double detune = 0.0;   // stepped -7..+7
        double attack = 0.01;  // normalized 0-1 mapped to time
        double decay = 0.3;
        double sustain = 0.7;
        double release = 0.3;
    };
    std::array<OpParams, kNumOperators> _op_params{};

    std::array<FmVoice, kMaxVoices> _voices{};
};

}  // namespace synth_canvas::fm_synth
