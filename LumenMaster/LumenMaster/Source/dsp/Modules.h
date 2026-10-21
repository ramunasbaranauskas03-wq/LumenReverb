#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include "LimiterCore.h"

// =====================================================================================
// Lumen Master processing modules (all double-precision paths, stereo):
//   BassControl, MultibandDynamics, Glue, Saturation, Imager, Clipper, Maximizer, Dither
// =====================================================================================
namespace lumen::master
{
// ------------------------------------------------------------------------------------ Bass control
struct BassParams { float freq = 120, punch = 0, sustain = 0, gain = 0; bool mono = true; };
class BassControl
{
public:
    void prepare (double sr) { fs = sr; split.fc = 0; split.reset(); fast.set (fs, 0.5, 40); slow.set (fs, 25, 150); tail.set (fs, 1, 350); fast.v = slow.v = tail.v = 0; gs = 1; }
    void process (float* L, float* R, int n, const BassParams& p) noexcept
    {
        split.set (fs, p.freq);
        const double sg = dbToGain (p.gain), k = 1.0 - std::exp (-1.0 / (0.002 * fs));
        for (int i = 0; i < n; ++i)
        {
            double lL, hL, lR, hR;
            split.split (L[i], 0, lL, hL); split.split (R[i], 1, lR, hR);
            if (p.mono) { const double m = 0.5 * (lL + lR); lL = lR = m; }
            const double det = std::max (std::abs (lL), std::abs (lR)) + 1e-9;
            const double f = fast.tick (det), s = slow.tick (det), t = tail.tick (det);
            double gdb = 0;
            if (p.punch != 0)   gdb += p.punch * 0.6 * std::clamp (gainToDb (f / std::max (s, 1e-9)), 0.0, 18.0);
            if (p.sustain != 0) gdb += p.sustain * 0.5 * std::clamp (gainToDb (t / std::max (f, 1e-9)), 0.0, 18.0);
            const double g = dbToGain (std::clamp (gdb, -12.0, 12.0)) * sg;
            gs += (g - gs) * k;
            L[i] = (float) (lL * gs + hL); R[i] = (float) (lR * gs + hR);
        }
        grDb.store ((float) gainToDb (gs), std::memory_order_relaxed);
    }
    std::atomic<float> grDb { 0.0f };
private:
    double fs = 48000, gs = 1; LR4 split; Envelope fast, slow, tail;
};

// ------------------------------------------------------------------------------------ Multiband dynamics
struct MbParams { float x1 = 120, x2 = 1000, x3 = 6000; float thr[4] { -18, -18, -18, -18 }, ratio[4] { 2, 2, 2, 2 }, gain[4] {}; float attack = 10, release = 120, mix = 1; };
class MultibandDynamics
{
public:
    void prepare (double sr) { fs = sr; bands.reset(); for (auto& e : env) e.v = 0; for (auto& g : gr) g.store (0); }
    void process (float* L, float* R, int n, const MbParams& p) noexcept
    {
        bands.set (fs, p.x1, p.x2, p.x3);
        static const double tscale[4] { 2.0, 1.2, 0.8, 0.5 };
        double mk[4];
        for (int b = 0; b < 4; ++b) { env[b].set (fs, p.attack * tscale[b], p.release * tscale[b]); mk[b] = dbToGain (p.gain[b]); }
        double maxGr[4] {};
        for (int i = 0; i < n; ++i)
        {
            double bl[4], br[4];
            bands.split (L[i], 0, bl); bands.split (R[i], 1, br);
            double ol = 0, orr = 0;
            for (int b = 0; b < 4; ++b)
            {
                const double e = env[b].tick (std::max (std::abs (bl[b]), std::abs (br[b])));
                const double over = gainToDb (e + 1e-12) - p.thr[b], kn = 6.0, sl = 1.0 / std::max (1.0f, p.ratio[b]) - 1.0;
                double g = 0;
                if (over > kn * 0.5) g = sl * over; else if (over > -kn * 0.5) g = sl * (over + kn * 0.5) * (over + kn * 0.5) / (2 * kn);
                maxGr[b] = std::min (maxGr[b], g);
                const double gl = dbToGain (g) * mk[b];
                ol += bl[b] * gl; orr += br[b] * gl;
            }
            // dry is the allpassed sum (phase aligned)
            if (p.mix < 1.0f) { const double dl = bl[0] + bl[1] + bl[2] + bl[3], dr = br[0] + br[1] + br[2] + br[3]; ol = dl + (ol - dl) * p.mix; orr = dr + (orr - dr) * p.mix; }
            L[i] = (float) ol; R[i] = (float) orr;
        }
        for (int b = 0; b < 4; ++b) gr[b].store ((float) maxGr[b], std::memory_order_relaxed);
    }
    std::atomic<float> gr[4];
private:
    double fs = 48000; BandSplitter bands; Envelope env[4];
};

// ------------------------------------------------------------------------------------ Glue (bus) compressor
struct GlueParams { float thr = -12, ratio = 2, attackMs = 3, releaseMs = -1 /* auto */, makeup = 0, mix = 1, scHz = 90; };
class Glue
{
public:
    void prepare (double sr) { fs = sr; hp.reset(); gdb = 0; slowDb = 0; grNow.store (0); lastSc = -1; }
    void process (float* L, float* R, int n, const GlueParams& p) noexcept
    {
        if (p.scHz != lastSc) { lastSc = p.scHz; hp.c = p.scHz > 0 ? designRbj (LowCut, fs, p.scHz, 0.7071, 0) : Coefs {}; }
        const double a = std::exp (-1.0 / (std::max (0.05f, p.attackMs) * 0.001 * fs));
        const bool autoRel = p.releaseMs <= 0;
        const double r = std::exp (-1.0 / ((autoRel ? 0.12 : p.releaseMs) * fs));
        const double rSlow = std::exp (-1.0 / (1.2 * fs)), aSlow = std::exp (-1.0 / (0.25 * fs));
        const double sl = 1.0 / std::max (1.0f, p.ratio) - 1.0, mk = dbToGain (p.makeup);
        double worst = 0;
        for (int i = 0; i < n; ++i)
        {
            const double sc = std::max (std::abs (hp.tick (L[i], 0)), std::abs (hp.tick (R[i], 1)));
            const double over = gainToDb (sc + 1e-12) - p.thr, kn = 4.0;
            double want = 0;
            if (over > kn * 0.5) want = sl * over; else if (over > -kn * 0.5) want = sl * (over + kn * 0.5) * (over + kn * 0.5) / (2 * kn);
            // log-domain smoothing; auto release = fast + slow program-dependent stage
            if (want < gdb) gdb = want + (gdb - want) * a; else gdb = want + (gdb - want) * r;
            double g = gdb;
            if (autoRel) { if (want < slowDb) slowDb = want + (slowDb - want) * aSlow; else slowDb = want + (slowDb - want) * rSlow; g = std::min (gdb, slowDb * 0.7 + gdb * 0.3); }
            worst = std::min (worst, g);
            const double gl = dbToGain (g) * mk;
            const double wl = L[i] * gl, wr = R[i] * gl;
            L[i] = (float) (L[i] + (wl - L[i]) * p.mix); R[i] = (float) (R[i] + (wr - R[i]) * p.mix);
        }
        grNow.store ((float) worst, std::memory_order_relaxed);
    }
    std::atomic<float> grNow { 0.0f };
private:
    double fs = 48000, gdb = 0, slowDb = 0; float lastSc = -1; Biquad hp;
};

// ------------------------------------------------------------------------------------
// Oversampled module shell: linear-phase 4x FIR oversampling that keeps running while the
// module is off, so switching never jumps in time; when off the output is the exactly
// delayed input (bit-transparent), with a 10 ms crossfade on every switch.
class OsShell
{
public:
    void prepare (double sr, int maxBlock)
    {
        fs = sr; maxN = maxBlock;
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os->initProcessing ((size_t) maxBlock); os->reset();
        lat = (int) std::lround (os->getLatencyInSamples());
        for (int c = 0; c < 2; ++c) { d[c].prepare (lat + 1); d[c].setDelay (lat); dry[c].assign ((size_t) maxBlock, 0.0f); }
        f = -1;
    }
    int latency() const { return lat; }
    template <typename F> void process (float* L, float* R, int n, bool on, F&& shape) noexcept
    {
        if (n > maxN) return;
        for (int i = 0; i < n; ++i) { dry[0][(size_t) i] = d[0].tick (L[i]); dry[1][(size_t) i] = d[1].tick (R[i]); }
        const float target = on ? 1.0f : 0.0f;
        if (f < 0) f = target;
        if (f == 0.0f && target == 0.0f) { std::copy (dry[0].begin(), dry[0].begin() + n, L); std::copy (dry[1].begin(), dry[1].begin() + n, R); idle = true; return; }
        if (idle) { os->reset(); idle = false; }
        float* ch[2] { L, R };
        juce::dsp::AudioBlock<float> blk (ch, 2, (size_t) n);
        auto up = os->processSamplesUp (blk);
        if (on || f > 0) shape (up);
        os->processSamplesDown (blk);
        if (f == 0.0f && target == 0.0f) { std::copy (dry[0].begin(), dry[0].begin() + n, L); std::copy (dry[1].begin(), dry[1].begin() + n, R); return; }
        if (f == 1.0f && target == 1.0f) return;
        const float step = 1.0f / (float) (0.01 * fs);
        for (int i = 0; i < n; ++i)
        {
            f = target > f ? std::min (target, f + step) : std::max (target, f - step);
            L[i] = dry[0][(size_t) i] + (L[i] - dry[0][(size_t) i]) * f;
            R[i] = dry[1][(size_t) i] + (R[i] - dry[1][(size_t) i]) * f;
        }
    }
private:
    double fs = 48000; int maxN = 0, lat = 0; float f = -1; bool idle = false;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    DelayLine d[2]; std::vector<float> dry[2];
};

// ------------------------------------------------------------------------------------ Saturation / exciter (4x oversampled, always running)
struct SatParams { int mode = 0; float drive = 3, tone = 0, exciter = 0, mix = 1; bool on = false; };
class Saturation
{
public:
    void prepare (double sr, int maxBlock)
    {
        fs = sr; shell.prepare (sr, maxBlock);
        pre.reset(); post.reset(); exHp.reset(); lastTone = 99;
        exHp.c = designRbj (LowCut, fs * 4, 4500, 0.7071, 0);
    }
    int latency() const { return shell.latency(); }
    void process (float* L, float* R, int n, const SatParams& p) noexcept
    {
        if (p.tone != lastTone) { lastTone = p.tone; pre.c = designRbj (HighShelf, fs * 4, 2500, 0.5, p.tone * 6.0); post.c = designRbj (HighShelf, fs * 4, 2500, 0.5, -p.tone * 6.0); }
        const double g = dbToGain (p.drive);
        shell.process (L, R, n, p.on, [&] (juce::dsp::AudioBlock<float>& up)
        {
            for (size_t c = 0; c < 2; ++c)
            {
                auto* d = up.getChannelPointer (c);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    const double x = d[i];
                    const double xt = p.tone != 0.0f ? pre.tick (x, (int) c) : x;
                    double y = shape (xt, g, p.mode);
                    if (p.tone != 0.0f) y = post.tick (y, (int) c);
                    if (p.exciter > 0) { const double h = exHp.tick (x, (int) c); y += p.exciter * 0.35 * std::tanh (h * 4.0) * 0.5; }
                    d[i] = (float) (x + (y - x) * p.mix);
                }
            }
        });
    }
    static double shape (double x, double g, int mode) noexcept
    {
        const double u = x * g;
        switch (mode)
        {
            case 1: { const double b = 0.25, tb = std::tanh (b); return (std::tanh (u + b) - tb) / ((1.0 - tb * tb) * g); }                  // tube (even harmonics)
            case 2: { const double v = std::clamp (u, -1.5, 1.5); return (v - v * v * v / 6.75) / g; }                                         // warm (cubic)
            case 3: { const double a = std::abs (u); const double y = a < 0.9 ? a : 0.9 + 0.1 * std::tanh ((a - 0.9) / 0.1); return (u < 0 ? -y : y) / g; } // hard
            default: return std::tanh (u) / g;                                                                                                // tape
        }
    }
private:
    double fs = 48000; float lastTone = 99;
    OsShell shell; Biquad pre, post, exHp;
};

