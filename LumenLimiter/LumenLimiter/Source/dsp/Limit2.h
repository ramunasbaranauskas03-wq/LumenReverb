#pragma once
// Lumen Limiter 2 - mastering maximizer (FG-X style chain, rebuilt):
//   FG-style glue compressor -> transient section (Lo Punch / Detail / Transient)
//   -> GAIN -> [oversampled: colour + clipper] -> two-stage true-peak limiter with Balance
//   -> dither.  Monitoring (equal loudness, M / S / L / R / Delta) is done by the processor.
#include "Common.h"
#include "LimiterCore.h"

namespace lumen::master
{

// ------------------------------------------------------------------------------------ helpers
inline float onePoleCoef (double ms, double fs) { return (float) (1.0 - std::exp (-1.0 / (std::max (0.001, ms) * 0.001 * fs))); }
inline float gdb (float g) { return 8.68588963807f * std::log (std::max (g, 1.0e-9f)); }
inline float dbg (float d) { return std::exp (d * 0.11512925465f); }

struct Svf2
{
    double a1 = 0, a2 = 0, a3 = 0, k = 1.41421356237, ic1 = 0, ic2 = 0;
    void set (double fc, double fs, double q = 0.70710678118)
    {
        const double g = std::tan (kPi * std::clamp (fc, 5.0, 0.48 * fs) / fs);
        k = 1.0 / q; a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    inline double lp (double v0) noexcept
    {
        const double v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; return v2;
    }
    inline double hp (double v0) noexcept
    {
        const double v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; return v0 - k * v1 - v2;
    }
};

// ------------------------------------------------------------------------------------ FG-style glue compressor
struct CompParams { bool on = false, autoRel = false, midSide = false; float ratio = 2, thr = -18, attack = 10, release = 150, scHp = 60, mix = 1; };
class GlueComp
{
public:
    void prepare (double sr) { fs = sr; for (auto& f : hp) f.reset(); for (auto& v : y) v = 0; for (auto& v : rms) v = 0; for (auto& v : sus) v = 0; lastHp = -1; mixSm = 1; onSm = 0; }
    void process (float* L, float* R, int n, const CompParams& p) noexcept
    {
        if (std::abs (p.scHp - lastHp) > 0.01f) { for (auto& f : hp) f.set (p.scHp, fs); lastHp = p.scHp; }
        const float aC = onePoleCoef (p.attack, fs), rC = onePoleCoef (p.release, fs), rmsC = onePoleCoef (8.0, fs), susC = onePoleCoef (p.release * 2.0, fs);
        const float s = 1.0f - 1.0f / std::max (1.0f, p.ratio), W = 6.0f;
        const float smC = onePoleCoef (30.0, fs);
        float worst = 0;
        for (int i = 0; i < n; ++i)
        {
            onSm += ((p.on ? 1.0f : 0.0f) - onSm) * smC; mixSm += (p.mix - mixSm) * smC;
            if (onSm < 1.0e-4f && ! p.on) { y[0] = y[1] = 0; continue; }
            float a = L[i], b = R[i];
            if (p.midSide) { const float m = 0.5f * (a + b), sd = 0.5f * (a - b); a = m; b = sd; }
            const float d[2] { (float) hp[0].hp (a), (float) hp[1].hp (b) };
            float t[2];
            for (int c = 0; c < 2; ++c)
            {
                rms[c] += (d[c] * d[c] - rms[c]) * rmsC;
                const float lv = gdb (0.5f * std::fabs (d[c]) + 0.5f * std::sqrt (2.0f * rms[c]));
                const float over = lv - p.thr;
                t[c] = 2 * over <= -W ? 0.0f : 2 * std::abs (over) < W ? -s * (over + W * 0.5f) * (over + W * 0.5f) / (2 * W) : -s * over;
            }
            if (! p.midSide) t[0] = t[1] = std::min (t[0], t[1]);     // linked stereo
            float g[2];
            for (int c = 0; c < 2; ++c)
            {
                if (t[c] < y[c]) y[c] += (t[c] - y[c]) * aC;
                else
                {
                    float rc = rC;
                    if (p.autoRel)
                    {
                        const float q = y[c] < -0.05f ? std::clamp (sus[c] / y[c], 0.0f, 1.0f) : 1.0f;
                        rc = onePoleCoef (std::max (15.0f, p.release * 0.2f) + (p.release * 1.5f - std::max (15.0f, p.release * 0.2f)) * q, fs);
                    }
                    y[c] += (t[c] - y[c]) * rc;
                }
                sus[c] += (y[c] - sus[c]) * susC;
                g[c] = dbg (y[c] * onSm);
                worst = std::min (worst, y[c]);
            }
            float oa = a * g[0], ob = b * g[1];
            if (p.midSide) { const float l2 = oa + ob, r2 = oa - ob; oa = l2; ob = r2; }
            L[i] = L[i] + (oa - L[i]) * mixSm; R[i] = R[i] + (ob - R[i]) * mixSm;
        }
        grDb.store (worst * onSm, std::memory_order_relaxed);
    }
    std::atomic<float> grDb { 0 };
private:
    double fs = 48000; Svf2 hp[2]; float y[2] {}, rms[2] {}, sus[2] {}, lastHp = -1, mixSm = 1, onSm = 0;
};

// ------------------------------------------------------------------------------------ transients: Lo Punch, Detail, Transient
// sliding maximum over the last W samples (monotonic deque) - a ripple-free envelope for low frequencies
struct SlidingMax
{
    std::vector<float> val; std::vector<int64_t> idx; int cap = 1; int64_t head = 0, tail = 0, n = 0; int W = 1;
    void prepare (int w) { W = std::max (1, w); cap = 1; while (cap < W + 4) cap <<= 1; val.assign ((size_t) cap, 0.0f); idx.assign ((size_t) cap, 0); head = tail = n = 0; }
    inline float push (float x) noexcept
    {
        const int64_t i = n++;
        while (tail != head && val[(size_t) ((tail - 1) & (cap - 1))] <= x) --tail;
        val[(size_t) (tail & (cap - 1))] = x; idx[(size_t) (tail & (cap - 1))] = i; ++tail;
        while (idx[(size_t) (head & (cap - 1))] <= i - W) ++head;
        return val[(size_t) (head & (cap - 1))];
    }
};

struct TransParams { float loPunch = 0, loFreq = 100, detail = 0, detFreq = 5000, transient = 0; };
class Transients
{
public:
    void prepare (double sr)
    {
        fs = sr;
        // per band: fast / slow envelope times (ms) - RMS style for the lows so the waveform itself is not seen as a transient
        static constexpr double tf[3] { 8.0, 0.4, 4.0 }, ts[3] { 100.0, 25.0, 60.0 };
        static constexpr double gr[3] { 45.0, 15.0, 30.0 };
        for (int k = 0; k < 3; ++k) gRel[k] = onePoleCoef (gr[k], fs);
        for (int k = 0; k < 3; ++k) { fC[k] = onePoleCoef (tf[k], fs); sC[k] = onePoleCoef (ts[k], fs); }
        fR = onePoleCoef (60, fs);
        for (auto& m : smax) m.prepare ((int) (0.012 * fs));
        for (auto& e : env) { e[0] = 0; e[1] = 0; }
        for (auto& f : lo) f.reset();
        for (auto& f : hi) f.reset();
        lastLo = lastHi = -1; for (auto& g : gSm) g = 0;
    }
    void process (float* L, float* R, int n, const TransParams& p) noexcept
    {
        if (std::abs (p.loFreq - lastLo) > 0.01f) { for (auto& f : lo) f.set (p.loFreq, fs); lastLo = p.loFreq; }
        if (std::abs (p.detFreq - lastHi) > 0.01f) { for (auto& f : hi) f.set (p.detFreq, fs); lastHi = p.detFreq; }
        const float smC = onePoleCoef (1.5, fs);
        const bool any = p.loPunch > 1.0e-3f || p.detail > 1.0e-3f || std::abs (p.transient) > 1.0e-3f || std::abs (gSm[0]) + std::abs (gSm[1]) + std::abs (gSm[2]) > 1.0e-4f;
        if (! any) return;
        for (int i = 0; i < n; ++i)
        {
            float lb[2], hb[2];
            for (int c = 0; c < 2; ++c)
            {
                const float x = c == 0 ? L[i] : R[i];
                lb[c] = (float) lo[c].lp (x);
                hb[c] = x - (float) hi[c].lp (x);     // complementary: x = lp + hb exactly
            }
            const float bands[3] { smax[0].push (std::max (std::fabs (lb[0]), std::fabs (lb[1]))), std::max (std::fabs (hb[0]), std::fabs (hb[1])),
                                   smax[1].push (std::max (std::fabs (L[i]), std::fabs (R[i]))) };
            const float amt[3] { p.loPunch * 12.0f, p.detail * 9.0f, p.transient * 8.0f };
            static constexpr float dead[3] { 0.5f, 0.5f, 0.5f };
            for (int k = 0; k < 3; ++k)
            {
                float& f = env[k][0]; float& s = env[k][1];
                if (k == 1) { f += (bands[k] - f) * (bands[k] > f ? fC[k] : fR); s += (bands[k] - s) * (bands[k] > s ? sC[k] : fR); }
                else        { f = bands[k]; s += (bands[k] - s) * sC[k]; }   // 12 ms sliding max vs. a slow (symmetric) follower
                const float tr = std::clamp (gdb (f + 1.0e-9f) - gdb (s + 1.0e-9f) - dead[k], 0.0f, 12.0f) / 12.0f;
                const float target = amt[k] * tr;
                const bool rising = std::abs (target) > std::abs (gSm[k]);
                gSm[k] += (target - gSm[k]) * (rising ? smC : gRel[k]);
            }
            const float gl = dbg (gSm[0]) - 1.0f, gh = dbg (gSm[1]) - 1.0f, gt = dbg (gSm[2]);
            for (int c = 0; c < 2; ++c)
            {
                float& x = c == 0 ? L[i] : R[i];
                x = (x + lb[c] * gl + hb[c] * gh) * gt;
            }
        }
    }
    float punchDb() const { return gSm[0]; }
    float detailDb() const { return gSm[1]; }
private:
    double fs = 48000; float gRel[3] {}, fC[3] {}, sC[3] {}, fR = 0, env[3][2] {}, gSm[3] {}, lastLo = -1, lastHi = -1;
    Svf2 lo[2], hi[2];
    SlidingMax smax[2];
};

// ------------------------------------------------------------------------------------ colour + clipper (oversampled)
enum ColorType { ColorClean, ColorWarm, ColorTape, ColorBright };
class ColorClip
{
public:
    void prepare (double osRate)
    {
        fs = osRate;
        levC = onePoleCoef (300, fs); smC = onePoleCoef (30, fs);
        hpC = 1.0f - std::exp (-2.0f * (float) kPi * 3500.0f / (float) fs);
        tapeC = 1.0f - std::exp (-2.0f * (float) kPi * 16000.0f / (float) fs);
        dcC = 1.0f - std::exp (-2.0f * (float) kPi * 6.0f / (float) fs);
        for (int c = 0; c < 2; ++c) { hiLp[c] = tape[c] = dc[c] = 0; }
        pIn = pOut = 1.0e-6f; lev = 1; amt = 0; drv = 0; clipSm = 0; reduction = 0;
    }
    inline void process (float& l, float& r, int type, float drive, float clip, float shape, float ceilLin) noexcept
    {
        // colour
        amt += ((type == ColorClean ? 0.0f : std::min (1.0f, drive * 25.0f)) - amt) * smC;
        drv += (drive - drv) * smC;
        if (amt > 1.0e-4f)
        {
            const float g = dbg (drv * 18.0f);
            float x[2] { l, r }, y[2];
            for (int c = 0; c < 2; ++c)
            {
                const float u = x[c] * g;
                switch (type)
                {
                    case ColorWarm:   y[c] = std::tanh (u + 0.18f) - 0.17808087f; break;
                    case ColorTape:   tape[c] += (u - tape[c]) * tapeC; y[c] = 0.63661977f * std::atan (tape[c] * 1.5707963f); break;
                    case ColorBright: { hiLp[c] += (u - hiLp[c]) * hpC; const float h = u - hiLp[c]; y[c] = std::tanh (u) + 0.3f * std::tanh (2.5f * h); break; }
                    default:          y[c] = u; break;
                }
            }
            pIn += (x[0] * x[0] + x[1] * x[1] - pIn) * levC; pOut += (y[0] * y[0] + y[1] * y[1] - pOut) * levC;
            if (pIn > 1.0e-10f) lev += (std::clamp (std::sqrt (pIn / std::max (pOut, 1.0e-12f)), 0.05f, 4.0f) - lev) * levC;
            for (int c = 0; c < 2; ++c)
            {
                float v = y[c] * lev; dc[c] += (v - dc[c]) * dcC; v -= dc[c];
                x[c] += (v - x[c]) * amt;
            }
            l = x[0]; r = x[1];
        }
        // clipper: shaves peaks before the limiter (threshold relative to the ceiling)
        clipSm += (clip - clipSm) * smC;
        if (clipSm > 1.0e-4f)
        {
            const float th = ceilLin * dbg (6.0f * (1.0f - clipSm));
            const float a0 = std::max (std::fabs (l), std::fabs (r));
            l = th * clipShape (l / th, shape); r = th * clipShape (r / th, shape);
            const float a1 = std::max (std::fabs (l), std::fabs (r));
            if (a0 > 1.0e-6f) reduction = std::max (reduction, gdb (a0) - gdb (a1));
        }
    }
    float takeReduction() { const float r = reduction; reduction = 0; return r; }
private:
    double fs = 48000;
    float levC = 0, smC = 0, hpC = 0, tapeC = 0, dcC = 0, pIn = 0, pOut = 0, lev = 1, amt = 0, drv = 0, clipSm = 0, reduction = 0;
    float hiLp[2] {}, tape[2] {}, dc[2] {};
};

// ------------------------------------------------------------------------------------ true-peak detectors (8 phases)
#if defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
template <int Taps>
class TruePeakT
{
public:
    static constexpr int kTaps = Taps, kPhases = 8, kDelay = kTaps / 2;
    TruePeakT()
    {
        auto i0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        const double beta = kTaps >= 128 ? 10.0 : 9.0, half = kTaps / 2.0;
        for (int ph = 1; ph < kPhases; ++ph)
        {
            const double frac = (double) ph / kPhases; double sum = 0;
            double tmp[kTaps];
            for (int j = 0; j < kTaps; ++j)
            {
                const double t = (double) (j - (kTaps / 2 - 1)) - frac;
                const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (kPi * t) / (kPi * t);
                const double rr = t / half, win = std::abs (rr) < 1.0 ? i0 (beta * std::sqrt (1.0 - rr * rr)) / i0 (beta) : 0.0;
                tmp[j] = sinc * win; sum += tmp[j];
            }
            for (int j = 0; j < kTaps; ++j) wT[j][ph] = (float) (tmp[j] / sum);
        }
        for (int j = 0; j < kTaps; ++j) wT[j][0] = j == kTaps - 1 - kDelay ? 1.0f : 0.0f;
        reset();
    }
    void reset() { std::fill (std::begin (h), std::end (h), 0.0f); pos = 0; }
    inline float push (float x, bool interSample) noexcept
    {
        h[pos] = x; h[pos + kTaps] = x;
        pos = pos + 1 == kTaps ? 0 : pos + 1;
        const float* hp = h + pos;
        if (! interSample) return std::fabs (hp[kTaps - 1 - kDelay]);
        alignas (32) float acc[kPhases] {};
        for (int j = 0; j < kTaps; ++j) { const float v = hp[j]; for (int ph = 0; ph < kPhases; ++ph) acc[ph] += wT[j][ph] * v; }
        float m = 0; for (int ph = 0; ph < kPhases; ++ph) m = std::max (m, std::abs (acc[ph]));
        return m;
    }
private:
    alignas (32) float wT[kTaps][kPhases] {};
    float h[2 * kTaps] {}; int pos = 0;
};
#if defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif
using TP32 = TruePeakT<32>;
using TP128 = TruePeakT<128>;

// ------------------------------------------------------------------------------------ the limiter
enum LimitMode { ModeClear, ModeTight, ModePunchy, ModeLoud };
struct LimitParams { float ceiling = -1, release = 100, lookahead = 3, balance = 0, link = 1; int mode = ModeTight; bool truePeak = true; };

class Limiter2
{
public:
    static constexpr double kMaxLookMs = 10.0;
    void prepare (double sr)
    {
        fs = sr;
        Wmax = (int) std::ceil (kMaxLookMs * 0.001 * fs) + 1;
        W2 = std::max (4, (int) std::round (0.0005 * fs));
        for (int c = 0; c < 2; ++c)
        {
            gp[c].prepare (Wmax + 4); det[c].reset(); det2[c].reset(); gp2[c].prepare (W2 + 4); gp2[c].setWindow (W2);
            ad[c].prepare (Wmax + TP32::kDelay + 4); pre[c].prepare (Wmax + 4);
            ad2[c].prepare (W2 + TP128::kDelay + 4); ad2[c].setDelay (W2 + TP128::kDelay - 1);
            loSplit[c].reset();
        }
        curW = -1; lastXo = -1;
    }
    int latency() const { return Wmax + TP32::kDelay - 1 + W2 + TP128::kDelay - 1; }

