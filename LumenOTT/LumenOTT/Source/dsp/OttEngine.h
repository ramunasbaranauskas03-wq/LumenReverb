#pragma once
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <atomic>

// =====================================================================================
// Lumen OTT 2 - three-band upward + downward compressor
//
//   * phase-coherent Linkwitz-Riley bands; DEPTH blends against the identically
//     filtered dry signal, so 0..100 % never combs
//   * modern gain computer: short RMS detector -> OTT static curve (upward 1:4 below,
//     ~inf:1 above) -> gain smoothed in dB with separate attack / release
//     => tails really come up, peaks really go down, no lag-pumping
//   * 2 ms lookahead: the gain moves before the transient arrives -> clean, no clicks,
//     no overshoot (the "expensive" sound)
//   * styles: CLEAN (lookahead, RMS), CLASSIC (OTT timing), SMASH (fast + grit)
//   * noise guard + boost limit so silence / hiss is not pulled up
// =====================================================================================
namespace lumen::ott
{
constexpr int kBands = 3;
constexpr double kPi = 3.14159265358979323846;

struct BandP { float downThr = -30, upThr = -40, gainDb = 0, downRatio = 66.7f, upRatio = 4.0f; bool solo = false, bypass = false; };
struct Params
{
    float x[2] { 88.3f, 2500.0f };
    BandP band[kBands];
    float depth = 1, time = 0.5f, upward = 1, downward = 1, inDb = 0, outDb = 0;
    int style = 0;                       // 0 Clean, 1 Classic, 2 Smash
    float maxBoost = 24.0f, floorDb = -70.0f, link = 1.0f;
    bool autoGain = false, adaptive = true;
};

// ---- Linkwitz-Riley 4th order (2 x Butterworth biquads), TDF2 double
struct Bq
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;
    void reset() { s1 = s2 = 0; }
    inline double tick (double x) noexcept { const double y = b0 * x + s1; s1 = b1 * x - a1 * y + s2; s2 = b2 * x - a2 * y; return y; }
    void set (bool hp, double f, double fs)
    {
        const double w = 2 * kPi * std::clamp (f, 10.0, 0.45 * fs) / fs, cw = std::cos (w), al = std::sin (w) / (2 * 0.70710678);
        const double a0 = 1 + al;
        if (hp) { b0 = (1 + cw) / 2 / a0; b1 = -(1 + cw) / a0; b2 = b0; } else { b0 = (1 - cw) / 2 / a0; b1 = (1 - cw) / a0; b2 = b0; }
        a1 = -2 * cw / a0; a2 = (1 - al) / a0;
    }
    void setAllpass (double f, double fs)
    {
        const double w = 2 * kPi * std::clamp (f, 10.0, 0.45 * fs) / fs, cw = std::cos (w), al = std::sin (w) / (2 * 0.70710678), a0 = 1 + al;
        b0 = (1 - al) / a0; b1 = -2 * cw / a0; b2 = (1 + al) / a0; a1 = -2 * cw / a0; a2 = (1 - al) / a0;
    }
};
struct Split3
{
    Bq lp1[2], hp1[2], lp2[2], hp2[2], apLow;
    void set (double x1, double x2, double fs)
    {
        for (auto& q : lp1) q.set (false, x1, fs); for (auto& q : hp1) q.set (true, x1, fs);
        for (auto& q : lp2) q.set (false, x2, fs); for (auto& q : hp2) q.set (true, x2, fs);
        apLow.setAllpass (x2, fs);
    }
    void reset() { for (auto* a : { lp1, hp1, lp2, hp2 }) for (int i = 0; i < 2; ++i) a[i].reset(); apLow.reset(); }
    inline void split (double x, double* b) noexcept
    {
        const double lo = lp1[1].tick (lp1[0].tick (x)), rest = hp1[1].tick (hp1[0].tick (x));
        b[0] = apLow.tick (lo);                                   // phase-match the low band to the 2nd crossover
        b[1] = lp2[1].tick (lp2[0].tick (rest));
        b[2] = hp2[1].tick (hp2[0].tick (rest));
    }
};

struct Delay
{
    std::vector<float> buf; int pos = 0, d = 0;
    void prepare (int dd) { d = std::max (0, dd); buf.assign ((size_t) d + 1, 0.0f); pos = 0; }
    inline float tick (float x) noexcept { if (d == 0) return x; buf[(size_t) pos] = x; pos = pos == d ? 0 : pos + 1; return buf[(size_t) pos]; }
};

