#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <complex>
#include <algorithm>
#include <vector>
#include <array>
#include <atomic>
#include <random>
#include "Devices.h"
#include "Loudness.h"
#include "Headphones.h"

namespace lumen::mixcheck
{
constexpr double kPi = 3.14159265358979323846;

// ------------------------------------------------------------------ biquad (RBJ)
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline double process (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    static double w0 (double fs, double f) { return 2 * kPi * std::clamp (f, 1.0, fs * 0.49) / fs; }
    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) { b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0; }
    void copyCoeffs (const Biquad& o) { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }
    void lp (double fs, double f, double q) { const double w = w0 (fs, f), al = std::sin (w) / (2 * q), c = std::cos (w); set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
    void hp (double fs, double f, double q) { const double w = w0 (fs, f), al = std::sin (w) / (2 * q), c = std::cos (w); set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
    void lp1 (double fs, double f) { const double k = std::tan (w0 (fs, f) / 2); set (k, k, 0, 1 + k, k - 1, 0); }
    void hp1 (double fs, double f) { const double k = std::tan (w0 (fs, f) / 2); set (1, -1, 0, 1 + k, k - 1, 0); }
    void bell (double fs, double f, double db, double q)
    { const double A = std::pow (10.0, db / 40), w = w0 (fs, f), al = std::sin (w) / (2 * q), c = std::cos (w); set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A); }
    void lowShelf (double fs, double f, double db, double q)
    {
        const double A = std::pow (10.0, db / 40), w = w0 (fs, f), c = std::cos (w), al = std::sin (w) / (2 * q), s = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + s), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - s), (A + 1) + (A - 1) * c + s, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - s);
    }
    void highShelf (double fs, double f, double db, double q)
    {
        const double A = std::pow (10.0, db / 40), w = w0 (fs, f), c = std::cos (w), al = std::sin (w) / (2 * q), s = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + s), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - s), (A + 1) - (A - 1) * c + s, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - s);
    }
    void unity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
    double magnitude (double fs, double f) const
    {
        const double w = 2 * kPi * f / fs; const std::complex<double> zz = std::polar (1.0, -w), zz2 = zz * zz;
        return std::abs ((b0 + b1 * zz + b2 * zz2) / (1.0 + a1 * zz + a2 * zz2));
    }
};

// Butterworth sections for order 1..8
inline int butterSections (Biquad* s, double fs, double f, int order, bool high)
{
    static const double q[9][4] = { {}, {}, { 0.70710678 }, {}, { 0.54119610, 1.30656296 }, {}, { 0.51763809, 0.70710678, 1.93185165 }, {}, { 0.50979558, 0.60134489, 0.89997622, 2.56291545 } };
    if (order <= 0 || f <= 0) return 0;
    if (order == 1) { if (high) s[0].hp1 (fs, f); else s[0].lp1 (fs, f); return 1; }
    const int o = std::min (8, order + (order & 1)), n = o / 2;
    for (int k = 0; k < n; ++k) { if (high) s[k].hp (fs, f, q[o][k]); else s[k].lp (fs, f, q[o][k]); }
    return n;
}

// ------------------------------------------------------------------ helpers
struct OnePole { double a = 0, z = 0; void set (double fs, double f) { a = std::exp (-2 * kPi * f / fs); } inline double process (double x) noexcept { z = x + a * (z - x); return z; } void reset() { z = 0; } };