// ------------------------------------------------------------------------------------ Imager (3-band width, phase aligned)
struct ImagerParams { float x1 = 250, x2 = 5000, wLow = 1, wMid = 1, wHigh = 1; };
class Imager
{
public:
    void prepare (double sr) { fs = sr; a.reset(); b.reset(); apLow.reset(); m1.reset(); m2.reset(); }
    void process (float* L, float* R, int n, const ImagerParams& p) noexcept
    {
        a.set (fs, p.x1); b.set (fs, p.x2); apLow.set (fs, p.x2); m1.set (fs, p.x1); m2.set (fs, p.x2);
        for (int i = 0; i < n; ++i)
        {
            const double m = 0.5 * ((double) L[i] + R[i]), s = 0.5 * ((double) L[i] - R[i]);
            double lo, rest, mid, hi; a.split (s, 0, lo, rest); b.split (rest, 0, mid, hi);
            lo = apLow.tick (lo, 0);
            const double so = lo * p.wLow + mid * p.wMid + hi * p.wHigh;
            const double mo = m2.tick (m1.tick (m, 0), 0);
            L[i] = (float) (mo + so); R[i] = (float) (mo - so);
        }
    }
private:
    double fs = 48000; LR4 a, b; LRAllpass apLow, m1, m2;
};

