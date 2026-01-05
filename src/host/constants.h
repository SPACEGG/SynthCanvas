#pragma once

#include <cstddef>
#include <cstdint>

namespace synth_canvas {
namespace host {
namespace constants {

// Audio Settings & Defaults
// 0 allows Oboe/systems to choose the optimal native rate
constexpr int32_t kUnspecifiedSampleRate = 0;
constexpr int32_t kDefaultSampleRate = 48000;
constexpr int32_t kFallbackSampleRate = 44100;

constexpr int32_t kDefaultFramesPerBlock = 512;
constexpr int32_t kDefaultChannelCount = 2;
constexpr int32_t kDefaultAudioPortCount = 1;
constexpr int32_t kBufferCapacityMultiplier = 8;

// Queue Sizes
constexpr size_t kSnapshotQueueSize = 32;
constexpr size_t kEventQueueSize = 4096;

// MIDI & Protocol
constexpr double kMidiMaxVelocity = 127.0;
constexpr int kMidiCcStatusByte = 0xB0;
constexpr int16_t kDefaultEventPortIndex = 0;
constexpr int32_t kClapInvalidId = -1;

// Timing & Timeouts
constexpr int kPluginDeactivateTimeoutMs = 200;
constexpr int kPluginDeactivateSleepMs = 1;

// Identifiers
constexpr uint32_t kAudioOutputNoteId = 0;
constexpr uint32_t kInitialPluginInstanceId = 1;

}  // namespace constants
}  // namespace host
}  // namespace synth_canvas