class Delay
{
public:
    void prepare (int maxD) { int n = 1; while (n < maxD + 4) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; pos = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline void push (float x) noexcept { buf[(size_t) pos] = x; pos = (pos + 1) & mask; }
    inline float tap (float d) const noexcept    // linear interpolation, d >= 1
    {
        const float rp = (float) pos - 1.0f - d, fl = std::floor (rp); const int i = (int) fl; const float fr = rp - fl;
        const float a = buf[(size_t) (i & mask)], b = buf[(size_t) ((i + 1) & mask)]; return a + (b - a) * fr;
    }
private:
    std::vector<float> buf; int mask = 0, pos = 0;
};

// compressor / limiter, stereo linked, optional side-chain low pass
struct Comp
{
    double env = 0, attC = 0, relC = 0; float thr = 0, ratio = 0; Biquad sc[2]; bool useSc = false; double gr = 0;
    void set (double fs, float threshold, float r, float attMs, float relMs, float scHz)
    {
        thr = threshold; ratio = r;
        attC = std::exp (-1.0 / (std::max (0.1f, attMs) * 0.001 * fs)); relC = std::exp (-1.0 / (std::max (1.0f, relMs) * 0.001 * fs));
        useSc = scHz > 0; if (useSc) for (auto& b : sc) b.lp (fs, scHz, 0.70710678);
    }
    void reset() { env = 0; gr = 0; for (auto& b : sc) b.reset(); }
    inline double gainFor (double l, double r) noexcept
    {
        if (ratio <= 0) return 1.0;
        double x = std::max (std::abs (l), std::abs (r));
        if (useSc) { const double m = 0.5 * (l + r); x = std::abs (sc[1].process (sc[0].process (m))) * 1.4; }
        env = x > env ? x + attC * (env - x) : x + relC * (env - x);
        const double db = 20 * std::log10 (env + 1e-12), over = db - thr;
        gr = over > 0 ? -over * (1.0 - 1.0 / ratio) : 0.0;
        return std::pow (10.0, gr / 20);
    }
};

// 8-line feedback delay network room
class Room
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& d : line) d.prepare ((int) (fs * 0.2));
        pre.prepare ((int) (fs * 0.25));
        for (auto& l : lp) l.reset();
    }
    void clear() { for (auto& d : line) d.clear(); pre.clear(); for (auto& l : lp) l.reset(); }
    void set (float rt, float size, float predelayMs, float damp)
    {
        static const float base[8] { 0.0297f, 0.0371f, 0.0411f, 0.0437f, 0.0503f, 0.0571f, 0.0613f, 0.0687f };
        const float sc = 0.25f + 1.1f * std::clamp (size, 0.05f, 1.0f);
        for (int k = 0; k < 8; ++k)
        {
            len[k] = std::max (8.0f, base[k] * sc * (float) fs);
            g[k] = (float) std::pow (10.0, -3.0 * len[k] / (std::max (0.05f, rt) * fs));
            lp[k].set (fs, damp);
        }
        preD = std::max (1.0f, predelayMs * 0.001f * (float) fs);
        float g2 = 0; for (float v : g) g2 += v * v; g2 /= 8.0f;
        inGain = 0.35f * std::sqrt (std::max (0.05f, 1.0f - g2));     // energy-normalised: long rooms do not boom
    }
    inline void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        pre.push (0.5f * (inL + inR)); const float side = 0.5f * (inL - inR);
        const float x = pre.tap (preD);
        float o[8];
        for (int k = 0; k < 8; ++k) o[k] = (float) lp[k].process (line[k].tap (len[k])) * g[k];
        float s = 0; for (float v : o) s += v; s *= 0.25f;                     // Householder feedback
        for (int k = 0; k < 8; ++k) line[k].push (o[k] - s + ((k & 1) ? x - side * 0.5f : x + side * 0.5f) * inGain);
        outL = (o[0] + o[2] + o[4] + o[6]) * 0.5f; outR = (o[1] + o[3] + o[5] + o[7]) * 0.5f;
    }
private:
    double fs = 48000; Delay line[8], pre; OnePole lp[8]; float len[8] {}, g[8] {}, preD = 1, inGain = 0.35f;
};

