#pragma once
#include "PumpEngine.h"     // LR4
#include <cmath>
#include <cstring>
#include <cstdint>
#include <algorithm>

// =====================================================================================
// Lumen OTT engine — 3-band upward + downward compressor
//   * classic OTT defaults (Ableton "OTT" preset: crossovers 88.3 Hz / 2.5 kHz, thresholds, ratios, times)
//   * phase-coherent LR4 bands, and DEPTH blends against the same allpassed dry -> no comb filtering
//   * NOISE GUARD: upward boost fades out below a floor and is capped (OTT's hiss / room-noise problem)
//   * LEVEL LOCK: thresholds follow the long-term input level, so the effect is the same on quiet and loud sources
//   * three timing styles, peak / RMS detection, soft knee, stereo link, auto gain
// =====================================================================================
namespace lumen::ott
{

constexpr int kBands = 3;

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
inline float toDb (float x) noexcept   { return 6.0205999f * fastLog2 (std::max (x, 1.0e-9f)); }
inline float fromDb (float d) noexcept { return fastExp2 (d * 0.16609640f); }

struct BandP
{
    float downThr = -30, upThr = -40, inDb = 5.2f, outDb = 6;
    float downRatio = 66.7f, upRatio = 4.0f;
    bool solo = false, bypass = false;
};
struct Params
{
    float x[2] { 88.3f, 2500.0f };
    BandP band[kBands];
    float depth = 1, time = 0.5f, upward = 1, downward = 1;
    float inDb = 0, outDb = 0;
    int style = 0;                // 0 Classic, 1 Smack, 2 Smooth
    bool rms = false;
    float knee = 0.3f, link = 1.0f;
    float maxBoost = 30.0f, floorDb = -70.0f;
    bool levelLock = false, autoGain = false;
};

class Splitter3
{
public:
    void set (float x1, float x2, float fs) { lo.set (x1, fs); hi.set (x2, fs); ap.set (x2, fs); }
    void reset() { lo.reset(); hi.reset(); ap.reset(); }
    inline void split (float x, float* out) noexcept
    {
        float l, rest, a1, a2, m, h;
        lo.process (x, l, rest);
        hi.process (rest, m, h);
        ap.process (l, a1, a2);              // low band gets the 2nd crossover's allpass -> sum is an allpass
        out[0] = a1 + a2; out[1] = m; out[2] = h;
    }
private:
    LR4 lo, hi, ap;
};

class OttEngine
{
public:
    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        for (auto& s : split) s.reset();
        for (auto& b : st) for (auto& c : b) c = {};
        lastX[0] = lastX[1] = -1.0f;
        inPow = outPow = 1.0e-6f; agDb = 0.0f; lockLvl = 0.0f; lockPow = 0.0158f; lockInit = true;
        first = true;
        resetMeters();
    }

    void setParams (const Params& np) noexcept
    {
        p = np;
        const float x1 = std::clamp (p.x[0], 30.0f, 1000.0f), x2 = std::clamp (p.x[1], x1 * 2.0f, std::min (16000.0f, fs * 0.42f));
        if (std::abs (x1 - lastX[0]) > 0.01f || std::abs (x2 - lastX[1]) > 0.01f)
        {
            for (auto& s : split) s.set (x1, x2, fs);
            lastX[0] = x1; lastX[1] = x2;
        }
        // time constants (ms) of the classic OTT preset, scaled by TIME and the style
        static const float baseAtt[kBands] { 47.8f, 22.4f, 13.5f }, baseRel[kBands] { 282.0f, 282.0f, 132.0f };
        const float tMul = std::pow (2.0f, (p.time - 0.5f) * 4.0f);            // 0.25x .. 4x
        const float aMul = p.style == 1 ? 0.3f : p.style == 2 ? 2.0f : 1.0f;
        const float rMul = p.style == 1 ? 1.3f : p.style == 2 ? 1.6f : 1.0f;
        for (int b = 0; b < kBands; ++b)
        {
            att[b] = coef (baseAtt[b] * tMul * aMul);
            rel[b] = coef (baseRel[b] * tMul * rMul);
            inG[b] = fromDb (p.band[b].inDb);
            outT[b] = fromDb (p.band[b].outDb);
            if (first) outG[b] = outT[b];
        }
        rmsC = 1.0f - std::exp (-1.0f / (0.008f * fs));
        gSm = std::exp (-1.0f / (0.0015f * fs));
        inT = fromDb (p.inDb); oT = fromDb (p.outDb);
        if (first) { inSm = inT; oSm = oT; depthSm = p.depth; first = false; }
    }