// ------------------------------------------------------------------------------------ Clipper (4x oversampled soft clip at 0 dBFS, always running)
struct ClipParams { bool on = false; float drive = 0, soft = 0.5f; };
class Clipper
{
public:
    void prepare (double sr, int maxBlock) { shell.prepare (sr, maxBlock); }
    int latency() const { return shell.latency(); }
    void process (float* L, float* R, int n, const ClipParams& p) noexcept
    {
        const float g = (float) dbToGain (p.drive), hard = 1.0f - p.soft;
        float red = 0;
        shell.process (L, R, n, p.on, [&] (juce::dsp::AudioBlock<float>& up)
        {
            for (size_t c = 0; c < 2; ++c)
            {
                auto* d = up.getChannelPointer (c);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    const float x = d[i] * g, y = lumen::clipShape (x, hard);
                    red = std::max (red, std::abs (x) - std::abs (y));
                    d[i] = y;
                }
            }
        });
        clipDb.store (red > 1e-6f ? (float) gainToDb (1.0 + red) : 0.0f, std::memory_order_relaxed);
    }
    std::atomic<float> clipDb { 0.0f };
private:
    OsShell shell;
};

// ------------------------------------------------------------------------------------
// 8x true-peak detector (32-tap Kaiser windowed-sinc per phase): tighter than the BS.1770
// 4x meters, so a master limited with it reads at or below the ceiling on any TP meter.
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
                const double t = (double) (j - (kTaps / 2 - 1)) - frac;          // tap j = sample n-(kTaps-1)+j, target n-kDelay+frac
                const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (kPi * t) / (kPi * t);
                const double r = t / half, win = std::abs (r) < 1.0 ? i0 (beta * std::sqrt (1.0 - r * r)) / i0 (beta) : 0.0;
                tmp[j] = sinc * win; sum += tmp[j];
            }
            for (int j = 0; j < kTaps; ++j) wT[j][ph] = (float) (tmp[j] / sum);
        }
        for (int j = 0; j < kTaps; ++j) wT[j][0] = j == kTaps - 1 - kDelay ? 1.0f : 0.0f;     // phase 0 = the sample itself
        reset();
    }
    void reset() { std::fill (std::begin (h), std::end (h), 0.0f); pos = 0; }
    inline float push (float x) noexcept
    {
        // double-written ring so the window is always contiguous: h[pos .. pos+kTaps)
        h[pos] = x; h[pos + kTaps] = x;
        pos = pos + 1 == kTaps ? 0 : pos + 1;
        const float* hp = h + pos;
        alignas (32) float acc[kPhases] {};
        for (int j = 0; j < kTaps; ++j)
        {
            const float v = hp[j];
            for (int ph = 0; ph < kPhases; ++ph) acc[ph] += wT[j][ph] * v;      // vectorises over the 8 phases
        }
        float m = 0; for (int ph = 0; ph < kPhases; ++ph) m = std::max (m, std::abs (acc[ph]));
        return m;
    }