// procedural environment noise
class NoiseGen
{
public:
    void prepare (double sampleRate, unsigned seed)
    {
        fs = sampleRate; rng.seed (seed);
        for (auto& b : f) b.reset();
        for (auto& p : pk) p = {};
        kind = -1;
    }
    void setType (int type)
    {
        if (type == kind) return;
        kind = type;
        for (auto& b : f) b.reset();
        for (int c = 0; c < 2; ++c)
        {
            Biquad* q = f + c * 3;
            switch (type)
            {
                case Crowd:   q[0].hp (fs, 250, 0.7); q[1].lp (fs, 3200, 0.7); q[2].bell (fs, 900, 6, 0.8); break;
                case Road:    q[0].lp (fs, 180, 0.7); q[1].lp (fs, 400, 0.7); q[2].bell (fs, 1100, -6, 0.6); break;
                case Street:  q[0].hp (fs, 120, 0.7); q[1].lp (fs, 4000, 0.7); q[2].bell (fs, 500, 4, 0.7); break;
                case Hiss:    q[0].hp (fs, 1500, 0.7); q[1].lp (fs, 15000, 0.7); q[2].unity(); break;
                case Crackle: q[0].hp (fs, 300, 0.7); q[1].lp (fs, 4500, 0.7); q[2].unity(); break;
                case Living:  q[0].hp (fs, 60, 0.7); q[1].lp (fs, 1500, 0.7); q[2].unity(); break;
                default: break;
            }
        }
    }
    inline void next (float& l, float& r) noexcept
    {
        if (kind <= NoNoise) { l = r = 0; return; }
        float v[2];
        for (int c = 0; c < 2; ++c)
        {
            const float w = nd (rng);
            auto& p = pk[c]; p[0] = 0.99765f * p[0] + w * 0.0990460f; p[1] = 0.96300f * p[1] + w * 0.2965164f; p[2] = 0.57000f * p[2] + w * 1.0526913f;
            float x = (p[0] + p[1] + p[2] + w * 0.1848f) * 0.25f;
            if (kind == Hiss) x = w * 0.5f;
            if (kind == Crackle) x = w * 0.15f + ((uni (rng) < 0.0006f) ? (uni (rng) - 0.5f) * 30.0f : 0.0f);
            v[c] = (float) f[c * 3 + 2].process (f[c * 3 + 1].process (f[c * 3].process (x)));
        }
        if ((++t & 255) == 0)
            for (int k = 0; k < 3; ++k) mod[k] += (uni (rng) - mod[k]) * (kind == Crowd ? 0.02f : 0.004f);
        const float m = kind == Crowd ? 0.55f + 0.9f * mod[0] * mod[1] : kind == Street ? 0.6f + 0.6f * mod[2] : 1.0f;
        l = v[0] * m; r = v[1] * m;
    }
private:
    double fs = 48000; int kind = -1; unsigned t = 0;
    Biquad f[6]; std::array<float, 3> pk[2] {}; float mod[3] { 0.5f, 0.5f, 0.5f };
    std::mt19937 rng { 7 }; std::normal_distribution<float> nd { 0.0f, 1.0f }; std::uniform_real_distribution<float> uni { 0.0f, 1.0f };
};

// ------------------------------------------------------------------ broadcast processing (FM / AM)
class Broadcast
{
public:
    void prepare (double sampleRate) { fs = sampleRate; reset(); }
    void reset()
    {
        agcEnv = 1e-4; agcGain = 1;
        for (auto& b : split) b.reset();
        for (auto& c : band) c.reset();
        for (auto& b : emph) b.reset();
        for (auto& b : post) b.reset();
    }
    void set (bool am)
    {
        isAm = am;
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < 2; ++k) { split[c * 8 + k].lp (fs, 200, 0.70710678); split[c * 8 + 2 + k].hp (fs, 200, 0.70710678); split[c * 8 + 4 + k].lp (fs, 3000, 0.70710678); split[c * 8 + 6 + k].hp (fs, 3000, 0.70710678); }
            emph[c * 2].highShelf (fs, 3200, am ? 6 : 10, 0.7); emph[c * 2 + 1].highShelf (fs, 3200, am ? -6 : -10, 0.7);
        }
        const float thr = am ? -30.0f : -24.0f, ratio = am ? 10.0f : 5.0f;
        band[0].set (fs, thr, ratio, 12, 220, 0); band[1].set (fs, thr, ratio, 4, 140, 0); band[2].set (fs, thr, ratio, 1.5f, 80, 0);
        // FM: steep 15 kHz pilot-protection low pass (two 8th-order Butterworths)
        nPost = 0;
        if (! am) { const int h = butterSections (post, fs, 15800, 8, false); for (int k = 0; k < h; ++k) post[h + k].copyCoeffs (post[k]); nPost = 2 * h; }
        for (int k = 0; k < nPost; ++k) post[8 + k].copyCoeffs (post[k]);
    }
    inline void process (float& l, float& r) noexcept
    {
        const double p = 0.5 * ((double) l * l + (double) r * r);
        agcEnv += (p - agcEnv) * (1.0 / (2.0 * fs));
        const double want = std::clamp (std::sqrt (0.01 / (agcEnv + 1e-9)), 0.3, 6.0);
        agcGain += (want - agcGain) * (1.0 / (1.5 * fs));
        double x[2] { l * agcGain, r * agcGain }, b[2][3];
        for (int c = 0; c < 2; ++c)
        {
            const double lo = split[c * 8 + 1].process (split[c * 8].process (x[c]));
            const double rest = split[c * 8 + 3].process (split[c * 8 + 2].process (x[c]));
            b[c][0] = lo;
            b[c][1] = split[c * 8 + 5].process (split[c * 8 + 4].process (rest));
            b[c][2] = split[c * 8 + 7].process (split[c * 8 + 6].process (rest));
        }
        const double makeup = isAm ? 5.0 : 3.2;
        for (int k = 0; k < 3; ++k) { const double g = band[k].gainFor (b[0][k], b[1][k]) * makeup; b[0][k] *= g; b[1][k] *= g; }
        for (int c = 0; c < 2; ++c)
        {
            double y = b[c][0] + b[c][1] + b[c][2];          // LR4 bands are in phase
            y = emph[c * 2].process (y);
            y = std::clamp (y, -0.9, 0.9);                   // broadcast clipper (pre-emphasised: highs clip first)
            y = emph[c * 2 + 1].process (y);
            for (int k = 0; k < nPost; ++k) y = post[c * 8 + k].process (y);
            (c == 0 ? l : r) = (float) (y * 0.8);
        }
    }