    void process (float* L, float* R, int n) noexcept
    {
        const float sm = 1.0f - std::exp (-1.0f / (0.02f * fs));
        const float agC = 1.0f - std::exp (-1.0f / (3.0f * fs));
        const float lockC = 1.0f - std::exp (-1.0f / (1.5f * fs));
        const bool rmsDet = p.rms || p.style == 2;
        bool anySolo = false;
        for (auto& b : p.band) anySolo |= b.solo;
        const float knee = 0.5f + 11.5f * p.knee;
        for (int i = 0; i < n; ++i)
        {
            inSm += (inT - inSm) * sm; oSm += (oT - oSm) * sm; depthSm += (p.depth - depthSm) * sm;
            const float xl = L[i] * inSm, xr = R[i] * inSm;
            // level lock: thresholds follow the long-term level (relative to -18 dBFS RMS)
            if (lockInit) { lockPow = std::max (1.0e-8f, 0.5f * (xl * xl + xr * xr)); if (lockPow > 1.0e-7f) lockInit = false; }
            lockPow += (0.5f * (xl * xl + xr * xr) - lockPow) * lockC;
            const float lockOff = p.levelLock ? std::clamp (toDb (std::sqrt (lockPow)) + 18.0f, -24.0f, 18.0f) : 0.0f;
            lockLvl = lockOff;
            float bl[kBands], br[kBands];
            split[0].split (xl, bl); split[1].split (xr, br);
            float wl = 0, wr = 0, dl = 0, dr = 0;
            for (int b = 0; b < kBands; ++b)
            {
                dl += bl[b]; dr += br[b];
                const auto& bp = p.band[b];
                outG[b] += (outT[b] - outG[b]) * sm;
                const float sl = bl[b] * inG[b], sr = br[b] * inG[b];
                float gl = 0.0f, gr = 0.0f;
                if (! bp.bypass)
                {
                    auto& cl = st[b][0]; auto& cr = st[b][1];
                    const float al = rmsDet ? rmsEnv (cl, sl) : std::abs (sl), ar = rmsDet ? rmsEnv (cr, sr) : std::abs (sr);
                    // level detector with the band's attack / release (the classic OTT ballistics)
                    cl.env = al > cl.env ? al + (cl.env - al) * att[b] : al + (cl.env - al) * rel[b];
                    cr.env = ar > cr.env ? ar + (cr.env - ar) * att[b] : ar + (cr.env - ar) * rel[b];
                    const float mx = std::max (cl.env, cr.env);
                    const float lvL = toDb (p.link * mx + (1.0f - p.link) * cl.env), lvR = toDb (p.link * mx + (1.0f - p.link) * cr.env);
                    gl = smoothGain (cl, curve (lvL, bp, lockOff, knee));
                    gr = smoothGain (cr, curve (lvR, bp, lockOff, knee));
                }
                float yl = sl * fromDb (gl) * outG[b], yr = sr * fromDb (gr) * outG[b];
                if (bp.bypass) { yl = bl[b]; yr = br[b]; }
                meterLvl[b] = std::max (meterLvl[b], std::max (std::abs (sl), std::abs (sr)));
                if (gl > meterUp[b]) meterUp[b] = gl;
                if (gl < meterDown[b]) meterDown[b] = gl;
                if (anySolo && ! bp.solo) continue;
                wl += yl; wr += yr;
            }
            if (anySolo) { dl = wl; dr = wr; }
            // depth blends with the allpassed dry (same phase as the bands) -> no comb filtering at any setting
            float ol = dl + (wl - dl) * depthSm, orr = dr + (wr - dr) * depthSm;
            if (p.autoGain)
            {
                inPow += (0.5f * (dl * dl + dr * dr) - inPow) * agC;
                outPow += (0.5f * (ol * ol + orr * orr) - outPow) * agC;
                agDb = std::clamp (10.0f * std::log10 ((inPow + 1e-10f) / (outPow + 1e-10f)), -24.0f, 12.0f);
            }
            else agDb *= 0.9995f;
            const float g = oSm * fromDb (agDb);
            L[i] = ol * g; R[i] = orr * g;
        }
    }

