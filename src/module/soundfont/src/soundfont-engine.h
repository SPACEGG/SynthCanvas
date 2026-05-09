#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Forward declaration for tsf
struct tsf;

namespace synth_canvas::soundfont_plugin {

class SoundfontEngine {
   public:
    SoundfontEngine() = default;

    ~SoundfontEngine();

    // Load a Soundfont file (.sf2)
    auto load(const std::string& path) -> bool;

    // Set output sample rate
    void setSampleRate(float sample_rate);

    // Note handling
    void noteOn(int channel, int key, float velocity);
    void noteOff(int channel, int key);
    void allNotesOff(int channel);

    // Pitch handling
    void setPitchBend(int channel, int pitch_wheel_14bit);

    // Preset management
    void setPreset(int channel, int index);
    [[nodiscard]] auto getPresetCount() const -> int;
    [[nodiscard]] auto getPresetName(int index) const -> const char*;

    // Rendering
    void process(float** outputs, uint32_t frames);

   private:
    void cleanup();

    std::mutex _tsf_mutex;
    tsf* _tsf = nullptr;
    float _sample_rate = 44100.0f;
    int _current_preset = 0;
    std::vector<float> _render_buffer;
};

}  // namespace synth_canvas::soundfont_plugin
