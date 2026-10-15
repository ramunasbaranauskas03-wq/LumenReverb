#pragma once
#include "PumpEngine.h"     // SVFp, LR4 (phase-coherent crossovers)
#include <cmath>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <vector>

// =====================================================================================
// Lumen Transient 2 — 4-band, level-independent transient designer
//   ATTACK  : PUNCH  = the first ms of the hit (fast env vs. short follower)
//             BODY   = the weight right after it (short follower vs. long follower)
//   RELEASE : SUSTAIN = the tail +/- (slow-release vs. fast-release envelope)
//             GATE    = expander on the tail differential (cuts rooms / ringing hard)
//   CHARACTER: DRIVE (per-band saturation), TIME (detector speed), GAIN, AMOUNT
// Bands are split with Linkwitz-Riley crossovers (sum = allpass of the input -> phase coherent).
// Detection can come from the input or from an external sidechain split into the same bands.
// =====================================================================================
namespace lumen::tr
{

constexpr int kBands = 4;

inline float fastLog2 (float x) noexcept
{
    std::uint32_t i; std::memcpy (&i, &x, 4);
    float e = (float) ((int) ((i >> 23) & 255) - 128);
    i = (i & 0x807FFFFFu) | 0x3F800000u;
    float m; std::memcpy (&m, &i, 4);
    return e + (-0.34484843f * m + 2.02466578f) * m - 0.65871759f;
}
inline float fastExp2 (float p) noexcept
{
    p = std::clamp (p, -60.0f, 60.0f);
    const float w = std::floor (p), z = p - w;
    const float a = 1.0f + z * (0.6951786f + z * (0.2261646f + z * 0.0786568f));
    const std::uint32_t bits = (std::uint32_t) ((int) w + 127) << 23;
    float s; std::memcpy (&s, &bits, 4);
    return s * a;
}
inline float toDb (float x) noexcept   { return 6.0205999f * fastLog2 (x); }
inline float fromDb (float d) noexcept { return fastExp2 (d * 0.16609640f); }

inline float fastTanh (float x) noexcept
{
    x = std::clamp (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

struct BandP
{
    float punch = 0, body = 0, sustain = 0, gate = 0;       // shape  (-1..1, gate 0..1)
    float drive = 0, time = 0.4f, gainDb = 0, amount = 1;   // character
    bool solo = false, bypass = false;
};
struct Params
{
    int bands = 4; float x[3] { 120, 1000, 6000 };
    BandP band[kBands];
    int lookahead = 0;              // samples (at the engine rate)
    float mix = 1, outDb = 0, link = 1;
    bool clip = false, autoGain = false, sidechain = false, delta = false;
};

//==============================================================================
class Detector
{
public:
    void prepare (float fs_) { fs = fs_; e1 = eA = eB = e3 = e4 = 0.0f; gSm = 0.0f; h1 = h3 = h4 = 0; lastTime = -1.0f; setTime (0.4f); }
    void setTime (float t) noexcept
    {
        if (std::abs (t - lastTime) < 1.0e-4f) return;
        lastTime = t;
        auto c = [this] (float ms) { return std::exp (-1.0f / (std::max (0.02f, ms) * 0.001f * fs)); };
        aFast  = c (0.08f);
        aPunch = c (0.6f + 4.0f * t);
        aBody  = c (12.0f + 50.0f * t);
        r1 = c (60.0f + 240.0f * t);
        r3 = c (180.0f + 600.0f * t);
        r4 = c (15.0f + 60.0f * t);
        gCoef = 1.0f - c (0.3f + 0.5f * t);
        holdN = (int) ((0.015f + 0.015f * t) * fs);
    }
    // x = (linked) band level. returns the gain in dB for this sample
    inline float process (float x, const BandP& b) noexcept
    {
        if (x >= e1) { e1 = aFast * e1 + (1.0f - aFast) * x; h1 = holdN; } else if (h1 > 0) --h1; else e1 = r1 * e1 + (1.0f - r1) * x;
        // followers of the fast envelope: they lag on the way up and track it down instantly,
        // so only the onset reads as "transient" (no leaking into the tail)
        eA = e1 > eA ? aPunch * eA + (1.0f - aPunch) * e1 : e1;
        eB = eA > eB ? aBody * eB + (1.0f - aBody) * eA : eA;
        if (x >= e3) { e3 = aFast * e3 + (1.0f - aFast) * x; h3 = holdN; } else if (h3 > 0) --h3; else e3 = r3 * e3 + (1.0f - r3) * x;
        if (x >= e4) { e4 = aFast * e4 + (1.0f - aFast) * x; h4 = holdN; } else if (h4 > 0) --h4; else e4 = r4 * e4 + (1.0f - r4) * x;
        const float fl = 1.0e-5f;
        dPunch = std::clamp (toDb ((e1 + fl) / (eA + fl)), 0.0f, 30.0f);
        dBody  = std::clamp (toDb ((eA + fl) / (eB + fl)), 0.0f, 30.0f);
        dSus   = std::clamp (toDb ((e3 + fl) / (e4 + fl)), 0.0f, 30.0f);
        const float lvl = std::clamp ((toDb (e3 + 1e-9f) + 70.0f) / 10.0f, 0.0f, 1.0f);   // below -70 dBFS: do nothing
        float g = b.punch * 1.2f * dPunch + b.body * 0.9f * dBody + b.sustain * 1.4f * dSus;
        if (b.gate > 0.0f) g -= b.gate * 3.0f * std::max (0.0f, dSus - 1.5f);
        g *= lvl * b.amount;
        gSm += (std::clamp (g, -40.0f, 18.0f) - gSm) * gCoef;
        return gSm;
    }
    float gainDb() const noexcept { return gSm; }
    float dPunch = 0, dBody = 0, dSus = 0;
private:
    float fs = 48000, e1 = 0, eA = 0, eB = 0, e3 = 0, e4 = 0, gSm = 0, lastTime = -1;
    float aFast = 0.9f, aPunch = 0.99f, aBody = 0.999f, r1 = 0.999f, r3 = 0.999f, r4 = 0.99f, gCoef = 0.1f;
    int holdN = 1000, h1 = 0, h3 = 0, h4 = 0;
};

class Delay
{
public:
    void prepare (int maxSamples) { for (auto& b : buf) b.assign ((size_t) maxSamples + 2, 0.0f); pos = 0; }
    void setDelay (int d) { delay = std::clamp (d, 0, (int) buf[0].size() - 2); }
    inline void process (float& l, float& r) noexcept
    {
        if (delay == 0) return;
        const int n = (int) buf[0].size();
        buf[0][(size_t) pos] = l; buf[1][(size_t) pos] = r;
        int rp = pos - delay; if (rp < 0) rp += n;
        l = buf[0][(size_t) rp]; r = buf[1][(size_t) rp];
        pos = (pos + 1) % n;
    }
private:
    std::vector<float> buf[2];
    int pos = 0, delay = 0;
};

//==============================================================================
// up to 4-way phase-coherent splitter. Lower bands get the allpasses of the later crossovers,
// so every band has the same phase response and the sum is a pure allpass.
class Splitter
{
public:
    void set (const float* xs, float fs)
    {
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 3; ++k) { xo[c][k].set (xs[k], fs); apA[c][k].set (xs[k], fs); apB[c][k].set (xs[k], fs); }
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 3; ++k) { xo[c][k].reset(); apA[c][k].reset(); apB[c][k].reset(); }
    }
    inline void split (float x, int c, int nb, float* out) noexcept
    {
        if (nb <= 1) { out[0] = x; return; }
        float lo, rest, a1, a2;
        xo[c][0].process (x, lo, rest);
        if (nb == 2) { out[0] = lo; out[1] = rest; return; }
        float b1, rest2;
        xo[c][1].process (rest, b1, rest2);
        apA[c][1].process (lo, a1, a2); lo = a1 + a2;
        if (nb == 3) { out[0] = lo; out[1] = b1; out[2] = rest2; return; }
        float b2, b3;
        xo[c][2].process (rest2, b2, b3);
        apA[c][2].process (lo, a1, a2); lo = a1 + a2;
        apB[c][2].process (b1, a1, a2); b1 = a1 + a2;
        out[0] = lo; out[1] = b1; out[2] = b2; out[3] = b3;
    }
private:
    LR4 xo[2][3], apA[2][3], apB[2][3];
};

//==============================================================================
class TransientEngine
{
public:
    void prepare (double sampleRate, int maxLookahead)
    {
        fs = (float) sampleRate;
        for (auto& c : det) for (auto& d : c) d.prepare (fs);
        split.reset(); scSplit.reset();
        for (auto& d : la) d.prepare (maxLookahead + 4);
        inPow = outPow = 1.0e-6f; agDb = 0.0f;
        for (auto& v : lastX) v = -1.0f;
        lastBands = -1;
        first = true;
        resetMeters();
    }

