#pragma once
#include <cmath>
#include <complex>
#include <algorithm>
#include <array>

// =====================================================================================
// Lumen EQ 2 — filter design
//   "Analog-matched" biquads: poles mapped exactly (impulse invariance), zeros solved so the
//   digital magnitude equals the analog prototype at DC, Nyquist and the band centre
//   (Vicanek-style).  Result: no "cramping" near Nyquist — a 16 kHz bell is a real bell,
//   an air shelf keeps its shape — with zero latency.
// =====================================================================================
namespace lumen::eq2
{
constexpr double kPi = 3.14159265358979323846;

enum Type { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, Tilt, kNumTypes };
enum Chan { Stereo = 0, Mid, Side, Left, Right, kNumChans };
inline int slopeOrder (int s) { static const int o[] { 1, 2, 3, 4, 6, 8, 12, 16 }; return o[std::clamp (s, 0, 7)]; }   // 6..96 dB/oct
inline int slopeDb (int s) { return 6 * slopeOrder (s); }

struct Coefs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

inline double magnitude (const Coefs& c, double w)
{
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    return std::abs ((c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2));
}

// |analog prototype|^2 at normalised frequency W (= f / fc)
struct Proto { double n0, n1, n2, d0, d1, d2; int order; };
inline double protoMag2 (const Proto& p, double W)
{
    const std::complex<double> s (0.0, W);
    const auto n = p.n2 * s * s + p.n1 * s + p.n0, d = p.d2 * s * s + p.d1 * s + p.d0;
    return std::norm (n / d);
}

// matched digital section for an analog prototype at centre wc (rad / sample)
inline Coefs matched (const Proto& p, double wc)
{
    wc = std::clamp (wc, 1e-5, kPi * 0.9995);
    Coefs c;
    if (p.order == 1)
    {
        const double pole = -p.d0 / p.d1;                  // normalised
        c.a1 = -std::exp (pole * wc); c.a2 = 0;
        const double A0 = (1 + c.a1) * (1 + c.a1), A1 = (1 - c.a1) * (1 - c.a1);
        const double s0 = std::sqrt (protoMag2 (p, 0.0) * A0), s1 = std::sqrt (protoMag2 (p, kPi / wc) * A1);
        c.b0 = 0.5 * (s0 + s1); c.b1 = 0.5 * (s0 - s1); c.b2 = 0;
        return c;
    }
    // poles of d2 s^2 + d1 s + d0 (normalised), mapped exactly
    const double disc = p.d1 * p.d1 - 4 * p.d2 * p.d0;
    if (disc < 0)
    {
        const double re = -p.d1 / (2 * p.d2) * wc, im = std::sqrt (-disc) / (2 * p.d2) * wc;
        c.a1 = -2 * std::exp (re) * std::cos (im); c.a2 = std::exp (2 * re);
    }
    else
    {
        const double r1 = (-p.d1 + std::sqrt (disc)) / (2 * p.d2) * wc, r2 = (-p.d1 - std::sqrt (disc)) / (2 * p.d2) * wc;
        c.a1 = -(std::exp (r1) + std::exp (r2)); c.a2 = std::exp (r1 + r2);
    }
    const double A0 = (1 + c.a1 + c.a2) * (1 + c.a1 + c.a2), A1 = (1 - c.a1 + c.a2) * (1 - c.a1 + c.a2), A2 = -4 * c.a2;
    auto phi = [] (double w, double& f0, double& f1, double& f2) { const double s = std::sin (w * 0.5); f1 = s * s; f0 = 1 - f1; f2 = 4 * f0 * f1; };
    double f0, f1, f2;
    const double wm = std::min (wc, kPi * 0.9);
    const double B0 = protoMag2 (p, 0.0) * A0, B1 = protoMag2 (p, kPi / wc) * A1;
    phi (wm, f0, f1, f2);
    const double Am = A0 * f0 + A1 * f1 + A2 * f2;
    double B2 = (protoMag2 (p, wm / wc) * Am - B0 * f0 - B1 * f1) / f2;
    const double s0 = std::sqrt (std::max (0.0, B0)), s1 = std::sqrt (std::max (0.0, B1));
    const double u = 0.5 * (s0 + s1);
    if (u * u + B2 < 0) B2 = -u * u;
    c.b0 = 0.5 * (u + std::sqrt (u * u + B2)); c.b2 = u - c.b0; c.b1 = 0.5 * (s0 - s1);
    return c;
}

// precise variant: least-squares numerator (relative error over the whole band, exact poles);
// falls back to the 3-point match when that is better (notches: zeros on the unit circle)
inline double sectionError (const Proto& p, const Coefs& c, double wc)
{
    double e = 0;
    for (int k = 0; k < 40; ++k)
    {
        const double w = 1e-3 * std::pow (0.99 * kPi / 1e-3, k / 39.0);
        const double a = protoMag2 (p, w / wc); if (a < 1e-4) continue;
        e = std::max (e, std::abs (10 * std::log10 (a) - 20 * std::log10 (magnitude (c, w) + 1e-12)));
    }
    return e;
}
inline Coefs matchedPrecise (const Proto& p, double wc)
{
    wc = std::clamp (wc, 1e-5, kPi * 0.9995);
    const Coefs c3 = matched (p, wc);
    if (p.order == 1 || (std::abs (p.n1) < 1e-300 && p.n0 > 0 && p.n2 > 0)) return c3;
    Coefs c = c3;
    const double A0 = (1 + c.a1 + c.a2) * (1 + c.a1 + c.a2), A1 = (1 - c.a1 + c.a2) * (1 - c.a1 + c.a2), A2 = -4 * c.a2;
    double M[3][3] {}, v[3] {};
    auto addPt = [&] (double w, double wt)
    {
        const double sn = std::sin (w * 0.5), f1 = sn * sn, f0 = 1 - f1, f2 = 4 * f0 * f1;
        const double T = protoMag2 (p, w / wc) * (A0 * f0 + A1 * f1 + A2 * f2);
        const double r[3] { f0, f1, f2 }, ww = wt / (T * T + 1e-30);
        for (int i = 0; i < 3; ++i) { v[i] += ww * r[i] * T; for (int j = 0; j < 3; ++j) M[i][j] += ww * r[i] * r[j]; }
    };
    for (int k = 0; k < 64; ++k) addPt (1e-3 * std::pow (0.99 * kPi / 1e-3, k / 63.0), 1.0);
    addPt (1e-4, 50.0); addPt (kPi - 1e-4, 5.0); addPt (std::min (wc, kPi * 0.95), 20.0);
    auto det = [] (const double a[3][3]) { return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]); };
    const double D = det (M);
    if (! (std::abs (D) > 1e-300)) return c3;
    double B[3];
    for (int k = 0; k < 3; ++k) { double t[3][3]; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) t[i][j] = j == k ? v[i] : M[i][j]; B[k] = det (t) / D; }
    const double s0 = std::sqrt (std::max (0.0, B[0])), s1 = std::sqrt (std::max (0.0, B[1]));
    const double u = 0.5 * (s0 + s1); double B2 = B[2]; if (u * u + B2 < 0) B2 = -u * u;
    c.b0 = 0.5 * (u + std::sqrt (u * u + B2)); c.b2 = u - c.b0; c.b1 = 0.5 * (s0 - s1);
    if (! std::isfinite (c.b0) || ! std::isfinite (c.b1) || ! std::isfinite (c.b2)) return c3;
    return sectionError (p, c, wc) < sectionError (p, c3, wc) ? c : c3;
}

