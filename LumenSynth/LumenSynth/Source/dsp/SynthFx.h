#pragma once
#include "SynthEngine.h"
#include "ReverbEngine.h"
#include "DelayLine.h"
#include "PumpEngine.h"     // SVFp, LR4
#include <cmath>
#include <algorithm>

namespace lumen::synth
{

enum FxId { fxHyper, fxDist, fxFlanger, fxPhaser, fxChorus, fxDelay, fxComp, fxReverb, fxEq, fxOtt, fxPump, kNumFx };

struct FxP
{
    bool hypOn = false;  float hypDetune = 0.35f, hypDim = 0.5f, hypMix = 0.4f;
    bool distOn = false; int distMode = 0; float distDrive = 0.3f, distMix = 1;
    bool flaOn = false;  float flaRate = 0.2f, flaDepth = 0.6f, flaFb = 0.5f, flaMix = 0.5f;
    bool phaOn = false;  float phaRate = 0.3f, phaDepth = 0.7f, phaFb = 0.5f, phaMix = 0.5f;
    bool choOn = false;  float choRate = 0.6f, choDepth = 0.5f, choMix = 0.4f;
    bool dlyOn = false;  double dlyBeats = 0.75; float dlyFb = 0.35f, dlyTone = 0.6f, dlyMix = 0.2f; bool dlyPing = true;
    bool cmpOn = false;  float cmpThresh = -18, cmpRatio = 4, cmpAttack = 5, cmpRelease = 120, cmpGain = 3;
    bool revOn = false;  float revSize = 0.6f, revDecay = 2.5f, revDamp = 0.5f, revMix = 0.2f;
    bool eqOn = false;   float eqLow = 0, eqMid = 0, eqMidFreq = 1200, eqHigh = 0;
    bool ottOn = false;  float ottDepth = 0.5f, ottTime = 0.5f;
    bool pumpOn = false; double pumpBeats = 1; float pumpDepth = 0.7f, pumpCurve = 0.5f;
    float volDb = -3; bool clip = true;
    int order[kNumFx] { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    float mod[kNumFxDst] {};     // global modulation offsets (see Dst::dFxDistDrive..)

    void applyMods() noexcept
    {
        auto c01 = [] (float v) { return std::clamp (v, 0.0f, 1.0f); };
        distDrive = c01 (distDrive + mod[dFxDistDrive - dFxDistDrive]);
        distMix   = c01 (distMix   + mod[dFxDistMix - dFxDistDrive]);
        hypMix    = c01 (hypMix    + mod[dFxHyperMix - dFxDistDrive]);
        choMix    = c01 (choMix    + mod[dFxChorusMix - dFxDistDrive]);
        phaMix    = c01 (phaMix    + mod[dFxPhaserMix - dFxDistDrive]);
        flaMix    = c01 (flaMix    + mod[dFxFlangerMix - dFxDistDrive]);
        dlyMix    = c01 (dlyMix    + mod[dFxDelayMix - dFxDistDrive]);
        dlyFb     = std::clamp (dlyFb + 0.95f * mod[dFxDelayFb - dFxDistDrive], 0.0f, 0.95f);
        revMix    = c01 (revMix    + mod[dFxReverbMix - dFxDistDrive]);
        revSize   = c01 (revSize   + mod[dFxReverbSize - dFxDistDrive]);
        pumpDepth = c01 (pumpDepth + mod[dFxPumpDepth - dFxDistDrive]);
    }
};

inline float mixDry (float m) { return std::min (1.0f, 2.0f * (1.0f - m)); }
inline float mixWet (float m) { return std::min (1.0f, 2.0f * m); }

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    // type 0 low shelf, 1 peak, 2 high shelf (RBJ)
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
};

//==============================================================================
class Distortion
{
public:
    void prepare (float fs_)
    {
        fs = fs_;
        for (int c = 0; c < 2; ++c) { for (auto& f : up[c]) { f.set (0.21f * 2.0f * fs, 2.0f * fs); f.reset(); } for (auto& f : dn[c]) { f.set (0.21f * 2.0f * fs, 2.0f * fs); f.reset(); } }
        holdN = 0; held[0] = held[1] = 0; dcx[0] = dcx[1] = dcy[0] = dcy[1] = 0;
    }
    void process (float* L, float* R, int n, int mode, float drive, float mix) noexcept
    {
        const float g = std::pow (10.0f, drive * 36.0f / 20.0f);
        const float comp = mode == 3 ? 1.0f : 1.0f / std::pow (g, 0.42f);
        const float dryG = mixDry (mix), wetG = mixWet (mix);
        float* ch[2] { L, R };
        if (mode == 3)
        {
            const float q = std::pow (2.0f, 15.0f - 13.0f * drive);
            const int down = 1 + (int) (drive * drive * 15.0f);
            for (int i = 0; i < n; ++i)
            {
                if (holdN <= 0) { held[0] = std::round (L[i] * q) / q; held[1] = std::round (R[i] * q) / q; holdN = down; }
                --holdN;
                for (int c = 0; c < 2; ++c) ch[c][i] = dryG * ch[c][i] + wetG * held[c];
            }
            return;
        }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float x = ch[c][i];
                float y = 0.0f;
                for (int k = 0; k < 2; ++k)
                {
                    float u = k == 0 ? 2.0f * x : 0.0f, lo, hi;
                    up[c][0].process (u, lo, hi); up[c][1].process (lo, u, hi);
                    float s = shape (u * g, mode);
                    dn[c][0].process (s, lo, hi); dn[c][1].process (lo, s, hi);
                    if (k == 0) y = s;
                }
                const float d = y - dcx[c] + 0.9995f * dcy[c]; dcx[c] = y; dcy[c] = d;
                ch[c][i] = dryG * x + wetG * d * comp;
            }
    }
private:
    static inline float shape (float x, int mode) noexcept
    {
        switch (mode)
        {
            case 0:  return fastTanh (x + 0.15f) - 0.1489f;
            case 1:  return std::clamp (x, -1.0f, 1.0f);
            case 2:  return std::sin (1.5707963f * x);
            default: return x;
        }
    }
    float fs = 48000;
    SVFp up[2][2], dn[2][2];
    float held[2] {}, dcx[2] {}, dcy[2] {};
    int holdN = 0;
};

