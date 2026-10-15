#pragma once
#include <juce_dsp/juce_dsp.h>
#include "PumpEngine.h"     // SVFp, LR4
#include <cmath>
#include <algorithm>
#include <vector>
#include <complex>

// =====================================================================================
// Lumen Space Control engine — 4-band Mid/Side stereo imager
//   per band: WIDTH (side gain 0..200%), SPREAD (adds a 90-degree copy of the mid to the side:
//   widens even mono sources and is 100% mono-compatible because it never touches the mid),
//   GAIN, solo / mute.
//   Global: MONO BELOW (side high-pass), PHASE SAFE (correlation guard), global WIDTH, MIX, OUTPUT.
// Two crossover modes with the SAME band magnitudes (LR4 shapes, |LP|^2 + |HP|^2 style sums = 1):
//   MIN PHASE   : zero latency, IIR Linkwitz-Riley splits + IIR Hilbert pair
//   LINEAR PHASE: the whole linear M/S network is folded into three FIRs (M->M, S->S, M->S),
//                 designed in the frequency domain, run by juce::dsp::Convolution. No phase shift at all.
// =====================================================================================
namespace lumen::sc
{

constexpr int kBands = 4;

struct BandP { float width = 1, spread = 0, gainDb = 0; bool solo = false, mute = false; };
struct Params
{
    int bands = 4; float x[3] { 150, 1500, 7000 };
    BandP band[kBands];
    bool linear = false;
    float globalWidth = 1, monoBelow = 0, safe = -1, mix = 1, outDb = 0;
    bool monoCheck = false, sideOnly = false;
};

// effective linear gains of each band (solo / mute / global width applied)
struct Gains { float m[kBands] {}, s[kBands] {}, sp[kBands] {}; };
inline Gains computeGains (const Params& p)
{
    Gains g;
    bool anySolo = false;
    for (int b = 0; b < p.bands; ++b) anySolo |= p.band[b].solo;
    for (int b = 0; b < kBands; ++b)
    {
        const auto& bp = p.band[b];
        const bool on = b < p.bands && ! bp.mute && (! anySolo || bp.solo);
        const float lv = on ? std::pow (10.0f, bp.gainDb / 20.0f) : 0.0f;
        g.m[b] = lv;
        g.s[b] = lv * std::clamp (bp.width * p.globalWidth, 0.0f, 3.0f);
        g.sp[b] = lv * 0.75f * bp.spread * std::min (1.5f, p.globalWidth);
    }
    return g;
}

// sanitized, ordered crossovers
inline void crossovers (const Params& p, float fs, float* xs)
{
    xs[0] = std::clamp (p.x[0], 20.0f, 2000.0f);
    xs[1] = std::clamp (p.x[1], xs[0] * 1.25f, std::min (14000.0f, fs * 0.4f));
    xs[2] = std::clamp (p.x[2], xs[1] * 1.25f, std::min (18000.0f, fs * 0.45f));
}

//==============================================================================
// up to 4-way phase-coherent LR4 splitter (lower bands get the later crossovers' allpasses)
class Splitter
{
public:
    void set (const float* xs, float fs)
    {
        for (int k = 0; k < 3; ++k) { xo[k].set (xs[k], fs); apA[k].set (xs[k], fs); apB[k].set (xs[k], fs); }
    }
    void reset() { for (int k = 0; k < 3; ++k) { xo[k].reset(); apA[k].reset(); apB[k].reset(); } }
    inline void split (float x, int nb, float* out) noexcept
    {
        if (nb <= 1) { out[0] = x; return; }
        float lo, rest, a1, a2;
        xo[0].process (x, lo, rest);
        if (nb == 2) { out[0] = lo; out[1] = rest; return; }
        float b1, rest2;
        xo[1].process (rest, b1, rest2);
        apA[1].process (lo, a1, a2); lo = a1 + a2;
        if (nb == 3) { out[0] = lo; out[1] = b1; out[2] = rest2; return; }
        float b2, b3;
        xo[2].process (rest2, b2, b3);
        apA[2].process (lo, a1, a2); lo = a1 + a2;
        apB[2].process (b1, a1, a2); b1 = a1 + a2;
        out[0] = lo; out[1] = b1; out[2] = b2; out[3] = b3;
    }
private:
    LR4 xo[3], apA[3], apB[3];
};

//==============================================================================
// IIR Hilbert pair (two 4-stage allpass chains with ~90 degrees difference, 15 Hz .. 20 kHz at 44.1k+)
class Hilbert
{
public:
    void reset() { for (auto& s : st) s = {}; d1 = 0.0f; }
    inline void process (float x, float& re, float& im) noexcept
    {
        static constexpr float c0[4] { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
        static constexpr float c1[4] { 0.6923878000000f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        float a = x, b = x;
        for (int i = 0; i < 4; ++i) a = section (st[i], c0[i] * c0[i], a);
        for (int i = 0; i < 4; ++i) b = section (st[4 + i], c1[i] * c1[i], b);
        re = a; im = d1; d1 = b;
    }
private:
    struct S { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    static inline float section (S& s, float a2, float x) noexcept
    {
        const float y = a2 * (x + s.y2) - s.x2;
        s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y;
        return y;
    }
    S st[8];
    float d1 = 0.0f;
};

//==============================================================================
// Linear-phase FIR design: |band| shapes are exactly the LR4 magnitudes, so both modes sound the same tonally.
struct FirSet { juce::AudioBuffer<float> mm, ss, ms; };

inline int firLength (double fs) { int n = 1; while (n < fs * 0.17) n <<= 1; return n; }

inline void designFirs (const Params& p, double fs, int N, FirSet& out)
{
    const Gains g = computeGains (p);
    float xs[3]; crossovers (p, (float) fs, xs);
    const int nb = std::clamp (p.bands, 1, kBands);
    const int order = (int) std::round (std::log2 ((double) N));
    juce::dsp::FFT fft (order);
    std::vector<float> bufM ((size_t) N * 2, 0.0f), bufS ((size_t) N * 2, 0.0f), bufX ((size_t) N * 2, 0.0f);
    for (int k = 0; k <= N / 2; ++k)
    {
        const double f = std::max (1e-3, k * fs / N);
        double lp[3], hp[3];
        for (int c = 0; c < 3; ++c) { const double r = std::pow (f / xs[c], 4.0); lp[c] = 1.0 / (1.0 + r); hp[c] = r / (1.0 + r); }
        double B[kBands] {};
        if (nb == 1) B[0] = 1;
        else if (nb == 2) { B[0] = lp[0]; B[1] = hp[0]; }
        else if (nb == 3) { B[0] = lp[0]; B[1] = hp[0] * lp[1]; B[2] = hp[0] * hp[1]; }
        else { B[0] = lp[0]; B[1] = hp[0] * lp[1]; B[2] = hp[0] * hp[1] * lp[2]; B[3] = hp[0] * hp[1] * hp[2]; }
        double hm = 0, hs = 0, hx = 0;
        for (int b = 0; b < nb; ++b) { hm += g.m[b] * B[b]; hs += g.s[b] * B[b]; hx += g.sp[b] * B[b]; }
        if (p.monoBelow >= 20.0f) { const double r = std::pow (f / p.monoBelow, 4.0); const double h = r / (1.0 + r); hs *= h; hx *= h; }
        if (k == 0) hx = 0.0;                                   // Hilbert has no DC
        const double sgn = (k & 1) ? -1.0 : 1.0;                // linear phase: delay of N/2 samples
        bufM[(size_t) (2 * k)] = (float) (hm * sgn);
        bufS[(size_t) (2 * k)] = (float) (hs * sgn);
        // -j for the 90-degree copy:  (a)(-j) -> imag = -a
        bufX[(size_t) (2 * k + 1)] = (k == N / 2) ? 0.0f : (float) (-hx * sgn);
    }
    fft.performRealOnlyInverseTransform (bufM.data());
    fft.performRealOnlyInverseTransform (bufS.data());
    fft.performRealOnlyInverseTransform (bufX.data());
    for (auto* b : { &out.mm, &out.ss, &out.ms }) b->setSize (1, N, false, false, true);
    for (int i = 0; i < N; ++i)
    {
        // gentle window (flat in the middle) — keeps the response exact for wide bands, cleans the far tails
        const double t = (double) i / (N - 1);
        const double w = t < 0.1 ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * t / 0.1)
                       : t > 0.9 ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (1.0 - t) / 0.1) : 1.0;
        out.mm.setSample (0, i, (float) (bufM[(size_t) i] * w));
        out.ss.setSample (0, i, (float) (bufS[(size_t) i] * w));
        out.ms.setSample (0, i, (float) (bufX[(size_t) i] * w));
    }
}

//==============================================================================
class Delay
{
public:
    void prepare (int maxD) { for (auto& b : buf) b.assign ((size_t) maxD + 2, 0.0f); pos = 0; }
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
    void clear() { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); }
private:
    std::vector<float> buf[2];
    int pos = 0, delay = 0;
};

//==============================================================================
class SpaceEngine
{
public:
    static constexpr int kScope = 2048;

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        N = firLength (fs);
        blk = std::max (32, maxBlock);
        juce::dsp::ProcessSpec spec { fs, (juce::uint32) blk, 1 };
        for (auto* c : { &convM, &convS, &convX }) { c->reset(); c->prepare (spec); }
        tmpM.setSize (1, blk); tmpS.setSize (1, blk); tmpX.setSize (1, blk);
        dryDelay.prepare (N);
        for (auto& s : spM) s.reset();
        for (auto& s : spS) s.reset();
        for (auto& s : spH) s.reset();
        anaM.reset(); anaS.reset();
        hilM.reset(); hilS.reset();
        for (auto& v : lastX) v = -1;
        lastBands = -1; first = true;
        cLR = cLL = cRR = 1e-9f; safeG = 1.0f; mE = sE = 1e-9f;
        for (int b = 0; b < kBands; ++b) bandEm[b] = bandEs[b] = bandEmIn[b] = bandEsIn[b] = 1e-9f;
        scopePos = 0;
    }

