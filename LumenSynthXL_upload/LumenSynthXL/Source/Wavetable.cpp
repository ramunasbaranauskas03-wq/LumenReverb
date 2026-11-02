#include "Wavetable.h"

namespace lxl
{
static constexpr int N = Wavetable::kSize;
static constexpr int kFactoryFrames = 64;

using Frame = std::vector<float>;

// ------------------------------------------------------------------ mip building
std::unique_ptr<Wavetable> Wavetable::fromFrames (const std::vector<std::vector<float>>& frames, const juce::String& name, bool keepSource)
{
    auto wt = std::make_unique<Wavetable>();
    wt->name = name;
    wt->numFrames = juce::jmax (1, (int) frames.size());
    wt->data.assign ((size_t) wt->numFrames * kLevels * kStride, 0.0f);

    juce::dsp::FFT fft (11);
    std::vector<float> spec ((size_t) N * 2), work ((size_t) N * 2);

    for (int f = 0; f < (int) frames.size(); ++f)
    {
        const auto& src = frames[(size_t) f];
        std::fill (spec.begin(), spec.end(), 0.0f);
        for (int i = 0; i < N; ++i)
            spec[(size_t) i] = i < (int) src.size() ? src[(size_t) i] : 0.0f;

        fft.performRealOnlyForwardTransform (spec.data(), true);
        spec[0] = spec[1] = 0.0f; // remove DC

        float scale = 1.0f;
        for (int lvl = 0; lvl < kLevels; ++lvl)
        {
            const int maxH = juce::jmin (N / 2 - 1, (N / 2) >> lvl);
            std::fill (work.begin(), work.end(), 0.0f);
            for (int h = 1; h <= maxH; ++h)
            {
                work[(size_t) h * 2]     = spec[(size_t) h * 2];
                work[(size_t) h * 2 + 1] = spec[(size_t) h * 2 + 1];
            }
            fft.performRealOnlyInverseTransform (work.data());

            if (lvl == 0)
            {
                float peak = 0.0f;
                for (int i = 0; i < N; ++i) peak = juce::jmax (peak, std::abs (work[(size_t) i]));
                scale = peak > 1.0e-9f ? 1.0f / peak : 0.0f;
            }

            float* dst = wt->data.data() + ((size_t) f * kLevels + (size_t) lvl) * kStride;
            for (int i = 0; i < N; ++i) dst[i] = work[(size_t) i] * scale;
            dst[N] = dst[0];
        }
    }

    if (keepSource)
    {
        wt->sourceFrames.reserve ((size_t) wt->numFrames * N);
        for (auto& fr : frames)
            for (int i = 0; i < N; ++i)
                wt->sourceFrames.push_back (i < (int) fr.size() ? fr[(size_t) i] : 0.0f);
    }
    return wt;
}

// ------------------------------------------------------------------ helpers
namespace
{
/** Sum of sine harmonics via inverse FFT. amps[h] for h = 1..; phases optional (in cycles). */
Frame additive (const std::vector<float>& amps, const std::vector<float>* phases = nullptr)
{
    static thread_local juce::dsp::FFT fft (11);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    const int H = juce::jmin ((int) amps.size() - 1, N / 2 - 1);
    for (int h = 1; h <= H; ++h)
    {
        const float a = amps[(size_t) h];
        const float ph = phases != nullptr ? (*phases)[(size_t) h] * kTwoPi : 0.0f;
        // a * sin(wt + ph) = a*sin(ph)*cos + a*cos(ph)*sin
        buf[(size_t) h * 2]     = a * std::sin (ph);
        buf[(size_t) h * 2 + 1] = -a * std::cos (ph);
    }
    fft.performRealOnlyInverseTransform (buf.data());
    return Frame (buf.begin(), buf.begin() + N);
}

template <typename Fn>
Frame timeDomain (Fn&& fn)
{
    Frame f ((size_t) N);
    for (int i = 0; i < N; ++i) f[(size_t) i] = fn ((float) i / (float) N);
    return f;
}

Frame lerpFrames (const Frame& a, const Frame& b, float t)
{
    Frame r ((size_t) N);
    for (int i = 0; i < N; ++i) r[(size_t) i] = a[(size_t) i] + t * (b[(size_t) i] - a[(size_t) i]);
    return r;
}

Frame normalised (Frame f)
{
    float p = 0.0f;
    for (auto v : f) p = juce::jmax (p, std::abs (v));
    if (p > 0.0f) for (auto& v : f) v /= p;
    return f;
}

std::vector<float> sawAmps()    { std::vector<float> a (1024, 0.0f); for (int h = 1; h < 1024; ++h) a[(size_t) h] = 1.0f / (float) h; return a; }
std::vector<float> squareAmps() { std::vector<float> a (1024, 0.0f); for (int h = 1; h < 1024; h += 2) a[(size_t) h] = 1.0f / (float) h; return a; }

Frame sine()     { std::vector<float> a (2, 0.0f); a[1] = 1.0f; return normalised (additive (a)); }
Frame saw()      { return normalised (additive (sawAmps())); }
Frame square()   { return normalised (additive (squareAmps())); }
Frame triangle()
{
    std::vector<float> a (1024, 0.0f);
    for (int h = 1; h < 1024; h += 2) a[(size_t) h] = ((h / 2) % 2 == 0 ? 1.0f : -1.0f) / (float) (h * h);
    return normalised (additive (a));
}
Frame pulse (float w)
{
    std::vector<float> a (1024, 0.0f), ph (1024, 0.25f);
    for (int h = 1; h < 1024; ++h) a[(size_t) h] = std::sin (kPi * (float) h * w) / (float) h;
    return normalised (additive (a, &ph));
}

float formantGain (float freq, float vowelPos)
{
    static const float F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2290, 3010 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
    static const float G[3] = { 1.0f, 0.55f, 0.3f };
    const float p = juce::jlimit (0.0f, 3.999f, vowelPos * 4.0f);
    const int i = (int) p;
    const float t = p - (float) i;
    float g = 0.0f;
    for (int k = 0; k < 3; ++k)
    {
        const float fc = F[i][k] + t * (F[i + 1][k] - F[i][k]);
        const float bw = 60.0f + fc * 0.08f;
        const float x = (freq - fc) / bw;
        g += G[k] / (1.0f + x * x);
    }
    return g;
}

Frame formantFrame (float vowel, float f0, float tilt)
{
    std::vector<float> a (1024, 0.0f);
    for (int h = 1; h < 1024; ++h)
    {
        const float fh = f0 * (float) h;
        if (fh > 16000.0f) break;
        a[(size_t) h] = formantGain (fh, vowel) / std::pow ((float) h, tilt);
    }
    return normalised (additive (a));
}

std::vector<float> randomSpectrum (uint32_t seed, float slope, int maxH)
{
    Rng r (seed);
    std::vector<float> a (1024, 0.0f);
    for (int h = 1; h <= maxH; ++h)
    {
        const float v = r.uni();
        a[(size_t) h] = (v * v) / std::pow ((float) h, slope);
    }
    a[1] = juce::jmax (a[1], 0.6f);
    return a;
}

float foldTri (float x)
{
    // triangle fold into [-1, 1]
    x = (x + 1.0f) * 0.25f;
    x = x - std::floor (x);
    return 1.0f - 4.0f * std::abs (x - 0.5f) - 0.0f;
}

const char* kNames[] = {
    "Basic Shapes", "Sine Harmonics", "Saw Sweep", "PWM Square", "Sync Saw", "Formant Vowels", "Digital Spectra", "FM Bell",
    "FM Growl", "Organ", "Odd Sweep", "Hollow", "Phase Saw", "Growl Vowel", "Fold Sine", "Tri Fold",
    "Bit Saw", "Spectral Comb", "Choir", "Neuro", "Pluck", "Metallic", "Super Square", "Noise Spectra",
    "Saw Stack", "Square Sweep", "Squeal", "Talk Box", "Dirt Saw", "Brass", "Strings", "Glass Bell",
    "Mallet", "Warm Spectra", "Fidget", "Future Bwah"
};
} // namespace

namespace WavetableFactory
{
juce::StringArray names()
{
    juce::StringArray s;
    for (auto* n : kNames) s.add (n);
    return s;
}
int numFactoryTables() { return (int) std::size (kNames); }

std::unique_ptr<Wavetable> make (int index)
{
    index = juce::jlimit (0, numFactoryTables() - 1, index);
    std::vector<Frame> frames;
    frames.reserve (kFactoryFrames);
    const int F = kFactoryFrames;

    auto forEach = [&] (auto&& fn)
    {
        for (int k = 0; k < F; ++k)
            frames.push_back (fn ((float) k / (float) (F - 1)));
    };

    switch (index)
    {
        case 0: // Basic Shapes: sine > triangle > saw > square > narrow pulse
        {
            const Frame shapes[5] = { sine(), triangle(), saw(), square(), pulse (0.12f) };
            forEach ([&] (float m)
            {
                const float p = m * 4.0f;
                const int i = juce::jmin (3, (int) p);
                return lerpFrames (shapes[i], shapes[i + 1], p - (float) i);
            });
            break;
        }
        case 1: // Sine Harmonics
            forEach ([] (float m)
            {
                std::vector<float> a (1024, 0.0f);
                const float n = 1.0f + m * 63.0f;
                for (int h = 1; h <= (int) std::ceil (n); ++h)
                    a[(size_t) h] = (1.0f / std::sqrt ((float) h)) * juce::jlimit (0.0f, 1.0f, n - (float) h + 1.0f);
                return additive (a);
            });
            break;
        case 2: // Saw Sweep
            forEach ([] (float m)
            {
                std::vector<float> a (1024, 0.0f);
                const float fc = 1.2f * std::exp2 (m * 9.5f);
                for (int h = 1; h < 1024; ++h) { const float x = (float) h / fc; a[(size_t) h] = (1.0f / (float) h) / (1.0f + x * x * x * x); }
                return additive (a);
            });
            break;
        case 3: forEach ([] (float m) { return pulse (0.5f - m * 0.47f); }); break;
        case 4: // Sync Saw
            forEach ([] (float m)
            {
                const float r = 1.0f + m * 7.0f;
                return timeDomain ([r] (float t) { return 2.0f * frac (t * r) - 1.0f; });
            });
            break;
        case 5: forEach ([] (float m) { return formantFrame (m, 110.0f, 0.3f); }); break;
        case 6: // Digital Spectra: interpolate 8 random keyframes
        {
            std::vector<std::vector<float>> keys;
            for (int k = 0; k < 8; ++k) keys.push_back (randomSpectrum (1000u + (uint32_t) k * 77u, 0.5f, 6 + k * 6));
            forEach ([&] (float m)
            {
                const float p = m * 7.0f;
                const int i = juce::jmin (6, (int) p);
                const float t = p - (float) i;
                std::vector<float> a (1024, 0.0f);
                for (int h = 1; h < 1024; ++h) a[(size_t) h] = keys[(size_t) i][(size_t) h] + t * (keys[(size_t) i + 1][(size_t) h] - keys[(size_t) i][(size_t) h]);
                return additive (a);
            });
            break;
        }
        case 7: forEach ([] (float m) { const float I = m * 6.0f; return timeDomain ([I] (float t) { return std::sin (kTwoPi * t + I * std::sin (kTwoPi * 3.0f * t)); }); }); break;
        case 8: forEach ([] (float m) { const float I = m * 9.0f; return timeDomain ([I] (float t) { return std::sin (kTwoPi * t + I * std::sin (kTwoPi * t)); }); }); break;
        case 9: // Organ registrations
        {
            static const float reg[4][9] = { { 1, .8f, 0, .6f, 0, .4f, 0, .3f, 0 }, { 1, 0, .9f, 0, .7f, 0, .5f, 0, .4f },
                                             { .6f, 1, .8f, .8f, .6f, .6f, .5f, .5f, .4f }, { 1, .2f, .3f, .1f, 0, .8f, 0, .9f, .7f } };
            static const int hs[9] = { 1, 2, 3, 4, 5, 6, 8, 10, 12 };
            forEach ([] (float m)
            {
                const float p = m * 3.0f;
                const int i = juce::jmin (2, (int) p);
                const float t = p - (float) i;
                std::vector<float> a (16, 0.0f);
                for (int k = 0; k < 9; ++k) a[(size_t) hs[k]] = reg[i][k] + t * (reg[i + 1][k] - reg[i][k]);
                return additive (a);
            });
            break;
        }
        case 10:
            forEach ([] (float m)
            {
                std::vector<float> a (1024, 0.0f);
                const float fc = 1.5f * std::exp2 (m * 9.5f);
                for (int h = 1; h < 1024; h += 2) { const float x = (float) h / fc; a[(size_t) h] = (1.0f / (float) h) / (1.0f + x * x * x * x); }
                return additive (a);
            });
            break;
        case 11: // Hollow: moving spectral notches
            forEach ([] (float m)
            {
                std::vector<float> a (1024, 0.0f);
                const float c = 0.02f + m * 0.3f;
                for (int h = 1; h < 1024; ++h) a[(size_t) h] = (1.0f / (float) h) * std::abs (std::cos (kPi * (float) h * c));
                return additive (a);
            });
            break;
        case 12: // Phase Saw
        {
            const Frame s = saw();
            forEach ([&] (float m)
            {
                const int off = (int) (m * 0.5f * (float) N);
                Frame r ((size_t) N);
                for (int i = 0; i < N; ++i) r[(size_t) i] = s[(size_t) i] + s[(size_t) ((i + off) % N)];
                return r;
            });
            break;
        }
        case 13: // Growl Vowel
            forEach ([] (float m)
            {
                Frame f = formantFrame (m, 80.0f, 0.15f);
                for (auto& v : f) v = std::tanh (v * 3.5f);
                return f;
            });
            break;
        case 14: forEach ([] (float m) { const float g = 1.0f + m * 8.0f; return timeDomain ([g] (float t) { return std::sin (g * std::sin (kTwoPi * t)); }); }); break;
        case 15:
        {
            const Frame tri = triangle();
            forEach ([&] (float m)
            {
                Frame r ((size_t) N);
                for (int i = 0; i < N; ++i) r[(size_t) i] = foldTri (tri[(size_t) i] * (1.0f + m * 5.0f));
                return r;
            });
            break;
        }
        case 16:
        {
            const Frame s = saw();
            forEach ([&] (float m)
            {
                const float levels = std::exp2 (8.0f - m * 6.5f);
                Frame r ((size_t) N);
                for (int i = 0; i < N; ++i) r[(size_t) i] = std::round (s[(size_t) i] * levels) / levels;
                return r;
            });
            break;
        }
        case 17:
            forEach ([] (float m)
            {
                std::vector<float> a (1024, 0.0f);
                for (int h = 1; h < 1024; ++h)
                    a[(size_t) h] = std::pow ((float) h, -0.75f) * (0.5f + 0.5f * std::cos (kTwoPi * ((float) h * 0.11f + m)));
                return additive (a);
            });
            break;
        case 18: forEach ([] (float m) { return formantFrame (0.6f + 0.4f * std::sin (m * kPi), 220.0f, 0.9f + m * 0.4f); }); break;
        case 19:
            forEach ([] (float m)
            {
                const float I = 2.0f + m * 6.0f;
                return timeDomain ([I] (float t) { return std::tanh (3.0f * std::sin (kTwoPi * t + I * std::sin (kTwoPi * 2.0f * t))); });
            });
            break;
        case 20:
            forEach ([] (float m)
            {
                std::vector<float> a (1024, 0.0f);
                const float k = 0.002f + m * m * 0.4f;
                for (int h = 1; h < 1024; ++h) a[(size_t) h] = (1.0f / (float) h) * std::exp (-(float) h * k);
                return additive (a);
            });
            break;
        case 21:
        {
            static const int primes[] = { 1, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97 };
            forEach ([] (float m)
            {
                std::vector<float> a (128, 0.0f);
                for (int i = 0; i < (int) std::size (primes); ++i)
                {
                    const float c = (float) i / (float) std::size (primes);
                    const float x = (c - m) * 4.0f;
                    a[(size_t) primes[i]] = 0.25f + std::exp (-x * x);
                }
                return additive (a);
            });
            break;
        }
        case 22:
        {
            const Frame sq = square();
            forEach ([&] (float m)
            {
                Frame r ((size_t) N);
                for (int i = 0; i < N; ++i) r[(size_t) i] = sq[(size_t) i] + m * 0.8f * sq[(size_t) ((i * 2) % N)];
                return r;
            });
            break;
        }
        case 23: // Noise Spectra
        {
            std::vector<std::vector<float>> keys, phs;
            for (int k = 0; k < 4; ++k)
            {
                keys.push_back (randomSpectrum (777u + (uint32_t) k * 31u, 0.35f, 1000));
                Rng r (999u + (uint32_t) k);
                std::vector<float> p (1024);
                for (auto& v : p) v = r.uni();
                phs.push_back (p);
            }
            forEach ([&] (float m)
            {
                const float p = m * 3.0f;
                const int i = juce::jmin (2, (int) p);
                const float t = p - (float) i;
                std::vector<float> a (1024, 0.0f);
                for (int h = 1; h < 1024; ++h) a[(size_t) h] = keys[(size_t) i][(size_t) h] + t * (keys[(size_t) i + 1][(size_t) h] - keys[(size_t) i][(size_t) h]);
                return additive (a, &phs[(size_t) i]);
            });
            break;
        }
        case 24: // Saw Stack: saw -> saw + octave -> + two octaves -> + fifth-ish (3rd harmonic stack)
            forEach ([] (float m)
            {
                std::vector<float> a (1024, 0.0f);
                for (int h = 1; h < 1024; ++h) a[(size_t) h] += 1.0f / (float) h;
                const float o1 = juce::jlimit (0.0f, 1.0f, m * 3.0f), o2 = juce::jlimit (0.0f, 1.0f, m * 3.0f - 1.0f), o3 = juce::jlimit (0.0f, 1.0f, m * 3.0f - 2.0f);
                for (int h = 2; h < 1024; h += 2) a[(size_t) h] += o1 * 0.7f * 2.0f / (float) h;
                for (int h = 4; h < 1024; h += 4) a[(size_t) h] += o2 * 0.5f * 4.0f / (float) h;
                for (int h = 3; h < 1024; h += 3) a[(size_t) h] += o3 * 0.45f * 3.0f / (float) h;
                return additive (a);
            });
            break;
        case 25: // Square Sweep
            forEach ([] (float m)
            {
                std::vector<float> a (1024, 0.0f);
                const float fc = 1.2f * std::exp2 (m * 9.5f);
                for (int h = 1; h < 1024; h += 2) { const float x = (float) h / fc; a[(size_t) h] = (1.0f / (float) h) / (1.0f + x * x * x * x); }
                return additive (a);
            });
            break;
        case 26: // Squeal: saw with a resonant peak sweeping up the spectrum (electro / complextro)
            forEach ([] (float m)
            {
                std::vector<float> a (1024, 0.0f);
                const float pk = 2.0f * std::exp2 (m * 5.3f);
                for (int h = 1; h < 1024; ++h)
                {
                    const float x = ((float) h - pk) / (0.6f + pk * 0.12f);
                    a[(size_t) h] = 0.6f / (float) h + 1.6f / (1.0f + x * x) / std::sqrt ((float) h);
                }
                return additive (a);
            });
            break;
        case 27: // Talk Box: saw through moving vowel formants
            forEach ([] (float m) { return formantFrame (m, 140.0f, 0.0f); });
            break;
        case 28: // Dirt Saw: progressively saturated saw
        {
            const Frame s = saw();
            forEach ([&] (float m)
            {
                const float g = 1.0f + m * m * 12.0f;
                Frame r ((size_t) N);
                for (int i = 0; i < N; ++i) r[(size_t) i] = std::tanh (s[(size_t) i] * g + 0.2f * m * s[(size_t) ((i * 3) % N)]);
                return r;
            });
            break;
        }
        case 29: // Brass: saw with formant hump that opens
            forEach ([] (float m)
            {
                std::vector<float> a (1024, 0.0f);
                const float bright = 3.0f + m * 30.0f;
                for (int h = 1; h < 1024; ++h)
                {
                    const float x = (float) h / bright;
                    a[(size_t) h] = (1.0f / std::pow ((float) h, 0.7f)) * std::exp (-x * x * 0.5f) * (1.0f + 0.6f * std::exp (-std::pow (((float) h - 6.0f) / 3.0f, 2.0f)));
                }
                return additive (a);
            });
            break;
        case 30: // Strings: bowed saw, weak fundamental, body resonances
            forEach ([] (float m)
            {
                std::vector<float> a (1024, 0.0f);
                Rng r (5150u);
                for (int h = 1; h < 1024; ++h)
                {
                    const float body = 0.7f + 0.3f * std::sin ((float) h * 0.9f + m * 4.0f);
                    a[(size_t) h] = (1.0f / (float) h) * body * (h == 1 ? 0.55f : 1.0f) * std::exp (-(float) h * (0.03f - m * 0.022f));
                }
                return additive (a);
            });
            break;
        case 31: // Glass Bell: sparse high partials
            forEach ([] (float m)
            {
                std::vector<float> a (128, 0.0f);
                const int parts[] = { 1, 3, 5, 9, 12, 17, 21, 28, 34, 41 };
                for (int k = 0; k < (int) std::size (parts); ++k)
                {
                    const float c = (float) k / 9.0f;
                    a[(size_t) parts[k]] = std::exp (-std::pow ((c - m) * 2.2f, 2.0f)) + (k == 0 ? 0.8f : 0.05f);
                }
                return additive (a);
            });
            break;
        case 32: // Mallet: marimba-like 1 / 4 / 10 partials, softening
            forEach ([] (float m)
            {
                std::vector<float> a (64, 0.0f);
                a[1] = 1.0f; a[4] = 0.55f * (1.0f - m * 0.7f); a[10] = 0.3f * (1.0f - m); a[2] = 0.12f * m; a[20] = 0.08f * (1.0f - m);
                return additive (a);
            });
            break;
        case 33: // Warm Spectra: smooth, blurred low-mid spectra for pads
        {
            std::vector<std::vector<float>> keys;
            for (int k = 0; k < 5; ++k)
            {
                Rng r (8080u + (uint32_t) k * 13u);
                std::vector<float> a (1024, 0.0f);
                for (int h = 1; h < 1024; ++h) a[(size_t) h] = (0.5f + 0.5f * r.uni()) / std::pow ((float) h, 1.15f + 0.1f * (float) k);
                for (int pass = 0; pass < 3; ++pass)
                    for (int h = 2; h < 1022; ++h) a[(size_t) h] = 0.25f * a[(size_t) h - 1] + 0.5f * a[(size_t) h] + 0.25f * a[(size_t) h + 1];
                keys.push_back (a);
            }
            forEach ([&] (float m)
            {
                const float p = m * 4.0f;
                const int i = juce::jmin (3, (int) p);
                const float t = p - (float) i;
                std::vector<float> a (1024, 0.0f);
                for (int h = 1; h < 1024; ++h) a[(size_t) h] = keys[(size_t) i][(size_t) h] + t * (keys[(size_t) i + 1][(size_t) h] - keys[(size_t) i][(size_t) h]);
                return additive (a);
            });
            break;
        }
        case 34: // Fidget: square + saw hybrid with moving resonant notch/peak (electro house)
            forEach ([] (float m)
            {
                std::vector<float> a (1024, 0.0f);
                const float pk = 3.0f + m * 40.0f;
                for (int h = 1; h < 1024; ++h)
                {
                    const float base = (h % 2 == 1 ? 1.0f : 0.45f) / (float) h;
                    const float x = ((float) h - pk) / (1.0f + pk * 0.08f);
                    a[(size_t) h] = base * (1.0f + 6.0f / (1.0f + x * x));
                }
                return additive (a);
            });
            break;
        case 35: // Future Bwah: formant 'wah' bass, saturated (future house)
            forEach ([] (float m)
            {
                Frame f = formantFrame (0.15f + 0.7f * m, 65.0f, -0.1f);
                for (auto& v : f) v = std::tanh (v * 2.2f);
                return f;
            });
            break;
        default:
            forEach ([] (float) { return saw(); });
            break;
    }

    return Wavetable::fromFrames (frames, kNames[index], false);
}

std::unique_ptr<Wavetable> fromFile (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) { error = "Unsupported or unreadable audio file."; return nullptr; }

