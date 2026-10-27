#pragma once
#include <cmath>
#include <complex>
#include <vector>
#include <array>
#include <algorithm>
#include <atomic>
#include <memory>

// =====================================================================================
// Lumen Master - shared DSP pieces: biquads (digital RBJ + exact analog magnitude),
// Linkwitz-Riley crossovers, envelopes.
// =====================================================================================
namespace lumen::master
{
inline constexpr double kPi = 3.14159265358979323846;   // (M_PI is not defined by MSVC)
inline double dbToGain (double d) { return std::pow (10.0, d / 20.0); }
inline double gainToDb (double g) { return 20.0 * std::log10 (std::max (g, 1e-12)); }

enum BandType { Bell, LowShelf, HighShelf, LowCut, HighCut, TiltShelf };
enum BandChan { ChStereo, ChMid, ChSide };

struct Coefs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

// transposed direct form II, double precision, two channels
struct Biquad
{
    Coefs c;
    double s1[2] {}, s2[2] {};
    void reset() { s1[0] = s1[1] = s2[0] = s2[1] = 0; }
    inline double tick (double x, int ch) noexcept
    {
        const double y = c.b0 * x + s1[ch];
        s1[ch] = c.b1 * x - c.a1 * y + s2[ch];
        s2[ch] = c.b2 * x - c.a2 * y;
        return y;
    }
};

// RBJ cookbook designs
inline Coefs designRbj (int type, double fs, double f, double q, double gDb)
{
    f = std::clamp (f, 5.0, 0.49 * fs);
    q = std::max (0.05, q);
    const double A = std::pow (10.0, gDb / 40.0), w = 2.0 * kPi * f / fs, cw = std::cos (w), sw = std::sin (w);
    const double al = sw / (2.0 * q);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type)
    {
        case Bell:      b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; break;
        case LowShelf:
        {
            const double sa = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) - (A - 1) * cw + sa); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - sa);
            a0 = (A + 1) + (A - 1) * cw + sa; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - sa; break;
        }
        case HighShelf:
        {
            const double sa = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) + (A - 1) * cw + sa); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - sa);
            a0 = (A + 1) - (A - 1) * cw + sa; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sa; break;
        }
        case LowCut:    b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        case HighCut:   b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        default: break;
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

// Q values of the 2nd-order sections that make 12 / 24 / 48 dB/oct Butterworth cuts
inline std::vector<double> cutQs (int slope, double userQ)
{
    if (slope <= 0) return { userQ };
    if (slope == 1) return { 0.54119610, 1.3065630 };
    return { 0.50979558, 0.60134489, 0.89997622, 2.5629154 };
}

// exact analog magnitude of one band (used by the linear-phase EQ: no cramping near Nyquist)
inline double analogMag (int type, double f, double f0, double q, double gDb, int slope)
{
    using C = std::complex<double>;
    const C s (0.0, f / std::max (1.0, f0));
    const double A = std::pow (10.0, gDb / 40.0);
    q = std::max (0.05, q);
    switch (type)
    {
        case Bell:      return std::abs ((s * s + s * (A / q) + 1.0) / (s * s + s / (A * q) + 1.0));
        case LowShelf:  return std::abs (A * (s * s + (std::sqrt (A) / q) * s + A) / (A * s * s + (std::sqrt (A) / q) * s + 1.0));
        case HighShelf: return std::abs (A * (A * s * s + (std::sqrt (A) / q) * s + 1.0) / (s * s + (std::sqrt (A) / q) * s + A));
        case TiltShelf:
        {
            // tilt around f0: low shelf -g/2 and high shelf +g/2 (gentle Q)
            const double a = std::pow (10.0, -gDb / 80.0), b = std::pow (10.0, gDb / 80.0), qq = 0.5;
            const C lo = a * (s * s + (std::sqrt (a) / qq) * s + a) / (a * s * s + (std::sqrt (a) / qq) * s + 1.0);
            const C hi = b * (b * s * s + (std::sqrt (b) / qq) * s + 1.0) / (s * s + (std::sqrt (b) / qq) * s + b);
            return std::abs (lo * hi);
        }
        case LowCut:
        case HighCut:
        {
            double m = 1.0;
            for (double qq : cutQs (slope, q)) m *= std::abs (type == LowCut ? (s * s) / (s * s + s / qq + 1.0) : 1.0 / (s * s + s / qq + 1.0));
            return m;
        }
        default: return 1.0;
    }
}