    int latency() const noexcept { return p.linear ? N / 2 : 0; }
    int firSize() const noexcept { return N; }

    // linear mode: call from a background thread whenever params change
    void loadFirs (FirSet&& f)
    {
        convM.loadImpulseResponse (std::move (f.mm), fs, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        convS.loadImpulseResponse (std::move (f.ss), fs, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        convX.loadImpulseResponse (std::move (f.ms), fs, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    }

    void setParams (const Params& np) noexcept
    {
        const bool modeChanged = np.linear != p.linear;
        p = np;
        p.bands = std::clamp (p.bands, 1, kBands);
        float xs[3]; crossovers (p, (float) fs, xs);
        if (std::abs (xs[0] - lastX[0]) > 0.01f || std::abs (xs[1] - lastX[1]) > 0.01f || std::abs (xs[2] - lastX[2]) > 0.01f)
        {
            for (int k = 0; k < 3; ++k) lastX[k] = xs[k];
            for (auto& s : spM) s.set (xs, (float) fs);
            for (auto& s : spS) s.set (xs, (float) fs);
            for (auto& s : spH) s.set (xs, (float) fs);
            anaM.set (xs, (float) fs); anaS.set (xs, (float) fs);
        }
        if (p.bands != lastBands) { for (auto& s : spM) s.reset(); for (auto& s : spS) s.reset(); for (auto& s : spH) s.reset(); anaM.reset(); anaS.reset(); lastBands = p.bands; }
        const float mb = std::max (20.0f, p.monoBelow);
        if (std::abs (mb - lastMono) > 0.01f) { monoHp.set (mb, (float) fs); lastMono = mb; }
        tgt = computeGains (p);
        outT = std::pow (10.0f, p.outDb / 20.0f);
        if (first) outSm = outT;
        if (first || modeChanged) { cur = tgt; first = false; dryDelay.clear(); }
        dryDelay.setDelay (latency());
    }

    void process (float* L, float* R, int n) noexcept
    {
        const float sm = 1.0f - std::exp (-1.0f / (0.015f * (float) fs));
        const float eC = 1.0f - std::exp (-1.0f / (0.3f * (float) fs));     // meters
        const float cC = 1.0f - std::exp (-1.0f / (0.1f * (float) fs));     // correlation guard
        const float safeAtt = 1.0f - std::exp (-1.0f / (0.01f * (float) fs)), safeRel = 1.0f - std::exp (-1.0f / (0.25f * (float) fs));
        const int nb = p.bands;
        for (int start = 0; start < n; start += blk)
        {
            const int len = std::min (blk, n - start);
            float* l = L + start; float* r = R + start;
            float* m = tmpM.getWritePointer (0); float* s = tmpS.getWritePointer (0); float* x = tmpX.getWritePointer (0);
            for (int i = 0; i < len; ++i)
            {
                m[i] = 0.5f * (l[i] + r[i]); s[i] = 0.5f * (l[i] - r[i]);
                // band analysis of the input (for the meters + smart assist), same in both modes
                float bm[kBands] {}, bs[kBands] {};
                anaM.split (m[i], nb, bm); anaS.split (s[i], nb, bs);
                for (int b = 0; b < nb; ++b) { bandEmIn[b] += (bm[b] * bm[b] - bandEmIn[b]) * eC; bandEsIn[b] += (bs[b] * bs[b] - bandEsIn[b]) * eC; }
            }
            if (p.linear)
            {
                std::copy (m, m + len, x);
                juce::dsp::AudioBlock<float> bM (tmpM.getArrayOfWritePointers(), 1, (size_t) len), bS (tmpS.getArrayOfWritePointers(), 1, (size_t) len), bX (tmpX.getArrayOfWritePointers(), 1, (size_t) len);
                convM.process (juce::dsp::ProcessContextReplacing<float> (bM));
                convS.process (juce::dsp::ProcessContextReplacing<float> (bS));
                convX.process (juce::dsp::ProcessContextReplacing<float> (bX));
                for (int i = 0; i < len; ++i) s[i] += x[i];
                cur = tgt;
            }
            else
            {
                for (int i = 0; i < len; ++i)
                {
                    float mr, mi, sr, si;
                    hilM.process (m[i], mr, mi);          // mr: allpassed mid, mi: its 90-degree copy
                    hilS.process (s[i], sr, si);          // sr: side through the same allpass as mr
                    float bm[kBands] {}, bs[kBands] {}, bh[kBands] {};
                    spM[0].split (mr, nb, bm); spS[0].split (sr, nb, bs); spH[0].split (mi, nb, bh);
                    float mo = 0, so = 0, sx = 0;
                    for (int b = 0; b < nb; ++b)
                    {
                        cur.m[b] += (tgt.m[b] - cur.m[b]) * sm; cur.s[b] += (tgt.s[b] - cur.s[b]) * sm; cur.sp[b] += (tgt.sp[b] - cur.sp[b]) * sm;
                        mo += cur.m[b] * bm[b]; so += cur.s[b] * bs[b]; sx += cur.sp[b] * bh[b];
                    }
                    so += sx;
                    if (p.monoBelow >= 20.0f) { float lo, hi; monoHp.process (so, lo, hi); so = hi; }
                    m[i] = mo; s[i] = so;
                }
            }
            for (int i = 0; i < len; ++i)
            {
                // phase safe: keep the output correlation above the limit by lowering the side only
                mE += (m[i] * m[i] - mE) * cC; sE += (s[i] * s[i] - sE) * cC;
                float want = 1.0f;
                if (p.safe > -0.99f && sE > 1e-10f)
                {
                    const float t = std::clamp (p.safe, -0.99f, 0.95f);
                    want = std::min (1.0f, std::sqrt (mE * (1.0f - t) / ((1.0f + t) * sE)));
                }
                safeG += (want - safeG) * (want < safeG ? safeAtt : safeRel);
                const float so = s[i] * safeG;
                float ol = m[i] + so, orr = m[i] - so;
                float dl = l[i], dr = r[i];
                dryDelay.process (dl, dr);
                ol = dl + (ol - dl) * p.mix; orr = dr + (orr - dr) * p.mix;
                // meters (post)
                cLR += (ol * orr - cLR) * eC; cLL += (ol * ol - cLL) * eC; cRR += (orr * orr - cRR) * eC;
                if (p.sideOnly) { const float sd = 0.5f * (ol - orr); ol = sd; orr = sd; }
                else if (p.monoCheck) { const float md = 0.5f * (ol + orr); ol = md; orr = md; }
                outSm += (outT - outSm) * sm;
                l[i] = ol * outSm; r[i] = orr * outSm;
                scopeL[scopePos] = l[i]; scopeR[scopePos] = r[i]; scopePos = (scopePos + 1) & (kScope - 1);
            }
        }
        // band output estimates (energies scale with the squared gains; the 90-degree copy keeps energy)
        for (int b = 0; b < kBands; ++b)
        {
            if (b >= nb) { bandEm[b] = bandEs[b] = 0.0f; continue; }
            const float sg = safeG;
            bandEm[b] = tgt.m[b] * tgt.m[b] * bandEmIn[b];
            bandEs[b] = sg * sg * (tgt.s[b] * tgt.s[b] * bandEsIn[b] + tgt.sp[b] * tgt.sp[b] * bandEmIn[b]);
        }
    }

    // ---- meters (read after process) ----
    float correlation() const noexcept { return cLR / std::sqrt (std::max (1e-12f, cLL * cRR)); }
    float safeGain() const noexcept { return safeG; }
    static float widthOf (float em, float es) { return std::sqrt (es / std::max (1e-12f, em)); }
    static float corrOf (float em, float es) { return (em - es) / std::max (1e-12f, em + es); }
    float bandEm[kBands] {}, bandEs[kBands] {}, bandEmIn[kBands] {}, bandEsIn[kBands] {};
    float scopeL[kScope] {}, scopeR[kScope] {};
    int scopePos = 0;

private:
    Params p;
    double fs = 48000;
    int N = 8192, blk = 512;
    juce::dsp::Convolution convM, convS, convX;
    juce::AudioBuffer<float> tmpM, tmpS, tmpX;
    Splitter spM[1], spS[1], spH[1], anaM, anaS;
    Hilbert hilM, hilS;
    LR4 monoHp;
    Delay dryDelay;
    Gains tgt, cur;
    float lastX[3] { -1, -1, -1 }, lastMono = -1;
    int lastBands = -1;
    bool first = true;
    float cLR = 0, cLL = 1, cRR = 1, safeG = 1, mE = 0, sE = 0, outT = 1, outSm = 1;
};

} // namespace lumen::sc
