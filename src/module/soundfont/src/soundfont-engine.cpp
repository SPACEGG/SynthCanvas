#define TSF_IMPLEMENTATION
#include "soundfont-engine.h"

#include <cstdint>
#include <cstring>

#include "tsf.h"

namespace synth_canvas::soundfont_plugin {

SoundfontEngine::~SoundfontEngine() { cleanup(); }

void SoundfontEngine::cleanup() {
    if (_tsf) {
        tsf_close(_tsf);
        _tsf = nullptr;
    }
}

auto SoundfontEngine::load(const std::string& path) -> bool {
    cleanup();

    _tsf = tsf_load_filename(path.c_str());
    if (!_tsf) {
        return false;
    }

    tsf_set_output(_tsf, TSF_STEREO_UNWEAVED, _sample_rate, 0.0f);

    // Set default pitch bend range to ±2 semitones as per spec
    tsf_channel_set_pitchrange(_tsf, 0, 2.0f);

    // Initialize preset on channel 0
    tsf_channel_set_presetindex(_tsf, 0, _current_preset);

    return true;
}

void SoundfontEngine::setSampleRate(float sample_rate) {
    _sample_rate = sample_rate;
    if (_tsf) {
        tsf_set_output(_tsf, TSF_STEREO_UNWEAVED, _sample_rate, 0.0f);
    }
}

void SoundfontEngine::noteOn(int key, float velocity) {
    if (_tsf) {
        tsf_channel_note_on(_tsf, 0, key, velocity);
    }
}

void SoundfontEngine::noteOff(int key) {
    if (_tsf) {
        tsf_channel_note_off(_tsf, 0, key);
    }
}

void SoundfontEngine::allNotesOff() {
    if (_tsf) {
        tsf_channel_note_off_all(_tsf, 0);
    }
}

void SoundfontEngine::setPitchBend(int pitch_wheel_14bit) {
    if (_tsf) {
        tsf_channel_set_pitchwheel(_tsf, 0, pitch_wheel_14bit);
    }
}

void SoundfontEngine::setPreset(int index) {
    if (_tsf && index >= 0 && index < tsf_get_presetcount(_tsf)) {
        _current_preset = index;
        tsf_channel_set_presetindex(_tsf, 0, _current_preset);
    }
}

auto SoundfontEngine::getPresetCount() const -> int { return _tsf ? tsf_get_presetcount(_tsf) : 0; }

auto SoundfontEngine::getPresetName(int index) const -> const char* {
    return _tsf ? tsf_get_presetname(_tsf, index) : nullptr;
}

void SoundfontEngine::process(float** outputs, uint32_t frames) {
    if (_tsf && outputs[0] && outputs[1]) {
        // Ensure our internal buffer is large enough for UNWEAVED stereo [L0,L1...,R0,R1...]
        if (_render_buffer.size() < frames * 2) {
            _render_buffer.resize(frames * 2);
        }

        tsf_render_float(_tsf, _render_buffer.data(), static_cast<int>(frames), 0);

        // Copy Left and Right channels to separate output pointers
        std::memcpy(outputs[0], _render_buffer.data(), frames * sizeof(float));
        std::memcpy(outputs[1], _render_buffer.data() + frames, frames * sizeof(float));
    } else {
        // Clear buffers if no TSF or invalid outputs
        for (uint32_t i = 0; i < frames; ++i) {
            if (outputs[0]) outputs[0][i] = 0.0f;
            if (outputs[1]) outputs[1][i] = 0.0f;
        }
    }
}

}  // namespace synth_canvas::soundfont_plugin
