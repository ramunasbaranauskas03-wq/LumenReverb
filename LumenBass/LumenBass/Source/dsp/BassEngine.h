#pragma once
#include "PumpEngine.h"      // SVFp, LR4
#include <cmath>
#include <algorithm>
#include <vector>

namespace lumen
{

// RBJ low shelf
struct LowShelf
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set (double f0, double gainDb, double fs)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w0 = 2.0 * 3.14159265358979 * f0 / fs;
        const double cw = std::cos (w0), sw = std::sin (w0), S = 1.0;
        const double alpha = sw / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
        const double sa = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1) + (A - 1) * cw + sa;
        b0 = A * ((A + 1) - (A - 1) * cw + sa) / a0;
        b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0;
        b2 = A * ((A + 1) - (A - 1) * cw - sa) / a0;
        a1 = -2 * ((A - 1) + (A + 1) * cw) / a0;
        a2 = ((A + 1) + (A - 1) * cw - sa) / a0;
    }
    inline float process (float x) noexcept
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return (float) y;
    }
    void reset() { x1 = x2 = y1 = y2 = 0; }
};

//==============================================================================
// Low-end processor:
//   * Mono below `monoHz`: side is LR4 high-passed, mid gets the matching LR4 allpass,
//     so everything above the split keeps its exact stereo/phase relationship.
//   * Width for the side above the split.
//   * Harmonics: the low band is normalised by its envelope and fed through Chebyshev
//     polynomials -> exact 2nd/3rd/4th harmonics that track the bass level (phone translation).
//   * Sub low-shelf, output gain, harmonics listen.
class BassEngine
{
public:
    struct Params
    {
        bool  monoOn = true;
        float monoHz = 120.0f, width = 1.0f;
        float harmAmt = 0.0f, harmFocus = 100.0f, harmChar = 0.4f, harmTone = 1500.0f;
        float subDb = 0.0f, outDb = 0.0f;
        bool  listen = false;
    };

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        envAtk = std::exp (-1.0f / (0.001f * fs));
        envRel = std::exp (-1.0f / (0.06f * fs));
        smooth = 1.0f - std::exp (-1.0f / (0.03f * fs));
        meterCoef = 1.0f - std::exp (-1.0f / (0.4f * fs));
        for (auto* f : { &mLpIn, &mLpOut }) f->set (150.0f, fs);
        for (auto& f : lpIn)  f.set (150.0f, fs);
        for (auto& f : lpOut) f.set (150.0f, fs);
        subIn.set (100.0f, fs); subOut.set (100.0f, fs);
        audHpIn.set (150.0f, fs); audHpOut.set (150.0f, fs);
        audLpIn.set (1500.0f, fs); audLpOut.set (1500.0f, fs);
        env = 0.0f; monoSm = 1.0f; widthSm = 1.0f; harmSm = 0.0f; outSm = 1.0f;
        eInL = eInR = eInLR = eOutL = eOutR = eOutLR = 0.0f;
        eSubIn = eAudIn = eSubOut = eAudOut = 0.0f;
        first = true;
    }

    void beginBlock (const Params& p) noexcept
    {
        params = p;
        const float mono = std::clamp (p.monoHz, 20.0f, 0.4f * fs);
        sideHp.set (mono, fs); midAp.set (mono, fs);
        srcLp.set (std::clamp (p.harmFocus, 30.0f, 0.4f * fs), fs);
        hHp.set (std::clamp (p.harmFocus * 1.4f, 40.0f, 0.4f * fs), fs);
        hLp.set (std::clamp (p.harmTone, 300.0f, 0.45f * fs), fs);
        shelf.set (60.0, p.subDb, fs);
        outTarget = std::pow (10.0f, p.outDb / 20.0f);
        if (first) { monoSm = p.monoOn ? 1.0f : 0.0f; widthSm = p.width; harmSm = p.harmAmt; outSm = outTarget; first = false; }
    }

    inline void process (float& l, float& r) noexcept
    {
        monoSm  += ((params.monoOn ? 1.0f : 0.0f) - monoSm) * smooth;
        widthSm += (params.width - widthSm) * smooth;
        harmSm  += (params.harmAmt - harmSm) * smooth;
        outSm   += (outTarget - outSm) * smooth;

        // ---- input meters ----
        meterIn (l, r);

        float m = 0.5f * (l + r), s = 0.5f * (l - r);

        // ---- harmonics from the low band of the mid ----
        float srcL, t, lo;
        srcLp.lp1.process (m, lo, t); srcLp.lp2.process (lo, srcL, t);
        const float a = std::fabs (srcL);
        env = a > env ? envAtk * env + (1.0f - envAtk) * a : envRel * env + (1.0f - envRel) * a;
        float h = 0.0f;
        if (env > 1.0e-5f)
        {
            const float x  = std::clamp (srcL / env, -1.0f, 1.0f);
            const float x2 = x * x;
            const float T2 = 2.0f * x2 - 1.0f;
            const float T3 = x * (4.0f * x2 - 3.0f);
            const float T4 = 8.0f * x2 * x2 - 8.0f * x2 + 1.0f;
            const float c  = params.harmChar;                     // 0 = even/warm, 1 = odd/edgy
            h = env * ((1.0f - c) * (0.75f * T2 + 0.3f * T4) + c * (0.8f * T3 + 0.25f * T2));
        }
        // keep only the generated harmonics (remove DC + fundamental region), shape the top
        float hh;
        hHp.hp1.process (h, t, hh);  hHp.hp2.process (hh, t, h);
        hLp.lp1.process (h, hh, t);  hLp.lp2.process (hh, h, t);
        h *= harmSm * 4.0f;

        // ---- mono below split, width above ----
        float sHigh, mAp, a1, a2;
        sideHp.hp1.process (s, t, hh); sideHp.hp2.process (hh, t, sHigh);
        midAp.process (m, a1, a2); mAp = a1 + a2;
        m = m + (mAp - m) * monoSm;
        s = s + (sHigh - s) * monoSm;
        s *= widthSm;

        m += h;
        m = shelf.process (m);

        if (params.listen) { l = r = h * outSm; meterOut (l, r); return; }

        l = (m + s) * outSm;
        r = (m - s) * outSm;
        meterOut (l, r);
    }

    // ---- meters (read on the message thread through the processor's atomics) ----
    float lowCorrIn() const  { return eInLR  / std::sqrt (std::max (eInL * eInR, 1.0e-18f)); }
    float lowCorrOut() const { return eOutLR / std::sqrt (std::max (eOutL * eOutR, 1.0e-18f)); }
    float phoneIn() const    { return eAudIn  / std::max (eAudIn + eSubIn, 1.0e-12f); }
    float phoneOut() const   { return eAudOut / std::max (eAudOut + eSubOut, 1.0e-12f); }
    bool  hasBass() const    { return eSubIn + eAudIn > 1.0e-7f; }