private:
    double fs = 48000, agcEnv = 1e-4, agcGain = 1; bool isAm = false;
    Biquad split[16], emph[4], post[16]; Comp band[3]; int nPost = 0;
};

// ------------------------------------------------------------------ binaural speaker virtualisation (headphone listening)
// two virtual speakers at +-30 degrees: interaural delay, frequency-dependent head shadow, ear-facing presence,
// six early reflections per speaker (desk, floor, ceiling, side walls) -> the sound sits in front of you, not inside your head
class Binaural
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& d : dl) d.prepare ((int) (fs * 0.03));
        itd = (float) (0.00027 * fs);
        for (int s = 0; s < 2; ++s)
        {
            ipsi[s].highShelf (fs, 2500, 5.5, 0.6);
            contraA[s].highShelf (fs, 1500, -9.0, 0.55); contraB[s].lp (fs, 9000, 0.6);
            erLp[s].lp (fs, 5000, 0.6);
        }
        calibrate();
        reset();
    }
    // self-calibration: a centred (phantom) source must come out tonally flat, otherwise the
    // virtual room would colour every mix decision -> octave-band compensation, solved iteratively
    void calibrate()
    {
        constexpr int ord = 13, N = 1 << ord;
        juce::dsp::FFT fft (ord); std::vector<float> buf (2 * N);
        float gain[kBands] {}; norm = 1.0;
        for (int pass = 0; pass < 6; ++pass)
        {
            for (int k = 0; k < kBands; ++k) for (int e = 0; e < 2; ++e) comp[e][k].bell (fs, 31.25 * std::pow (2.0, k), gain[k], 1.1);
            reset();
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < N; ++i) { double l = i == 0 ? 1.0 : 0.0, r = l; process (l, r); buf[(size_t) i] = (float) l; }
            fft.performFrequencyOnlyForwardTransform (buf.data());
            double e[kBands] {}; int cnt[kBands] {};
            for (int b = 1; b < N / 2; ++b)
            {
                const double f = b * fs / N; const int k = (int) std::floor (std::log2 (f / 22.1));
                if (k >= 0 && k < kBands) { e[k] += (double) buf[(size_t) b] * buf[(size_t) b]; ++cnt[k]; }
            }
            double db[kBands], mean = 0; int m = 0;
            for (int k = 0; k < kBands; ++k) { db[k] = cnt[k] ? 10 * std::log10 (e[k] / cnt[k] + 1e-30) : 0; if (k >= 1 && k <= 9) { mean += db[k]; ++m; } }
            mean /= std::max (1, m);
            for (int k = 0; k < kBands; ++k) gain[k] = (float) std::clamp ((double) gain[k] - (db[k] - mean) * 0.85, -12.0, 12.0);
            norm *= std::pow (10.0, -mean / 20.0);
        }
        for (int k = 0; k < kBands; ++k) for (int e = 0; e < 2; ++e) comp[e][k].bell (fs, 31.25 * std::pow (2.0, k), gain[k], 1.1);
        compGain.assign (gain, gain + kBands);
    }
    std::vector<float> compGain;
    void reset()
    {
        for (auto& d : dl) d.clear();
        for (int s = 0; s < 2; ++s) { ipsi[s].reset(); contraA[s].reset(); contraB[s].reset(); erLp[s].reset(); }
        for (auto& e : comp) for (auto& b : e) b.reset();
    }
    inline void process (double& l, double& r) noexcept
    {
        static const float erMs[6] { 2.9f, 4.7f, 6.1f, 8.3f, 11.2f, 14.9f }, erG[6] { 0.11f, 0.09f, 0.075f, 0.06f, 0.05f, 0.04f }, erSame[6] { 0.7f, 0.3f, 0.6f, 0.4f, 0.55f, 0.45f };
        const double in[2] { l, r };
        double ear[2] { 0, 0 };
        for (int s = 0; s < 2; ++s)
        {
            dl[s].push ((float) in[s]);
            const int o = 1 - s;
            ear[s] += ipsi[s].process (in[s]);
            ear[o] += contraB[s].process (contraA[s].process (dl[s].tap (itd)));
            double same = 0, other = 0;
            for (int k = 0; k < 6; ++k) { const double v = dl[s].tap (erMs[k] * 0.001f * (float) fs) * erG[k]; same += v * erSame[k]; other += v * (1.0f - erSame[k]); }
            const double e = erLp[s].process (same + other);
            ear[s] += e * 0.55; ear[o] += e * 0.45;
        }
        for (int k = 0; k < kBands; ++k) { ear[0] = comp[0][k].process (ear[0]); ear[1] = comp[1][k].process (ear[1]); }
        l = ear[0] * norm; r = ear[1] * norm;
    }
