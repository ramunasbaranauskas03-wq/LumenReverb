#include "Wavetables.h"
#include <juce_dsp/juce_dsp.h>

namespace lumen
{

static constexpr double kTwoPi = 6.283185307179586;

static const char* const kNames[] = { "Basic Shapes", "Saw Stack", "PWM", "Vox Formant", "Hard Sync",
                                      "FM Growl", "Phase Res", "Organ", "Fold", "Spectral", "Dirty Saw", "Metal FM",
                                      "Analog Saw", "Acid", "Analog Pulse", "Neuro Grind", "DX Bells", "Choir", "Digital Crush", "Sub Harmonics", "Screech Sync", "Square Stack", nullptr };

const char* const* WavetableBank::names() { return kNames; }
int WavetableBank::numTables() { return 22; }

const WavetableBank& WavetableBank::get()
{
    static const WavetableBank bank;
    return bank;
}

//==============================================================================
static double saw (double p) { return 2.0 * (p - std::floor (p)) - 1.0; }

static double genTime (int table, double t, double p)
{
    switch (table)
    {
        case 0:
        {
            const double s = std::sin (kTwoPi * p);
            const double tri = 1.0 - 4.0 * std::abs (p - std::floor (p + 0.75) + 0.25);
            const double sw = saw (p + 0.5);
            const double sq = p < 0.5 ? 1.0 : -1.0;
            if (t < 1.0 / 3.0) { const double k = t * 3.0; return s + (tri - s) * k; }
            if (t < 2.0 / 3.0) { const double k = (t - 1.0 / 3.0) * 3.0; return tri + (sw - tri) * k; }
            const double k = (t - 2.0 / 3.0) * 3.0; return sw + (sq - sw) * k;
        }
        case 1:  return saw (p) + t * 0.6 * saw (2.0 * p) + t * t * 0.4 * saw (3.0 * p) + t * t * 0.25 * saw (4.0 * p);
        case 2:  { const double w = 0.5 - 0.45 * t; return (p - std::floor (p)) < w ? 1.0 : -1.0; }
        case 4:  return saw ((1.0 + 7.0 * t) * p);
        case 5:  return std::sin (kTwoPi * p + 9.0 * t * std::sin (kTwoPi * 2.0 * p) + 2.0 * t * std::sin (kTwoPi * p));
        case 6:  { const double r = 1.0 + 15.0 * t; return (1.0 - p) * std::sin (kTwoPi * p * r) + (1.0 - t) * std::sin (kTwoPi * p) * 0.3; }
        case 8:  { const double g = 1.0 + 7.0 * t; return std::sin (g * std::sin (kTwoPi * p) * 1.5707963); }
        case 10: { const double g = 1.0 + 12.0 * t; return std::tanh (g * (saw (p) + 0.3 * t)) - std::tanh (g * 0.3 * t); }
        case 11: return std::sin (kTwoPi * p + (1.0 + 7.0 * t) * std::sin (kTwoPi * 3.0 * p) + 3.0 * t * std::sin (kTwoPi * 5.0 * p));
        case 12: // Analog Saw: capacitor-charge ramp (curvature grows), slight reset softness
        {
            const double ph = p - std::floor (p), a = 0.35 + 3.2 * t;
            return 2.0 * (1.0 - std::exp (-a * ph)) / (1.0 - std::exp (-a)) - 1.0;
        }
        case 13: // Acid: curved saw -> drooping square (TB-style)
        {
            const double ph = p - std::floor (p);
            const double sw = 2.0 * (1.0 - std::exp (-1.2 * ph)) / (1.0 - std::exp (-1.2)) - 1.0;
            const double sq = (ph < 0.5 ? 1.0 : -1.0) * (1.0 - 0.45 * (ph < 0.5 ? ph : ph - 0.5) * 2.0);
            return sw + (sq - sw) * t;
        }
        case 14: // Analog Pulse: 50% -> 6% with capacitor droop
        {
            const double ph = p - std::floor (p), w = 0.5 - 0.44 * t;
            return ph < w ? 1.0 - 0.35 * ph / w : -1.0 + 0.35 * (ph - w) / (1.0 - w);
        }
        case 15: // Neuro Grind: FM into a wavefolder
        {
            const double x = std::sin (kTwoPi * p + (1.5 + 9.0 * t) * std::sin (kTwoPi * 2.0 * p) + 1.5 * t * std::sin (kTwoPi * 3.0 * p));
            return std::sin ((1.0 + 3.5 * t) * 1.5707963 * x);
        }
        case 16: // DX Bells: 1:7 FM + 1:2 shimmer
            return std::sin (kTwoPi * p + (0.4 + 4.5 * t) * std::sin (kTwoPi * 7.0 * p)) * 0.75 + 0.35 * std::sin (kTwoPi * 2.0 * p + 2.0 * t * std::sin (kTwoPi * 3.0 * p));
        case 18: // Digital Crush: saw quantised from 64 to 3 steps
        {
            const double L = std::round (std::exp2 (6.0 - 4.5 * t));
            return std::floor (saw (p) * L + 0.5) / L;
        }
        case 20: // Screech Sync: synced square with a falling window
        {
            const double ph = p - std::floor (p), r = 1.0 + 11.0 * t;
            const double q = r * ph - std::floor (r * ph);
            return (q < 0.5 ? 1.0 : -1.0) * (1.0 - 0.8 * ph);
        }
        case 21: // Square Stack: square + octave square -> + saw
        {
            auto sq = [] (double x) { x -= std::floor (x); return x < 0.5 ? 1.0 : -1.0; };
            return sq (p) + 0.5 * t * sq (2.0 * p) + 0.35 * t * saw (p * 3.0);
        }
        default: return 0.0;
    }
}

static double genHarm (int table, double t, int k)
{
    if (table == 3)
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
    if (table == 7)
    {
        static const int h[] = { 1, 2, 3, 4, 6, 8, 10, 12, 16 };
        static const double base[] = { 1.0, 0.8, 0.0, 0.5, 0.0, 0.3, 0.0, 0.0, 0.0 };
        static const double full[] = { 1.0, 1.0, 0.8, 0.8, 0.6, 0.7, 0.4, 0.5, 0.5 };
        for (int i = 0; i < 9; ++i)
            if (h[i] == k) return base[i] + (full[i] - base[i]) * t;
        return 0.0;
    }
    if (table == 9)   // Spectral: brightness sweep with a resonant peak riding the edge
    {
        const double edge = 2.0 + std::pow (t, 1.6) * 120.0;
        const double roll = 1.0 / (1.0 + std::pow ((double) k / edge, 6.0));
        const double peak = 2.5 * std::exp (-0.5 * std::pow (((double) k - edge) / (0.08 * edge + 1.0), 2.0));
        return (roll + peak) / (double) k;
    }
    if (table == 17)  // Choir: wide formants for 'ah - eh - ee - oh - oo', breathy top
    {
        static const double F[5][3] = { { 700, 1220, 2600 }, { 530, 1840, 2480 }, { 300, 2200, 2950 }, { 500, 900, 2400 }, { 330, 870, 2240 } };
        const double v = t * 4.0; const int a = std::min (3, (int) v); const double fr = v - a;
        const double f = 130.0 * k; double amp = 0.0;
        for (int i = 0; i < 3; ++i)
        {
            const double fc = F[a][i] + (F[a + 1][i] - F[a][i]) * fr, bw = 130.0 + 90.0 * i;
            amp += std::exp (-0.5 * std::pow ((f - fc) / bw, 2.0)) * (i == 0 ? 1.0 : i == 1 ? 0.7 : 0.4);
        }
        return (amp + 0.05 / (1.0 + k * 0.05)) / std::pow ((double) k, 0.7);
    }
    if (table == 19)  // Sub Harmonics: sine -> sine + 2nd..5th (warm bass body)
    {
        if (k == 1) return 1.0;
        if (k > 6) return 0.0;
        const double on = std::clamp (t * 5.0 - (k - 2), 0.0, 1.0);
        return on * 0.55 / (k - 0.5);
    }
    return 0.0;
}

static bool isAdditive (int t) { return t == 3 || t == 7 || t == 9 || t == 17 || t == 19; }

//==============================================================================
// Band-limit a set of full-band spectra into mip levels.
static void buildMips (Table& tb, std::vector<std::vector<float>>& spectra, bool normalise)
{
    juce::dsp::FFT fft (11);
    const int N = Table::kSize;
    tb.data.assign ((size_t) tb.frames * Table::kMips * N, 0.0f);
    std::vector<float> work ((size_t) 2 * N);
    for (int fr = 0; fr < tb.frames; ++fr)
    {
        auto& spec = spectra[(size_t) fr];
        spec[0] = spec[1] = 0.0f;   // DC
        float peak = 0.0f;
        std::vector<float> mips ((size_t) Table::kMips * N);
        for (int m = 0; m < Table::kMips; ++m)
        {
            const int maxH = 1024 >> m;
            work = spec;
            for (int k = maxH + 1; k < N; ++k) { work[(size_t) (2 * k)] = 0.0f; work[(size_t) (2 * k + 1)] = 0.0f; }
            fft.performRealOnlyInverseTransform (work.data());
            for (int i = 0; i < N; ++i)
            {
                mips[(size_t) (m * N + i)] = work[(size_t) i];
                if (m == 0) peak = std::max (peak, std::abs (work[(size_t) i]));
            }
        }
        float norm = 1.0f;
        if (normalise) norm = peak > 1.0e-9f ? std::min (1.0f / peak, 15.85f) : 1.0f;
        for (int m = 0; m < Table::kMips; ++m)
        {
            float* dst = tb.data.data() + (((size_t) fr * Table::kMips + (size_t) m) * N);
            for (int i = 0; i < N; ++i) dst[i] = mips[(size_t) (m * N + i)] * norm;
        }
    }
}

std::shared_ptr<Table> Table::fromCycles (const std::vector<float>& cycles, int frames, const std::string& name)
{
    auto tb = std::make_shared<Table>();
    tb->frames = std::clamp (frames, 1, 256);
    tb->name = name;
    tb->source.assign (cycles.begin(), cycles.begin() + (std::ptrdiff_t) std::min (cycles.size(), (size_t) tb->frames * kSize));
    tb->source.resize ((size_t) tb->frames * kSize, 0.0f);
    juce::dsp::FFT fft (11);
    std::vector<std::vector<float>> spectra ((size_t) tb->frames, std::vector<float> ((size_t) 2 * kSize, 0.0f));
    for (int f = 0; f < tb->frames; ++f)
    {
        auto& s = spectra[(size_t) f];
        double mean = 0.0;
        for (int i = 0; i < kSize; ++i) mean += tb->source[(size_t) (f * kSize + i)];
        mean /= kSize;
        for (int i = 0; i < kSize; ++i) s[(size_t) i] = (float) (tb->source[(size_t) (f * kSize + i)] - mean);
        fft.performRealOnlyForwardTransform (s.data());
    }
    buildMips (*tb, spectra, true);
    return tb;
}

std::shared_ptr<Table> Table::fromAudio (const std::vector<float>& mono, const std::string& name)
{
    const size_t len = mono.size();
    std::vector<float> cycles;
    int frames = 1;
    if (len >= (size_t) kSize && len % (size_t) kSize == 0)
    {
        const int total = (int) (len / (size_t) kSize);
        frames = std::min (total, 256);
        cycles.resize ((size_t) frames * kSize);
        for (int f = 0; f < frames; ++f)
        {
            const int src = frames == total ? f : (int) std::lround ((double) f * (total - 1) / std::max (1, frames - 1));
            std::copy (mono.begin() + (std::ptrdiff_t) src * kSize, mono.begin() + (std::ptrdiff_t) (src + 1) * kSize,
                       cycles.begin() + (std::ptrdiff_t) f * kSize);
        }
    }
    else if (len > (size_t) kSize)
    {
        frames = (int) std::clamp<size_t> (len / (size_t) kSize, 2, 64);
        cycles.resize ((size_t) frames * kSize);
        for (int f = 0; f < frames; ++f)
        {
            const size_t start = (size_t) ((double) f * (double) (len - (size_t) kSize) / (double) (frames - 1));
            // short crossfade at the wrap point keeps the cycle continuous
            for (int i = 0; i < kSize; ++i) cycles[(size_t) (f * kSize + i)] = mono[start + (size_t) i];
            const int xf = 64;
            for (int i = 0; i < xf; ++i)
            {
                const float w = (float) i / (float) xf;
                auto& a = cycles[(size_t) (f * kSize + kSize - xf + i)];
                a = a * (1.0f - w) + cycles[(size_t) (f * kSize + i)] * w;
            }
        }
    }
    else
    {
        // shorter than one frame: resample to a single cycle
        frames = 1;
        cycles.resize ((size_t) kSize);
        for (int i = 0; i < kSize; ++i)
        {
            const double x = (double) i * (double) std::max<size_t> (1, len) / kSize;
            const size_t i0 = std::min ((size_t) x, len > 0 ? len - 1 : 0);
            const size_t i1 = std::min (i0 + 1, len > 0 ? len - 1 : 0);
            const double fr = x - (double) i0;
            cycles[(size_t) i] = len > 0 ? (float) (mono[i0] * (1.0 - fr) + mono[i1] * fr) : 0.0f;
        }
    }
    return fromCycles (cycles, frames, name);
}

//==============================================================================
WavetableBank::WavetableBank()
{
    juce::dsp::FFT fft (11);
    const int N = kSize;
    for (int tb = 0; tb < numTables(); ++tb)
    {
        auto t = std::make_shared<Table>();
        t->frames = kFrames;
        t->name = kNames[tb];
        std::vector<std::vector<float>> spectra ((size_t) kFrames, std::vector<float> ((size_t) 2 * N, 0.0f));
        for (int fr = 0; fr < kFrames; ++fr)
        {
            const double tt = (double) fr / (kFrames - 1);
            auto& spec = spectra[(size_t) fr];
            if (isAdditive (tb))
            {
                for (int k = 1; k <= 1023; ++k)
                    spec[(size_t) (2 * k + 1)] = (float) (-genHarm (tb, tt, k) * N * 0.5);
            }
            else
            {
                const int over = 8;
                std::vector<double> big ((size_t) (N * over));
                for (int i = 0; i < N * over; ++i) big[(size_t) i] = genTime (tb, tt, (double) i / (N * over));
                for (int i = 0; i < N; ++i)
                {
                    double acc = 0.0;
                    for (int j = 0; j < over; ++j) acc += big[(size_t) (i * over + j)];
                    spec[(size_t) i] = (float) (acc / over);
                }
                fft.performRealOnlyForwardTransform (spec.data());
            }
        }
        buildMips (*t, spectra, true);
        tables.push_back (std::move (t));
    }
}

} // namespace lumen
