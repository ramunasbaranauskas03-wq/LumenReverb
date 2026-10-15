#pragma once
#include <cmath>
#include <complex>
#include <algorithm>

namespace lumen
{
// RBJ biquad (stereo state). type 0 low shelf, 1 peak, 2 high shelf
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    void set (int type, double f0, double gainDb, double q, double fs)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w0 = 2.0 * 3.141592653589793 * std::min (f0, 0.45 * fs) / fs;
        const double cw = std::cos (w0), sw = std::sin (w0);
        double a0;
        if (type == 1)
        {
            const double alpha = sw / (2.0 * q);
            a0 = 1 + alpha / A; b0 = (1 + alpha * A) / a0; b1 = -2 * cw / a0; b2 = (1 - alpha * A) / a0; a1 = -2 * cw / a0; a2 = (1 - alpha / A) / a0;
            return;
        }
        const double alpha = sw / 2.0 * std::sqrt (2.0);
        const double sa = 2.0 * std::sqrt (A) * alpha;
        if (type == 0)
        {
            a0 = (A + 1) + (A - 1) * cw + sa;
            b0 = A * ((A + 1) - (A - 1) * cw + sa) / a0; b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0; b2 = A * ((A + 1) - (A - 1) * cw - sa) / a0;
            a1 = -2 * ((A - 1) + (A + 1) * cw) / a0; a2 = ((A + 1) + (A - 1) * cw - sa) / a0;
        }
        else
        {
            a0 = (A + 1) - (A - 1) * cw + sa;
            b0 = A * ((A + 1) + (A - 1) * cw + sa) / a0; b1 = -2 * A * ((A - 1) + (A + 1) * cw) / a0; b2 = A * ((A + 1) + (A - 1) * cw - sa) / a0;
            a1 = 2 * ((A - 1) - (A + 1) * cw) / a0; a2 = ((A + 1) - (A - 1) * cw - sa) / a0;
        }
    }
    inline float process (float x, int c) noexcept
    {
        const double y = b0 * x + z1[c];
        z1[c] = b1 * x - a1 * y + z2[c];
        z2[c] = b2 * x - a2 * y;
        return (float) y;
    }
    double magnitudeDb (double f, double fs) const
    {
        const std::complex<double> z = std::exp (std::complex<double> (0.0, -2.0 * 3.141592653589793 * f / fs));
        const auto h = (b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z);
        return 20.0 * std::log10 (std::abs (h) + 1e-12);
    }
};

inline int meqType (int band, int numBands) { return band == 0 ? 0 : band == numBands - 1 ? 2 : 1; }
}
