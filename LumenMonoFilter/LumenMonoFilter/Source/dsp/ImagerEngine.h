#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <atomic>
#include <complex>
#include <memory>

// =====================================================================================
// Lumen MonoFilter v2 engine — mono maker + 4-band stereo imager + stereoizer
//
//   source / polarity -> [bass phase align] -> M/S -> side += stereoize(mid)
//      ZERO LATENCY : side -> mono maker (Butterworth HP 6..48 dB/oct, bass-width blend)
//                          -> LR4 4-band tree (bands in phase) -> per-band width -> global width
//                     the MID is never touched, so the mono sum is bit-identical to the input
//      LINEAR PHASE : the same width curve as one zero-phase FIR on the side (partitioned
//                     FFT convolution, constant latency), mid delayed to match: no phase shift
//   -> balance -> mix (latency compensated dry) -> monitor (mono / side / lows / band / delta)
//   -> auto gain -> output
// =====================================================================================
namespace lumen::mf
{
constexpr double kPi = 3.14159265358979323846;
template <typename T> inline bool same (T a, T b) noexcept { return ! (a < b || a > b); }   // exact compare without -Wfloat-equal

struct Params
{
    int source = 0, polarity = 0;
    bool linear = false;
    bool monoOn = true; float monoFreq = 120; int slope = 2; float lowW = 0;
    bool align = false, alignAuto = true; float alignMs = 0;
    float stAmt = 0; int stMode = 0; float stFreq = 300, stTime = 12;
    float x[3] { 200, 2000, 8000 };
    float w[4] { 1, 1, 1, 1 };
    float width = 1, balance = 0, mix = 1, outDb = 0;
    bool autoGain = false;
    int monitor = 0;
};

// crossovers kept ordered with at least a third of an octave between them
inline void orderedX (const Params& p, double fs, float out[3])
{
    const float nyq = (float) (fs * 0.45);
    out[1] = std::clamp (p.x[1], 250.0f, std::min (6000.0f, nyq * 0.5f));
    out[0] = std::clamp (p.x[0], 40.0f, out[1] / 1.26f);
    out[2] = std::clamp (p.x[2], out[1] * 1.26f, std::max (out[1] * 1.26f, std::min (16000.0f, nyq)));
}
inline int slopeOrder (int s) { return s <= 0 ? 1 : s == 1 ? 2 : s == 2 ? 4 : 8; }

// ---- magnitude model (shared by the linear-phase FIR and the UI curve)
inline double lpMag (double f, double fc) { const double r = f / fc, r4 = r * r * r * r; return 1.0 / (1.0 + r4); }   // LR4 (in phase with its HP)
inline double hpMag (double f, double fc) { return 1.0 - lpMag (f, fc); }
inline double butterHpMag (double f, double fc, int order) { const double r = std::max (1e-9, fc / std::max (1e-6, f)); return 1.0 / std::sqrt (1.0 + std::pow (r, 2.0 * order)); }
inline void bandMags (double f, const float x[3], double out[4])
{
    const double l2 = lpMag (f, x[1]), h2 = 1.0 - l2;
    const double l1 = lpMag (f, x[0]), l3 = lpMag (f, x[2]);
    out[0] = l2 * l1; out[1] = l2 * (1.0 - l1); out[2] = h2 * l3; out[3] = h2 * (1.0 - l3);
}
// side gain at f WITHOUT the global width (that one is applied after the filter, sample-smoothed)
inline double sideCurve (const Params& p, double fs, double f)
{
    float x[3]; orderedX (p, fs, x);
    double b[4]; bandMags (f, x, b);
    double s = 0; for (int k = 0; k < 4; ++k) s += p.w[k] * b[k];
    if (p.monoOn) s *= p.lowW + (1.0 - p.lowW) * butterHpMag (f, p.monoFreq, slopeOrder (p.slope));
    return s;
}

// ---- building blocks
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline double process (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    static double w0 (double fs, double f) { return 2 * kPi * std::clamp (f, 1.0, fs * 0.49) / fs; }
    void lp (double fs, double f, double q) { const double w = w0 (fs, f), al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al; b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void hp (double fs, double f, double q) { const double w = w0 (fs, f), al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al; b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void ap (double fs, double f, double q) { const double w = w0 (fs, f), al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al; b0 = (1 - al) / a0; b1 = -2 * c / a0; b2 = 1.0; a1 = b1; a2 = b0; }
    void hp1 (double fs, double f) { const double k = std::tan (w0 (fs, f) / 2), a0 = 1 + k; b0 = 1 / a0; b1 = -1 / a0; b2 = 0; a1 = (k - 1) / a0; a2 = 0; }
    void unity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
};

// Linkwitz-Riley 24 dB/oct split: lo + hi = 2nd-order allpass, lo and hi in phase
struct LR4
{
    Biquad l[2], h[2]; double f = -1;
    void set (double fs, double fc) { if (same (fc, f)) return; f = fc; for (auto& b : l) b.lp (fs, fc, 0.70710678); for (auto& b : h) b.hp (fs, fc, 0.70710678); }
    void reset() { for (auto& b : l) b.reset(); for (auto& b : h) b.reset(); }
    inline void process (double x, double& lo, double& hi) noexcept { lo = l[1].process (l[0].process (x)); hi = h[1].process (h[0].process (x)); }
    inline double lowOnly (double x) noexcept { return l[1].process (l[0].process (x)); }
};

// 4 bands: split at x2, each half split again with the other half's allpass for phase match
struct BandTree
{
    LR4 mid, lo, hi; Biquad apLo, apHi;
    void set (double fs, const float x[3])
    {
        mid.set (fs, x[1]); lo.set (fs, x[0]); hi.set (fs, x[2]);
        apLo.ap (fs, x[2], 0.70710678); apHi.ap (fs, x[0], 0.70710678);
    }
    void reset() { mid.reset(); lo.reset(); hi.reset(); apLo.reset(); apHi.reset(); }
    inline void process (double x, double b[4]) noexcept
    {
        double a, c; mid.process (x, a, c);
        lo.process (apLo.process (a), b[0], b[1]);
        hi.process (apHi.process (c), b[2], b[3]);
    }
};

struct ButterHP   // order 1, 2, 4 or 8
{
    Biquad s[4]; int n = 0; double f = -1; int ord = -1;
    void set (double fs, double fc, int order)
    {
        if (same (fc, f) && order == ord) return;
        f = fc; ord = order;
        static const double q4[2] { 0.54119610, 1.30656296 }, q8[4] { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
        if (order == 1) { s[0].hp1 (fs, fc); n = 1; }
        else if (order == 2) { s[0].hp (fs, fc, 0.70710678); n = 1; }
        else if (order == 4) { for (int k = 0; k < 2; ++k) s[k].hp (fs, fc, q4[k]); n = 2; }
        else { for (int k = 0; k < 4; ++k) s[k].hp (fs, fc, q8[k]); n = 4; }
    }
    void reset() { for (auto& b : s) b.reset(); }
    inline double process (double x) noexcept { for (int k = 0; k < n; ++k) x = s[k].process (x); return x; }
};

class Delay   // integer delay, fixed maximum
{
public:
    void prepare (int maxD) { int n = 1; while (n < maxD + 2) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; pos = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline float process (float x, int d) noexcept { buf[(size_t) pos] = x; const float y = buf[(size_t) ((pos - d) & mask)]; pos = (pos + 1) & mask; return y; }
private:
    std::vector<float> buf; int mask = 0, pos = 0;
};

class FracDelay  // cubic interpolation
{
public:
    void prepare (int maxD) { int n = 1; while (n < maxD + 8) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; pos = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline float process (float x, float d) noexcept
    {
        buf[(size_t) pos] = x;
        const float rp = (float) pos - d, fl = std::floor (rp);
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

// flat-magnitude decorrelator: cascade of Schroeder allpasses with mutually prime delays
class Decorrelator
{
public:
    void prepare (double fs)
    {
        static const double ms[kN] { 0.47, 1.13, 1.79, 2.53, 3.31, 4.43 };
        for (int k = 0; k < kN; ++k) { d[k] = std::max (1, (int) std::round (ms[k] * 0.001 * fs)); line[k].assign ((size_t) d[k], 0.0f); p[k] = 0; }
    }
    void clear() { for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f); }
    inline float process (float x) noexcept
    {
        for (int k = 0; k < kN; ++k)
        {
            auto& l = line[k]; const float z = l[(size_t) p[k]];
            const float v = x + g * z; const float y = z - g * v;
            l[(size_t) p[k]] = v; if (++p[k] == d[k]) p[k] = 0;
            x = y;
        }
        return x;
    }
private:
    static constexpr int kN = 6; static constexpr float g = 0.62f;
    std::vector<float> line[kN]; int d[kN] {}, p[kN] {};
};

// ---- uniform partitioned FFT convolution, one channel, latency = B, IR swap crossfades over one block
class Convolver
{
public:
    struct IR { int parts = 0; std::vector<std::complex<float>> h; };
    ~Convolver() { delete cur; delete pending.exchange (nullptr); delete garbage.exchange (nullptr); }
    void prepare (int blockSize, int maxParts)
    {
        B = blockSize; maxP = maxParts; bins = B + 1;
        int order = 0; while ((1 << order) < 2 * B) ++order;
        fft = std::make_unique<juce::dsp::FFT> (order);
        fdl.assign ((size_t) (maxP * bins), {}); acc.assign ((size_t) bins, {}); acc2.assign ((size_t) bins, {});
        inBuf.assign ((size_t) B, 0.0f); prevIn.assign ((size_t) B, 0.0f); outBuf.assign ((size_t) B, 0.0f); tmp.assign ((size_t) B, 0.0f);
        work.assign ((size_t) (4 * B), 0.0f);
        fdlPos = 0; idx = 0;
        work[0] = 1.0f; fft->performRealOnlyForwardTransform (work.data()); fft->performRealOnlyInverseTransform (work.data());
        invScale = 1.0f / work[0];
        std::fill (work.begin(), work.end(), 0.0f);
    }
    void clear()
    {
        std::fill (fdl.begin(), fdl.end(), std::complex<float> {});
        for (auto* v : { &inBuf, &prevIn, &outBuf }) std::fill (v->begin(), v->end(), 0.0f);
        idx = 0;
    }
    int blockSize() const { return B; }
    bool canAccept() const { return garbage.load() == nullptr; }   // a newer IR may replace a pending one
    void submit (IR* ir) { delete pending.exchange (ir); }
    void collectGarbage() { delete garbage.exchange (nullptr); }
    void setImmediately (IR* ir) { delete pending.exchange (nullptr); delete cur; cur = ir; }   // not while processing

    inline void process (float* x, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float v = x[i]; x[i] = outBuf[(size_t) idx]; inBuf[(size_t) idx] = v;
            if (++idx == B) { runBlock(); idx = 0; }
        }
    }

    // builds the partitioned spectrum of a time-domain IR (worker thread)
    IR* makeIR (const std::vector<float>& h) const
    {
        auto* ir = new IR(); ir->parts = std::min (maxP, (int) ((h.size() + (size_t) B - 1) / (size_t) B));
        ir->h.assign ((size_t) (ir->parts * bins), {});
        int order = 0; while ((1 << order) < 2 * B) ++order;
        juce::dsp::FFT f (order); std::vector<float> w ((size_t) (4 * B));
        for (int p = 0; p < ir->parts; ++p)
        {
            std::fill (w.begin(), w.end(), 0.0f);
            for (int i = 0; i < B && (size_t) (p * B + i) < h.size(); ++i) w[(size_t) i] = h[(size_t) (p * B + i)];
            f.performRealOnlyForwardTransform (w.data(), true);
            for (int k = 0; k < bins; ++k) ir->h[(size_t) (p * bins + k)] = { w[(size_t) (2 * k)], w[(size_t) (2 * k + 1)] };
        }
        return ir;
    }

private:
    void runBlock() noexcept
    {
        IR* nx = pending.exchange (nullptr);
        std::copy (prevIn.begin(), prevIn.end(), work.begin());
        std::copy (inBuf.begin(), inBuf.end(), work.begin() + B);
        std::fill (work.begin() + 2 * B, work.end(), 0.0f);
        fft->performRealOnlyForwardTransform (work.data(), true);
        auto* X = fdl.data() + fdlPos * bins;
        for (int k = 0; k < bins; ++k) X[k] = { work[(size_t) (2 * k)], work[(size_t) (2 * k + 1)] };
        std::copy (inBuf.begin(), inBuf.end(), prevIn.begin());
        convolve (cur, acc); toTime (acc, outBuf.data());
        if (nx != nullptr)
        {
            convolve (nx, acc2); toTime (acc2, tmp.data());
            for (int i = 0; i < B; ++i) { const float t = ((float) i + 0.5f) / (float) B; outBuf[(size_t) i] += (tmp[(size_t) i] - outBuf[(size_t) i]) * t; }
        }
        fdlPos = (fdlPos + 1) % maxP;
        if (nx != nullptr) { IR* old = cur; cur = nx; if (old != nullptr) delete garbage.exchange (old); }
    }
    void convolve (const IR* ir, std::vector<std::complex<float>>& y) noexcept
    {
        std::fill (y.begin(), y.end(), std::complex<float> {});
        if (ir == nullptr) { const auto* x0 = fdl.data() + fdlPos * bins; for (int k = 0; k < bins; ++k) y[(size_t) k] = x0[k]; return; }
        for (int p = 0; p < ir->parts; ++p)
        {
            const auto* x = fdl.data() + ((fdlPos - p + maxP) % maxP) * bins;
            const auto* h = ir->h.data() + p * bins;
            for (int k = 0; k < bins; ++k) y[(size_t) k] += x[k] * h[k];
        }
    }
    void toTime (const std::vector<std::complex<float>>& y, float* dst) noexcept
    {
        for (int k = 0; k < bins; ++k) { work[(size_t) (2 * k)] = y[(size_t) k].real(); work[(size_t) (2 * k + 1)] = y[(size_t) k].imag(); }
        for (int k = 1; k < B; ++k) { work[(size_t) (2 * (2 * B - k))] = y[(size_t) k].real(); work[(size_t) (2 * (2 * B - k) + 1)] = -y[(size_t) k].imag(); }
        fft->performRealOnlyInverseTransform (work.data());
        for (int i = 0; i < B; ++i) dst[i] = work[(size_t) (B + i)] * invScale;
    }

    int B = 256, maxP = 1, bins = 257, fdlPos = 0, idx = 0;
    float invScale = 1.0f;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<std::complex<float>> fdl, acc, acc2;
    std::vector<float> inBuf, prevIn, outBuf, tmp, work;
    IR* cur = nullptr;
    std::atomic<IR*> pending { nullptr }, garbage { nullptr };
};

// zero-phase side FIR (centre tap at N/2) from the width curve
inline std::vector<float> designSideFir (const Params& p, double fs, int N)
{
    int order = 0; while ((1 << order) < N) ++order;
    juce::dsp::FFT big (order);
    std::vector<float> w ((size_t) (2 * N), 0.0f);
    w[0] = 1.0f; big.performRealOnlyForwardTransform (w.data()); big.performRealOnlyInverseTransform (w.data());
    const float scale = 1.0f / w[0];
    std::fill (w.begin(), w.end(), 0.0f);
    const int half = N / 2;
    for (int k = 0; k <= half; ++k)
    {
        const double f = std::max (0.5, (double) k * fs / N);
        const float m = (float) sideCurve (p, fs, f);
        w[(size_t) (2 * k)] = m;
        if (k > 0 && k < half) w[(size_t) (2 * (N - k))] = m;
    }
    big.performRealOnlyInverseTransform (w.data());
    std::vector<float> h ((size_t) N);
    for (int i = 0; i < N; ++i)
    {
        const int src = (i - half + N) % N;
        // Kaiser-like taper (Blackman-Harris 4-term on the outer 30 %, flat inside): keeps the low-frequency resolution
        const double t = std::abs ((i - half) / (double) half);
        double win = 1.0;
        if (t > 0.7) { const double u = (t - 0.7) / 0.3; win = 0.5 + 0.5 * std::cos (kPi * std::min (1.0, u)); }
        h[(size_t) i] = (float) (w[(size_t) src] * scale * win);
    }
    return h;
}

// ---- L/R lag tracker for the low band (decimated cross-correlation)
class LagTracker
{
public:
    void prepare (double fs, float splitF)
    {
        rate = fs; dec = std::max (1, (int) std::round (fs / 4000.0)); decCnt = 0;
        const double lpF = std::min (1000.0, std::max (60.0, (double) splitF));
        for (auto& b : lp) b.lp (fs, lpF, 0.70710678);
        for (auto& b : lp) b.reset();
        std::fill (std::begin (ringL), std::end (ringL), 0.0f); std::fill (std::begin (ringR), std::end (ringR), 0.0f);
        std::fill (std::begin (xc), std::end (xc), 0.0); eL = eR = 1e-9; ringPos = 0; evalCnt = 0; confidence = 0;
    }
    void setSplit (float splitF) { const double lpF = std::min (1000.0, std::max (60.0, (double) splitF)); for (auto& b : lp) { const auto z1 = b.z1, z2 = b.z2; b.lp (rate, lpF, 0.70710678); b.z1 = z1; b.z2 = z2; } }
    inline void push (float xl, float xr, float& tauEst, float& pol) noexcept
    {
        const double a = lp[1].process (lp[0].process (xl)), b = lp[3].process (lp[2].process (xr));
        if (++decCnt < dec) return;
        decCnt = 0;
        ringL[ringPos] = (float) a; ringR[ringPos] = (float) b;
        const int ref = (ringPos - K + kRing) % kRing;
        const float lref = ringL[ref];
        const double c = 1.0 / 4000.0;   // ~1 s memory
        for (int k = -K; k <= K; ++k) { const float rv = ringR[(ref + k + kRing) % kRing]; xc[k + K] += (lref * rv - xc[k + K]) * c; }
        eL += ((double) lref * lref - eL) * c; eR += ((double) ringR[ref] * ringR[ref] - eR) * c;
        ringPos = (ringPos + 1) % kRing;
        if (++evalCnt < 1000) return;    // every 0.25 s
        evalCnt = 0;
        const double norm = std::sqrt (eL * eR) + 1e-15;
        int best = K; double bv = 0;
        for (int k = 0; k <= 2 * K; ++k) if (std::abs (xc[k]) > bv) { bv = std::abs (xc[k]); best = k; }
        confidence = (float) (bv / norm);
        if (confidence < 0.35f || eL < 1e-10 || eR < 1e-10) return;
        double frac = 0;
        if (best > 0 && best < 2 * K)
        {
            const double y0 = std::abs (xc[best - 1]), y1 = std::abs (xc[best]), y2 = std::abs (xc[best + 1]);
            const double den = y0 - 2 * y1 + y2; if (std::abs (den) > 1e-15) frac = 0.5 * (y0 - y2) / den;
        }
        tauEst = (float) (-((best - K) + frac) * dec);
        const float want = xc[best] < 0 ? -1.0f : 1.0f;
        if ((want < 0.0f) != (pol < 0.0f)) { if (++votes >= 2) { pol = want; votes = 0; } } else votes = 0;
    }
    float confidence = 0;
private:
    static constexpr int K = 12, kRing = 64;
    Biquad lp[4]; double rate = 48000;
    float ringL[kRing] {}, ringR[kRing] {};
    double xc[2 * K + 1] {}, eL = 1e-9, eR = 1e-9;
    int ringPos = 0, dec = 12, decCnt = 0, evalCnt = 0, votes = 0;
};

// =====================================================================================
class ImagerEngine
{
public:
    static constexpr int kConvBlock = 256;

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate; blk = std::max (32, std::min (maxBlock, 2048));
        N = 1; while (N < fs * 0.17) N <<= 1;
        conv.prepare (kConvBlock, N / kConvBlock);
        linLat = N / 2 + kConvBlock;
        D = (int) std::round (0.003 * fs);
        for (auto& d : alignLo) d.prepare (2 * D + 8);
        for (auto& d : alignHi) d.prepare (D + 8);
        midDelay.prepare (linLat + 8);
        for (auto& d : dryDelay) d.prepare (linLat + D + 8);
        haas.prepare ((int) std::ceil (0.031 * fs) + 8);
        dec.prepare (fs);
        mBuf.assign ((size_t) blk, 0.0f); sBuf.assign ((size_t) blk, 0.0f); dryL.assign ((size_t) blk, 0.0f); dryR.assign ((size_t) blk, 0.0f);
        tracker.prepare (fs, 200.0f);
        tauSm = 0; tauEst = 0; pol = 1;
        inPow = outPow = 1e-9; agDb = 0;
        first = true; wasLinear = -1; lastLat = -1;
        for (auto* f : { &stHp[0], &stHp[1] }) f->f = -1;
        resetFilters();
    }

    int latency() const noexcept { return (p.linear ? linLat : 0) + (p.align ? D : 0); }
    int linearLatency() const noexcept { return linLat; }
    int firLength() const noexcept { return N; }
    Convolver& convolver() noexcept { return conv; }
    double sampleRate() const noexcept { return fs; }

    // offline / prepare: install the FIR now
    void buildNow (const Params& q) { conv.setImmediately (conv.makeIR (designSideFir (q, fs, N))); }

    void setParams (const Params& np) noexcept
    {
        p = np;
        float x[3]; orderedX (p, fs, x);
        tree.set (fs, x);
        for (int c = 0; c < 2; ++c) { monTree[c].set (fs, x); monLows[c].set (fs, p.monoFreq); }
        monoHp.set (fs, p.monoFreq, slopeOrder (p.slope));
        const double sf = std::clamp ((double) p.stFreq, 40.0, fs * 0.4);
        if (! same (sf, stHp[0].f)) { for (auto& f : stHp) { f.f = sf; f.b[0].hp (fs, sf, 0.54119610); f.b[1].hp (fs, sf, 1.30656296); } }
        const float splitF = std::clamp (p.monoFreq * 2.0f, 60.0f, 1500.0f);
        for (auto& s : alignSplit) s.set (fs, splitF);
        tracker.setSplit (splitF);
        outT = std::pow (10.0f, p.outDb / 20.0f);
        if (first)
        {
            first = false;
            for (int b = 0; b < 4; ++b) wSm[b] = p.w[b];
            widthSm = p.width; lowWSm = p.lowW; monoSm = p.monoOn ? 1.0f : 0.0f; stSm = p.stAmt; treeSm = treeTarget() ? 1.0f : 0.0f;
            balSm = p.balance; mixSm = p.mix; outSm = outT; haasSm = (float) (p.stTime * 0.001 * fs);
        }
    }

    void process (float* L, float* R, int n) noexcept
    {
        const int lin = p.linear ? 1 : 0;
        if (lin != wasLinear)
        {
            // switching modes: start the new path from silence (no stale history from the other mode)
            conv.clear(); midDelay.clear();
            if (wasLinear >= 0) { for (auto& d : dryDelay) d.clear(); }
            wasLinear = lin;
        }
        for (int start = 0; start < n; start += blk)
        {
            const int len = std::min (blk, n - start);
            processChunk (L + start, R + start, len);
        }
    }

    // meters / UI
    float alignMsNow() const noexcept { return (float) (tauSm / fs * 1000.0); }
    float polarityNow() const noexcept { return pol; }
    float alignConfidence() const noexcept { return tracker.confidence; }
    float autoGainDb() const noexcept { return agDb; }

private:
    bool treeTarget() const noexcept { for (float w : p.w) if (std::abs (w - 1.0f) > 1e-4f) return true; return false; }

    void resetFilters()
    {
        tree.reset(); monoHp.reset();
        for (int c = 0; c < 2; ++c) { monTree[c].reset(); monLows[c].reset(); alignSplit[c].reset(); }
        for (auto& f : stHp) for (auto& b : f.b) b.reset();
        conv.clear(); midDelay.clear(); haas.clear(); dec.clear();
        for (auto& d : dryDelay) d.clear();
        for (auto& d : alignLo) d.clear();
        for (auto& d : alignHi) d.clear();
    }

    void processChunk (float* l, float* r, int len) noexcept
    {
        const float sm = 1.0f - (float) std::exp (-1.0 / (0.02 * fs));
        const float tauC = 1.0f - (float) std::exp (-1.0 / (0.4 * fs));
        const float treeT = treeTarget() ? 1.0f : 0.0f, monoT = p.monoOn ? 1.0f : 0.0f;
        const float haasT = (float) (std::clamp (p.stTime, 1.0f, 30.0f) * 0.001 * fs);
        const int totalLat = latency();
        double ip = 0;

        // ---- stage 1: input, source, align, M/S, stereoize, (zero latency side processing)
        for (int i = 0; i < len; ++i)
        {
            float xl = l[i], xr = r[i];
            dryL[(size_t) i] = dryDelay[0].process (xl, totalLat);
            dryR[(size_t) i] = dryDelay[1].process (xr, totalLat);
            ip += 0.5 * ((double) xl * xl + (double) xr * xr);
            switch (p.source)
            {
                case 1: std::swap (xl, xr); break;
                case 2: xr = xl; break;
                case 3: xl = xr; break;
                case 4: xl = xr = 0.5f * (xl + xr); break;
                default: break;
            }
            if (p.polarity == 1) xl = -xl; else if (p.polarity == 2) xr = -xr;

            if (p.align)
            {
                if (p.alignAuto) tracker.push (xl, xr, tauEst, pol);
                const float target = p.alignAuto ? tauEst : (float) (p.alignMs * 0.001 * fs);
                tauSm += (target - tauSm) * tauC;
                const float polarity = p.alignAuto ? pol : 1.0f;
                double ll, lh, rl, rh;
                alignSplit[0].process (xl, ll, lh); alignSplit[1].process (xr, rl, rh);
                const float al = alignLo[0].process ((float) ll, (float) D);
                const float ar = alignLo[1].process ((float) rl, std::clamp ((float) D + tauSm, 2.0f, (float) (2 * D))) * polarity;
                xl = al + alignHi[0].process ((float) lh, (float) D);
                xr = ar + alignHi[1].process ((float) rh, (float) D);
            }

            float m = 0.5f * (xl + xr), s = 0.5f * (xl - xr);

            // stereoize: a decorrelated / delayed copy of the mid goes into the SIDE only -> the mono sum is untouched
            stSm += (p.stAmt - stSm) * sm; haasSm += (haasT - haasSm) * sm * 0.25f;
            const float src = p.stMode == 1 ? haas.process (m, haasSm) : dec.process (m);
            if (p.stMode == 1) dec.process (0.0f); else haas.process (m, haasSm);   // keep both lines running
            const double hpd = stHp[1].b[1].process (stHp[1].b[0].process (stHp[0].b[1].process (stHp[0].b[0].process (src))));
            if (stSm > 1e-5f) s += (float) (stSm * hpd);

            if (! p.linear)
            {
                lowWSm += (p.lowW - lowWSm) * sm; monoSm += (monoT - monoSm) * sm;
                widthSm += (p.width - widthSm) * sm; treeSm += (treeT - treeSm) * sm;
                const double hp = monoHp.process (s);
                const double k = monoSm * (1.0 - lowWSm);
                const double side = s - k * (s - hp);
                double b[4]; tree.process (side, b);
                double t = 0; for (int q = 0; q < 4; ++q) { wSm[q] += (p.w[q] - wSm[q]) * sm; t += wSm[q] * b[q]; }
                const double so = (1.0 - treeSm) * side + treeSm * t;
                sBuf[(size_t) i] = (float) (so * widthSm);
                mBuf[(size_t) i] = m;
            }
            else
            {
                mBuf[(size_t) i] = midDelay.process (m, linLat);
                sBuf[(size_t) i] = s;
            }
        }

        // ---- stage 2: linear-phase side FIR
        if (p.linear)
        {
            conv.process (sBuf.data(), len);
            for (int i = 0; i < len; ++i)
            {
                widthSm += (p.width - widthSm) * sm;
                sBuf[(size_t) i] *= widthSm;
                // keep the zero-latency smoothers in step so a mode switch does not jump
                lowWSm = p.lowW; monoSm = p.monoOn ? 1.0f : 0.0f; treeSm = treeT; for (int q = 0; q < 4; ++q) wSm[q] = p.w[q];
            }
        }

        // ---- stage 3: output, balance, mix, monitor, gain
        double op = 0;
        for (int i = 0; i < len; ++i)
        {
            const float m = mBuf[(size_t) i], s = sBuf[(size_t) i];
            float ol = m + s, orr = m - s;
            balSm += (p.balance - balSm) * sm;
            ol *= balSm > 0 ? 1.0f - balSm : 1.0f; orr *= balSm < 0 ? 1.0f + balSm : 1.0f;
            mixSm += (p.mix - mixSm) * sm;
            const float dl = dryL[(size_t) i], dr = dryR[(size_t) i];
            float yl = dl + mixSm * (ol - dl), yr = dr + mixSm * (orr - dr);
            op += 0.5 * ((double) yl * yl + (double) yr * yr);
            switch (p.monitor)
            {
                case 1: { const float mo = 0.5f * (yl + yr); yl = yr = mo; break; }
                case 2: { const float sd = 0.5f * (yl - yr); yl = sd; yr = -sd; break; }
                case 3: { yl = (float) monLows[0].lowOnly (yl); yr = (float) monLows[1].lowOnly (yr); break; }
                case 4: case 5: case 6: case 7:
                {
                    double a[4], b[4]; monTree[0].process (yl, a); monTree[1].process (yr, b);
                    yl = (float) a[p.monitor - 4]; yr = (float) b[p.monitor - 4]; break;
                }
                case 8: { yl = ol - dl; yr = orr - dr; break; }
                default: break;
            }
            outSm += (outT - outSm) * sm;
            const float g = outSm * std::pow (10.0f, agDb * 0.05f);
            l[i] = yl * g; r[i] = yr * g;
        }
        if (p.autoGain)
        {
            const double c = std::min (1.0, (double) len / (3.0 * fs));
            inPow += (ip / len - inPow) * c; outPow += (op / len - outPow) * c;
            if (inPow > 1e-9 && outPow > 1e-9)
                agDb = (float) std::clamp (10.0 * std::log10 (inPow / outPow), -12.0, 12.0);
        }
        else agDb *= 0.95f;
    }

    Params p;
    double fs = 48000;
    int blk = 512, N = 8192, linLat = 4352, D = 144, lastLat = -1, wasLinear = -1;
    Convolver conv;
    BandTree tree, monTree[2];
    LR4 monLows[2], alignSplit[2];
    ButterHP monoHp;
    struct Hp4 { Biquad b[2]; double f = -1; } stHp[2];
    Decorrelator dec; FracDelay haas;
    Delay midDelay, dryDelay[2];
    FracDelay alignLo[2], alignHi[2];
    LagTracker tracker;
    std::vector<float> mBuf, sBuf, dryL, dryR;
    float wSm[4] { 1, 1, 1, 1 }, widthSm = 1, lowWSm = 0, monoSm = 1, stSm = 0, treeSm = 0, balSm = 0, mixSm = 1, outSm = 1, outT = 1, haasSm = 576;
    float tauSm = 0, tauEst = 0, pol = 1;
    double inPow = 1e-9, outPow = 1e-9; float agDb = 0;
    bool first = true;
};

} // namespace lumen::mf
