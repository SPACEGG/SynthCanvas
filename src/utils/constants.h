#pragma once

#include <cstddef>
#include <cstdint>

namespace synth_canvas::host::constants {

// Audio Settings & Defaults
// 0 allows Oboe/systems to choose the optimal native rate
constexpr int32_t kUnspecifiedSampleRate = 0;
constexpr int32_t kDefaultSampleRate = 48000;
constexpr int32_t kFallbackSampleRate = 44100;

constexpr int32_t kDefaultFramesPerBlock = 512;
constexpr int32_t kDefaultChannelCount = 2;
constexpr int32_t kDefaultAudioPortCount = 1;
constexpr int32_t kBufferCapacityMultiplier = 8;

constexpr bool kEnableAudioProfiling = true;
constexpr int32_t kProfilingFixedBlockSize = 256;

// Queue Sizes
constexpr size_t kSnapshotQueueSize = 32;
constexpr size_t kEventQueueSize = 4096;

// MIDI & Protocol
namespace midi_status {
constexpr uint8_t kNoteOff = 0x80;
constexpr uint8_t kNoteOn = 0x90;
constexpr uint8_t kPolyPressure = 0xA0;
constexpr uint8_t kControlChange = 0xB0;
constexpr uint8_t kProgramChange = 0xC0;
constexpr uint8_t kAftertouch = 0xD0;
constexpr uint8_t kPitchBend = 0xE0;
constexpr uint8_t kSystem = 0xF0;
}  // namespace midi_status

constexpr double kMidiMaxVelocity = 127.0;
constexpr int kMidiCcStatusByte = 0xB0;
constexpr int16_t kDefaultEventPortIndex = 0;
constexpr int32_t kClapInvalidId = -1;

// Graph Constraints
constexpr size_t kMaxConnectionsPerPort = 32;

// Envelope Settings
constexpr size_t kMaxInternalPolyphony = 32;
constexpr double kEnvelopeSilenceThreshold = 1e-5;

// Modulation Quantization Settings
constexpr int32_t kModulationStepSize = 16;
constexpr double kModulationThreshold = 0.0001;

// Timing & Timeouts
constexpr int kPluginDeactivateTimeoutMs = 200;
constexpr int kPluginDeactivateSleepMs = 1;

// Identifiers
constexpr uint32_t kAudioOutputNoteId = 0;
constexpr uint32_t kInitialPluginInstanceId = 1;

}  // namespace synth_canvas::host::constants
