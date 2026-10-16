#pragma once
#include <juce_dsp/juce_dsp.h>
#include "PumpEngine.h"     // LR4
#include <cmath>
#include <algorithm>
#include <vector>
#include <atomic>

// =====================================================================================
// Lumen MonoFilter engine — frequency-dependent stereo width (mono bass done right)
//   * width envelope: LOW node (freq, width) -> smooth curve -> HIGH node (freq, width), 0..200 %
//   * ZERO LATENCY: mid untouched, side = lowW*S + (highW-lowW)*HP(S) (order follows the node spacing)
//   * LINEAR PHASE: the exact envelope as a zero-phase FIR on the side (and the HPF on both), no phase shift at all
//   * PHASE ALIGN: L/R time + polarity alignment of the low band before it is folded to mono -> no bass loss
//     (AUTO tracks it continuously, in BOTH modes), HPF, M/S trim, auto trim, source L/R/stereo, listen modes
// =====================================================================================
namespace lumen::mono
{
using lumen::LR4;

struct Params
{
    float lowF = 100, lowW = 0, highF = 250, highW = 1;
    float hpf = 0;                       // 0 = off
    bool linear = false, hq = false;
    int source = 0;                      // 0 stereo, 1 left, 2 right
    bool align = false, autoAlign = true, flip = false; float alignMs = 0;
    float midDb = 0, sideDb = 0; bool autoTrim = false;
    int monitor = 0;                     // 0 full, 1 low band, 2 high band, 3 mono sum, 4 side
    float outDb = 0;
};

inline float smoothstep (float t) { t = std::clamp (t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
inline float widthAt (const Params& p, float f)
{
    const float lo = std::max (10.0f, std::min (p.lowF, p.highF)), hi = std::max (lo * 1.001f, std::max (p.lowF, p.highF));
    if (f <= lo) return p.lowW;
    if (f >= hi) return p.highW;
    return p.lowW + (p.highW - p.lowW) * smoothstep (std::log (f / lo) / std::log (hi / lo));
}
inline float hpfMag (const Params& p, float f) { if (p.hpf < 15.0f) return 1.0f; const float r = p.hpf / std::max (1.0f, f); return 1.0f / std::sqrt (1.0f + r * r * r * r * r * r * r * r); }

inline int firLength (double fs, bool hq) { int n = 1; while (n < fs * (hq ? 0.34 : 0.17)) n <<= 1; return n; }

struct FirSet { juce::AudioBuffer<float> m, s; };
inline void designFirs (const Params& p, double fs, int N, FirSet& out)
{
    juce::dsp::FFT fft ((int) std::round (std::log2 ((double) N)));
    std::vector<float> bm ((size_t) N * 2, 0.0f), bs ((size_t) N * 2, 0.0f);
    const float sg = std::pow (10.0f, p.sideDb / 20.0f), mg = std::pow (10.0f, p.midDb / 20.0f);
    for (int k = 0; k <= N / 2; ++k)
    {
        const float f = std::max (0.01f, (float) (k * fs / N));
        const float h = hpfMag (p, f);
        const float sgn = (k & 1) ? -1.0f : 1.0f;                  // linear phase: delay N/2
        bm[(size_t) (2 * k)] = h * mg * sgn;
        bs[(size_t) (2 * k)] = h * widthAt (p, f) * sg * sgn;
    }
    fft.performRealOnlyInverseTransform (bm.data());
    fft.performRealOnlyInverseTransform (bs.data());
    out.m.setSize (1, N); out.s.setSize (1, N);
    for (int i = 0; i < N; ++i)
    {
        const double t = (double) i / (N - 1);
        const double w = t < 0.08 ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * t / 0.08) : t > 0.92 ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (1.0 - t) / 0.08) : 1.0;
        out.m.setSample (0, i, (float) (bm[(size_t) i] * w));
        out.s.setSample (0, i, (float) (bs[(size_t) i] * w));
    }
}

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process (float x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
    void hp (double fs, double f, double q) { const double w = 2 * juce::MathConstants<double>::pi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al; b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void lp (double fs, double f, double q) { const double w = 2 * juce::MathConstants<double>::pi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al; b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void hp1 (double fs, double f) { const double k = std::tan (juce::MathConstants<double>::pi * f / fs), a0 = 1 + k; b0 = 1 / a0; b1 = -1 / a0; b2 = 0; a1 = (k - 1) / a0; a2 = 0; }
    void unity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
};

class FracDelay
{
public:
    void prepare (int maxD) { int n = 1; while (n < maxD + 8) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; pos = 0; }
    inline float process (float x, float d) noexcept
    {
        buf[(size_t) pos] = x;
        const float rp = (float) pos - d;
        const float fl = std::floor (rp);
        const int i = (int) fl; const float f = rp - fl;
        auto at = [&] (int k) { return buf[(size_t) ((i + k) & mask)]; };
        const float xm1 = at (-1), x0 = at (0), x1 = at (1), x2 = at (2);
        pos = (pos + 1) & mask;
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
private:
    std::vector<float> buf; int mask = 0, pos = 0;
};

class MonoEngine
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        blk = std::max (32, maxBlock);
        D = (int) std::round (0.003 * fs);
        for (auto& d : dlLo) d.prepare (2 * D + 8);
        for (auto& d : dlHi) d.prepare (D + 8);
        for (auto& s : split) s.reset();
        for (auto& b : sHp) b.reset();
        for (auto& b : hpM) b.reset();
        for (auto& b : hpS) b.reset();
        for (auto& b : decLp) b.reset();
        for (auto& b : monSplit) b.reset();
        tmpM.setSize (1, blk); tmpS.setSize (1, blk);
        juce::dsp::ProcessSpec spec { fs, (juce::uint32) blk, 1 };
        convM.reset(); convS.reset(); convM.prepare (spec); convS.prepare (spec);
        firN = firLength (fs, p.hq);
        dec = std::max (1, (int) std::round (fs / 4000.0)); decCnt = 0;
        std::fill (std::begin (ringL), std::end (ringL), 0.0f); std::fill (std::begin (ringR), std::end (ringR), 0.0f);
        std::fill (std::begin (xc), std::end (xc), 0.0); eL = eR = 1e-9;
        ringPos = 0; evalCnt = 0;
        tauSm = 0; tauEst = 0; pol = 1.0f; polVotes = 0;
        inPow = outPow = 1e-9; trimDb = 0;
        first = true;
        for (auto& v : lastF) v = -1;
    }

    int latency() const noexcept { return (p.linear ? firN.load() / 2 : 0) + (p.align ? D : 0); }
    int firSize (bool hq) const noexcept { return firLength (fs, hq); }
    void loadFirs (FirSet&& f, int N)
    {
        convM.loadImpulseResponse (std::move (f.m), fs, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        convS.loadImpulseResponse (std::move (f.s), fs, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        firN = N;
    }

    void setParams (const Params& np) noexcept
    {
        p = np;
        const float lo = std::min (p.lowF, p.highF), hi = std::max (p.lowF, p.highF);
        const float fc = std::sqrt (lo * hi), span = std::log2 (std::max (1.0001f, hi / lo));
        const int order = span <= 1.2f ? 4 : span <= 2.5f ? 2 : 1;
        if (std::abs (fc - lastF[0]) > 0.01f || order != lastOrder)
        {
            // side high-pass whose transition follows the node spacing (Butterworth)
            if (order == 4) { sHp[0].hp (fs, fc, 0.5412); sHp[1].hp (fs, fc, 1.3066); }
            else if (order == 2) { sHp[0].hp (fs, fc, 0.7071); sHp[1].unity(); }
            else { sHp[0].hp1 (fs, fc); sHp[1].unity(); }
            for (int c = 0; c < 2; ++c) monSplit[c].set ((float) fc, (float) fs);
            lastF[0] = fc; lastOrder = order;
        }
        if (std::abs (p.hpf - lastF[1]) > 0.01f)
        {
            if (p.hpf >= 15.0f) { hpM[0].hp (fs, p.hpf, 0.5412); hpM[1].hp (fs, p.hpf, 1.3066); hpS[0].hp (fs, p.hpf, 0.5412); hpS[1].hp (fs, p.hpf, 1.3066); }
            else for (auto* b : { &hpM[0], &hpM[1], &hpS[0], &hpS[1] }) b->unity();
            lastF[1] = p.hpf;
        }
        const float splitF = std::clamp (hi, 40.0f, 2000.0f);
        if (std::abs (splitF - lastF[2]) > 0.01f)
        {
            for (auto& s : split) s.set (splitF, (float) fs);
            const float dl = std::min (splitF, 1200.0f);
            for (auto& b : decLp) b.lp (fs, dl, 0.7071);
            lastF[2] = splitF;
        }
        mg = std::pow (10.0f, p.midDb / 20.0f); sg = std::pow (10.0f, p.sideDb / 20.0f);
        outT = std::pow (10.0f, p.outDb / 20.0f);
        if (first) { outSm = outT; lowW = p.lowW; highW = p.highW; first = false; }
    }

    void process (float* L, float* R, int n) noexcept
    {
        const float sm = 1.0f - (float) std::exp (-1.0 / (0.02 * fs));
        const float tauC = 1.0f - (float) std::exp (-1.0 / (0.4 * fs));
        const float agC = 1.0f - (float) std::exp (-1.0 / (3.0 * fs));
        for (int start = 0; start < n; start += blk)
        {
            const int len = std::min (blk, n - start);
            float* l = L + start; float* r = R + start;
            float* m = tmpM.getWritePointer (0); float* s = tmpS.getWritePointer (0);
            double ip = 0;
            for (int i = 0; i < len; ++i)
            {
                float xl = l[i], xr = r[i];
                ip += 0.5 * ((double) xl * xl + (double) xr * xr);
                if (p.source == 1) xr = xl; else if (p.source == 2) xl = xr;
                if (p.autoAlign && p.align) track (xl, xr);
                if (p.align)
                {
                    // split both sides at the high node (LR4: low + high = the same allpass on L and R, so the image and
                    // the mono sum stay intact), then shift the RIGHT low band by tau (+ polarity) against the left
                    const float tau = p.autoAlign ? tauSm : (float) (p.alignMs * 0.001 * fs);
                    tauSm += ((p.autoAlign ? tauEst : tau) - tauSm) * tauC;
                    const float polarity = p.autoAlign ? pol : (p.flip ? -1.0f : 1.0f);
                    float ll, lh, rl, rh;
                    split[0].process (xl, ll, lh); split[1].process (xr, rl, rh);
                    ll = dlLo[0].process (ll, (float) D);
                    rl = dlLo[1].process (rl, std::clamp ((float) D + tau, 2.0f, (float) (2 * D))) * polarity;
                    lh = dlHi[0].process (lh, (float) D); rh = dlHi[1].process (rh, (float) D);
                    xl = ll + lh; xr = rl + rh;
                }
                m[i] = 0.5f * (xl + xr); s[i] = 0.5f * (xl - xr);
            }
            if (p.linear)
            {
                juce::dsp::AudioBlock<float> bM (tmpM.getArrayOfWritePointers(), 1, (size_t) len), bS (tmpS.getArrayOfWritePointers(), 1, (size_t) len);
                convM.process (juce::dsp::ProcessContextReplacing<float> (bM));
                convS.process (juce::dsp::ProcessContextReplacing<float> (bS));
                lowW = p.lowW; highW = p.highW;
            }
            else
            {
                for (int i = 0; i < len; ++i)
                {
                    lowW += (p.lowW - lowW) * sm; highW += (p.highW - highW) * sm;
                    float mm = hpM[1].process (hpM[0].process (m[i]));
                    float ss = hpS[1].process (hpS[0].process (s[i]));
                    const float hpSide = sHp[1].process (sHp[0].process (ss));
                    m[i] = mm * mg;
                    s[i] = (lowW * ss + (highW - lowW) * hpSide) * sg;
                }
            }
            double op = 0;
            for (int i = 0; i < len; ++i)
            {
                float ol = m[i] + s[i], orr = m[i] - s[i];
                op += 0.5 * ((double) ol * ol + (double) orr * orr);
                switch (p.monitor)
                {
                    case 1: case 2:
                    {
                        float a, b, c, d; monSplit[0].process (ol, a, b); monSplit[1].process (orr, c, d);
                        ol = p.monitor == 1 ? a : b; orr = p.monitor == 1 ? c : d; break;
                    }
                    case 3: { const float mo = 0.5f * (ol + orr); ol = orr = mo; break; }
                    case 4: { const float sd = 0.5f * (ol - orr); ol = sd; orr = -sd; break; }
                    default: break;
                }
                outSm += (outT - outSm) * sm;
                const float g = outSm * std::pow (10.0f, trimDb / 20.0f);
                l[i] = ol * g; r[i] = orr * g;
            }
            if (p.autoTrim)
            {
                inPow += (ip / len - inPow) * std::min (1.0, (double) agC * len);
                outPow += (op / len - outPow) * std::min (1.0, (double) agC * len);
                trimDb = (float) std::clamp (10.0 * std::log10 ((inPow + 1e-12) / (outPow + 1e-12)), -12.0, 12.0);
            }
            else trimDb *= 0.98f;
        }
    }

    // UI / tests
    float alignMs() const noexcept { return (float) (tauSm / fs * 1000.0); }
    float polarity() const noexcept { return pol; }
    float trim() const noexcept { return trimDb; }
    float alignConfidence = 0;

private:
    // decimated cross-correlation of the low bands: finds the lag (and polarity) of maximum correlation
    void track (float xl, float xr) noexcept
    {
        const float a = decLp[1].process (decLp[0].process (xl)), b = decLp[3].process (decLp[2].process (xr));
        if (++decCnt < dec) return;
        decCnt = 0;
        ringL[ringPos] = a; ringR[ringPos] = b;
        // reference: left sample K steps ago; right at the same time +- k
        const int ref = (ringPos - K + kRing) % kRing;
        const float lref = ringL[ref];
        const double c = 1.0 / (4000.0 * 1.0);   // ~1 s memory at the decimated rate
        for (int k = -K; k <= K; ++k) { const float rv = ringR[(ref + k + kRing) % kRing]; xc[k + K] += (lref * rv - xc[k + K]) * c; }
        eL += ((double) lref * lref - eL) * c; eR += ((double) ringR[ref] * ringR[ref] - eR) * c;
        ringPos = (ringPos + 1) % kRing;
        if (++evalCnt < 1000) return;              // every 0.25 s
        evalCnt = 0;
        const double norm = std::sqrt (eL * eR) + 1e-15;
        int best = K; double bv = 0;
        for (int k = 0; k <= 2 * K; ++k) if (std::abs (xc[k]) > bv) { bv = std::abs (xc[k]); best = k; }
        alignConfidence = (float) (bv / norm);
        if (alignConfidence < 0.35f || eL < 1e-10 || eR < 1e-10) return;
        double frac = 0;
        if (best > 0 && best < 2 * K)
        {
            const double y0 = std::abs (xc[best - 1]), y1 = std::abs (xc[best]), y2 = std::abs (xc[best + 1]);
            const double den = y0 - 2 * y1 + y2; if (std::abs (den) > 1e-15) frac = 0.5 * (y0 - y2) / den;
        }
        const double lagDec = (best - K) + frac;          // right is LATE by lagDec decimated samples when > 0
        tauEst = (float) (-lagDec * dec);                 // so its delay is shortened by that much
        const float wantPol = xc[best] < 0 ? -1.0f : 1.0f;
        if ((wantPol < 0.0f) != (pol < 0.0f)) { if (++polVotes >= 2) { pol = wantPol; polVotes = 0; } } else polVotes = 0;
    }

    Params p;
    double fs = 48000;
    int blk = 512, D = 144;
    std::atomic<int> firN { 8192 };
    FracDelay dlLo[2], dlHi[2];
    LR4 split[2], monSplit[2];
    Biquad sHp[2], hpM[2], hpS[2], decLp[4];
    juce::dsp::Convolution convM, convS;
    juce::AudioBuffer<float> tmpM, tmpS;
    float lastF[3] { -1, -1, -1 }; int lastOrder = -1;
    float mg = 1, sg = 1, outT = 1, outSm = 1, lowW = 0, highW = 1;
    // alignment tracker
    static constexpr int K = 12, kRing = 64;
    float ringL[kRing] {}, ringR[kRing] {};
    double xc[2 * K + 1] {}, eL = 1e-9, eR = 1e-9;
    int ringPos = 0, dec = 12, decCnt = 0, evalCnt = 0, polVotes = 0;
    float tauSm = 0, tauEst = 0, pol = 1;
    double inPow = 1e-9, outPow = 1e-9; float trimDb = 0;
    bool first = true;
};

} // namespace lumen::mono
