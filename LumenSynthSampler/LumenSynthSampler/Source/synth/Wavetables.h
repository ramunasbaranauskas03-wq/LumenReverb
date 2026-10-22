#pragma once
#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
#include <string>

namespace lumen
{

// One band-limited wavetable: `frames` morph frames x kMips mip levels x kSize samples.
// Mip m keeps harmonics 1 .. 1024 >> m.
struct Table
{
    static constexpr int kSize = 2048, kMips = 11;
    int frames = 1;
    std::string name;
    std::vector<float> data;
    std::vector<float> source;      // mip-0 source cycles (frames * kSize), kept for saving user tables

    inline const float* frame (int f, int mip) const noexcept
    {
        return data.data() + (((size_t) f * kMips + (size_t) mip) * kSize);
    }

    // frame-morphed, linear-interpolated read (used by displays)
    inline float read (float pos, int mip, float phase) const noexcept
    {
        const float fp = std::clamp (pos, 0.0f, 1.0f) * (float) (frames - 1);
        const int f0 = std::min ((int) fp, std::max (0, frames - 2));
        const float ff = frames > 1 ? fp - (float) f0 : 0.0f;
        const float x = phase * (float) kSize;
        int i0 = (int) x; const float fr = x - (float) i0;
        i0 &= kSize - 1; const int i1 = (i0 + 1) & (kSize - 1);
        const float* a = frame (f0, mip);
        const float sa = a[i0] + fr * (a[i1] - a[i0]);
        if (frames < 2 || ff < 1.0e-4f) return sa;
        const float* b = frame (f0 + 1, mip);
        return sa + ff * ((b[i0] + fr * (b[i1] - b[i0])) - sa);
    }

    static inline int mipFor (double freq, double fs) noexcept
    {
        const double maxHarm = 0.45 * fs / std::max (1.0, freq);
        int m = 0;
        while (m < kMips - 1 && (double) (1024 >> m) > maxHarm) ++m;
        return m;
    }

    // Build from raw single cycles (frames * kSize samples). DC removed, band-limited per mip,
    // each frame normalised (gain capped at +24 dB so near-silent frames don't explode).
    static std::shared_ptr<Table> fromCycles (const std::vector<float>& cycles, int frames, const std::string& name);

    // Import arbitrary audio: exact 2048-sample frames when the length allows (Serum format),
    // otherwise evenly spaced 2048-sample windows. Max 256 frames.
    static std::shared_ptr<Table> fromAudio (const std::vector<float>& mono, const std::string& name);
};

class WavetableBank
{
public:
    static constexpr int kSize = Table::kSize, kFrames = 16, kMips = Table::kMips;
    static const WavetableBank& get();
    static const char* const* names();
    static int numTables();

    const Table& table (int t) const noexcept { return *tables[(size_t) std::clamp (t, 0, numTables() - 1)]; }
    inline const float* frame (int t, int f, int mip) const noexcept { return table (t).frame (f, mip); }
    inline float read (int t, float pos, int mip, double phase) const noexcept { return table (t).read (pos, mip, (float) phase); }
    static inline int mipFor (double freq, double fs) noexcept { return Table::mipFor (freq, fs); }

private:
    WavetableBank();
    std::vector<std::shared_ptr<Table>> tables;
};

} // namespace lumen