    void setParams (const Params& np) noexcept
    {
        p = np;
        p.bands = std::clamp (p.bands, 1, kBands);
        for (int b = 0; b < kBands; ++b) for (auto& d : det[b]) d.setTime (p.band[b].time);
        float xs[3];
        xs[0] = std::clamp (p.x[0], 20.0f, 2000.0f);
        xs[1] = std::clamp (p.x[1], xs[0] * 1.25f, std::min (14000.0f, fs * 0.4f));
        xs[2] = std::clamp (p.x[2], xs[1] * 1.25f, std::min (18000.0f, fs * 0.45f));
        if (std::abs (xs[0] - lastX[0]) > 0.01f || std::abs (xs[1] - lastX[1]) > 0.01f || std::abs (xs[2] - lastX[2]) > 0.01f)
        {
            split.set (xs, fs); scSplit.set (xs, fs);
            for (int k = 0; k < 3; ++k) lastX[k] = xs[k];
        }
        if (p.bands != lastBands) { split.reset(); scSplit.reset(); lastBands = p.bands; }
        for (auto& l : la) l.setDelay (p.lookahead);
        outT = fromDb (p.outDb);
        for (int b = 0; b < kBands; ++b) bandGainT[b] = fromDb (p.band[b].gainDb);
        if (first)
        {
            outSm = outT; mixSm = p.mix;
            for (int b = 0; b < kBands; ++b) bandGainSm[b] = bandGainT[b];
            first = false;
        }
    }