// one band = up to 8 sections
struct Design { std::array<Coefs, 8> s {}; int n = 0; };

inline Design design (int type, double fs, double freq, double gainDb, double q, int slope, bool precise = true)
{
    Design d;
    const double wc = 2 * kPi * std::clamp (freq, 5.0, fs * 0.4999) / fs;
    q = std::clamp (q, 0.025, 40.0);
    auto add = [&] (const Proto& p) { if (d.n < 8) d.s[(size_t) d.n++] = precise ? matchedPrecise (p, wc) : matched (p, wc); };
    switch (type)
    {
        case Bell:
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            add ({ 1, A / q, 1, 1, 1 / (A * q), 1, 2 });
            break;
        }
        case LowShelf: case HighShelf:
        {
            const int ord = slopeOrder (slope);
            const int stages = ord <= 1 ? 1 : ord <= 2 ? 1 : 2;     // 6, 12, 24 dB/oct
            const double g = gainDb / stages;
            for (int k = 0; k < stages; ++k)
            {
                if (ord <= 1)
                {
                    const double G = std::pow (10.0, g / 20.0), r = std::sqrt (G);
                    if (type == LowShelf) add ({ r, 1, 0, 1 / r, 1, 0, 1 });               // (s + sqrt G) / (s + 1/sqrt G)
                    else add ({ G / r, G, 0, r, 1, 0, 1 });                                 // G (s + 1/sqrt G) / (s + sqrt G)
                }
                else
                {
                    const double A = std::pow (10.0, g / 40.0), sa = std::sqrt (A) / q;
                    if (type == LowShelf) add ({ A * A, A * sa, A, 1, sa, A, 2 });          // A (s^2 + sa s + A) / (A s^2 + sa s + 1)
                    else add ({ A, A * sa, A * A, A, sa, 1, 2 });                           // A (A s^2 + sa s + 1) / (s^2 + sa s + A)
                }
            }
            break;
        }
        case LowCut: case HighCut:
        {
            const int ord = slopeOrder (slope);
            const bool hp = type == LowCut;
            if (ord & 1) { if (hp) add ({ 0, 1, 0, 1, 1, 0, 1 }); else add ({ 1, 0, 0, 1, 1, 0, 1 }); }
            const int pairs = ord / 2;
            for (int k = 0; k < pairs; ++k)
            {
                // Butterworth section Q; the user Q shapes the resonance of the last (highest-Q) section
                double qk = 1.0 / (2.0 * std::sin ((2.0 * k + 1.0) * kPi / (2.0 * ord)));
                if (k == 0) qk *= q / 0.7071;
                if (hp) add ({ 0, 0, 1, 1, 1 / qk, 1, 2 }); else add ({ 1, 0, 0, 1, 1 / qk, 1, 2 });
            }
            break;
        }
        case Notch:
        {
            // bilinear (RBJ, pre-warped): exact null at the centre at any frequency
            const double al = std::sin (wc) / (2 * q), cw = std::cos (wc), a0 = 1 + al;
            Coefs c; c.b0 = 1 / a0; c.b1 = -2 * cw / a0; c.b2 = 1 / a0; c.a1 = -2 * cw / a0; c.a2 = (1 - al) / a0;
            d.s[(size_t) d.n++] = c;
            break;
        }
        case BandPass: add ({ 0, 1 / q, 0, 1, 1 / q, 1, 2 }); break;
        case Tilt:
        {
            const double t = std::pow (10.0, gainDb / 20.0), r = std::sqrt (t);          // -g/2 at DC, +g/2 at HF
            add ({ 1, r, 0, r, 1, 0, 1 });
            break;
        }
        default: break;
    }
    return d;
}