    // meters (UI), reset by the caller
    float meterLvl[kBands] {}, meterUp[kBands] {}, meterDown[kBands] {};
    void resetMeters() noexcept { for (int b = 0; b < kBands; ++b) { meterLvl[b] = 0; meterUp[b] = 0; meterDown[b] = 0; } }
    float autoGainDb() const noexcept { return agDb; }
    float lockOffsetDb() const noexcept { return lockLvl; }

    // static curve (dB of gain for an input level in dB) — public for the UI and tests
    float curve (float lv, const BandP& bp, float lockOff, float knee) const noexcept
    {
        const float dT = bp.downThr + lockOff, uT = bp.upThr + lockOff;
        const float dR = 1.0f - 1.0f / std::max (1.0f, bp.downRatio), uR = 1.0f - 1.0f / std::max (1.0f, bp.upRatio);
        float g = 0.0f;
        // downward (soft knee)
        const float o = lv - dT;
        if (o > knee * 0.5f) g -= o * dR;
        else if (o > -knee * 0.5f) { const float k = o + knee * 0.5f; g -= dR * k * k / (2.0f * knee); }
        g *= p.downward;
        // upward (soft knee), capped, faded out under the noise floor
        const float u = uT - lv;
        float up = 0.0f;
        if (u > knee * 0.5f) up = u * uR;
        else if (u > -knee * 0.5f) { const float k = u + knee * 0.5f; up = uR * k * k / (2.0f * knee); }
        up = std::min (up * p.upward, p.maxBoost);
        const float fl = p.floorDb + lockOff;
        if (lv < fl + 12.0f) up *= std::clamp ((lv - fl) / 12.0f, 0.0f, 1.0f);
        return g + up;
    }

private:
    struct Ch { float g = 0.0f, ms = 0.0f, env = 0.0f; };
    inline float rmsEnv (Ch& c, float x) noexcept { c.ms += (x * x - c.ms) * rmsC; return std::sqrt (c.ms) * 1.414f; }
    // short de-zipper on the gain (the ballistics are in the detector)
    inline float smoothGain (Ch& c, float target) noexcept { c.g = target + (c.g - target) * gSm; return c.g; }
    float coef (float ms) const noexcept { return std::exp (-1.0f / (std::max (0.05f, ms) * 0.001f * fs)); }

    Params p;
    float fs = 48000;
    Splitter3 split[2];
    Ch st[kBands][2];
    float att[kBands] {}, rel[kBands] {}, inG[kBands] { 1, 1, 1 }, outT[kBands] { 1, 1, 1 }, outG[kBands] { 1, 1, 1 };
    float rmsC = 0.01f, gSm = 0.9f, inT = 1, oT = 1, inSm = 1, oSm = 1, depthSm = 1;
    float inPow = 1e-6f, outPow = 1e-6f, agDb = 0, lockLvl = 0, lockPow = 0.0158f;
    float lastX[2] { -1, -1 };
    bool first = true, lockInit = true;
};

} // namespace lumen::ott