private:
    alignas (32) float wT[kTaps][kPhases] {};
    float h[2 * kTaps] {}; int pos = 0;
};
using TruePeak8 = TruePeakT<32>;      // stage 1
using TruePeak8x = TruePeakT<128>;    // stage 2: reference grade, accurate to ~0.48 fs

// ------------------------------------------------------------------------------------ True-peak maximizer
struct MaxParams { float drive = 0, ceiling = -1.0f, release = 100; int mode = 0; float link = 1; bool delta = false; bool on = true; };
class Maximizer
{
public:
    static constexpr double kMaxLookMs = 5.0;
    void prepare (double sr)
    {
        fs = sr;
        Wmax = (int) std::ceil (kMaxLookMs * 0.001 * fs) + 1;
        W2 = std::max (4, (int) std::round (0.0005 * fs));
        for (int c = 0; c < 2; ++c)
        {
            gp[c].prepare (Wmax + 4); det[c].reset();
            ad[c].prepare (Wmax + 32); pre[c].prepare (Wmax + 4);
            // stage 2: short true-peak catcher on the output of stage 1
            dryD[c].prepare (W2 + 160); dryD[c].setDelay (W2 + TruePeak8x::kDelay - 1);
            gp2[c].prepare (W2 + 4); gp2[c].setWindow (W2); det2[c].reset(); ad2[c].prepare (W2 + 160); ad2[c].setDelay (W2 + TruePeak8x::kDelay - 1);
        }
        curW = -1; slowG = 1; gainSm = 1;
    }
    int latency() const { return Wmax + TruePeak8::kDelay - 1 + W2 + TruePeak8x::kDelay - 1; }
    static int lookSamples (int mode, double fs) { const double ms = mode == 1 ? 1.5 : mode == 2 ? 1.0 : mode == 3 ? 5.0 : 3.0; return std::max (1, (int) std::round (ms * 0.001 * fs)); }

