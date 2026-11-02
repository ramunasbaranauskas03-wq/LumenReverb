#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <map>
#include <functional>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace lxl
{
constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

constexpr int kNumOsc       = 2;
constexpr int kNumModSlots  = 9;
constexpr int kNumMatrix    = 16;
constexpr int kNumMacros    = 8;
constexpr int kNumSteps     = 16;
constexpr int kNumInserts   = 2;
constexpr int kNumFilters   = 2;
constexpr int kMaxUnison    = 8;
constexpr int kMaxVoices    = 32;
constexpr int kSubBlock     = 32;

template <typename T>
inline T clamp01 (T x) noexcept { return x < T (0) ? T (0) : (x > T (1) ? T (1) : x); }

inline float frac (float x) noexcept { return x - std::floor (x); }

/** Pade-style tanh approximation, accurate enough for saturation, cheap. */
inline float fastTanh (float x) noexcept
{
    if (x > 3.0f)  return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Smooth soft clip with unity slope near zero. */
inline float softClip (float x) noexcept
{
    return fastTanh (x);
}

struct SineTable
{
    static constexpr int kSize = 4096;
    float data[kSize + 1];
    SineTable()
    {
        for (int i = 0; i <= kSize; ++i)
            data[i] = std::sin (kTwoPi * (float) i / (float) kSize);
    }
    static const SineTable& get() { static SineTable t; return t; }
};

/** Sine of a phase given in cycles (any real value). */
inline float fastSin (float cycles) noexcept
{
    const auto& t = SineTable::get();
    float p = cycles - std::floor (cycles);
    const float x = p * (float) SineTable::kSize;
    const int i = (int) x;
    const float f = x - (float) i;
    return t.data[i] + f * (t.data[i + 1] - t.data[i]);
}

struct Rng
{
    uint32_t s = 0x12345678u;
    explicit Rng (uint32_t seed = 0x9E3779B9u) : s (seed ? seed : 1u) {}
    inline uint32_t nextU() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float uni() noexcept { return (float) (nextU() >> 8) * (1.0f / 16777216.0f); }
    inline float bip() noexcept { return uni() * 2.0f - 1.0f; }
};

inline float midiToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

inline float dbToGain (float db) noexcept { return db <= -95.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }

/** Simple linear-interpolating delay line. */
class DelayLine
{
public:
    void setSize (int n) { buf.assign ((size_t) juce::nextPowerOfTwo (juce::jmax (4, n)), 0.0f); mask = (int) buf.size() - 1; pos = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline void push (float x) noexcept { buf[(size_t) pos] = x; pos = (pos + 1) & mask; }
    /** read 'delay' samples behind the most recently pushed sample (delay >= 1). */
    inline float read (float delay) const noexcept
    {
        const float d = juce::jlimit (1.0f, (float) (mask - 2), delay);
        const int di = (int) d;
        const float f = d - (float) di;
        const int i0 = (pos - di) & mask;
        const int i1 = (pos - di - 1) & mask;
        return buf[(size_t) i0] + f * (buf[(size_t) i1] - buf[(size_t) i0]);
    }
    int capacity() const noexcept { return mask; }
private:
    std::vector<float> buf { 4, 0.0f };
    int mask = 3, pos = 0;
};

/** Host tempo/position information passed into the engine each block. */
struct HostInfo
{
    double bpm = 120.0;
    double ppq = 0.0;
    bool playing = false;
};
} // namespace lxl
