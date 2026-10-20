#pragma once
#include "Resampler.h"
#include <vector>
#include <cmath>
#include <algorithm>

// sample analysis: root pitch (YIN), transients (spectral-flux style onset detection), tempo, zero crossings
namespace lumen::sampler
{

// returns a MIDI note number (with fraction) or -1 when the sound has no clear pitch
inline double detectRootAt (const SampleSource& s, double offsetSec, double thr)
{
    const int n = s.length;
    if (n < 2048) return -1;
    // mono, look at the loudest 200 ms after the attack
    const float* a = s.data (0, 0); const float* b = s.data (0, 1);
    int pkI = 0; float pk = 0;
    for (int i = 0; i < n; ++i) { const float v = std::abs (a[i] + b[i]); if (v > pk) { pk = v; pkI = i; } }
    if (pk < 1e-5f) return -1;
    const int W = std::min ((int) (s.rate * 0.12), 8192);
    int st = std::min (n - 2 * W - 1, pkI + (int) (s.rate * offsetSec));
    if (st < 0) st = 0;
    if (st + 2 * W >= n) return -1;
    std::vector<float> x ((size_t) (2 * W));
    for (int i = 0; i < 2 * W; ++i) x[(size_t) i] = 0.5f * (a[st + i] + b[st + i]);
    const int tauMin = (int) (s.rate / 2000.0), tauMax = std::min (W - 1, (int) (s.rate / 30.0));
    std::vector<double> d ((size_t) tauMax + 2, 0.0), dn ((size_t) tauMax + 2, 1.0);
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double sum = 0; for (int i = 0; i < W; ++i) { const double df = x[(size_t) i] - x[(size_t) (i + tau)]; sum += df * df; }
        d[(size_t) tau] = sum;
    }
    double run = 0;
    for (int tau = 1; tau <= tauMax; ++tau) { run += d[(size_t) tau]; dn[(size_t) tau] = d[(size_t) tau] * tau / std::max (1e-20, run); }
    int best = -1;
    for (int tau = std::max (2, tauMin); tau < tauMax; ++tau)
        if (dn[(size_t) tau] < thr && dn[(size_t) tau] <= dn[(size_t) tau - 1] && dn[(size_t) tau] <= dn[(size_t) tau + 1]) { best = tau; break; }
    if (best < 0) return -1;
    const double y0 = dn[(size_t) best - 1], y1 = dn[(size_t) best], y2 = dn[(size_t) best + 1];
    const double den = y0 - 2 * y1 + y2;
    const double t = best + (std::abs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0);
    const double f = s.rate / t;
    return 69.0 + 12.0 * std::log2 (f / 440.0);
}

// pitched sounds only: two windows must agree (drums / noise / chords give -1, so the root is left alone)
inline double detectRoot (const SampleSource& s)
{
    const double a = detectRootAt (s, 0.03, 0.12);
    if (a < 0) return -1;
    const double b = detectRootAt (s, 0.10, 0.15);
    if (b >= 0 && std::abs (a - b) > 0.3) return -1;
    return a;
}