class OttEngine
{
public:
    static constexpr double kLookMs = 2.0;
    static constexpr float kBandIn = 5.2f;     // OTT's +5.2 dB band input stage

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        look = (int) std::round (kLookMs * 0.001 * fs);
        for (auto& s : split) s.reset();
        for (auto& b : dly) for (auto& d : b) d.prepare (look);
        for (auto& b : st) for (auto& c : b) c = {};
        lastX[0] = lastX[1] = -1;
        agDb = 0; inPow = outPow = 1e-8; for (auto& v : ltLvl) v = -200;
        first = true;
        resetMeters();
    }
    int latency() const { return look; }

    void setParams (const Params& np) noexcept
    {
        p = np;
        const float x1 = std::clamp (p.x[0], 30.0f, 1000.0f), x2 = std::clamp (p.x[1], x1 * 2.0f, (float) std::min (16000.0, fs * 0.42));
        if (x1 != lastX[0] || x2 != lastX[1]) { for (auto& s : split) s.set (x1, x2, fs); lastX[0] = x1; lastX[1] = x2; }
        // timing per band (ms): detector window, gain attack, gain release
        static const float detW[3][3] { { 12, 5, 2.5f }, { 25, 12, 6 }, { 8, 3, 1.5f } };
        static const float attT[3][3] { { 5, 3, 1.2f }, { 47.8f, 22.4f, 13.5f }, { 3, 1.2f, 0.5f } };
        static const float relT[3][3] { { 120, 80, 50 }, { 282, 282, 132 }, { 70, 45, 30 } };
        const int s = std::clamp (p.style, 0, 2);
        const double tMul = std::pow (2.0, (p.time - 0.5) * 4.0);       // 0.25x .. 4x
        for (int b = 0; b < kBands; ++b)
        {
            det[b] = 1.0 - std::exp (-1.0 / (detW[s][b] * 0.001 * fs));
            att[b] = 1.0 - std::exp (-1.0 / (std::max (0.3, attT[s][b] * tMul) * 0.001 * fs));
            rel[b] = 1.0 - std::exp (-1.0 / (std::max (5.0, relT[s][b] * tMul) * 0.001 * fs));
        }
        ltUp = 1.0 - std::exp (-1.0 / (0.25 * fs)); ltDn = 1.0 - std::exp (-1.0 / (2.5 * fs));
        if (first) { depthSm = p.depth; inSm = dbg (p.inDb); outSm = dbg (p.outDb); for (int b = 0; b < kBands; ++b) bandSm[b] = dbg (p.band[b].gainDb + kBandIn); first = false; }
    }

    // static curve: gain (dB) for a detector level (dB, after the band input stage)
    float curve (float lv, int b, float off = 0.0f) const noexcept
    {
        const auto& bp = p.band[b];
        const float kn = 6.0f;
        float g = 0;
        const float dR = 1.0f - 1.0f / std::max (1.0f, bp.downRatio), o = lv - (bp.downThr + off);
        if (o > kn * 0.5f) g -= o * dR; else if (o > -kn * 0.5f) { const float k = o + kn * 0.5f; g -= dR * k * k / (2 * kn); }
        g *= p.downward;
        const float uR = 1.0f - 1.0f / std::max (1.0f, bp.upRatio), u = (bp.upThr + off) - lv;
        float up = 0;
        if (u > kn * 0.5f) up = u * uR; else if (u > -kn * 0.5f) { const float k = u + kn * 0.5f; up = uR * k * k / (2 * kn); }
        up = std::min (up * p.upward, p.maxBoost);
        const float fl = p.floorDb + std::min (0.0f, off);          // noise guard follows quiet sources down
        if (lv < fl + 15.0f) up *= std::clamp ((lv - fl) / 15.0f, 0.0f, 1.0f);
        return g + up;
    }

    void process (float* L, float* R, int n) noexcept
    {
        const double sm = 1.0 - std::exp (-1.0 / (0.02 * fs));
        const double agC = 1.0 - std::exp (-1.0 / (2.5 * fs));
        bool anySolo = false; for (auto& b : p.band) anySolo |= b.solo;
        const double inT = dbg (p.inDb), outT = dbg (p.outDb);
        double bandT[kBands]; for (int b = 0; b < kBands; ++b) bandT[b] = dbg (p.band[b].gainDb + kBandIn);
        const bool smash = p.style == 2;
        for (int i = 0; i < n; ++i)
        {
            inSm += (inT - inSm) * sm; outSm += (outT - outSm) * sm; depthSm += (p.depth - depthSm) * sm;
            for (int b = 0; b < kBands; ++b) bandSm[b] += (bandT[b] - bandSm[b]) * sm;
            const double xl = L[i] * inSm, xr = R[i] * inSm;
            double bl[kBands], br[kBands];
            split[0].split (xl, bl); split[1].split (xr, br);
            double wl = 0, wr = 0, dl = 0, dr = 0;
            for (int b = 0; b < kBands; ++b)
            {
                const auto& bp = p.band[b];
                auto& cl = st[b][0]; auto& cr = st[b][1];
                // RMS detectors (stereo linked), dB, + the band input stage
                cl.ms += (bl[b] * bl[b] - cl.ms) * det[b]; cr.ms += (br[b] * br[b] - cr.ms) * det[b];
                const double pl = cl.ms * 2.0, pr = cr.ms * 2.0, pm = std::max (pl, pr);
                const float lvL = (float) (10.0 * std::log10 (p.link * pm + (1 - p.link) * pl + 1e-14)) + kBandIn;
                const float lvR = (float) (10.0 * std::log10 (p.link * pm + (1 - p.link) * pr + 1e-14)) + kBandIn;
                // ADAPTIVE: thresholds ride on the band's program level, so OTT acts on any source level
                const float lvM = std::max (lvL, lvR);
                {
                    // program level: rises fast, falls slowly; deep tails / silence (30 dB under it, or under -70 dBFS) don't pull it down
                    auto& lt = ltLvl[b];
                    if (lvM > -65.0f) { if (lt < -150) lt = lvM; if (lvM > lt - 30.0f) lt += (lvM - lt) * (lvM > lt ? ltUp : ltDn); }
                }
                const float off = p.adaptive && ltLvl[b] > -150 ? std::clamp (ltLvl[b] - kRefLevel, -24.0f, 24.0f) : 0.0f;
                offNow[b] = off;
                // gain smoothing in dB (attack = gain going down, release = going up)
                const float tl = bp.bypass ? 0.0f : curve (lvL, b, off), tr = bp.bypass ? 0.0f : curve (lvR, b, off);
                cl.g += (tl - cl.g) * (tl < cl.g ? att[b] : rel[b]);
                cr.g += (tr - cr.g) * (tr < cr.g ? att[b] : rel[b]);
                // audio path: lookahead delay, then gain
                const double al = dly[b][0].tick ((float) bl[b]), ar = dly[b][1].tick ((float) br[b]);
                dl += al; dr += ar;
                double yl = al * std::pow (10.0, cl.g / 20.0) * bandSm[b], yr = ar * std::pow (10.0, cr.g / 20.0) * bandSm[b];
                if (bp.bypass) { yl = al; yr = ar; }
                if (smash) { yl = sat (yl); yr = sat (yr); }
                meterLvl[b] = std::max (meterLvl[b], std::max (lvL, lvR));
                meterGain[b] = std::abs (cl.g) > std::abs (meterGain[b]) ? cl.g : meterGain[b];
                if (anySolo && ! bp.solo) continue;
                wl += yl; wr += yr;
            }
            if (anySolo) { dl = wl; dr = wr; }
            double ol = dl + (wl - dl) * depthSm, orr = dr + (wr - dr) * depthSm;
            if (p.autoGain)
            {
                inPow += (0.5 * (dl * dl + dr * dr) - inPow) * agC; outPow += (0.5 * (ol * ol + orr * orr) - outPow) * agC;
                agDb = (float) std::clamp (10.0 * std::log10 ((inPow + 1e-12) / (outPow + 1e-12)), -30.0, 12.0);
            }
            else agDb *= 0.9995f;
            const double g = outSm * dbg (agDb);
            L[i] = (float) (ol * g); R[i] = (float) (orr * g);
        }
    }

    float meterLvl[kBands] {}, meterGain[kBands] {};
    void resetMeters() noexcept { for (int b = 0; b < kBands; ++b) { meterLvl[b] = -100.0f; meterGain[b] = 0.0f; } }
    float autoGainDb() const noexcept { return agDb; }
    float bandGainNow (int b) const noexcept { return st[b][0].g; }
    float thresholdOffset (int b) const noexcept { return offNow[b]; }
    static constexpr float kRefLevel = -22.0f;      // band level (after the +5.2 dB stage) the default thresholds are made for

private:
    static double dbg (double d) { return std::pow (10.0, d / 20.0); }
    static inline double sat (double x) noexcept { return std::tanh (x * 1.25) / 1.25 * (1.0 + 0.04 * std::abs (x)); }
    struct Ch { double ms = 0; float g = 0; };
    Params p; double fs = 48000; int look = 96;
    Split3 split[2];
    Delay dly[kBands][2];
    Ch st[kBands][2];
    double det[kBands] {}, att[kBands] {}, rel[kBands] {};
    double inSm = 1, outSm = 1, depthSm = 1, bandSm[kBands] { 1, 1, 1 };
    double inPow = 1e-8, outPow = 1e-8; float agDb = 0;
    float lastX[2] { -1, -1 };
    float ltLvl[kBands] { -200, -200, -200 }, offNow[kBands] {}; double ltUp = 0.001, ltDn = 0.0001;
    bool first = true;
};
} // namespace lumen::ott
