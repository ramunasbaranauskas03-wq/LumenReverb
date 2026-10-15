#pragma once
#include "FilterCore.h"
#include <cmath>
#include <algorithm>

// =====================================================================================
// Lumen Filter engine: stereo multimode filter + tempo LFO (stereo phase) + envelope follower
// + key-quantised cutoff + RISE build-up stage (HP riser / LP down / band squeeze).
// =====================================================================================
namespace lumen::flt
{

struct Params
{
    int type = fLadder; float cutoff = 20000, res = 0.1f, drive = 0, mix = 1, outDb = 0, spread = 0;
    int keyMode = 0, root = 0;                         // keyMode 0 off, 1 chromatic, 2 major, 3 minor
    bool lfoOn = false; int lfoShape = 0; bool lfoSync = true; double lfoBeats = 1; float lfoRate = 1, lfoDepth = 0, lfoStereo = 0;
    bool envOn = false; float envAmt = 0, envAttMs = 5, envRelMs = 150;
    float rise = 0; int riseMode = 0;                  // 0 HP riser, 1 LP down, 2 band squeeze
};

inline float quantiseToKey (float hz, int mode, int root)
{
    if (mode <= 0) return hz;
    static const bool major[12] = { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 };
    static const bool minor[12] = { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0 };
    const float midi = 69.0f + 12.0f * std::log2 (std::max (hz, 1.0f) / 440.0f);
    int best = (int) std::lround (midi);
    if (mode >= 2)
    {
        const bool* sc = mode == 2 ? major : minor;
        float bestD = 1e9f;
        for (int n = (int) std::floor (midi) - 2; n <= (int) std::ceil (midi) + 2; ++n)
        {
            const int pc = ((n - root) % 12 + 12) % 12;
            if (sc[pc] && std::abs ((float) n - midi) < bestD) { bestD = std::abs ((float) n - midi); best = n; }
        }
    }
    return 440.0f * std::exp2 (((float) best - 69.0f) / 12.0f);
}

class FilterEngine
{
public:
    static constexpr int kCtl = 8;

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        for (auto* f : { &fL, &fR, &riseHp, &riseLp }) { f->reset(); f->resetComb(); }
        lfoPh = 0; env = 0; ctl = 0; first = true;
        shA = 0.3f; shB = -0.4f;
        outSm = 1.0f; riseSm = 0.0f;
    }

    void setParams (const Params& np, double bpm, bool playing, double ppq) noexcept
    {
        p = np;
        bpmV = bpm > 1.0 ? bpm : 120.0;
        if (p.lfoSync && playing)
        {
            const double ph = ppq / p.lfoBeats;
            lfoPh = (float) (ph - std::floor (ph));
        }
        envA = std::exp (-1.0f / (std::max (0.1f, p.envAttMs) * 0.001f * fs));
        envR = std::exp (-1.0f / (std::max (5.0f, p.envRelMs) * 0.001f * fs));
        outTarget = std::pow (10.0f, p.outDb / 20.0f);
        if (first) { riseSm = p.rise; outSm = outTarget; }
    }

    // envSrc: signal driving the envelope follower (input or sidechain), same rate as L/R
    void process (float* L, float* R, const float* envSrcL, const float* envSrcR, int n) noexcept
    {
        const float riseCoef = 1.0f - std::exp (-1.0f / (0.02f * fs));
        const float outCoef = 1.0f - std::exp (-1.0f / (0.02f * fs));
        const float openCoef = 1.0f - std::exp (-1.0f / (0.01f * fs));
        const float lfoHz = p.lfoSync ? (float) (bpmV / 60.0 / p.lfoBeats) : p.lfoRate;
        for (int i = 0; i < n; ++i)
        {
            const float a = std::max (std::abs (envSrcL[i]), std::abs (envSrcR[i]));
            env = a > env ? envA * env + (1.0f - envA) * a : envR * env + (1.0f - envR) * a;
            riseSm += (p.rise - riseSm) * riseCoef;

            if (ctl == 0) control();
            ctl = (ctl + 1) % kCtl;
            // RISE also accelerates the LFO (classic build-up wobble speed-up)
            lfoPh += lfoHz * (1.0f + 3.0f * riseSm * riseSm) / fs;
            if (lfoPh >= 1.0f) { lfoPh -= 1.0f; shA = shB; shB = rnd() * 2.0f - 1.0f; }

            const float dl = L[i], dr = R[i];
            float l = fL.process (dl, 0), r = fR.process (dr, 1);
            openSmL += (openL - openSmL) * openCoef; openSmR += (openR - openSmR) * openCoef;
            l = dl + (l - dl) * p.mix * (1.0f - openSmL); r = dr + (r - dr) * p.mix * (1.0f - openSmR);
            const float rw = std::clamp (riseSm * 8.0f, 0.0f, 1.0f);
            if (rw > 0.0f)
            {
                float hl = l, hr = r;
                if (p.riseMode != 1) { hl = riseHp.process (hl, 0); hr = riseHp.process (hr, 1); }
                if (p.riseMode != 0) { hl = riseLp.process (hl, 0); hr = riseLp.process (hr, 1); }
                l += (hl - l) * rw; r += (hr - r) * rw;
            }
            outSm += (outTarget - outSm) * outCoef;
            L[i] = l * outSm; R[i] = r * outSm;
        }
    }

