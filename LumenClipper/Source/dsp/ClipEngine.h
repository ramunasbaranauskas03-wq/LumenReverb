#pragma once
#include "ClipCurves.h"
#include "PumpEngine.h"     // LR4
#include <cmath>
#include <algorithm>

// =====================================================================================
// Lumen Clipper core (runs at the oversampled rate).
//   x -> [M/S] -> drive / ceiling -> [up to 4 phase-coherent bands, each its own mode, trim, bypass]
//     -> final clip (main mode) -> * ceiling -> [M/S back] -> phase-compensated mix / delta
// ADAA delays the clipped path by half a sample per stage, so the dry path gets the identical
// linear filter ((x[n] + x[n-1]) / 2 per stage) -> perfect null in the linear region, exact mix.
// A 3-tap correction then restores the small high-frequency droop of the ADAA average.
// =====================================================================================
namespace lumen::clip
{

constexpr int kBands = 4;

struct BandP { int mode = -1; float trimDb = 0; bool bypass = false, solo = false; };   // mode -1 = main mode
struct Params
{
    int mode = Classic; float shape = 1, asym = 0;
    float driveDb = 0, ceilingDb = 0;
    int bands = 1; float x[3] { 120, 1000, 6000 };
    BandP band[kBands];
    bool ms = false; float sideDb = 0;
    bool adaa = true;
    float mix = 1; bool delta = false;
};

class Splitter
{
public:
    void set (const float* xs, float fs) { for (int k = 0; k < 3; ++k) { xo[k].set (xs[k], fs); apA[k].set (xs[k], fs); apB[k].set (xs[k], fs); } }
    void reset() { for (int k = 0; k < 3; ++k) { xo[k].reset(); apA[k].reset(); apB[k].reset(); } }
    inline void split (float x, int nb, float* out) noexcept
    {
        if (nb <= 1) { out[0] = x; return; }
        float lo, rest, a1, a2;
        xo[0].process (x, lo, rest);
        if (nb == 2) { out[0] = lo; out[1] = rest; return; }
        float b1, rest2;
        xo[1].process (rest, b1, rest2);
        apA[1].process (lo, a1, a2); lo = a1 + a2;
        if (nb == 3) { out[0] = lo; out[1] = b1; out[2] = rest2; return; }
        float b2, b3;
        xo[2].process (rest2, b2, b3);
        apA[2].process (lo, a1, a2); lo = a1 + a2;
        apB[2].process (b1, a1, a2); b1 = a1 + a2;
        out[0] = lo; out[1] = b1; out[2] = b2; out[3] = b3;
    }
private:
    LR4 xo[3], apA[3], apB[3];
};

class ClipEngine
{
public:
    void prepare (double osRate)
    {
        fs = osRate;
        for (auto& s : split) s.reset();
        for (auto& b : padBuf) std::fill (std::begin (b), std::end (b), 0.0f);
        for (int c = 0; c < 2; ++c) { wet1[c] = dry1[c] = dry2[c] = 0.0f; cor1[c] = cor2[c] = 0.0f; dcX[c] = dcY[c] = 0.0f; for (int b = 0; b < kBands; ++b) bypA[c][b] = 0.0f; }
        for (auto& v : lastX) v = -1.0f;
        lastBands = -1;
        first = true;
        dcR = (float) std::exp (-2.0 * 3.14159265358979 * 4.0 / fs);
        resetMeters();
    }

    void setParams (const Params& np)
    {
        p = np;
        p.bands = std::clamp (p.bands, 1, kBands);
        // curves (main + per band); keep ADAA states consistent when a curve changes
        auto setCurve = [this] (Curve& c, int mode, int idx)
        {
            const int m = std::clamp (mode, 0, (int) kNumModes - 1);
            if (c.mode != m || std::abs (c.shape - (m == Hard ? 0.0 : (double) p.shape)) > 1e-9 || std::abs (c.bias - 0.6 * p.asym) > 1e-9 || first)
            {
                c.set (m, p.shape, p.asym);
                for (int ch = 0; ch < 2; ++ch) { auto& a = idx < 0 ? adMain[ch] : adBand[ch][idx]; double f; a.F1 = c.F (a.x1, f); }
            }
        };
        setCurve (main, p.mode, -1);
        for (int b = 0; b < kBands; ++b) setCurve (bandCurve[b], p.band[b].mode < 0 ? p.mode : p.band[b].mode, b);
        float xs[3];
        xs[0] = std::clamp (p.x[0], 20.0f, 2000.0f);
        xs[1] = std::clamp (p.x[1], xs[0] * 1.25f, 14000.0f);
        xs[2] = std::clamp (p.x[2], xs[1] * 1.25f, 18000.0f);
        if (std::abs (xs[0] - lastX[0]) > 0.01f || std::abs (xs[1] - lastX[1]) > 0.01f || std::abs (xs[2] - lastX[2]) > 0.01f)
        {
            for (auto& s : split) s.set (xs, (float) fs);
            for (int k = 0; k < 3; ++k) lastX[k] = xs[k];
        }
        if (p.bands != lastBands) { for (auto& s : split) s.reset(); lastBands = p.bands; }
        ceilT = std::pow (10.0f, p.ceilingDb / 20.0f);
        driveT = std::pow (10.0f, p.driveDb / 20.0f);
        sideT = std::pow (10.0f, p.sideDb / 20.0f);
        for (int b = 0; b < kBands; ++b) trimT[b] = std::pow (10.0f, p.band[b].trimDb / 20.0f);
        if (first) { ceil = ceilT; drive = driveT; side = sideT; mix = p.mix; for (int b = 0; b < kBands; ++b) trim[b] = trimT[b]; first = false; }
    }