// onset positions (samples) from an energy-flux novelty curve; sensitivity 0..1
inline std::vector<int> detectTransients (const SampleSource& s, float sensitivity, int maxCount = 64)
{
    std::vector<int> out;
    const int n = s.length;
    if (n < 512) { out.push_back (0); return out; }
    const int hop = std::max (64, (int) (s.rate * 0.005));
    const float* a = s.data (0, 0); const float* b = s.data (0, 1);
    // band energies (low via one-pole, high = rest) per hop, log compressed
    std::vector<float> nov, lv, hv;
    float lpS = 0, prevLo = 0, prevHi = 0;
    const float alp = (float) (1.0 - std::exp (-2.0 * kPi * 200.0 / s.rate));
    for (int h = 0; h * hop < n; ++h)
    {
        double lo = 0, hi = 0;
        for (int i = h * hop; i < std::min (n, (h + 1) * hop); ++i)
        {
            const float x = 0.5f * (a[i] + b[i]);
            lpS += alp * (x - lpS);
            lo += lpS * lpS; hi += (x - lpS) * (x - lpS);
        }
        const float L = (float) std::log1p (1000.0 * lo / hop), H = (float) std::log1p (1000.0 * hi / hop);
        nov.push_back (std::max (0.0f, L - prevLo) + std::max (0.0f, H - prevHi));
        lv.push_back (L); hv.push_back (H);
        prevLo = L; prevHi = H;
    }
    // adaptive threshold: local median + offset
    const float k = 0.05f + 1.2f * (1.0f - sensitivity);
    const int minGap = std::max (1, (int) (0.08 * s.rate / hop));
    int last = -minGap;
    out.push_back (0);
    for (int i = 1; i + 1 < (int) nov.size(); ++i)
    {
        std::vector<float> w;
        for (int j = std::max (0, i - 16); j < std::min ((int) nov.size(), i + 16); ++j) w.push_back (nov[(size_t) j]);
        std::nth_element (w.begin(), w.begin() + (long) w.size() / 2, w.end());
        const float thr = w[w.size() / 2] + k;
        // a real attack: one band must rise clearly above everything it held in the previous 80 ms
        auto rises = [&] (const std::vector<float>& v)
        {
            float before = 0, after = 0;
            for (int j = std::max (0, i - 16); j < i; ++j) before = std::max (before, v[(size_t) j]);
            for (int j = i; j < std::min ((int) v.size(), i + 3); ++j) after = std::max (after, v[(size_t) j]);
            return after - before > 0.25f + 0.5f * (1.0f - sensitivity);
        };
        if (nov[(size_t) i] > thr && nov[(size_t) i] >= nov[(size_t) i - 1] && nov[(size_t) i] >= nov[(size_t) i + 1] && i - last >= minGap && (rises (lv) || rises (hv)))
        {
            // refine to the first sample that rises above 10 % of the local peak, then snap back to a zero crossing
            int pos = std::max (0, (i - 1) * hop);
            float lp = 0; for (int q = pos; q < std::min (n, pos + 2 * hop); ++q) lp = std::max (lp, std::abs (0.5f * (a[q] + b[q])));
            while (pos < n - 1 && std::abs (0.5f * (a[pos] + b[pos])) < 0.1f * lp) ++pos;
            int z = pos; while (z > 0 && pos - z < hop && ! ((a[z - 1] + b[z - 1]) * (a[z] + b[z]) <= 0)) --z;
            if (z - out.back() > (int) (0.07 * s.rate)) out.push_back (z);
            last = i;
            if ((int) out.size() >= maxCount) break;
        }
    }
    return out;
}

// tempo of a loop: assumes the file is a whole number of beats; picks the length in beats whose tempo
// is closest to the 90..180 bpm range and fits the onsets best
inline double detectBpm (const SampleSource& s)
{
    const double secs = s.length / s.rate;
    if (secs < 0.5) return 0;
    double best = 0, bestScore = 1e9;
    for (int beats : { 1, 2, 4, 8, 16, 32, 64 })
    {
        const double bpm = 60.0 * beats / secs;
        if (bpm < 70 || bpm > 190) continue;
        const double score = std::abs (bpm - 128.0) / 128.0 + 0.0 * beats;
        if (score < bestScore) { bestScore = score; best = bpm; }
    }
    return best;
}

inline int nearestZeroCrossing (const SampleSource& s, int pos, int radius = 2000)
{
    const float* a = s.data (0, 0); const float* b = s.data (0, 1);
    pos = std::clamp (pos, 1, std::max (1, s.length - 1));
    for (int d = 0; d < radius; ++d)
        for (int sgn : { 1, -1 })
        {
            const int i = pos + sgn * d;
            if (i < 1 || i >= s.length) continue;
            if ((a[i - 1] + b[i - 1]) <= 0.0f && (a[i] + b[i]) >= 0.0f) return i;
        }
    return pos;
}

} // namespace lumen::sampler
