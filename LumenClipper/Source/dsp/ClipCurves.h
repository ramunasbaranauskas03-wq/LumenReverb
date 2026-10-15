#pragma once
#include <cmath>
#include <algorithm>

// =====================================================================================
// Lumen Clipper transfer curves. Every curve:
//   * has slope 1 at 0 and saturates at exactly +-1 (= the ceiling), so switching modes keeps the
//     same clip level (no re-gaining like many clippers),
//   * comes with its exact antiderivative F, used for 1st-order ADAA (anti-derivative anti-aliasing).
// SHAPE morphs from a hard clip (0) to the mode's own curve (1). ASYM shifts the operating point
// (even harmonics, tube-like), keeping f(0) = 0.
// =====================================================================================
namespace lumen::clip
{

enum Mode { Hard = 0, Classic, Smooth, Crisp, Tape, Tube, Diode, kNumModes };

namespace detail
{
    // odd-symmetric building blocks on t = |x| >= 0 :  value g(t) and antiderivative G(t) (G(0) = 0)
    inline double hardG (double t, double& g)    { if (t <= 1.0) { g = t; return 0.5 * t * t; } g = 1.0; return t - 0.5; }
    inline double classicG (double t, double& g)
    {
        constexpr double a = 0.5, w = 0.5;                    // linear up to 0.5, quadratic knee to 1.5, then flat at 1
        if (t <= a) { g = t; return 0.5 * t * t; }
        if (t <= 1.0 + w) { const double d = t - a; g = t - d * d / (4.0 * w); return 0.5 * t * t - d * d * d / (12.0 * w); }
        g = 1.0; return (0.5 * 2.25 - 1.0 / 6.0) + (t - 1.5);
    }
    inline double tanhG (double t, double& g)    { g = std::tanh (t); return t + std::log1p (std::exp (-2.0 * t)) - 0.69314718055994531; }
    inline double tapeG (double t, double& g)
    {
        constexpr double k = 1.5707963267948966, c = 0.63661977236758134;   // pi/2, 2/pi
        g = c * std::atan (k * t);
        return c * (t * std::atan (k * t) - std::log1p (k * k * t * t) / 3.1415926535897932);
    }
    inline double softG (double t, double& g)    { g = t / (1.0 + t); return t - std::log1p (t); }
    inline double diodeG (double t, double& g)   { const double e = std::exp (-t); g = 1.0 - e; return t - 1.0 + e; }

    // base curve (before shape / asym): returns F(x), writes f(x)
    inline double base (int mode, double x, double& f)
    {
        const double t = std::abs (x);
        const bool neg = x < 0.0;
        double g = 0.0, G = 0.0;
        switch (mode)
        {
            case Hard:    G = hardG (t, g); break;
            case Classic: G = classicG (t, g); break;
            case Smooth:  G = tanhG (t, g); break;
            case Crisp:   G = neg ? tanhG (t, g) : classicG (t, g); break;     // harder top, rounder bottom
            case Tape:    G = tapeG (t, g); break;
            case Tube:    G = neg ? softG (t, g) : tanhG (t, g); break;        // soft, asymmetric
            case Diode:   G = diodeG (t, g); break;
            default:      G = hardG (t, g); break;
        }
        f = neg ? -g : g;
        return G;   // even
    }
}

struct Curve
{
    int mode = Hard;
    double shape = 1.0;     // 0 = hard clip, 1 = the mode's curve
    double bias = 0.0;      // asymmetry
    double fb = 0.0, Fb = 0.0;

    void set (int m, double s, double asym)
    {
        mode = std::clamp (m, 0, (int) kNumModes - 1);
        shape = mode == Hard ? 0.0 : std::clamp (s, 0.0, 1.0);
        bias = 0.6 * std::clamp (asym, -1.0, 1.0);
        Fb = raw (bias, fb);
    }
    // shaped curve without bias
    inline double raw (double x, double& f) const
    {
        double fh, fm;
        const double Fh = detail::base (Hard, x, fh);
        if (shape <= 0.0) { f = fh; return Fh; }
        const double Fm = detail::base (mode, x, fm);
        f = (1.0 - shape) * fh + shape * fm;
        return (1.0 - shape) * Fh + shape * Fm;
    }
    inline double F (double x, double& f) const
    {
        if (std::abs (bias) < 1e-12) return raw (x, f);
        double fx; const double Fx = raw (x + bias, fx);
        f = fx - fb;
        return Fx - Fb - x * fb;
    }
    inline double f (double x) const { double v; F (x, v); return v; }
    bool asymmetric() const { return std::abs (bias) > 1e-12 || ((mode == Crisp || mode == Tube) && shape > 0.0); }
};

// first-order ADAA state (one per channel / band)
struct Adaa
{
    double x1 = 0.0, F1 = 0.0;
    void reset (const Curve& c) { x1 = 0.0; double f; F1 = c.F (0.0, f); }
    inline float process (const Curve& c, double x) noexcept
    {
        double fx;
        const double Fx = c.F (x, fx);
        const double dx = x - x1;
        double y;
        if (std::abs (dx) > 1.0e-6) y = (Fx - F1) / dx;
        else y = c.f (0.5 * (x + x1));
        x1 = x; F1 = Fx;
        return (float) y;
    }
};

} // namespace lumen::clip