//==============================================================================
class Ott
{
public:
    void prepare (float fs_)
    {
        fs = fs_;
        for (auto& c : xs) { c[0].set (120, fs); c[1].set (2500, fs); c[2].set (2500, fs); c[0].reset(); c[1].reset(); c[2].reset(); }
        for (auto& e : env) e = 0;
        for (auto& g : gSm) g = 1;
    }
    void process (float* L, float* R, int n, float depth, float time) noexcept
    {
        static constexpr float tDown[3] = { -33.8f, -30.2f, -35.5f }, tUp[3] = { -40.8f, -41.8f, -40.8f };
        static constexpr float makeup[3] = { 10.3f, 5.7f, 10.3f };
        const float atk = std::exp (-1.0f / ((0.0005f + 0.009f * time) * fs));
        const float rel = std::exp (-1.0f / ((0.02f + 0.28f * time) * fs));
        const float gsm = 1.0f - std::exp (-1.0f / (0.002f * fs));
        for (int i = 0; i < n; ++i)
        {
            float b[2][3];
            for (int c = 0; c < 2; ++c)
            {
                const float x = c == 0 ? L[i] : R[i];
                float lo, rest, mid, hi, a1, a2;
                xs[c][0].process (x, lo, rest);
                xs[c][2].process (lo, a1, a2); lo = a1 + a2;
                xs[c][1].process (rest, mid, hi);
                b[c][0] = lo; b[c][1] = mid; b[c][2] = hi;
            }
            float outL = 0.0f, outR = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                const float a = std::max (std::fabs (b[0][k]), std::fabs (b[1][k]));
                env[k] = a > env[k] ? atk * env[k] + (1.0f - atk) * a : rel * env[k] + (1.0f - rel) * a;
                const float lvl = 20.0f * std::log10 (std::max (env[k], 1.0e-6f));
                float gdb = 0.0f;
                if (lvl > tDown[k]) gdb -= (lvl - tDown[k]) * (1.0f - 1.0f / 66.7f);
                if (lvl < tUp[k] && lvl > -75.0f) gdb += std::min ((tUp[k] - lvl) * (1.0f - 1.0f / 4.17f), 30.0f);
                gdb = depth * (gdb + makeup[k]);
                gSm[k] += (std::pow (10.0f, gdb / 20.0f) - gSm[k]) * gsm;
                outL += b[0][k] * gSm[k]; outR += b[1][k] * gSm[k];
            }
            L[i] = outL; R[i] = outR;
        }
    }
    float bandGain (int k) const { return gSm[k]; }