// magnitude of a digital biquad at frequency f
inline double digitalMag (const Coefs& c, double f, double fs)
{
    using C = std::complex<double>;
    const C z1 = std::polar (1.0, -2.0 * kPi * f / fs), z2 = z1 * z1;
    return std::abs ((c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2));
}

// ------------------------------------------------------------------------------------
// 4th-order Linkwitz-Riley split (two cascaded Butterworth sections). lo + hi = allpass.
struct LR4
{
    Biquad lp1, lp2, hp1, hp2;
    double fc = 0;
    void set (double fs, double f)
    {
        if (std::abs (f - fc) < 1e-9) return;
        fc = f;
        lp1.c = lp2.c = designRbj (HighCut, fs, f, 0.70710678, 0);
        hp1.c = hp2.c = designRbj (LowCut, fs, f, 0.70710678, 0);
    }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    inline void split (double x, int ch, double& lo, double& hi) noexcept
    {
        lo = lp2.tick (lp1.tick (x, ch), ch);
        hi = hp2.tick (hp1.tick (x, ch), ch);
    }
};

// 2nd-order allpass with the same phase as an LR4 crossover (lo + hi) at fc
struct LRAllpass
{
    Biquad ap; double fc = 0;
    void set (double fs, double f)
    {
        if (std::abs (f - fc) < 1e-9) return;
        fc = f;
        const double w = 2.0 * kPi * std::clamp (f, 5.0, 0.49 * fs) / fs, cw = std::cos (w), al = std::sin (w) / (2.0 * 0.70710678);   // LR4 lo+hi = (s^2 - sqrt2 s + 1)/(s^2 + sqrt2 s + 1)
        const double a0 = 1 + al;
        ap.c = { (1 - al) / a0, -2 * cw / a0, (1 + al) / a0, -2 * cw / a0, (1 - al) / a0 };
    }
    void reset() { ap.reset(); }
    inline double tick (double x, int ch) noexcept { return ap.tick (x, ch); }
};

// splits into up to 4 phase-aligned bands (tree with allpass compensation)
struct BandSplitter
{
    static constexpr int kMax = 4;
    LR4 x[3]; LRAllpass apLow[2], apMid;   // apLow[0]: band0 at f2, apLow[1]: band0 at f3, apMid: band1 at f3
    void set (double fs, double f1, double f2, double f3)
    {
        x[0].set (fs, f1); x[1].set (fs, f2); x[2].set (fs, f3);
        apLow[0].set (fs, f2); apLow[1].set (fs, f3); apMid.set (fs, f3);
    }
    void reset() { for (auto& v : x) v.reset(); apLow[0].reset(); apLow[1].reset(); apMid.reset(); }
    inline void split (double in, int ch, double* b) noexcept
    {
        double lo, rest; x[0].split (in, ch, lo, rest);
        double m1, rest2; x[1].split (rest, ch, m1, rest2);
        double m2, hi; x[2].split (rest2, ch, m2, hi);
        b[0] = apLow[1].tick (apLow[0].tick (lo, ch), ch);
        b[1] = apMid.tick (m1, ch);
        b[2] = m2; b[3] = hi;
    }
};

// peak/RMS envelope follower with attack / release in ms
struct Envelope
{
    double a = 0, r = 0, v = 0;
    void set (double fs, double attMs, double relMs)
    {
        a = std::exp (-1.0 / (std::max (0.01, attMs) * 0.001 * fs));
        r = std::exp (-1.0 / (std::max (0.1, relMs) * 0.001 * fs));
    }
    inline double tick (double x) noexcept { v = x > v ? x + (v - x) * a : x + (v - x) * r; return v; }
};

// integer delay line (any length, allocated in prepare)
struct DelayLine
{
    std::vector<float> buf; int pos = 0, d = 0;
    void prepare (int maxD) { buf.assign ((size_t) std::max (2, maxD + 1), 0.0f); pos = 0; d = std::min (d, maxD); }
    void setDelay (int dd) { d = std::clamp (dd, 0, (int) buf.size() - 1); }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline float tick (float x) noexcept
    {
        if (d == 0) return x;
        const int n = (int) buf.size();
        buf[(size_t) pos] = x;
        int r = pos - d; if (r < 0) r += n;
        pos = pos + 1 == n ? 0 : pos + 1;
        return buf[(size_t) r];
    }
};

} // namespace lumen::master