private:
    static constexpr int kBands = 10;
    double fs = 48000; Delay dl[2]; float itd = 13; Biquad ipsi[2], contraA[2], contraB[2], erLp[2], comp[2][kBands];
    double norm = 1.0;
};

// ------------------------------------------------------------------ headphone correction (always last)
class HeadphoneEq
{
public:
    HeadphoneEq() { bands.reserve (16); }
    void prepare (double sampleRate) { fs = sampleRate; set (bands, amount); }
    void set (const std::vector<PeqBand>& b, float amt)
    {
        bands = b; amount = amt; n = (int) std::min<size_t> (16, b.size());
        for (int k = 0; k < n; ++k)
        {
            const auto& e = b[(size_t) k]; const double g = e.g * amt;
            Biquad& q = s[k];
            switch (e.type)
            {
                case 0: q.bell (fs, e.f, g, e.q); break;
                case 1: q.lowShelf (fs, e.f, g, std::min (e.q, 1.0f)); break;
                case 2: q.highShelf (fs, e.f, g, std::min (e.q, 1.0f)); break;
                case 3: if (amt > 0.5f) q.lp (fs, e.f, e.q); else q.unity(); break;
                default: if (amt > 0.5f) q.hp (fs, e.f, e.q); else q.unity(); break;
            }
            q.reset(); s[16 + k] = q;
        }
        // preamp: never boost past 0 dB (a full-scale mix cannot clip)
        double mx = 0;
        for (double f = 20; f < 20000; f *= 1.02) { double m = 1; for (int k = 0; k < n; ++k) m *= s[k].magnitude (fs, f); mx = std::max (mx, m); }
        pre = (float) (1.0 / std::max (1.0, mx));
    }
    bool active() const noexcept { return n > 0; }
    float preampDb() const { return 20.0f * std::log10 (pre); }
    double responseDb (double f) const { double m = pre; for (int k = 0; k < n; ++k) m *= s[k].magnitude (fs, f); return 20 * std::log10 (m + 1e-12); }
    inline void process (float* L, float* R, int len) noexcept
    {
        if (n == 0) return;
        for (int i = 0; i < len; ++i)
        {
            double l = L[i] * pre, r = R[i] * pre;
            for (int k = 0; k < n; ++k) { l = s[k].process (l); r = s[16 + k].process (r); }
            L[i] = (float) l; R[i] = (float) r;
        }
    }
private:
    double fs = 48000; std::vector<PeqBand> bands; float amount = 1, pre = 1; int n = 0; Biquad s[32];
};