private:
    float fs = 48000;
    LR4 xs[2][3];
    float env[3] {}, gSm[3] { 1, 1, 1 };
};

//==============================================================================
class Chorus
{
public:
    void prepare (float fs_) { fs = fs_; for (auto& d : dl) d.prepare ((int) (0.05f * fs)); ph = 0; }
    void process (float* L, float* R, int n, float rate, float depth, float mix) noexcept
    {
        const float inc = rate / fs;
        const float base = 0.011f * fs, dep = (0.0005f + 0.0055f * depth) * fs;
        const float dryG = 1.0f - 0.5f * mix, wetG = mix;
        for (int i = 0; i < n; ++i)
        {
            ph += inc; if (ph >= 1.0f) ph -= 1.0f;
            dl[0].push (L[i]); dl[1].push (R[i]);
            const float s0 = std::sin (2.0f * kPiS * ph), s1 = std::sin (2.0f * kPiS * (ph + 0.25f));
            const float s2 = std::sin (2.0f * kPiS * (ph + 0.5f)), s3 = std::sin (2.0f * kPiS * (ph + 0.75f));
            const float wl = 0.5f * (dl[0].readLinear (base + dep * s0) + dl[1].readLinear (base * 1.31f + dep * s2));
            const float wr = 0.5f * (dl[1].readLinear (base + dep * s1) + dl[0].readLinear (base * 1.31f + dep * s3));
            L[i] = dryG * L[i] + wetG * wl;
            R[i] = dryG * R[i] + wetG * wr;
        }
    }
private:
    float fs = 48000, ph = 0;
    DelayLine dl[2];
};

//==============================================================================
// Hyper / Dimension: 6 slowly drifting, detuned delay voices spread across the stereo field.
class Hyper
{
public:
    void prepare (float fs_) { fs = fs_; dl.prepare ((int) (0.045f * fs)); for (int k = 0; k < 6; ++k) ph[k] = (float) k / 6.0f; }
    void process (float* L, float* R, int n, float detune, float dim, float mix) noexcept
    {
        static constexpr float baseMs[6] = { 7.1f, 9.3f, 11.7f, 13.9f, 16.4f, 19.2f };
        static constexpr float rates[6] = { 0.31f, 0.47f, 0.59f, 0.73f, 0.89f, 1.07f };
        const float rateMul = 0.4f + 3.0f * detune;
        const float depth = (0.15f + 2.2f * detune) * 0.001f * fs;
        const float dryG = 1.0f - 0.5f * mix, wetG = mix;
        const float cross = 1.0f - dim;
        const float norm = 1.0f / (3.0f * (1.0f + cross) * 0.75f);
        for (int i = 0; i < n; ++i)
        {
            dl.push (0.5f * (L[i] + R[i]));
            float even = 0.0f, odd = 0.0f;
            for (int k = 0; k < 6; ++k)
            {
                ph[k] += rates[k] * rateMul / fs; if (ph[k] >= 1.0f) ph[k] -= 1.0f;
                const float d = baseMs[k] * 0.001f * fs + depth * (0.5f + 0.5f * std::sin (2.0f * kPiS * ph[k]));
                const float v = dl.readLinear (d);
                if (k & 1) odd += v; else even += v;
            }
            const float wl = (even + cross * odd) * norm, wr = (odd + cross * even) * norm;
            L[i] = dryG * L[i] + wetG * wl;
            R[i] = dryG * R[i] + wetG * wr;
        }
    }
private:
    float fs = 48000, ph[6] {};
    DelayLine dl;
};

