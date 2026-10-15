#pragma once
#include "SynthEngine.h"
#include "ReverbEngine.h"
#include "DelayLine.h"
#include "PumpEngine.h"     // SVFp, LR4
#include <cmath>
#include <algorithm>

namespace lumen::synth
{

struct FxP
{
    bool distOn = false; int distMode = 0; float distDrive = 0.3f, distMix = 1;
    bool ottOn = false;  float ottDepth = 0.5f, ottTime = 0.5f;
    bool choOn = false;  float choRate = 0.6f, choDepth = 0.5f, choMix = 0.4f;
    bool dlyOn = false;  double dlyBeats = 0.75; float dlyFb = 0.35f, dlyTone = 0.6f, dlyMix = 0.2f; bool dlyPing = true;
    bool revOn = false;  float revSize = 0.6f, revDecay = 2.5f, revDamp = 0.5f, revMix = 0.2f;
    bool pumpOn = false; double pumpBeats = 1; float pumpDepth = 0.7f, pumpCurve = 0.5f;
    float volDb = -3; bool clip = true;
};

inline float mixDry (float m) { return std::min (1.0f, 2.0f * (1.0f - m)); }
inline float mixWet (float m) { return std::min (1.0f, 2.0f * m); }

//==============================================================================
class Distortion
{
public:
    void prepare (float fs_) { fs = fs_; for (auto& c : up) for (auto& f : c) f.reset(); for (auto& c : dn) for (auto& f : c) f.reset();
                               for (int c = 0; c < 2; ++c) { for (auto& f : up[c]) f.set (0.21f * 2.0f * fs, 2.0f * fs); for (auto& f : dn[c]) f.set (0.21f * 2.0f * fs, 2.0f * fs); }
                               holdN = 0; held[0] = held[1] = 0; dcx[0] = dcx[1] = dcy[0] = dcy[1] = 0; }
    void process (float* L, float* R, int n, int mode, float drive, float mix) noexcept
    {
        const float g = std::pow (10.0f, drive * 36.0f / 20.0f);
        const float comp = mode == 3 ? 1.0f : 1.0f / std::pow (g, 0.42f);
        const float dryG = mixDry (mix), wetG = mixWet (mix);
        float* ch[2] { L, R };
        if (mode == 3)
        {
            const float bits = 16.0f - 13.0f * drive;
            const float q = std::pow (2.0f, bits - 1.0f);
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
                for (int k = 0; k < 2; ++k)        // 2x oversampling (zero-stuff, LP, shape, LP, decimate)
                {
                    float u = k == 0 ? 2.0f * x : 0.0f, lo, hi;
                    up[c][0].process (u, lo, hi); up[c][1].process (lo, u, hi);
                    float s = shape (u * g, mode);
                    dn[c][0].process (s, lo, hi); dn[c][1].process (lo, s, hi);
                    if (k == 0) y = s;
                }
                // DC block (asymmetric warm mode)
                const float d = y - dcx[c] + 0.9995f * dcy[c]; dcx[c] = y; dcy[c] = d;
                ch[c][i] = dryG * x + wetG * d * comp;
            }
    }
private:
    static inline float shape (float x, int mode) noexcept
    {
        switch (mode)
        {
            case 0:  return fastTanh (x + 0.15f) - 0.1489f;                     // warm, asymmetric (even harmonics)
            case 1:  return std::clamp (x, -1.0f, 1.0f);
            case 2:  return std::sin (1.5707963f * x);                          // wavefolder
            default: return x;
        }
    }
    float fs = 48000;
    SVFp up[2][2], dn[2][2];
    float held[2] {}, dcx[2] {}, dcy[2] {};
    int holdN = 0;
};

//==============================================================================
// OTT-style 3-band upward + downward compressor
class Ott
{
public:
    void prepare (float fs_) { fs = fs_; x1.set (120.0f, fs); x2.set (2500.0f, fs); ap.set (2500.0f, fs); for (auto& c : xs) { c[0].set (120, fs); c[1].set (2500, fs); c[2].set (2500, fs); c[0].reset(); c[1].reset(); c[2].reset(); } for (auto& e : env) e = 0; for (auto& g : gSm) g = 1; }
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
                xs[c][2].process (lo, a1, a2); lo = a1 + a2;          // allpass at f2 so the low band stays phase-coherent
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
                const float g = std::pow (10.0f, gdb / 20.0f);
                gSm[k] += (g - gSm[k]) * gsm;
                outL += b[0][k] * gSm[k]; outR += b[1][k] * gSm[k];
            }
            L[i] = outL; R[i] = outR;
        }
    }
    float bandGainDb (int k) const { return 20.0f * std::log10 (std::max (gSm[k], 1.0e-6f)); }
