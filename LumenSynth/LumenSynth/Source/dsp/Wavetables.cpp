#include "Wavetables.h"
#include <juce_dsp/juce_dsp.h>

namespace lumen
{

static constexpr double kTwoPi = 6.283185307179586;

static const char* const kNames[] = { "Basic Shapes", "Saw Stack", "PWM", "Vox Formant", "Hard Sync",
                                      "FM Growl", "Phase Res", "Organ", "Fold", nullptr };

const char* const* WavetableBank::names() { return kNames; }
int WavetableBank::numTables() { return 9; }

const WavetableBank& WavetableBank::get()
{
    static const WavetableBank bank;
    return bank;
}

// --------------------------------------------------------------------------------------
// Naive (not band-limited) single-cycle generators. t = morph position 0..1, p = phase 0..1
static double saw (double p) { return 2.0 * (p - std::floor (p)) - 1.0; }

static double genTime (int table, double t, double p)
{
    switch (table)
    {
        case 0: // Basic: sine -> triangle -> saw -> square
        {
            const double s = std::sin (kTwoPi * p);
            const double tri = 1.0 - 4.0 * std::abs (p - std::floor (p + 0.75) + 0.25);
            const double sw = saw (p + 0.5);
            const double sq = p < 0.5 ? 1.0 : -1.0;
            if (t < 1.0 / 3.0) { const double k = t * 3.0; return s + (tri - s) * k; }
            if (t < 2.0 / 3.0) { const double k = (t - 1.0 / 3.0) * 3.0; return tri + (sw - tri) * k; }
            const double k = (t - 2.0 / 3.0) * 3.0; return sw + (sq - sw) * k;
        }
        case 1: // Saw stack: saw + octave + fifth layers fade in
            return saw (p) + t * 0.6 * saw (2.0 * p) + t * t * 0.4 * saw (3.0 * p) + t * t * 0.25 * saw (4.0 * p);
        case 2: // PWM 50% -> 5%
        {
            const double w = 0.5 - 0.45 * t;
            return (p - std::floor (p)) < w ? 1.0 : -1.0;
        }
        case 4: // Hard sync sweep: slave saw ratio 1 -> 8
            return saw ((1.0 + 7.0 * t) * p);
        case 5: // FM growl: carrier 1, modulator 2, index 0 -> 9
            return std::sin (kTwoPi * p + 9.0 * t * std::sin (kTwoPi * 2.0 * p) + 2.0 * t * std::sin (kTwoPi * p));
        case 6: // Phase resonance (CZ style): decaying resonant burst inside each cycle
        {
            const double r = 1.0 + 15.0 * t;
            return (1.0 - p) * std::sin (kTwoPi * p * r) * 1.0 + (1.0 - t) * std::sin (kTwoPi * p) * 0.3;
        }
        case 8: // Fold: sine driven into a wavefolder
        {
            const double g = 1.0 + 7.0 * t;
            return std::sin (g * std::sin (kTwoPi * p) * 1.5707963);
        }
        default: return 0.0;
    }
}

// Spectral generators (harmonic amplitudes) for additive tables. Returns amplitude of harmonic k.
static double genHarm (int table, double t, int k)
{
    if (table == 3) // Vox: morph through vowels a -> e -> i -> o -> u, formants relative to ~110 Hz fundamental
    {
        static const double F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2100, 3000 },
                                        { 450, 800, 2830 },  { 325, 700, 2530 } };
        const double v = t * 4.0;
        const int a = std::min (3, (int) v);
        const double fr = v - a;
        const double f = 110.0 * k;
        double amp = 0.0;
        for (int i = 0; i < 3; ++i)
        {
            const double fc = F[a][i] + (F[a + 1][i] - F[a][i]) * fr;
            const double bw = 80.0 + 40.0 * i;
            amp += std::exp (-0.5 * std::pow ((f - fc) / bw, 2.0)) * (i == 0 ? 1.0 : i == 1 ? 0.6 : 0.3);
        }
        return (amp + 0.02) / std::sqrt ((double) k);
    }
    if (table == 7) // Organ drawbars: 16', 8', 5 1/3', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'
    {
        static const int h[] = { 1, 2, 3, 4, 6, 8, 10, 12, 16 };
        static const double base[] = { 1.0, 0.8, 0.0, 0.5, 0.0, 0.3, 0.0, 0.0, 0.0 };
        static const double full[] = { 1.0, 1.0, 0.8, 0.8, 0.6, 0.7, 0.4, 0.5, 0.5 };
        for (int i = 0; i < 9; ++i)
            if (h[i] == k) return base[i] + (full[i] - base[i]) * t;
        return 0.0;
    }
    return 0.0;
}

WavetableBank::WavetableBank()
{
    const int nT = numTables();
    data.assign ((size_t) nT * kFrames * kMips * kSize, 0.0f);

    juce::dsp::FFT fft (11);                       // 2048
    std::vector<float> spec (2 * kSize), work (2 * kSize);

    for (int tb = 0; tb < nT; ++tb)
    {
        const bool additive = (tb == 3 || tb == 7);
        for (int fr = 0; fr < kFrames; ++fr)
        {
            const double t = (double) fr / (kFrames - 1);

            // ---- full-band spectrum of this frame ----
            std::fill (spec.begin(), spec.end(), 0.0f);
            if (additive)
            {
                // real-only FFT layout: [re0, im0, re1, im1, ...]; sine phase -> imaginary part
                for (int k = 1; k <= 1023; ++k)
                    spec[(size_t) (2 * k + 1)] = (float) (-genHarm (tb, t, k) * kSize * 0.5);
            }
            else
            {
                // supersample the naive waveform 8x then take the first 1024 harmonics (reduces aliasing in the source)
                const int over = 8;
                std::vector<double> big ((size_t) (kSize * over));
                for (int i = 0; i < kSize * over; ++i) big[(size_t) i] = genTime (tb, t, (double) i / (kSize * over));
                for (int i = 0; i < kSize; ++i)
                {
                    double acc = 0.0;
                    for (int j = 0; j < over; ++j) acc += big[(size_t) (i * over + j)];
                    spec[(size_t) i] = (float) (acc / over);
                }
                fft.performRealOnlyForwardTransform (spec.data());
                spec[0] = spec[1] = 0.0f;   // remove DC
            }

            // ---- mip levels ----
            float peak = 0.0f;
            std::vector<float> mips ((size_t) kMips * kSize);
            for (int m = 0; m < kMips; ++m)
            {
                const int maxH = 1024 >> m;
                work = spec;
                for (int k = maxH + 1; k <= kSize / 2; ++k) { work[(size_t) (2 * k)] = 0.0f; work[(size_t) (2 * k + 1)] = 0.0f; }
                // also clear the mirrored half if present
                for (int k = kSize / 2 + 1; k < kSize; ++k) { work[(size_t) (2 * k)] = 0.0f; work[(size_t) (2 * k + 1)] = 0.0f; }
                fft.performRealOnlyInverseTransform (work.data());
                for (int i = 0; i < kSize; ++i)
                {
                    mips[(size_t) (m * kSize + i)] = work[(size_t) i];
                    if (m == 0) peak = std::max (peak, std::abs (work[(size_t) i]));
                }
            }
            const float norm = peak > 1.0e-9f ? 1.0f / peak : 1.0f;
            for (int m = 0; m < kMips; ++m)
            {
                float* dst = data.data() + ((((size_t) tb * kFrames + (size_t) fr) * kMips + (size_t) m) * kSize);
                for (int i = 0; i < kSize; ++i) dst[i] = mips[(size_t) (m * kSize + i)] * norm;
            }
        }
    }
}

} // namespace lumen