//==============================================================================
class Flanger
{
public:
    void prepare (float fs_) { fs = fs_; for (auto& d : dl) d.prepare ((int) (0.012f * fs)); ph = 0; last[0] = last[1] = 0; }
    void process (float* L, float* R, int n, float rate, float depth, float fb, float mix) noexcept
    {
        float* ch[2] { L, R };
        const float minD = 0.0003f * fs, range = (0.0002f + 0.0058f * depth) * fs;
        for (int i = 0; i < n; ++i)
        {
            ph += rate / fs; if (ph >= 1.0f) ph -= 1.0f;
            for (int c = 0; c < 2; ++c)
            {
                const float x = ch[c][i];
                const float lfo = 0.5f + 0.5f * std::sin (2.0f * kPiS * (ph + 0.25f * (float) c));
                dl[c].push (fastTanh (x + fb * last[c]));
                const float d = dl[c].readLinear (minD + range * lfo);
                last[c] = d;
                ch[c][i] = x * (1.0f - 0.5f * mix) + 0.5f * mix * d;
            }
        }
    }
private:
    float fs = 48000, ph = 0, last[2] {};
    DelayLine dl[2];
};

//==============================================================================
class Phaser
{
public:
    void prepare (float fs_) { fs = fs_; ph = 0; for (auto& c : st) for (auto& x : c) x = 0; last[0] = last[1] = 0; }
    void process (float* L, float* R, int n, float rate, float depth, float fb, float mix) noexcept
    {
        float* ch[2] { L, R };
        for (int i = 0; i < n; ++i)
        {
            ph += rate / fs; if (ph >= 1.0f) ph -= 1.0f;
            if ((i & 7) == 0)
                for (int c = 0; c < 2; ++c)
                {
                    const float u = 0.5f + 0.5f * std::sin (2.0f * kPiS * (ph + 0.25f * (float) c));
                    const float f = 180.0f * std::exp2 (depth * 4.6f * u);
                    const float g = std::tan (kPiS * std::min (f, 0.45f * fs) / fs);
                    G[c] = g / (1.0f + g);
                }
            for (int c = 0; c < 2; ++c)
            {
                const float x = ch[c][i];
                float y = x + fb * last[c];
                for (int k = 0; k < 6; ++k)
                {
                    const float v = (y - st[c][k]) * G[c];
                    const float lp = v + st[c][k];
                    st[c][k] = lp + v;
                    y = 2.0f * lp - y;
                }
                last[c] = fastTanh (y);
                ch[c][i] = x * (1.0f - 0.5f * mix) + 0.5f * mix * y;
            }
        }
    }
private:
    float fs = 48000, ph = 0, G[2] { 0.1f, 0.1f }, st[2][6] {}, last[2] {};
};