private:
    float fs = 48000;
    LR4 x1, x2, ap;
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
                const float in = 0.5f * (L[i] + R[i]);
                dl[0].push (fastTanh (in + fb * yr));
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
class FxChain
{
public:
    void prepare (double sr, int maxBlock)
    {
        fs = (float) sr;
        dist.prepare (fs); ott.prepare (fs); cho.prepare (fs); dly.prepare (fs); rev.prepare (sr);
        dryL.assign ((size_t) maxBlock, 0.0f); dryR.assign ((size_t) maxBlock, 0.0f);
        wetL.assign ((size_t) maxBlock, 0.0f); wetR.assign ((size_t) maxBlock, 0.0f);
        pumpG = 1.0f; volSm = -1.0f; freePh = 0.0;
    }

    void process (float* L, float* R, int n, const FxP& p, double bpm, bool playing, double ppq) noexcept
    {
        if (p.distOn) dist.process (L, R, n, p.distMode, p.distDrive, p.distMix);
        if (p.ottOn)  ott.process (L, R, n, p.ottDepth, p.ottTime);
        if (p.choOn)  cho.process (L, R, n, p.choRate, p.choDepth, p.choMix);
        if (p.dlyOn)  dly.process (L, R, n, p.dlyBeats, bpm, p.dlyFb, p.dlyTone, p.dlyMix, p.dlyPing);
        if (p.revOn)
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
        // Tempo-locked sidechain pump
        const float gsm = 1.0f - std::exp (-1.0f / (0.0015f * fs));
        double ph = playing ? ppq / p.pumpBeats : freePh;
        const double phInc = bpm / 60.0 / p.pumpBeats / fs;
        const float relFrac = 0.15f + 0.8f * p.pumpCurve;
        const float volT = std::pow (10.0f, p.volDb / 20.0f);
        if (volSm < 0.0f) volSm = volT;
        const float vsm = 1.0f - std::exp (-1.0f / (0.02f * fs));
        for (int i = 0; i < n; ++i)
        {
            float g = 1.0f;
            if (p.pumpOn)
            {
                double f = ph - std::floor (ph);
                const float x = std::min (1.0f, (float) f / relFrac);
                const float u = 1.0f - (1.0f - x) * (1.0f - x);
                g = 1.0f - p.pumpDepth * (1.0f - u);
            }
            ph += phInc;
            pumpG += (g - pumpG) * gsm;
            volSm += (volT - volSm) * vsm;
            float l = L[i] * pumpG * volSm, r = R[i] * pumpG * volSm;
            if (p.clip) { l = softClip (l); r = softClip (r); }
            L[i] = l; R[i] = r;
        }
        freePh = ph - std::floor (ph);
        lastPump = pumpG;
    }

    static inline float softClip (float x) noexcept
    {
        const float a = std::fabs (x);
        if (a <= 0.75f) return x;
        const float y = 0.75f + 0.25f * fastTanh ((a - 0.75f) / 0.25f);
        return x < 0.0f ? -y : y;
    }

    float pumpGain() const noexcept { return lastPump; }
    void reset() { dly.clear(); rev.reset(); }

private:
    float fs = 48000;
    Distortion dist; Ott ott; Chorus cho; StereoDelay dly; ReverbEngine rev;
    std::vector<float> dryL, dryR, wetL, wetR;
    float pumpG = 1.0f, volSm = -1.0f, lastPump = 1.0f;
    double freePh = 0.0;
};

} // namespace lumen::synth