    // live values for the UI
    float cutoffL = 1000, cutoffR = 1000, lfoValue = 0, envValue = 0, riseHpHz = 20, riseLpHz = 20000;

private:
    float rnd() noexcept { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (float) (seed >> 8) * (1.0f / 16777216.0f); }

    void control() noexcept
    {
        float lfoL = 0.0f, lfoR = 0.0f;
        if (p.lfoOn)
        {
            lfoL = lfoShape (p.lfoShape, lfoPh, shA, shB);
            float ph2 = lfoPh + 0.5f * p.lfoStereo; ph2 -= std::floor (ph2);
            lfoR = lfoShape (p.lfoShape, ph2, shA, shB);
        }
        const float envN = std::clamp ((20.0f * std::log10 (std::max (env, 1.0e-6f)) + 40.0f) / 40.0f, 0.0f, 1.0f);
        lfoValue = lfoL; envValue = envN;
        const float base = std::log2 (std::clamp (p.cutoff, 20.0f, 20000.0f));
        const float envOct = p.envOn ? p.envAmt * 5.0f * envN : 0.0f;
        const float octL = base + p.lfoDepth * 4.0f * lfoL + envOct - 0.5f * p.spread;
        const float octR = base + p.lfoDepth * 4.0f * lfoR + envOct + 0.5f * p.spread;
        float hzL = std::exp2 (std::clamp (octL, 4.33f, 14.29f)), hzR = std::exp2 (std::clamp (octR, 4.33f, 14.29f));
        hzL = quantiseToKey (hzL, p.keyMode, p.root); hzR = quantiseToKey (hzR, p.keyMode, p.root);
        const float sm = first ? 1.0f : 0.35f;               // log-domain smoothing per control tick
        smL += (std::log2 (hzL) - smL) * sm; smR += (std::log2 (hzR) - smR) * sm;
        cutoffL = std::exp2 (smL); cutoffR = std::exp2 (smR);
        const float res = std::clamp (p.res + 0.3f * riseSm, 0.0f, 1.0f);
        // fully open = transparent: LP types fade out above 16 kHz, HP types below 25 Hz
        auto openness = [this] (float hz)
        {
            if (p.drive > 0.001f) return 0.0f;
            const bool lp = p.type == fLadder || p.type == fLP12 || p.type == fLP24;
            const bool hp = p.type == fHP12 || p.type == fHP24;
            if (lp) return std::clamp ((std::log2 (hz) - std::log2 (16000.0f)) / (std::log2 (19900.0f) - std::log2 (16000.0f)), 0.0f, 1.0f);
            if (hp) return std::clamp ((std::log2 (25.0f) - std::log2 (hz)) / (std::log2 (25.0f) - std::log2 (20.5f)), 0.0f, 1.0f);
            return 0.0f;
        };
        openL = openness (cutoffL); openR = openness (cutoffR);
        if (first) { openSmL = openL; openSmR = openR; }
        fL.set (p.type, cutoffL, res, p.drive, fs);
        fR.set (p.type, cutoffR, res, p.drive, fs);
        const float r = riseSm;
        riseHpHz = p.riseMode == 0 ? 20.0f * std::exp2 (r * 7.3f) : 20.0f * std::exp2 (r * 5.1f);
        riseLpHz = p.riseMode == 1 ? 20000.0f * std::exp2 (-r * 6.0f) : 20000.0f * std::exp2 (-r * 3.2f);
        riseHp.set (fHP24, riseHpHz, 0.15f + 0.45f * r, 0.0f, fs);
        riseLp.set (fLadder, riseLpHz, 0.1f + 0.45f * r, 0.0f, fs);
        first = false;
    }

    Params p;
    float fs = 48000;
    double bpmV = 120;
    VFilter fL, fR, riseHp, riseLp;
    bool first = true;
    float lfoPh = 0, shA = 0, shB = 0;
    float env = 0, envA = 0.9f, envR = 0.99f;
    float openL = 0, openR = 0, openSmL = 0, openSmR = 0;
    float smL = 10.0f, smR = 10.0f, outSm = 1, outTarget = 1, riseSm = 0;
    int ctl = 0;
    uint32_t seed = 0x2545F491u;
};

} // namespace lumen::flt