    void process (float* L, float* R, int n, const MaxParams& p) noexcept
    {
        const int W = std::min (Wmax, lookSamples (p.mode, fs));
        if (W != curW)
        {
            curW = W;
            for (int c = 0; c < 2; ++c)
            {
                gp[c].setWindow (W); det[c].reset();
                ad[c].setDelay (W + TruePeak8::kDelay - 1);
                pre[c].setDelay (Wmax - W);
            }
        }
        const float relMul = p.mode == 1 ? 0.6f : p.mode == 2 ? 0.45f : p.mode == 3 ? 1.8f : 1.0f;
        const float relMs = std::max (1.0f, p.release * relMul);
        const bool dual = p.mode == 0 || p.mode == 3;
        const float rFast = std::exp (-1.0f / ((dual ? relMs * 0.4f : relMs) * 0.001f * (float) fs));
        const float rSlow = std::exp (-1.0f / (relMs * 3.0f * 0.001f * (float) fs));
        const float aSlow = std::exp (-1.0f / (0.35f * (float) fs));
        for (auto& g : gp) g.setRelease (rFast, rSlow, aSlow, dual);
        const float r2 = std::exp (-1.0f / (0.03f * (float) fs));
        for (auto& g : gp2) g.setRelease (r2, r2, r2, false);
        const float ceilDb = std::min (0.0f, p.ceiling);
        const float ceil = (float) dbToGain (ceilDb) * 0.9966f, hard = (float) dbToGain (ceilDb);
        const float ceil2 = hard * 0.990f;      // covers the 8x grid error (<= 0.08 dB up to 20 kHz)
        const float targetG = (float) dbToGain (p.drive);
        const float k = 1.0f - std::exp (-1.0f / (0.02f * (float) fs));
        const bool loud = p.mode == 2;
        float worst = 0;
        if (! p.on)
        {
            // off: same latency, no gain change at all
            for (int i = 0; i < n; ++i) { L[i] = ad2[0].process (ad[0].process (pre[0].tick (L[i]))); R[i] = ad2[1].process (ad[1].process (pre[1].tick (R[i]))); }
            grDb.store (0.0f, std::memory_order_relaxed);
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            gainSm += (targetG - gainSm) * k;
            float x[2] { pre[0].tick (L[i] * gainSm), pre[1].tick (R[i] * gainSm) };
            if (loud) for (auto& v : x) v = hard * 1.12f * lumen::clipShape (v / (hard * 1.12f), 0.6f);   // shave the fastest transients first
            float req[2];
            for (int c = 0; c < 2; ++c) { const float pk = det[c].push (x[c]); req[c] = pk > ceil ? ceil / pk : 1.0f; }
            const float lmin = std::min (req[0], req[1]);
            float out[2], st1[2], dry[2];
            for (int c = 0; c < 2; ++c)
            {
                const float r = req[c] + (lmin - req[c]) * p.link;
                const float g = gp[c].process (r);
                const float d = ad[c].process (x[c]);
                st1[c] = d * g; dry[c] = d;
                worst = std::max (worst, -(float) gainToDb (g));
            }
            // stage 2: catch the residual inter-sample overs that gain modulation creates (always linked)
            float req2[2];
            for (int c = 0; c < 2; ++c) { const float pk = det2[c].push (st1[c]); req2[c] = pk > ceil2 ? ceil2 / pk : 1.0f; }
            const float l2 = std::min (req2[0], req2[1]);
            for (int c = 0; c < 2; ++c)
            {
                const float g2 = gp2[c].process (l2);
                const float y = std::clamp (ad2[c].process (st1[c]) * g2, -hard, hard);
                const float dd = dryD[c].process (dry[c]);
                out[c] = p.delta ? dd - y : y;
            }
            L[i] = out[0]; R[i] = out[1];
        }
        grDb.store (worst, std::memory_order_relaxed);
    }
    std::atomic<float> grDb { 0.0f };
private:
    double fs = 48000; int Wmax = 240, curW = -1; float slowG = 1, gainSm = 1;
    lumen::GainPath gp[2], gp2[2]; TruePeak8 det[2]; TruePeak8x det2[2]; lumen::IntDelay ad2[2], dryD[2]; int W2 = 24; lumen::IntDelay ad[2]; DelayLine pre[2];
};

// ------------------------------------------------------------------------------------ Dither
class Dither
{
public:
    // mode: 0 off, 1 = 16-bit TPDF, 2 = 16-bit noise-shaped, 3 = 24-bit TPDF
    void process (float* L, float* R, int n, int mode) noexcept
    {
        if (mode <= 0) return;
        const double q = mode == 3 ? 8388608.0 : 32768.0, lsb = 1.0 / q;
        float* ch[2] { L, R };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                double x = ch[c][i];
                if (mode == 2) x -= 1.623 * e[c][0] - 0.982 * e[c][1] + 0.109 * e[c][2];
                const double d = (rnd() + rnd()) * lsb;
                const double y = std::round ((x + d) * q) / q;
                if (mode == 2) { e[c][2] = e[c][1]; e[c][1] = e[c][0]; e[c][0] = y - x; }
                ch[c][i] = (float) std::clamp (y, -1.0, 1.0 - lsb);
            }
    }
private:
    double rnd() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (double) s / 4294967296.0 - 0.5; }
    uint32_t s = 0x1234567u; double e[2][3] {};
};

} // namespace lumen::master