//==============================================================================
class Compressor
{
public:
    void prepare (float fs_) { fs = fs_; envDb = -100.0f; grDb = 0.0f; }
    void process (float* L, float* R, int n, float thr, float ratio, float atkMs, float relMs, float makeupDb) noexcept
    {
        const float atk = std::exp (-1.0f / (std::max (0.05f, atkMs) * 0.001f * fs));
        const float rel = std::exp (-1.0f / (std::max (5.0f, relMs) * 0.001f * fs));
        const float knee = 6.0f, slope = 1.0f - 1.0f / std::max (1.0f, ratio);
        const float mk = std::pow (10.0f, makeupDb / 20.0f);
        for (int i = 0; i < n; ++i)
        {
            const float lvl = 20.0f * std::log10 (std::max (std::max (std::fabs (L[i]), std::fabs (R[i])), 1.0e-6f));
            const float over = lvl - thr;
            float target = 0.0f;
            if (over >= knee * 0.5f) target = -slope * over;
            else if (over > -knee * 0.5f) target = -slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
            grDb = target < grDb ? atk * grDb + (1.0f - atk) * target : rel * grDb + (1.0f - rel) * target;
            const float g = std::pow (10.0f, grDb / 20.0f) * mk;
            L[i] *= g; R[i] *= g;
        }
    }
    float gainReductionDb() const { return grDb; }
private:
    float fs = 48000, envDb = -100.0f, grDb = 0.0f;
};

//==============================================================================
class StereoDelay
{
public:
    void prepare (float fs_) { fs = fs_; for (auto& d : dl) d.prepare ((int) (3.1f * fs)); timeSm = -1; for (auto& f : lp) f.reset(); for (auto& f : hp) f.reset(); }
    void clear() { for (auto& d : dl) d.clear(); }
    void process (float* L, float* R, int n, double beats, double bpm, float fb, float tone, float mix, bool ping) noexcept
    {
        const float target = (float) std::clamp (beats * 60.0 / bpm, 0.01, 3.0) * fs;
        if (timeSm < 0) timeSm = target;
        const float tsm = 1.0f - std::exp (-1.0f / (0.08f * fs));
        const float lpHz = 1200.0f * std::pow (15.0f, tone);
        for (int c = 0; c < 2; ++c) { lp[c].set (lpHz, fs); hp[c].set (140.0f, fs); }
        const float dryG = mixDry (mix), wetG = mixWet (mix);
        for (int i = 0; i < n; ++i)
        {
            timeSm += (target - timeSm) * tsm;
            float yl = dl[0].readCubic (timeSm), yr = dl[1].readCubic (timeSm);
            float lo, hi, t;
            lp[0].process (yl, lo, hi); hp[0].process (lo, t, yl);
            lp[1].process (yr, lo, hi); hp[1].process (lo, t, yr);
            if (ping)
            {
                dl[0].push (fastTanh (0.5f * (L[i] + R[i]) + fb * yr));
                dl[1].push (fastTanh (fb * yl));
            }
            else
            {
                dl[0].push (fastTanh (L[i] + fb * yl));
                dl[1].push (fastTanh (R[i] + fb * yr));
            }
            L[i] = dryG * L[i] + wetG * yl;
            R[i] = dryG * R[i] + wetG * yr;
        }
    }
private:
    float fs = 48000, timeSm = -1;
    DelayLine dl[2];
    SVFp lp[2], hp[2];
};