// ------------------------------------------------------------------ user controls
struct Controls
{
    float room = 1, noise = 0, drive = 1;       // multipliers of the device defaults
    float volume = 83;                          // listening level, dB SPL
    bool headphones = true, levelMatch = true;  // headphones = virtual speakers in front of you
    float outDb = 0;
};

// =====================================================================================
// One device chain (also used offline by ANALYZE ALL)
// =====================================================================================
class DeviceChain
{
public:
    void prepare (double sampleRate, unsigned seed = 1)
    {
        fs = sampleRate;
        room.prepare (fs); noise.prepare (fs, seed); bc.prepare (fs); bin.prepare (fs);
        carDelay.prepare ((int) (fs * 0.01));
        kIn.prepare (fs); kInR.prepare (fs); kOut.prepare (fs); kOutR.prepare (fs);
        lmC = 1.0 / (fs * 2.5); exRel = std::exp (-1.0 / (0.04 * fs));
        current = -1; setDevice (0);
        lmGain = lmTarget = 1; pIn = pOut = 1e-9; streamGain = 1; outG = 1;
    }

    void setDevice (int idx)
    {
        idx = std::clamp (idx, 0, numDevices() - 1);
        if (idx == current) return;
        current = idx;
        const auto& d = devices()[(size_t) idx];
        dev = &d;
        nHp = butterSections (hpS, fs, d.hp, d.hpOrder, true); nLp = butterSections (lpS, fs, d.lp, d.lpOrder, false);
        for (int k = 0; k < 4; ++k) { hpS[4 + k].copyCoeffs (hpS[k]); lpS[4 + k].copyCoeffs (lpS[k]); }
        nEq = (int) std::min<size_t> (8, d.eq.size());
        for (int k = 0; k < nEq; ++k)
        {
            const auto& e = d.eq[(size_t) k];
            if (e.type == 0) eq[k].bell (fs, e.f, e.g, e.q); else if (e.type == 1) eq[k].lowShelf (fs, e.f, e.g, e.q); else eq[k].highShelf (fs, e.f, e.g, e.q);
            eq[8 + k].copyCoeffs (eq[k]);
        }
        for (int k = 0; k < 4; ++k) { monoLp[k].lp (fs, std::max (20.0f, d.monoBelow), 0.70710678); monoHp[k].hp (fs, std::max (20.0f, d.monoBelow), 0.70710678); }
        for (auto& b : exLp) b.lp (fs, d.exFreq, 0.70710678);
        lim.set (fs, d.limThr, d.limRatio, d.limAtt, d.limRel, d.limSc);
        room.set (d.rt, d.size, d.predelay, d.damp);
        noise.setType (d.noise);
        if (d.special == FM || d.special == AM) bc.set (d.special == AM);
        carD = d.delayRms * 0.001f * (float) fs; carG = std::pow (10.0f, d.gainRdB / 20.0f);
        setControls (ctl);
        resetState();
        // level match: start from what this device needed last time (no level drift after a switch)
        warm = (int) (fs * 0.6);
        if (lmMemory != nullptr && lmMemory[idx] > 0.0f)
        {
            pOut = pIn * lmMemory[idx]; warm = 0;
            lmGain = lmTarget = (float) std::clamp (std::sqrt (1.0 / lmMemory[idx]), 0.06, 16.0);
            outG = (ctl.levelMatch && d.special != Stream ? lmTarget : 1.0f) * std::pow (10.0f, ctl.outDb / 20.0f);
        }
        else pOut = pIn;
    }
    void syncInputPower (const DeviceChain& o) noexcept { pIn = o.pIn; }
    float* lmMemory = nullptr;                    // shared per-device memory (owner provides >= numDevices floats)
    int device() const noexcept { return current; }
    const Device& info() const noexcept { return *dev; }