    // x already contains the GAIN push. Returns in place.
    void process (float* L, float* R, int n, const LimitParams& p) noexcept
    {
        const int W = std::clamp ((int) std::round (p.lookahead * 0.001 * fs), 1, Wmax);
        if (W != curW)
        {
            curW = W;
            for (int c = 0; c < 2; ++c) { gp[c].setWindow (W); det[c].reset(); ad[c].setDelay (W + TP32::kDelay - 1); pre[c].setDelay (Wmax - W); }
        }
        if (lastXo < 0) { for (auto& f : loSplit) f.set (150.0, fs); lastXo = 150; }
        // mode -> release behaviour
        const float relMul = p.mode == ModeClear ? 1.6f : p.mode == ModeTight ? 0.7f : p.mode == ModePunchy ? 1.0f : 0.45f;
        const float relMs = std::max (1.0f, p.release * relMul);
        const bool dual = p.mode == ModeClear || p.mode == ModePunchy;
        const float rFast = std::exp (-1.0f / ((dual ? relMs * 0.4f : relMs) * 0.001f * (float) fs));
        const float rSlow = std::exp (-1.0f / (relMs * 3.0f * 0.001f * (float) fs));
        const float aSlow = std::exp (-1.0f / (0.35f * (float) fs));
        for (auto& g : gp) g.setRelease (rFast, rSlow, aSlow, dual);
        const float r2 = std::exp (-1.0f / (0.03f * (float) fs));
        for (auto& g : gp2) g.setRelease (r2, r2, r2, false);
        const float ceilDb = std::clamp (p.ceiling, -24.0f, 0.0f);
        const float hard = (float) dbToGain (ceilDb), ceil1 = hard * 0.9966f, ceil2 = hard * (p.truePeak ? 0.990f : 0.9995f);
        // Punchy / Loud: a soft pre-clip shaves the very first edge of transients (keeps punch, gains loudness)
        const float preClip = p.mode == ModeLoud ? 1.12f : p.mode == ModePunchy ? 1.30f : 0.0f;
        const float preHard = p.mode == ModeLoud ? 0.6f : 0.35f;
        const float b = std::clamp (p.balance, -1.0f, 1.0f) * 0.8f;
        const float wLo = 1.0f - b, wHi = 1.0f + b;
        float worst = 0, worstLo = 0, worstHi = 0, preRed = 0;
        for (int i = 0; i < n; ++i)
        {
            float x[2] { pre[0].tick (L[i]), pre[1].tick (R[i]) };
            if (preClip > 0)
                for (auto& v : x)
                {
                    const float a0 = std::fabs (v);
                    v = hard * preClip * clipShape (v / (hard * preClip), preHard);
                    if (a0 > 1.0e-6f) preRed = std::max (preRed, gdb (a0) - gdb (std::fabs (v) + 1.0e-12f));
                }
            float req[2];
            for (int c = 0; c < 2; ++c) { const float pk = det[c].push (x[c], p.truePeak); req[c] = pk > ceil1 ? ceil1 / pk : 1.0f; }
            const float lmin = std::min (req[0], req[1]);
            float st1[2];
            for (int c = 0; c < 2; ++c)
            {
                const float r = req[c] + (lmin - req[c]) * p.link;
                const float g = gp[c].process (r);
                const float d = ad[c].tick (x[c]);
                if (std::abs (b) > 1.0e-4f && g < 0.99999f)
                {
                    // Balance: the same overall reduction, skewed towards the lows (b < 0) or the highs (b > 0)
                    const float lg = std::log (g);
                    const float gL = std::exp (lg * wLo), gH = std::exp (lg * wHi);
                    const float lo = (float) loSplit[c].lp (d);
                    st1[c] = lo * gL + (d - lo) * gH;
                    worstLo = std::max (worstLo, -gdb (gL)); worstHi = std::max (worstHi, -gdb (gH));
                }
                else { loSplit[c].lp (d); st1[c] = d * g; }
                worst = std::max (worst, -gdb (g));
            }
            // stage 2: catches residual (inter-sample) overs - always linked, guarantees the ceiling
            float req2[2];
            for (int c = 0; c < 2; ++c) { const float pk = det2[c].push (st1[c], p.truePeak); req2[c] = pk > ceil2 ? ceil2 / pk : 1.0f; }
            const float l2 = std::min (req2[0], req2[1]);
            for (int c = 0; c < 2; ++c)
            {
                const float g2 = gp2[c].process (l2);
                const float yv = std::clamp (ad2[c].tick (st1[c]) * g2, -hard, hard);
                (c == 0 ? L[i] : R[i]) = yv;
            }
        }
        grDb.store (-worst, std::memory_order_relaxed);
        preClipDb.store (preRed, std::memory_order_relaxed);
        grLo.store (std::abs (b) > 1.0e-4f ? -worstLo : -worst, std::memory_order_relaxed);
        grHi.store (std::abs (b) > 1.0e-4f ? -worstHi : -worst, std::memory_order_relaxed);
    }
    std::atomic<float> grDb { 0 }, grLo { 0 }, grHi { 0 }, preClipDb { 0 };
private:
    double fs = 48000; int Wmax = 480, W2 = 24, curW = -1; double lastXo = -1;
    lumen::GainPath gp[2], gp2[2]; TP32 det[2]; TP128 det2[2];
    DelayLine ad[2], ad2[2], pre[2];
    Svf2 loSplit[2];
};

// ------------------------------------------------------------------------------------ dither
class Dither
{
public:
    // 0 off, 1 = 24-bit TPDF, 2 = 16-bit TPDF, 3 = 16-bit noise-shaped
    void process (float* L, float* R, int n, int mode) noexcept
    {
        if (mode <= 0) return;
        const double q = mode == 1 ? 8388608.0 : 32768.0, lsb = 1.0 / q;
        float* ch[2] { L, R };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                double x = ch[c][i];
                if (mode == 3) x -= 1.623 * e[c][0] - 0.982 * e[c][1] + 0.109 * e[c][2];
                const double d = (rnd() + rnd()) * lsb;
                const double yv = std::round ((x + d) * q) / q;
                if (mode == 3) { e[c][2] = e[c][1]; e[c][1] = e[c][0]; e[c][0] = yv - x; }
                ch[c][i] = (float) std::clamp (yv, -1.0, 1.0 - lsb);
            }
    }
private:
    double rnd() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (double) s / 4294967296.0 - 0.5; }
    uint32_t s = 0x1234567u; double e[2][3] {};
};

} // namespace lumen::master