//==============================================================================
// 2x -> 1x decimator for HQ mode: 63-tap Kaiser-windowed half-band FIR.
class Decimator2
{
public:
    static constexpr int kTaps = 63;
    Decimator2()
    {
        auto bessel0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        const double beta = 8.0, M = kTaps - 1;
        double sum = 0.0;
        for (int i = 0; i < kTaps; ++i)
        {
            const double n = i - M / 2.0;
            const double sinc = std::abs (n) < 1e-9 ? 0.5 : std::sin (3.141592653589793 * 0.5 * n) / (3.141592653589793 * n);
            const double r = 2.0 * i / M - 1.0;
            h[i] = (float) (sinc * bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta));
            sum += h[i];
        }
        for (auto& x : h) x = (float) (x / sum);
        reset();
    }
    void reset() { for (auto& c : hist) std::fill (std::begin (c), std::end (c), 0.0f); pos = 0; }
    // in: 2n samples per channel, out: n samples
    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            for (int k = 0; k < 2; ++k)
            {
                hist[0][pos] = hist[0][pos + kTaps] = inL[2 * i + k];
                hist[1][pos] = hist[1][pos + kTaps] = inR[2 * i + k];
                pos = (pos + 1) % kTaps;
            }
            float al = 0.0f, ar = 0.0f;
            const float* hl = hist[0] + pos; const float* hr = hist[1] + pos;
            for (int t = 0; t < kTaps; ++t) { al += h[t] * hl[t]; ar += h[t] * hr[t]; }
            outL[i] = al; outR[i] = ar;
        }
    }
    static constexpr int latency() { return (kTaps - 1) / 4; }   // in output samples (approx.)
private:
    float h[kTaps] {};
    float hist[2][2 * kTaps] {};
    int pos = 0;
};

//==============================================================================
class FxChain
{
public:
    void prepare (double sr, int /*maxBlock*/)
    {
        fs = (float) sr;
        hyp.prepare (fs); dist.prepare (fs); fla.prepare (fs); pha.prepare (fs); cho.prepare (fs); dly.prepare (fs);
        cmp.prepare (fs); rev.prepare (sr); ott.prepare (fs);
        for (auto& b : eq) b.reset();
        lastEq[0] = lastEq[1] = lastEq[2] = lastEq[3] = 1.0e9f;
        pumpG = 1.0f; volSm = -1.0f; freePh = 0.0;
    }

    void process (float* L, float* R, int n, const FxP& p, double bpm, bool playing, double ppq) noexcept
    {
        for (int slot = 0; slot < kNumFx; ++slot)
        {
            switch (p.order[slot])
            {
                case fxHyper:   if (p.hypOn)  hyp.process (L, R, n, p.hypDetune, p.hypDim, p.hypMix); break;
                case fxDist:    if (p.distOn) dist.process (L, R, n, p.distMode, p.distDrive, p.distMix); break;
                case fxFlanger: if (p.flaOn)  fla.process (L, R, n, p.flaRate, p.flaDepth, p.flaFb, p.flaMix); break;
                case fxPhaser:  if (p.phaOn)  pha.process (L, R, n, p.phaRate, p.phaDepth, p.phaFb, p.phaMix); break;
                case fxChorus:  if (p.choOn)  cho.process (L, R, n, p.choRate, p.choDepth, p.choMix); break;
                case fxDelay:   if (p.dlyOn)  dly.process (L, R, n, p.dlyBeats, bpm, p.dlyFb, p.dlyTone, p.dlyMix, p.dlyPing); break;
                case fxComp:    if (p.cmpOn)  cmp.process (L, R, n, p.cmpThresh, p.cmpRatio, p.cmpAttack, p.cmpRelease, p.cmpGain); break;
                case fxReverb:  if (p.revOn)  reverb (L, R, n, p); break;
                case fxEq:      if (p.eqOn)   equalise (L, R, n, p); break;
                case fxOtt:     if (p.ottOn)  ott.process (L, R, n, p.ottDepth, p.ottTime); break;
                case fxPump:    if (p.pumpOn) pump (L, R, n, p, bpm, playing, ppq); else lastPump = 1.0f; break;
                default: break;
            }
        }
        const float volT = std::pow (10.0f, p.volDb / 20.0f);
        if (volSm < 0.0f) volSm = volT;
        const float vsm = 1.0f - std::exp (-1.0f / (0.02f * fs));
        for (int i = 0; i < n; ++i)
        {
            volSm += (volT - volSm) * vsm;
            float l = L[i] * volSm, r = R[i] * volSm;
            if (p.clip) { l = softClip (l); r = softClip (r); }
            L[i] = l; R[i] = r;
        }
    }