    void setControls (const Controls& c)
    {
        ctl = c;
        // loudness contour: quieter listening -> less bass / air perceived (ISO 226 trend)
        const double d = std::clamp (83.0 - c.volume, -17.0, 43.0);
        contour[0].lowShelf (fs, 110, std::clamp (-0.33 * d, -16.0, 5.0), 0.6); contour[1].copyCoeffs (contour[0]);
        contour[2].highShelf (fs, 9000, std::clamp (-0.09 * d, -5.0, 2.0), 0.6); contour[3].copyCoeffs (contour[2]);
        driveG = std::pow (10.0f, (dev->driveDb * c.drive + (dev->driveDb > 0 ? std::clamp (((float) c.volume - 83.0f) * 0.3f, -6.0f, 9.0f) : 0.0f)) / 20.0f);
        roomMix = std::clamp (dev->room * c.room, 0.0f, 0.9f);
        noiseG = c.noise > 0.001f && dev->noise != NoNoise ? std::pow (10.0f, dev->noiseDb / 20.0f) * c.noise : 0.0f;
    }

    void resetState()
    {
        for (auto& b : hpS) b.reset();
        for (auto& b : lpS) b.reset();
        for (auto& b : eq) b.reset();
        for (auto& b : monoLp) b.reset();
        for (auto& b : monoHp) b.reset();
        for (auto& b : exLp) b.reset();
        for (auto& b : contour) b.reset();
        lim.reset(); room.clear(); bc.reset(); carDelay.clear(); bin.reset();
        exEnv = 0;
    }

    void setStreamGain (float g) noexcept { streamGain = g; }
    juce::dsp::Convolution* customIR = nullptr; bool customLoaded = false;

    void process (float* L, float* R, int n) noexcept
    {
        const auto& d = *dev;
        if (current == 0) { applyOut (L, R, n, true); return; }
        for (int i = 0; i < n; ++i) { const double a = kIn.process (L[i]), b = kInR.process (R[i]); pIn += (0.5 * (a * a + b * b) - pIn) * lmC; }

        if (d.special == Custom && customIR != nullptr && customLoaded)
        {
            float* ch[2] { L, R }; juce::dsp::AudioBlock<float> blk (ch, 2, (size_t) n);
            customIR->process (juce::dsp::ProcessContextReplacing<float> (blk));
        }
        for (int i = 0; i < n; ++i)
        {
            double l = L[i], r = R[i];
            // listener position (car): far speaker later and quieter
            if (d.delayRms > 0) { carDelay.push ((float) r); r = carDelay.tap (std::max (1.0f, carD)) * carG; }
            // stereo: mono lows, width
            if (d.monoBelow > 0)
            {
                // Linkwitz-Riley split: lows summed to mono, highs untouched (lo + hi = allpass, no comb)
                const double ll = monoLp[1].process (monoLp[0].process (l)), rl = monoLp[3].process (monoLp[2].process (r));
                const double hl = monoHp[1].process (monoHp[0].process (l)), hr = monoHp[3].process (monoHp[2].process (r));
                const double m = 0.5 * (ll + rl); l = m + hl; r = m + hr;
            }
            if (d.width < 0.999f) { const double m = 0.5 * (l + r), s = 0.5 * (l - r) * d.width; l = m + s; r = m - s; }
            if (d.special == FM || d.special == AM) { float fl = (float) l, fr = (float) r; bc.process (fl, fr); l = fl; r = fr; }
            // speaker / box response
            for (int k = 0; k < nHp; ++k) { l = hpS[k].process (l); r = hpS[4 + k].process (r); }
            for (int k = 0; k < nEq; ++k) { l = eq[k].process (l); r = eq[8 + k].process (r); }
            for (int k = 0; k < nLp; ++k) { l = lpS[k].process (l); r = lpS[4 + k].process (r); }
            // device DSP limiter (protects the driver, so it comes first)
            if (d.limRatio > 0) { const double g = lim.gainFor (l, r); l *= g; r *= g; }
            // driver: soft limit, harder when the cone moves a lot (bass excursion -> IMD)
            if (d.driveDb > 0.01f)
            {
                const double ex = std::abs (exLp[1].process (exLp[0].process (0.5 * (l + r))));
                exEnv = ex > exEnv ? ex : exEnv * exRel;
                const double g = driveG * (1.0 + d.excursion * std::min (1.0, exEnv * 3.0));
                l = shape (l * g) / driveG; r = shape (r * g) / driveG;
            }
            // listening level
            l = contour[2].process (contour[0].process (l)); r = contour[3].process (contour[1].process (r));
            // room
            if (roomMix > 0.0005f)
            {
                float wl, wr; room.process ((float) l, (float) r, wl, wr);
                l = l * (1.0 - 0.4 * roomMix) + wl * roomMix; r = r * (1.0 - 0.4 * roomMix) + wr * roomMix;
            }
            // headphones: the device's speakers are placed in front of you
            if (ctl.headphones && d.speaker) bin.process (l, r);
            L[i] = (float) l; R[i] = (float) r;
        }
        for (int i = 0; i < n; ++i)
        {
            const double a = kOut.process (L[i]), b = kOutR.process (R[i]);
            pOut += (0.5 * (a * a + b * b) - pOut) * (warm > 0 ? lmC * 12.0 : lmC);
            if (warm > 0) --warm;
        }
        if (lmMemory != nullptr && warm == 0 && pIn > 1e-9 && pOut > 1e-12) lmMemory[current] = (float) (pOut / pIn);
        lmTarget = (d.special != Stream && ctl.levelMatch && pIn > 1e-10 && pOut > 1e-12) ? (float) std::clamp (std::sqrt (pIn / pOut), 0.06, 16.0) : 1.0f;
        // environment noise: the world, not the device -> not level matched
        if (noiseG > 0)
            for (int i = 0; i < n; ++i) { float a, b; noise.next (a, b); L[i] += a * noiseG / std::max (0.06f, lmGain); R[i] += b * noiseG / std::max (0.06f, lmGain); }
        applyOut (L, R, n, false);
    }