    // ADAA = 2 half-sample stages on every path (single band gets one extra linear average) -> 1 sample,
    // + 1 sample for the droop correction = 2 oversampled samples of linear delay
    int linearDelay() const noexcept { return p.adaa ? 2 : 0; }
    void setPad (int samples) { pad = std::clamp (samples, 0, kMaxPad - 1); }
    static constexpr int kMaxPad = 128;

    void process (float* L, float* R, int n) noexcept
    {
        const float sm = 1.0f - (float) std::exp (-1.0 / (0.02 * fs));
        const int nb = p.bands;
        const int st = p.adaa ? 2 : 0;
        const bool dcOn = main.asymmetric() || (nb > 1 && [this, nb] { for (int b = 0; b < nb; ++b) if (bandCurve[b].asymmetric()) return true; return false; }());
        bool anySolo = false;
        for (int b = 0; b < nb; ++b) anySolo |= p.band[b].solo;
        const float corA = st == 0 ? 0.0f : 0.25f;    // droop correction for two half-sample averages
        for (int i = 0; i < n; ++i, padPos = (padPos + 1) % kMaxPad)
        {
            ceil += (ceilT - ceil) * sm; drive += (driveT - drive) * sm; side += (sideT - side) * sm; mix += (p.mix - mix) * sm;
            float in[2] { L[i], R[i] };
            float u[2];
            if (p.ms) { u[0] = 0.5f * (in[0] + in[1]); u[1] = 0.5f * (in[0] - in[1]) * side; }
            else { u[0] = in[0]; u[1] = in[1]; }
            const float gIn = drive / ceil;
            float y[2];
            for (int c = 0; c < 2; ++c)
            {
                const float v = u[c] * gIn;
                prePk = std::max (prePk, std::abs (v));
                if (nb == 1) { y[c] = clipOne (main, adMain[c], v); if (st > 0) y[c] = avg (wet1[c], y[c]); }
                else
                {
                    float bs[kBands] {};
                    split[c].split (v, nb, bs);
                    float sum = 0.0f;
                    for (int b = 0; b < nb; ++b)
                    {
                        const auto& bp = p.band[b];
                        trim[b] += (trimT[b] - trim[b]) * sm;
                        float yb;
                        const float vb = bs[b] * trim[b];
                        bandPk[b] = std::max (bandPk[b], std::abs (vb));
                        if (bp.bypass) yb = st > 0 ? avg (bypA[c][b], bs[b]) : bs[b];          // same half-sample delay as ADAA
                        else yb = clipOne (bandCurve[b], adBand[c][b], vb) / trim[b];
                        if (anySolo && ! bp.solo) continue;
                        sum += yb;
                    }
                    // final stage keeps the sum under the ceiling (and glues the bands)
                    y[c] = clipOne (main, adMain[c], sum);
                }
                if (dcOn) { const float o = y[c] - dcX[c] + dcR * dcY[c]; dcX[c] = y[c]; dcY[c] = o; y[c] = o; }
                else { dcX[c] = dcY[c] = 0.0f; }
                y[c] *= ceil;
            }
            if (p.ms) { const float m = y[0], s = y[1] / std::max (1e-6f, side); y[0] = m + s; y[1] = m - s; }
            for (int c = 0; c < 2; ++c)
            {
                // dry with the same linear behaviour as the wet path
                float d = in[c];
                if (st >= 1) d = avg (dry1[c], d);
                if (st >= 2) d = avg (dry2[c], d);
                float o = p.delta ? (y[c] - d) : d + (y[c] - d) * mix;
                // ADAA droop correction: [-a, 1 + 2a, -a]
                if (corA > 0.0f)
                {
                    const float out = -corA * o + (1.0f + 2.0f * corA) * cor1[c] - corA * cor2[c];
                    cor2[c] = cor1[c]; cor1[c] = o; o = out;
                }
                if (pad > 0)
                {
                    padBuf[c][padPos] = o;
                    o = padBuf[c][(padPos - pad + kMaxPad) % kMaxPad];
                }
                (c == 0 ? L : R)[i] = o;
            }
        }
    }

    // meters (reset by the caller)
    float prePk = 0, bandPk[kBands] {};
    void resetMeters() noexcept { prePk = 0; for (auto& v : bandPk) v = 0; }
    const Curve& mainCurve() const noexcept { return main; }

private:
    static inline float avg (float& z, float x) noexcept { const float y = 0.5f * (x + z); z = x; return y; }
    inline float clipOne (const Curve& c, Adaa& a, float v) noexcept
    {
        if (p.adaa) return a.process (c, (double) v);
        a.x1 = v; return (float) c.f ((double) v);
    }

    Params p;
    double fs = 192000;
    Curve main, bandCurve[kBands];
    Adaa adMain[2], adBand[2][kBands];
    Splitter split[2];
    float padBuf[2][kMaxPad] {}; int pad = 0, padPos = 0;
    float wet1[2] {}, dry1[2] {}, dry2[2] {}, cor1[2] {}, cor2[2] {}, dcX[2] {}, dcY[2] {}, bypA[2][kBands] {}, dcR = 0.9999f;
    float ceil = 1, ceilT = 1, drive = 1, driveT = 1, side = 1, sideT = 1, mix = 1, trim[kBands] { 1, 1, 1, 1 }, trimT[kBands] { 1, 1, 1, 1 };
    float lastX[3] { -1, -1, -1 };
    int lastBands = -1;
    bool first = true;
};

} // namespace lumen::clip