    static inline float softClip (float x) noexcept
    {
        const float a = std::fabs (x);
        if (a <= 0.75f) return x;
        const float y = 0.75f + 0.25f * fastTanh ((a - 0.75f) / 0.25f);
        return x < 0.0f ? -y : y;
    }

    float pumpGain() const noexcept { return lastPump; }
    float compGrDb() const noexcept { return cmp.gainReductionDb(); }
    float ottBand (int k) const noexcept { return ott.bandGain (k); }
    void reset() { dly.clear(); rev.reset(); }

private:
    void reverb (float* L, float* R, int n, const FxP& p) noexcept
    {
        ReverbEngine::Params rp;
        rp.size = p.revSize; rp.decay = p.revDecay; rp.dampHz = 18000.0f * std::pow (1500.0f / 18000.0f, p.revDamp);
        rp.lowCutHz = 180.0f; rp.highCutHz = 15000.0f; rp.early = 0.35f; rp.diffusion = 0.8f; rp.width = 1.25f;
        rp.modDepth = 0.4f; rp.modRate = 0.5f; rp.preDelayMs = 12.0f;
        rev.setParams (rp); rev.beginBlock (n);
        const float dryG = mixDry (p.revMix), wetG = mixWet (p.revMix);
        for (int i = 0; i < n; ++i)
        {
            float wl, wr; rev.processSample (L[i], R[i], wl, wr);
            L[i] = dryG * L[i] + wetG * wl; R[i] = dryG * R[i] + wetG * wr;
        }
    }
    void equalise (float* L, float* R, int n, const FxP& p) noexcept
    {
        if (std::fabs (p.eqLow - lastEq[0]) > 1e-4f || std::fabs (p.eqMid - lastEq[1]) > 1e-4f || std::fabs (p.eqMidFreq - lastEq[2]) > 1e-2f || std::fabs (p.eqHigh - lastEq[3]) > 1e-4f)
        {
            eq[0].set (0, 110.0, p.eqLow, 0.7, fs); eq[1].set (1, p.eqMidFreq, p.eqMid, 0.8, fs); eq[2].set (2, 7500.0, p.eqHigh, 0.7, fs);
            lastEq[0] = p.eqLow; lastEq[1] = p.eqMid; lastEq[2] = p.eqMidFreq; lastEq[3] = p.eqHigh;
        }
        for (int i = 0; i < n; ++i)
            for (int b = 0; b < 3; ++b) { L[i] = eq[b].process (L[i], 0); R[i] = eq[b].process (R[i], 1); }
    }
    void pump (float* L, float* R, int n, const FxP& p, double bpm, bool playing, double ppq) noexcept
    {
        const float gsm = 1.0f - std::exp (-1.0f / (0.0015f * fs));
        double ph = playing ? ppq / p.pumpBeats : freePh;
        const double phInc = bpm / 60.0 / p.pumpBeats / fs;
        const float relFrac = 0.15f + 0.8f * p.pumpCurve;
        for (int i = 0; i < n; ++i)
        {
            const double f = ph - std::floor (ph);
            const float x = std::min (1.0f, (float) f / relFrac);
            const float u = 1.0f - (1.0f - x) * (1.0f - x);
            const float g = 1.0f - p.pumpDepth * (1.0f - u);
            ph += phInc;
            pumpG += (g - pumpG) * gsm;
            L[i] *= pumpG; R[i] *= pumpG;
        }
        freePh = ph - std::floor (ph);
        lastPump = pumpG;
    }

    float fs = 48000;
    Hyper hyp; Distortion dist; Flanger fla; Phaser pha; Chorus cho; StereoDelay dly; Compressor cmp; ReverbEngine rev; Ott ott;
    Biquad eq[3];
    float lastEq[4] {};
    float pumpG = 1.0f, volSm = -1.0f, lastPump = 1.0f;
    double freePh = 0.0;
};

} // namespace lumen::synth