    float levelMatchDb() const noexcept { return 20.0f * std::log10 (lmGain); }
    float limiterGrDb() const noexcept { return (float) lim.gr; }

private:
    static inline double shape (double x) noexcept { const double ax = std::abs (x); return ax < 0.6 ? x : (x > 0 ? 1.0 : -1.0) * (0.6 + 0.4 * std::tanh ((ax - 0.6) / 0.4)); }

    void applyOut (float* L, float* R, int n, bool bypass) noexcept
    {
        const float outT = std::pow (10.0f, ctl.outDb / 20.0f) * (bypass ? 1.0f : (dev->special == Stream ? streamGain : lmTarget));
        const float c = (float) (1.0 - std::exp (-1.0 / (0.04 * fs)));
        for (int i = 0; i < n; ++i)
        {
            outG += (outT - outG) * c;
            if (! bypass && dev->special != Stream) lmGain += (lmTarget - lmGain) * c;
            L[i] *= outG; R[i] *= outG;
        }
    }

    struct KW   // K-weighting for level matching
    {
        Biquad a, b;
        void prepare (double rate) { a.highShelf (rate, 1500, 4.0, 0.7071); b.hp (rate, 38, 0.5); }
        inline double process (double x) noexcept { return b.process (a.process (x)); }
    } kIn, kInR, kOut, kOutR;

    double fs = 48000; int current = -1; const Device* dev = &devices()[0];
    Controls ctl;
    Biquad hpS[8], lpS[8], eq[16], monoLp[4], monoHp[4], exLp[2], contour[4];
    int nHp = 0, nLp = 0, nEq = 0;
    Comp lim; Room room; NoiseGen noise; Broadcast bc; Binaural bin;
    Delay carDelay; float carD = 0, carG = 1;
    double exEnv = 0, exRel = 0.9995; float driveG = 1, roomMix = 0, noiseG = 0;
    double pIn = 1e-9, pOut = 1e-9, lmC = 1.0 / (48000.0 * 2.5);
    float lmTarget = 1, lmGain = 1, outG = 1, streamGain = 1; int warm = 0;
};

} // namespace lumen::mixcheck
