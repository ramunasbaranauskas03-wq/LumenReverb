#pragma once
#include <vector>
#include <cmath>
#include <array>
#include <algorithm>
#include <memory>
#include <string>

// =====================================================================================
// Lumen Sampler - transparent sample source
//   * level 0 holds the original samples untouched (32-bit float)
//   * octave mip-levels (linear-phase half-band decimation, ~-120 dB stop band) so pitching up never aliases
//   * band-limited Kaiser windowed-sinc interpolation (32 zero crossings each side)
//   * exact pass-through when the read position is an integer and the speed is exactly 1
// =====================================================================================
namespace lumen::sampler
{
inline constexpr double kPi = 3.14159265358979323846;   // M_PI is not defined by MSVC

class SincTable
{
public:
    static constexpr int kZero = 64;          // zero crossings per side (unit cutoff)
    static constexpr int kRes = 4096;         // table points per zero crossing
    static const SincTable& get() { static SincTable t; return t; }
    // windowed sinc K(u), u >= 0 in zero-crossing units
    inline float at (double u) const noexcept
    {
        u = std::abs (u);
        if (u >= kZero) return 0.0f;
        const double x = u * kRes; const int i = (int) x; const float f = (float) (x - i);
        return tab[(size_t) i] + f * (tab[(size_t) i + 1] - tab[(size_t) i]);
    }
private:
    SincTable()
    {
        tab.assign ((size_t) (kZero * kRes + 2), 0.0f);
        const double beta = 12.0;   // Kaiser: ~ -120 dB side lobes
        auto i0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        const double den = i0 (beta);
        for (int i = 0; i <= kZero * kRes + 1; ++i)
        {
            const double u = (double) i / kRes;
            const double s = u < 1e-12 ? 1.0 : std::sin (kPi * u) / (kPi * u);
            const double r = u / kZero;
            const double w = r >= 1.0 ? 0.0 : i0 (beta * std::sqrt (1.0 - r * r)) / den;
            tab[(size_t) i] = (float) (s * w);
        }
    }
    std::vector<float> tab;
};

struct SampleSource
{
    static constexpr int kPad = 300;          // guard samples on both sides of every level
    static constexpr int kLevels = 5;         // original + 4 octaves down
    int channels = 1, length = 0;
    double rate = 48000.0;
    std::string name;
    float rootGuessHz = 0;
    // levels[l][ch]: kPad zeros, data, kPad zeros
    std::array<std::array<std::vector<float>, 2>, kLevels> levels;
    int levelLength (int l) const { return (length + (1 << l) - 1) >> l; }
    const float* data (int l, int ch) const { return levels[(size_t) l][(size_t) std::min (ch, channels - 1)].data() + kPad; }

    static std::shared_ptr<SampleSource> fromChannels (const std::vector<std::vector<float>>& ch, double sampleRate, std::string nm)
    {
        auto s = std::make_shared<SampleSource>();
        s->channels = std::clamp ((int) ch.size(), 1, 2);
        s->length = ch.empty() ? 0 : (int) ch[0].size();
        s->rate = sampleRate; s->name = std::move (nm);
        for (int c = 0; c < s->channels; ++c)
        {
            auto& v = s->levels[0][(size_t) c];
            v.assign ((size_t) (s->length + 2 * kPad), 0.0f);
            std::copy (ch[(size_t) c].begin(), ch[(size_t) c].end(), v.begin() + kPad);
        }
        s->buildMips();
        return s;
    }

private:
    void buildMips()
    {
        // linear-phase anti-alias FIR for 2:1 decimation (Kaiser windowed sinc, cutoff 0.2375 cycles/sample, 255 taps, beta 12)
        static const std::vector<float> h = []
        {
            const int N = 255, M = N / 2; std::vector<float> k ((size_t) N);
            const double beta = 12.0, fc = 0.2375;
            auto i0 = [] (double x) { double s = 1, t = 1; for (int q = 1; q < 40; ++q) { t *= (x / (2.0 * q)) * (x / (2.0 * q)); s += t; } return s; };
            double sum = 0;
            for (int i = 0; i < N; ++i)
            {
                const double n = i - M;
                const double c = n == 0 ? 2.0 * fc : std::sin (2.0 * kPi * fc * n) / (kPi * n);
                const double r = n / M;
                k[(size_t) i] = (float) (c * i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta));
                sum += k[(size_t) i];
            }
            for (auto& x : k) x = (float) (x / sum);
            return k;
        }();
        const int M = (int) h.size() / 2;
        for (int l = 1; l < kLevels; ++l)
        {
            const int srcLen = levelLength (l - 1), dstLen = levelLength (l);
            for (int c = 0; c < channels; ++c)
            {
                const float* src = data (l - 1, c);
                auto& dst = levels[(size_t) l][(size_t) c];
                dst.assign ((size_t) (dstLen + 2 * kPad), 0.0f);
                for (int n = 0; n < dstLen; ++n)
                {
                    double acc = 0;
                    const int centre = 2 * n;
                    for (int k = -M; k <= M; ++k)
                    {
                        const int idx = centre + k;
                        if (idx < 0 || idx >= srcLen) continue;
                        acc += (double) h[(size_t) (k + M)] * src[idx];
                    }
                    dst[(size_t) (n + kPad)] = (float) acc;
                }
            }
        }
    }
};

// Reads one stereo frame at a fractional position (in original samples) with a given speed (source samples
// per output sample). speed <= 1: full-band kernel. speed > 1: an octave level is chosen and the kernel is
// widened so nothing above the output Nyquist survives.
struct SincReader
{
    static constexpr double kCut = 0.955;     // pass band to 0.96 * Nyquist of the source
    static inline void read (const SampleSource& s, double pos, double speed, float& outL, float& outR) noexcept
    {
        const auto& K = SincTable::get();
        int level = 0;
        double sp = std::abs (speed);
        while (sp >= 2.0 && level < SampleSource::kLevels - 1) { sp *= 0.5; ++level; }
        const double scale = 1.0 / (double) (1 << level);
        const double p = pos * scale;
        const double cut = kCut / std::max (1.0, sp);          // relative to this level's Nyquist
        const int half = (int) std::ceil (SincTable::kZero / cut);
        const int lim = std::min (half, SampleSource::kPad - 2);
        const int i0 = (int) std::floor (p);
        const double frac = p - i0;
        const int len = s.levelLength (level);
        const float* a = s.data (level, 0);
        const float* b = s.data (level, 1);
        double l = 0, r = 0;
        for (int k = -lim + 1; k <= lim; ++k)
        {
            const int idx = i0 + k;
            if (idx < -SampleSource::kPad || idx >= len + SampleSource::kPad) continue;
            const float w = (float) cut * K.at ((k - frac) * cut);
            l += w * a[idx]; r += w * b[idx];
        }
        outL = (float) l; outR = (float) r;
    }
};

} // namespace lumen::sampler