    // scL/scR: optional external detection signal (used when p.sidechain)
    void process (float* L, float* R, const float* scL, const float* scR, int n) noexcept
    {
        const float sm = 1.0f - std::exp (-1.0f / (0.02f * fs));
        const float agC = 1.0f - std::exp (-1.0f / (2.0f * fs));
        const int nb = p.bands;
        bool anySolo = false;
        for (int b = 0; b < nb; ++b) anySolo |= p.band[b].solo;
        const bool useSc = p.sidechain && scL != nullptr && scR != nullptr;
        const bool linked = p.link > 0.999f;
        for (int i = 0; i < n; ++i)
        {
            float bs[2][kBands] {}, ds[2][kBands] {};
            split.split (L[i], 0, nb, bs[0]); split.split (R[i], 1, nb, bs[1]);
            const float (*dv)[kBands] = bs;
            if (useSc) { scSplit.split (scL[i], 0, nb, ds[0]); scSplit.split (scR[i], 1, nb, ds[1]); dv = ds; }

            float out[2] {}, dry[2] {};
            for (int b = 0; b < nb; ++b)
            {
                const BandP& bp = p.band[b];
                const float al = std::abs (dv[0][b]), ar = std::abs (dv[1][b]);
                const float mx = std::max (al, ar);
                float gl, gr;
                if (linked) gl = gr = det[b][0].process (mx, bp);
                else
                {
                    gl = det[b][0].process (p.link * mx + (1.0f - p.link) * al, bp);
                    gr = det[b][1].process (p.link * mx + (1.0f - p.link) * ar, bp);
                }
                float l = bs[0][b], r = bs[1][b];
                la[b].process (l, r);
                dry[0] += l; dry[1] += r;
                bandGainSm[b] += (bandGainT[b] - bandGainSm[b]) * sm;
                const float gAvg = 0.5f * (gl + gr);
                if (std::abs (gAvg) > std::abs (gExt[b])) gExt[b] = gAvg;
                float yl = l, yr = r;
                if (! bp.bypass)
                {
                    yl *= fromDb (gl); yr *= fromDb (gr);
                    if (bp.drive > 0.001f)
                    {
                        const float dg = 1.0f + bp.drive * 6.0f, comp = 1.0f / (1.0f + bp.drive * 4.5f);
                        yl = fastTanh (yl * dg) * comp; yr = fastTanh (yr * dg) * comp;
                    }
                    yl *= bandGainSm[b]; yr *= bandGainSm[b];
                }
                inPk[b] = std::max (inPk[b], std::max (std::abs (l), std::abs (r)));
                outPk[b] = std::max (outPk[b], std::max (std::abs (yl), std::abs (yr)));
                if (anySolo && ! bp.solo) continue;
                out[0] += yl; out[1] += yr;
            }
            mixSm += (p.mix - mixSm) * sm;
            float l = dry[0] + (out[0] - dry[0]) * mixSm, r = dry[1] + (out[1] - dry[1]) * mixSm;
            if (anySolo) { l = out[0]; r = out[1]; }
            if (p.autoGain)
            {
                inPow += (0.5f * (dry[0] * dry[0] + dry[1] * dry[1]) - inPow) * agC;
                outPow += (0.5f * (l * l + r * r) - outPow) * agC;
                agDb = std::clamp (10.0f * std::log10 ((inPow + 1e-9f) / (outPow + 1e-9f)), -12.0f, 12.0f);
            }
            else agDb *= 0.999f;
            outSm += (outT - outSm) * sm;
            const float og = outSm * fromDb (agDb);
            l *= og; r *= og;
            if (p.delta) { l -= dry[0] * outSm; r -= dry[1] * outSm; }
            if (p.clip) { l = softClip (l); r = softClip (r); }
            L[i] = l; R[i] = r;
        }
        for (int b = 0; b < kBands; ++b) bandGain[b] = b < nb ? det[b][0].gainDb() : 0.0f;
    }

    static inline float softClip (float x) noexcept
    {
        const float a = std::abs (x), k = 0.7f;
        if (a <= k) return x;
        const float y = k + (1.0f - k) * std::tanh ((a - k) / (1.0f - k));
        return x < 0.0f ? -y : y;
    }

    // live data for the UI (accumulated between resetMeters calls)
    float bandGain[kBands] {}, gExt[kBands] {}, inPk[kBands] {}, outPk[kBands] {};
    void resetMeters() noexcept { for (int b = 0; b < kBands; ++b) gExt[b] = inPk[b] = outPk[b] = 0.0f; }
    float autoGainDb() const noexcept { return agDb; }

private:
    Params p;
    float fs = 48000;
    Detector det[kBands][2];
    Splitter split, scSplit;
    Delay la[kBands];
    float inPow = 1e-6f, outPow = 1e-6f, agDb = 0, outT = 1, outSm = 1, mixSm = 1;
    float bandGainT[kBands] { 1, 1, 1, 1 }, bandGainSm[kBands] { 1, 1, 1, 1 };
    float lastX[3] { -1, -1, -1 };
    int lastBands = -1;
    bool first = true;
};

} // namespace lumen::tr
