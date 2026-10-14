#pragma once
#include <cmath>
#include <algorithm>
#include <vector>
#include <complex>

namespace lumen
{

constexpr double kPiE = 3.14159265358979323846;

enum EqType { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch };

struct BiquadCoefs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

// RBJ cookbook designs
inline BiquadCoefs design (int type, double f, double gainDb, double q, double fs)
{
    f = std::clamp (f, 10.0, 0.49 * fs);
    q = std::max (0.05, q);
    const double A = std::pow (10.0, gainDb / 40.0), w0 = 2.0 * kPiE * f / fs;
    const double cw = std::cos (w0), sw = std::sin (w0), alpha = sw / (2.0 * q);
    double b0, b1, b2, a0, a1, a2;
    switch (type)
    {
        case LowShelf:
        {
            const double sa = 2.0 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1) - (A - 1) * cw + sa); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - sa);
            a0 = (A + 1) + (A - 1) * cw + sa;       a1 = -2 * ((A - 1) + (A + 1) * cw);     a2 = (A + 1) + (A - 1) * cw - sa;
            break;
        }
        case HighShelf:
        {
            const double sa = 2.0 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1) + (A - 1) * cw + sa); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - sa);
            a0 = (A + 1) - (A - 1) * cw + sa;       a1 = 2 * ((A - 1) - (A + 1) * cw);       a2 = (A + 1) - (A - 1) * cw - sa;
            break;
        }
        case LowCut:  b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case HighCut: b0 = (1 - cw) / 2; b1 = 1 - cw;    b2 = (1 - cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case Notch:   b0 = 1; b1 = -2 * cw; b2 = 1; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case Bell:
        default:      b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

inline double magnitudeDb (const BiquadCoefs& c, double f, double fs)
{
    const std::complex<double> z = std::polar (1.0, -2.0 * kPiE * f / fs), z2 = z * z;
    const auto num = c.b0 + c.b1 * z + c.b2 * z2;
    const auto den = 1.0 + c.a1 * z + c.a2 * z2;
    return 20.0 * std::log10 (std::max (1e-12, std::abs (num / den)));
}

inline bool isGainType (int t) { return t == Bell || t == LowShelf || t == HighShelf; }
inline bool differs (float a, float b) { return std::fabs (a - b) > 1.0e-6f; }
inline bool isDynamic (float d) { return std::fabs (d) > 1.0e-3f; }

//==============================================================================
struct EqBand
{
    bool on = true; int type = Bell;
    float freq = 1000.0f, gainDb = 0.0f, q = 0.7f;
    float dynDb = 0.0f, threshDb = -30.0f; bool external = false;
};

class EqEngine
{
public:
    static constexpr int kBands = 6;
    static constexpr int kSub = 32;   // coefficient update interval (samples)

    void prepare (double rate)
    {
        fs = rate;
        for (auto& row : st) for (auto& s : row) s = {};
        for (auto& s : dst) s = {};
        for (int b = 0; b < kBands; ++b) { env[b] = 0.0f; curDyn[b] = 0.0f; lastGain[b] = 1e9f; coefs[b] = {}; }
        setTimes (10.0f, 150.0f);
        counter = 0;
    }

    void setTimes (float attackMs, float releaseMs)
    {
        atk = (float) std::exp (-1.0 / (std::max (0.1f, attackMs) * 0.001 * fs));
        rel = (float) std::exp (-1.0 / (std::max (1.0f, releaseMs) * 0.001 * fs));
    }

    void setBands (const EqBand* b)
    {
        for (int k = 0; k < kBands; ++k)
        {
            const bool changed = b[k].type != bands[k].type || differs (b[k].freq, bands[k].freq) || differs (b[k].q, bands[k].q);
            bands[k] = b[k];
            if (changed)
            {
                // detector: constant 0 dB peak band-pass at the band frequency
                const double w0 = 2.0 * kPiE * std::clamp ((double) b[k].freq, 10.0, 0.49 * fs) / fs;
                const double alpha = std::sin (w0) / (2.0 * std::max (0.5f, b[k].q)), a0 = 1 + alpha;
                det[k] = { alpha / a0, 0.0, -alpha / a0, -2 * std::cos (w0) / a0, (1 - alpha) / a0 };
                lastGain[k] = 1e9f;   // force recompute
            }
        }
    }

    // l, r: audio (in/out). dIn: mono internal detector; dSc: mono sidechain detector.
    inline void process (float& l, float& r, float dIn, float dSc) noexcept
    {
        // ---- detectors ----
        for (int k = 0; k < kBands; ++k)
        {
            const auto& b = bands[k];
            if (! b.on || ! isDynamic (b.dynDb) || ! isGainType (b.type)) { curDyn[k] = 0.0f; continue; }
            auto& d = dst[k];
            const double x = b.external ? dSc : dIn;
            const double y = det[k].b0 * x + d.z1;
            d.z1 = det[k].b1 * x - det[k].a1 * y + d.z2;
            d.z2 = det[k].b2 * x - det[k].a2 * y;
            const float a = (float) std::fabs (y);
            env[k] = a > env[k] ? atk * env[k] + (1.0f - atk) * a : rel * env[k] + (1.0f - rel) * a;
        }

        if (counter++ % kSub == 0)
        {
            for (int k = 0; k < kBands; ++k)
            {
                const auto& b = bands[k];
                float g = b.gainDb;
                if (b.on && isDynamic (b.dynDb) && isGainType (b.type))
                {
                    const float lvl = 20.0f * std::log10 (std::max (env[k], 1.0e-6f));
                    const float amt = std::clamp ((lvl - b.threshDb) / 12.0f, 0.0f, 1.0f);   // 12 dB soft range
                    curDyn[k] = b.dynDb * amt;
                    g += curDyn[k];
                }
                if (std::fabs (g - lastGain[k]) > 0.02f)
                {
                    coefs[k] = design (b.type, b.freq, g, b.q, fs);
                    lastGain[k] = g;
                }
            }
        }

        double io[2] = { l, r };
        for (int c = 0; c < 2; ++c)
        {
            double x = io[c];
            for (int k = 0; k < kBands; ++k)
            {
                if (! bands[k].on) continue;
                auto& s = st[c][k];
                const auto& q = coefs[k];
                const double y = q.b0 * x + s.z1;
                s.z1 = q.b1 * x - q.a1 * y + s.z2;
                s.z2 = q.b2 * x - q.a2 * y;
                x = y;
            }
            io[c] = x;
        }
        l = (float) io[0]; r = (float) io[1];
    }

    float dynGain (int k) const noexcept { return curDyn[k]; }
    double sampleRate() const noexcept { return fs; }

private:
    struct S { double z1 = 0, z2 = 0; };
    double fs = 48000.0;
    EqBand bands[kBands];
    BiquadCoefs coefs[kBands], det[kBands];
    S st[2][kBands], dst[kBands];
    float env[kBands] {}, curDyn[kBands] {}, lastGain[kBands] {};
    float atk = 0.99f, rel = 0.999f;
    unsigned counter = 0;
};

//==============================================================================
// Unmasking analysis: where does the sidechain (e.g. kick) collide with the track (e.g. bass)?
struct MaskZone { float freq, q, severity, scPeakDb; };

std::vector<MaskZone> findMasking (const std::vector<float>& track, const std::vector<float>& sc, double fs, int maxZones);

} // namespace lumen
