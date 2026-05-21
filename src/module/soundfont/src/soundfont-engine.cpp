#define TSF_IMPLEMENTATION
#include "soundfont-engine.h"

#include <cstdint>
#include <cstring>
#include <mutex>

#include "tsf.h"

namespace synth_canvas::soundfont_plugin {

SoundfontEngine::~SoundfontEngine() { cleanup(); }

void SoundfontEngine::cleanup() {
    std::lock_guard<std::mutex> lock(_tsf_mutex);
    if (_tsf) {
        tsf_close(_tsf);
        _tsf = nullptr;
    }
}

auto SoundfontEngine::load(const std::string& path) -> bool {
    tsf* new_tsf = tsf_load_filename(path.c_str());
    if (!new_tsf) {
        return false;
    }

    tsf_set_output(new_tsf, TSF_STEREO_UNWEAVED, _sample_rate, 0.0f);

    // Set default pitch bend range to ±2 semitones for all possible channels
    for (int i = 0; i < 16; ++i) {
        tsf_channel_set_pitchrange(new_tsf, i, 2.0f);
        tsf_channel_set_presetindex(new_tsf, i, _current_preset);
    }

    tsf* old_tsf = nullptr;
    {
        std::lock_guard<std::mutex> lock(_tsf_mutex);
        old_tsf = _tsf;
        _tsf = new_tsf;
    }

    if (old_tsf) {
        tsf_close(old_tsf);
    }

    return true;
}

void SoundfontEngine::setSampleRate(float sample_rate) {
    _sample_rate = sample_rate;
    std::lock_guard<std::mutex> lock(_tsf_mutex);
    if (_tsf) {
        tsf_set_output(_tsf, TSF_STEREO_UNWEAVED, _sample_rate, 0.0f);
    }
}

void SoundfontEngine::noteOn(int channel, int key, float velocity) {
    std::lock_guard<std::mutex> lock(_tsf_mutex);
    if (_tsf) {
        tsf_channel_note_on(_tsf, channel, key, velocity);
    }
}

void SoundfontEngine::noteOff(int channel, int key) {
    std::lock_guard<std::mutex> lock(_tsf_mutex);
    if (_tsf) {
        tsf_channel_note_off(_tsf, channel, key);
    }
}

void SoundfontEngine::allNotesOff(int channel) {
    std::lock_guard<std::mutex> lock(_tsf_mutex);
    if (_tsf) {
        tsf_channel_note_off_all(_tsf, channel);
    }
}

void SoundfontEngine::setPitchBend(int channel, int pitch_wheel_14bit) {
    std::lock_guard<std::mutex> lock(_tsf_mutex);
    if (_tsf) {
        tsf_channel_set_pitchwheel(_tsf, channel, pitch_wheel_14bit);
    }
}

void SoundfontEngine::setPreset(int channel, int index) {
    std::lock_guard<std::mutex> lock(_tsf_mutex);
    if (_tsf && index >= 0 && index < tsf_get_presetcount(_tsf)) {
        _current_preset = index;
        tsf_channel_set_presetindex(_tsf, channel, _current_preset);
    }
}

auto SoundfontEngine::getPresetCount() const -> int { return _tsf ? tsf_get_presetcount(_tsf) : 0; }

auto SoundfontEngine::getPresetName(int index) const -> const char* {
    return _tsf ? tsf_get_presetname(_tsf, index) : nullptr;
}

void SoundfontEngine::process(float** outputs, uint32_t frames) {
    std::lock_guard<std::mutex> lock(_tsf_mutex);
    if (_tsf && outputs[0] && outputs[1]) {
        if (_render_buffer.size() < frames * 2) {
            _render_buffer.resize(frames * 2);
        }

        tsf_render_float(_tsf, _render_buffer.data(), static_cast<int>(frames), 0);

        std::memcpy(outputs[0], _render_buffer.data(), frames * sizeof(float));
        std::memcpy(outputs[1], _render_buffer.data() + frames, frames * sizeof(float));
    } else {
        for (uint32_t i = 0; i < frames; ++i) {
            if (outputs[0]) outputs[0][i] = 0.0f;
            if (outputs[1]) outputs[1][i] = 0.0f;
        }
    }
}

}  // namespace synth_canvas::soundfont_plugin