inline double designMagDb (const Design& d, double fs, double f)
{
    double m = 1; const double w = 2 * kPi * f / fs;
    for (int k = 0; k < d.n; ++k) m *= magnitude (d.s[(size_t) k], w);
    return 20 * std::log10 (m + 1e-12);
}

// analog prototype magnitude of a whole band (for tests: digital vs analog)
inline double analogMagDb (int type, double freq, double gainDb, double q, int slope, double f)
{
    Design dummy; (void) dummy;
    const double W = f / freq; double m2 = 1;
    q = std::clamp (q, 0.025, 40.0);
    auto mul = [&] (const Proto& p) { m2 *= protoMag2 (p, W); };
    switch (type)
    {
        case Bell: { const double A = std::pow (10.0, gainDb / 40.0); mul ({ 1, A / q, 1, 1, 1 / (A * q), 1, 2 }); break; }
        case LowShelf: case HighShelf:
        {
            const int ord = slopeOrder (slope), stages = ord <= 2 ? 1 : 2; const double g = gainDb / stages;
            for (int k = 0; k < stages; ++k)
            {
                if (ord <= 1) { const double G = std::pow (10.0, g / 20.0), r = std::sqrt (G); if (type == LowShelf) mul ({ r, 1, 0, 1 / r, 1, 0, 1 }); else mul ({ G / r, G, 0, r, 1, 0, 1 }); }
                else { const double A = std::pow (10.0, g / 40.0), sa = std::sqrt (A) / q; if (type == LowShelf) mul ({ A * A, A * sa, A, 1, sa, A, 2 }); else mul ({ A, A * sa, A * A, A, sa, 1, 2 }); }
            }
            break;
        }
        case LowCut: case HighCut:
        {
            const int ord = slopeOrder (slope); const bool hp = type == LowCut;
            if (ord & 1) { if (hp) mul ({ 0, 1, 0, 1, 1, 0, 1 }); else mul ({ 1, 0, 0, 1, 1, 0, 1 }); }
            for (int k = 0; k < ord / 2; ++k) { double qk = 1.0 / (2.0 * std::sin ((2.0 * k + 1.0) * kPi / (2.0 * ord))); if (k == 0) qk *= q / 0.7071; if (hp) mul ({ 0, 0, 1, 1, 1 / qk, 1, 2 }); else mul ({ 1, 0, 0, 1, 1 / qk, 1, 2 }); }
            break;
        }
        case Notch: mul ({ 1, 0, 1, 1, 1 / q, 1, 2 }); break;
        case BandPass: mul ({ 0, 1 / q, 0, 1, 1 / q, 1, 2 }); break;
        case Tilt: { const double t = std::pow (10.0, gainDb / 20.0), r = std::sqrt (t); mul ({ 1, r, 0, r, 1, 0, 1 }); break; }
        default: break;
    }
    return 10 * std::log10 (m2 + 1e-24);
}

// DF2T biquad with the state in double
struct Section
{
    Coefs c; double z1 = 0, z2 = 0;
    inline double process (double x) noexcept { const double y = c.b0 * x + z1; z1 = c.b1 * x - c.a1 * y + z2; z2 = c.b2 * x - c.a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
};
}
