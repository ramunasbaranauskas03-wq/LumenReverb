#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

namespace lumen
{

// Band-limited wavetable bank.
// Each table has kFrames morph frames; each frame has kMips mip levels
// (mip m keeps harmonics 1 .. 1024 >> m), each kSize samples long.
class WavetableBank
{
public:
    static constexpr int kSize = 2048, kFrames = 16, kMips = 11;

    static const WavetableBank& get();                 // built once, shared by all instances
    static const char* const* names();
    static int numTables();

    inline const float* frame (int table, int frame, int mip) const noexcept
    {
        return data.data() + ((((size_t) table * kFrames + (size_t) frame) * kMips + (size_t) mip) * kSize);
    }

    // mip whose top harmonic stays below ~0.45 fs for this frequency
    static inline int mipFor (double freq, double fs) noexcept
    {
        const double maxHarm = 0.45 * fs / std::max (1.0, freq);
        int m = 0;
        while (m < kMips - 1 && (double) (1024 >> m) > maxHarm) ++m;
        return m;
    }

    // Single read: frame-morphed, linear-interpolated sample at phase [0,1)
    inline float read (int table, float pos, int mip, double phase) const noexcept
    {
        const float fp = std::clamp (pos, 0.0f, 1.0f) * (float) (kFrames - 1);
        const int f0 = std::min ((int) fp, kFrames - 2);
        const float ff = fp - (float) f0;
        const double x = phase * kSize;
        const int i0 = (int) x & (kSize - 1);
        const int i1 = (i0 + 1) & (kSize - 1);
        const float fr = (float) (x - std::floor (x));
        const float* a = frame (table, f0, mip);
        const float sa = a[i0] + fr * (a[i1] - a[i0]);
        if (ff < 1.0e-4f) return sa;
        const float* b = frame (table, f0 + 1, mip);
        const float sb = b[i0] + fr * (b[i1] - b[i0]);
        return sa + ff * (sb - sa);
    }

private:
    WavetableBank();
    std::vector<float> data;
};

} // namespace lumen
