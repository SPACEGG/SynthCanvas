#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

// Forward declaration for tsf
struct tsf;

namespace synth_canvas::soundfont_plugin {

class SoundfontEngine {
public:
    SoundfontEngine();
    ~SoundfontEngine();

    // Load a Soundfont file (.sf2)
    bool load(const std::string& path);

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
    int getPresetCount() const;
    const char* getPresetName(int index) const;

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

};

}  // namespace synth_canvas::soundfont_plugin