    const auto len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) 2048 * 512);
    if (len < 64) { error = "File is too short."; return nullptr; }

    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, reader->numChannels), len);
    reader->read (&buf, 0, len, 0, true, true);
    std::vector<float> mono ((size_t) len, 0.0f);
    for (int c = 0; c < buf.getNumChannels(); ++c)
        for (int i = 0; i < len; ++i) mono[(size_t) i] += buf.getSample (c, i) / (float) buf.getNumChannels();

    std::vector<Frame> frames;
    if (len % N == 0)
    {
        const int nf = juce::jmin (256, len / N);
        for (int f = 0; f < nf; ++f)
            frames.emplace_back (mono.begin() + f * N, mono.begin() + (f + 1) * N);
    }
    else
    {
        // sample conversion: slice into single-cycle frames
        const int nf = len < N * 3 ? 1 : 64;
        const int seg = len / nf;
        const int win = juce::jmin (seg, N);
        for (int f = 0; f < nf; ++f)
        {
            const int start = f * seg;
            Frame fr ((size_t) N);
            for (int i = 0; i < N; ++i)
            {
                const float x = (float) i / (float) N * (float) (win - 1);
                const int xi = (int) x;
                const float t = x - (float) xi;
                const int a = juce::jmin (len - 1, start + xi), b = juce::jmin (len - 1, start + xi + 1);
                fr[(size_t) i] = mono[(size_t) a] + t * (mono[(size_t) b] - mono[(size_t) a]);
            }
            // make loop seamless
            const int xf = 96;
            for (int i = 0; i < xf; ++i)
            {
                const float w = (float) (i + 1) / (float) xf;
                const float s = w * w * (3.0f - 2.0f * w);
                fr[(size_t) (N - xf + i)] = fr[(size_t) (N - xf + i)] * (1.0f - s) + fr[0] * s;
            }
            frames.push_back (std::move (fr));
        }
    }

    auto wt = Wavetable::fromFrames (frames, file.getFileNameWithoutExtension(), true);
    if (wt == nullptr) error = "Could not build wavetable.";
    return wt;
}

std::unique_ptr<Wavetable> fromSourceData (const juce::MemoryBlock& raw, int numFrames, const juce::String& name)
{
    const auto* p = static_cast<const float*> (raw.getData());
    const int total = (int) (raw.getSize() / sizeof (float));
    numFrames = juce::jlimit (1, 256, juce::jmin (numFrames, total / N));
    if (total < N) return nullptr;
    std::vector<Frame> frames;
    for (int f = 0; f < numFrames; ++f) frames.emplace_back (p + f * N, p + (f + 1) * N);
    return Wavetable::fromFrames (frames, name, true);
}
} // namespace WavetableFactory
} // namespace lxl
