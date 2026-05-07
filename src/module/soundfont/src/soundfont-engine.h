#pragma once

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
    void noteOn(int key, float velocity);
    void noteOff(int key);
    void allNotesOff();

    // Pitch handling
    void setPitchBend(int pitch_wheel_14bit);

    // Preset management
    void setPreset(int index);
    [[nodiscard]] auto getPresetCount() const -> int;
    [[nodiscard]] auto getPresetName(int index) const -> const char*;

    // Rendering
    void process(float** outputs, uint32_t frames);

   private:
    void cleanup();

    tsf* _tsf = nullptr;
    float _sample_rate = 44100.0f;
    int _current_preset = 0;
    std::vector<float> _render_buffer;
};

}  // namespace synth_canvas::soundfont_plugin