private:
    inline void meterIn (float l, float r) noexcept
    {
        float lo, hi, ll, rr;
        lpIn[0].process (l, ll, hi); lpIn[1].process (r, rr, hi);
        eInL += (ll * ll - eInL) * meterCoef; eInR += (rr * rr - eInR) * meterCoef; eInLR += (ll * rr - eInLR) * meterCoef;
        const float m = 0.5f * (l + r);
        subIn.process (m, lo, hi);    eSubIn += (lo * lo - eSubIn) * meterCoef;
        audHpIn.process (m, lo, hi);  audLpIn.process (hi, lo, hi); eAudIn += (lo * lo - eAudIn) * meterCoef;
    }
    inline void meterOut (float l, float r) noexcept
    {
        float lo, hi, ll, rr;
        lpOut[0].process (l, ll, hi); lpOut[1].process (r, rr, hi);
        eOutL += (ll * ll - eOutL) * meterCoef; eOutR += (rr * rr - eOutR) * meterCoef; eOutLR += (ll * rr - eOutLR) * meterCoef;
        const float m = 0.5f * (l + r);
        subOut.process (m, lo, hi);    eSubOut += (lo * lo - eSubOut) * meterCoef;
        audHpOut.process (m, lo, hi);  audLpOut.process (hi, lo, hi); eAudOut += (lo * lo - eAudOut) * meterCoef;
    }

    Params params;
    float fs = 48000.0f;
    bool first = true;
    LR4 sideHp, midAp, srcLp, hHp, hLp;
    LowShelf shelf;
    float env = 0, envAtk = 0, envRel = 0, smooth = 0.01f, meterCoef = 0.0001f;
    float monoSm = 1, widthSm = 1, harmSm = 0, outSm = 1, outTarget = 1;

    SVFp mLpIn, mLpOut, lpIn[2], lpOut[2], subIn, subOut, audHpIn, audHpOut, audLpIn, audLpOut;
    float eInL = 0, eInR = 0, eInLR = 0, eOutL = 0, eOutR = 0, eOutLR = 0;
    float eSubIn = 0, eAudIn = 0, eSubOut = 0, eAudOut = 0;
};

//==============================================================================
// Bass analysis for Smart Assist: fundamental and how much energy sits below 100 Hz.
struct BassInfo { bool valid = false; float f0 = 0.0f, subRatio = 0.0f; };

inline BassInfo analyseBass (const std::vector<float>& x, float fs)
{
    BassInfo info;
    const int n = (int) x.size();
    if (n < (int) fs) return info;

    SVFp lp1, lp2, sub; lp1.set (250.0f, fs); lp2.set (250.0f, fs); sub.set (100.0f, fs);
    double eAll = 0.0, eSub = 0.0, peak = 0.0;
    std::vector<float> lpx ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        float lo, hi, y;
        lp1.process (x[(size_t) i], lo, hi); lp2.process (lo, y, hi);
        lpx[(size_t) i] = y;
        float s; sub.process (x[(size_t) i], s, hi);
        eAll += (double) x[(size_t) i] * x[(size_t) i];
        eSub += (double) s * s;
        peak = std::max (peak, (double) std::fabs (y));
    }
    if (eAll < 1.0e-6 || peak < 1.0e-3) return info;

    // zero-crossing rate over the voiced parts (|lp| above 15% of peak)
    // Period = time between consecutive upward crossings (Schmitt trigger with hysteresis),
    // only counted inside a continuous note (gaps longer than 60 ms start a new note).
    float prev = -1.0f, e = 0.0f;
    const float rel = std::exp (-1.0f / (0.03f * fs));
    int lastCross = -1;
    double cycles = 0.0, span = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float v = lpx[(size_t) i];
        const float a = std::fabs (v);
        e = a > e ? a : rel * e + (1.0f - rel) * a;
        if (e < 0.15 * peak) { lastCross = -1; prev = -1.0f; continue; }
        const float hyst = 0.2f * e;
        if (v > hyst && prev < 0.0f)
        {
            prev = 1.0f;
            if (lastCross >= 0 && i - lastCross < (int) (0.06f * fs)) { cycles += 1.0; span += (double) (i - lastCross); }
            lastCross = i;
        }
        else if (v < -hyst) prev = -1.0f;
    }
    info.valid = true;
    info.f0 = cycles >= 4.0 ? (float) (cycles * fs / span) : 0.0f;
    info.subRatio = (float) (eSub / eAll);
    return info;
}

} // namespace lumen
