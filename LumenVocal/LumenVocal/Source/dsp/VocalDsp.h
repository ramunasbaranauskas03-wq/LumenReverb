#pragma once
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdint>

// =====================================================================================
// Lumen Vocal building blocks
//   Yin       : pitch detector (decimated YIN with parabolic refinement, voiced / unvoiced)
//   Psola     : real-time pitch-synchronous overlap-add shifter with independent formant control
//               (ratio 1 = sample-exact delay of `latency`, so tuning never colours a clean note)
//   Scale     : key / scale snapping, diatonic harmony intervals, key detection (Krumhansl)
//   Biquad, DeEsser, VoxComp, Doubler
// =====================================================================================
namespace lumen::vox
{

constexpr double kPi = 3.14159265358979323846;

inline float midiFromHz (float f) { return 69.0f + 12.0f * std::log2 (f / 440.0f); }
inline float hzFromMidi (float m) { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }

//==============================================================================
class Yin
{
public:
    void prepare (double fs)
    {
        dec = std::max (1, (int) std::round (fs / 24000.0));
        rate = fs / dec;
        W = (int) std::round (rate * 0.027);                 // analysis window
        tauMax = (int) std::ceil (rate / 70.0);
        tauMin = std::max (2, (int) std::floor (rate / 1100.0));
        hop = std::max (32, (int) std::round (rate * 0.005));
        buf.assign ((size_t) (W + tauMax + 4), 0.0f);
        d.assign ((size_t) tauMax + 2, 0.0f);
        reset();
    }
    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        filled = 0; decCnt = 0; acc = 0.0f; hopCnt = 0; lp1 = lp2 = 0.0f;
        f0 = 0.0f; conf = 0.0f; hist[0] = hist[1] = hist[2] = 0.0f;
    }
    // returns true when a new estimate is available (f0 in Hz, 0 = unvoiced)
    inline bool push (float x) noexcept
    {
        // 2-pole lowpass (~1.8 kHz) before decimation: the fundamental is all we need
        const float a = 0.55f / (float) dec + 0.1f;
        lp1 += a * (x - lp1); lp2 += a * (lp1 - lp2);
        acc += lp2;
        if (++decCnt < dec) return false;
        const float v = acc / (float) dec; acc = 0.0f; decCnt = 0;
        std::move (buf.begin() + 1, buf.end(), buf.begin());
        buf.back() = v;
        if (filled < (int) buf.size()) ++filled;
        if (++hopCnt < hop || filled < (int) buf.size()) return false;
        hopCnt = 0;
        analyse();
        return true;
    }
    float f0 = 0.0f, conf = 0.0f, level = 0.0f;
    int hopSamples (double fs) const { return hop * dec; (void) fs; }

private:
    void analyse() noexcept
    {
        const int N = (int) buf.size();
        const float* x = buf.data() + (N - W - tauMax);
        double e = 0.0; for (int i = tauMax; i < tauMax + W; ++i) e += (double) x[i] * x[i];
        level = (float) std::sqrt (e / W);
        if (level < 3.0e-4f) { set (0.0f, 0.0f); return; }       // ~ -70 dBFS: silence
        // difference function d(tau) on the most recent W samples
        d[0] = 1.0f;
        double run = 0.0;
        float best = 1.0f; int bestTau = -1;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double s = 0.0;
            const float* a = x + tauMax;
            const float* b = x + tauMax - tau;
            for (int i = 0; i < W; ++i) { const float df = a[i] - b[i]; s += (double) df * df; }
            run += s;
            d[(size_t) tau] = (float) (s * tau / std::max (1e-12, run));
            if (tau >= tauMin + 1 && bestTau < 0 && d[(size_t) tau - 1] < 0.15f && d[(size_t) tau - 1] <= d[(size_t) tau]) bestTau = tau - 1;
        }
        if (bestTau < 0)
        {
            for (int tau = tauMin; tau <= tauMax; ++tau) if (d[(size_t) tau] < best) { best = d[(size_t) tau]; bestTau = tau; }
            if (best > 0.35f) { set (0.0f, 1.0f - best); return; }
        }
        float t = (float) bestTau;
        if (bestTau > 1 && bestTau < tauMax)
        {
            const float y0 = d[(size_t) bestTau - 1], y1 = d[(size_t) bestTau], y2 = d[(size_t) bestTau + 1];
            const float den = y0 - 2.0f * y1 + y2;
            if (std::abs (den) > 1e-9f) t += 0.5f * (y0 - y2) / den;
        }
        set ((float) (rate / t), 1.0f - d[(size_t) bestTau]);
    }
    void set (float f, float c) noexcept
    {
        // median of 3 removes single-frame octave jumps
        hist[0] = hist[1]; hist[1] = hist[2]; hist[2] = f;
        float a = hist[0], b = hist[1], cc = hist[2];
        if (a > b) std::swap (a, b);
        if (b > cc) std::swap (b, cc);
        if (a > b) std::swap (a, b);
        f0 = (hist[2] <= 0.0f) ? 0.0f : (b > 0.0f ? b : hist[2]);
        conf = c;
    }
    std::vector<float> buf, d;
    double rate = 24000.0;
    int dec = 2, W = 640, tauMax = 340, tauMin = 22, hop = 120, filled = 0, decCnt = 0, hopCnt = 0;
    float acc = 0.0f, lp1 = 0.0f, lp2 = 0.0f, hist[3] {};
};

//==============================================================================
class Psola
{
public:
    void prepare (double fs, int latencySamples)
    {
        sr = fs;
        L = latencySamples;
        int n = 1; while (n < 4 * L + 4096) n <<= 1;
        mask = n - 1;
        in.assign ((size_t) n, 0.0f); out.assign ((size_t) n, 0.0f);
        tMax = (float) (fs / 75.0);
        D = (int) std::ceil (3.6f * tMax) + 8;      // >= max hop (T/0.48) + max grain half-length (T/0.7)
        reset();
    }
    void reset()
    {
        std::fill (in.begin(), in.end(), 0.0f); std::fill (out.begin(), out.end(), 0.0f);
        t = 0; nextSynth = (double) D; aMark = -1.0e18;
    }
    int latency() const noexcept { return L; }
    static int minLatency (double fs) { const float tm = (float) (fs / 75.0); return (int) std::ceil (3.6f * tm + 8.0f + 1.6f * tm) + 16; }

    // T: period in samples (0 = unvoiced), ratio: pitch factor, fr: formant factor
    inline float process (float x, float T, float ratio, float fr) noexcept
    {
        in[(size_t) (t & mask)] = x;
        ratio = std::clamp (ratio, 0.48f, 2.1f);
        fr = std::clamp (fr, 0.7f, 1.45f);
        const bool voiced = T > 8.0f;
        const float Tp = voiced ? std::min (T, tMax) : (float) (sr * 0.004);
        while (nextSynth <= (double) (t + D))
        {
            spawn (nextSynth, Tp, voiced, ratio, fr);
            nextSynth += std::max (8.0, (double) (Tp / ratio));
        }
        const size_t oi = (size_t) (t & mask);
        const float y = out[oi];
        out[oi] = 0.0f;
        ++t;
        return y;
    }

private:
    inline float readIn (double pos) const noexcept
    {
        const double fl = std::floor (pos);
        const int64_t i = (int64_t) fl;
        const float fr = (float) (pos - fl);
        const float a = in[(size_t) (i & mask)], b = in[(size_t) ((i + 1) & mask)];
        return a + (b - a) * fr;
    }
    void spawn (double c, float T, bool voiced, float ratio, float fr) noexcept
    {
        const double target = c - (double) L;
        const bool unity = std::abs (ratio - 1.0f) < 1.0e-4f && std::abs (fr - 1.0f) < 1.0e-4f;
        if (! voiced || unity || aMark < target - 6.0 * T || aMark > target + 6.0 * T) aMark = target;   // unity: exact copy
        else aMark += std::round ((target - aMark) / T) * T;   // marks move in whole periods -> phase coherent
        const float To = T / fr;
        const int n = (int) std::ceil (To);
        // exact-overlap normalisation: Hann of half-length To at hop T/ratio sums to To*ratio/T
        const float norm = (T / ratio) / To;
        const int64_t ci = (int64_t) std::floor (c);
        const double cf = c - (double) ci;
        for (int j = -n; j <= n; ++j)
        {
            const double off = (double) j - cf;                       // output offset from the exact centre
            if (std::abs (off) >= To) continue;
            const float w = 0.5f + 0.5f * (float) std::cos (kPi * off / To);
            out[(size_t) ((ci + j) & mask)] += norm * w * readIn (aMark + off * fr);
        }
    }

    std::vector<float> in, out;
    double sr = 48000.0, nextSynth = 0.0, aMark = 0.0;
    int64_t t = 0;
    int L = 2048, D = 1200, mask = 0;
    float tMax = 640.0f;
};

//==============================================================================
struct Scale
{
    enum Type { Chromatic = 0, Major, Minor, HarmMinor, Dorian, MajPent, MinPent, kNumScales };
    static const int* steps (int type, int& n)
    {
        static const int chrom[] { 0,1,2,3,4,5,6,7,8,9,10,11 };
        static const int maj[] { 0,2,4,5,7,9,11 }, mnr[] { 0,2,3,5,7,8,10 }, hmn[] { 0,2,3,5,7,8,11 }, dor[] { 0,2,3,5,7,9,10 };
        static const int mp[] { 0,2,4,7,9 }, np[] { 0,3,5,7,10 };
        switch (type)
        {
            case Major: n = 7; return maj;  case Minor: n = 7; return mnr;  case HarmMinor: n = 7; return hmn;
            case Dorian: n = 7; return dor; case MajPent: n = 5; return mp;  case MinPent: n = 5; return np;
            default: n = 12; return chrom;
        }
    }
    static bool inScale (int note, int key, int type)
    {
        int n; const int* s = steps (type, n);
        const int pc = ((note - key) % 12 + 12) % 12;
        for (int i = 0; i < n; ++i) if (s[i] == pc) return true;
        return false;
    }
    // nearest allowed note to midi m (float)
    static int snap (float m, int key, int type)
    {
        const int base = (int) std::round (m);
        int best = base; float bd = 1e9f;
        for (int k = -3; k <= 3; ++k)
            if (inScale (base + k, key, type) && std::abs ((float) (base + k) - m) < bd) { bd = std::abs ((float) (base + k) - m); best = base + k; }
        return best;
    }
    // diatonic harmony: interval index 0 off, 1 -oct, 2 -5th, 3 -4th, 4 -3rd, 5 +3rd, 6 +4th, 7 +5th, 8 +oct
    static int harmony (int note, int interval, int key, int type)
    {
        static const int cand[9][3] { { 0,0,0 }, { -12,-12,-12 }, { -7,-6,-8 }, { -5,-6,-4 }, { -3,-4,-3 }, { 4,3,4 }, { 5,6,4 }, { 7,6,8 }, { 12,12,12 } };
        interval = std::clamp (interval, 0, 8);
        if (interval == 0) return note;
        for (int c = 0; c < 3; ++c) if (inScale (note + cand[interval][c], key, type)) return note + cand[interval][c];
        return note + cand[interval][0];
    }
    // Krumhansl-Schmuckler key finding from a pitch-class histogram; returns key 0..11, minor flag
    static void detectKey (const double* pc, int& key, bool& minor)
    {
        static const double maj[12] { 6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88 };
        static const double mnr[12] { 6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17 };
        auto corr = [&] (const double* prof, int k)
        {
            double mx = 0, my = 0; for (int i = 0; i < 12; ++i) { mx += pc[(i + k) % 12]; my += prof[i]; }
            mx /= 12; my /= 12;
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < 12; ++i) { const double a = pc[(i + k) % 12] - mx, b = prof[i] - my; sxy += a * b; sxx += a * a; syy += b * b; }
            return sxy / std::sqrt (std::max (1e-12, sxx * syy));
        };
        double best = -2; key = 0; minor = false;
        for (int k = 0; k < 12; ++k)
        {
            const double a = corr (maj, k), b = corr (mnr, k);
            if (a > best) { best = a; key = k; minor = false; }
            if (b > best) { best = b; key = k; minor = true; }
        }
    }
};

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process (float x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2)
    { b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0; }
    void peak (double fs, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void highShelf (double fs, double f, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * f / fs, c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
             (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
    void highPass (double fs, double f, double q)
    {
        const double w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void lowPass (double fs, double f, double q)
    {
        const double w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
};

//==============================================================================
// split-band de-esser: only the band above `freq` is turned down, and only when it dominates the voice
class DeEsser
{
public:
    void prepare (double fs_) { fs = fs_; hp1.reset(); hp2.reset(); lp1.reset(); lp2.reset(); envH = envF = 0.0f; g = 1.0f; lastF = -1; aA = coef (0.0008); aR = coef (0.05); }
    void set (float amount_, float freq)
    {
        amount = amount_;
        // Linkwitz-Riley 4 split: low + high sum to an allpass, so turning `high` down never boosts the crossover region
        if (std::abs (freq - lastF) > 1.0f)
        {
            hp1.highPass (fs, freq, 0.70710678); hp2.highPass (fs, freq, 0.70710678);
            lp1.lowPass (fs, freq, 0.70710678); lp2.lowPass (fs, freq, 0.70710678);
            lastF = freq;
        }
    }
    inline float process (float x) noexcept
    {
        const float h = hp2.process (hp1.process (x));
        const float lo = lp2.process (lp1.process (x));
        const float ah = std::abs (h), af = std::abs (x);
        envH = ah > envH ? envH + (ah - envH) * (1.0f - aA) : envH * aR;
        envF = af > envF ? envF + (af - envF) * (1.0f - aA) : envF * aR;
        float want = 1.0f;
        if (amount > 0.001f && envF > 1.0e-3f)
        {
            const float rDb = 20.0f * std::log10 ((envH + 1e-9f) / (envF + 1e-9f));
            const float thr = -4.0f - 13.0f * amount;
            const float gr = std::clamp ((rDb - thr) * (1.0f + 1.5f * amount), 0.0f, 20.0f);
            want = std::pow (10.0f, -gr / 20.0f);
        }
        g += (want - g) * (want < g ? 0.35f : 0.004f);
        grDb = std::min (grDb * 0.9995f, 20.0f * std::log10 (std::max (1e-6f, g)));
        return amount > 0.001f ? lo + h * g : x;
    }
    float grDb = 0.0f;
private:
    float coef (double s) const { return (float) std::exp (-1.0 / (s * fs)); }
    Biquad hp1, hp2, lp1, lp2;
    double fs = 48000;
    float amount = 0, envH = 0, envF = 0, g = 1, aA = 0.9f, aR = 0.999f, lastF = -1;
};

//==============================================================================
// vocal compressor: soft knee, program-dependent (opto-like) release, slow auto make-up
class VoxComp
{
public:
    void prepare (double fs_) { fs = fs_; env = 0.0f; gr = 0.0f; avgGr = 0.0f; aAtt = coef (0.004); aRel1 = coef (0.06); aRel2 = coef (0.6); aAvg = coef (2.5); }
    void set (float amount_) { amount = amount_; thr = -8.0f - 28.0f * amount; ratio = 2.0f + 4.0f * amount; }
    inline float process (float x) noexcept
    {
        if (amount < 0.001f) { gr = 0.0f; return x; }
        const float lv = 20.0f * std::log10 (std::abs (x) + 1e-9f);
        const float knee = 8.0f, over = lv - thr;
        float want = 0.0f;
        if (over > knee * 0.5f) want = over * (1.0f - 1.0f / ratio);
        else if (over > -knee * 0.5f) { const float k = over + knee * 0.5f; want = (1.0f - 1.0f / ratio) * k * k / (2.0f * knee); }
        if (want > env) env = want + (env - want) * aAtt;
        else
        {
            // fast release for short peaks, slower once compressed for a while (opto behaviour)
            const float r = aRel1 + (aRel2 - aRel1) * std::min (1.0f, avgGr / 6.0f);
            env = want + (env - want) * r;
        }
        avgGr = env + (avgGr - env) * aAvg;
        gr = env;
        const float makeup = 0.7f * avgGr;
        return x * std::pow (10.0f, (makeup - env) / 20.0f);
    }
    float gr = 0.0f;
private:
    float coef (double s) const { return (float) std::exp (-1.0 / (s * fs)); }
    double fs = 48000;
    float amount = 0, thr = -20, ratio = 3, env = 0, avgGr = 0, aAtt = 0.9f, aRel1 = 0.999f, aRel2 = 0.9999f, aAvg = 0.99999f;
};

//==============================================================================
// ADT doubler: two short, slowly drifting delays (a few cents of pitch wander each), panned apart
class Doubler
{
public:
    void prepare (double fs_)
    {
        fs = fs_;
        int n = 1; while (n < (int) (fs * 0.06)) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; pos = 0;
        ph[0] = 0.0; ph[1] = 0.37;
        lp[0] = lp[1] = 0.0f;
    }
    inline void process (float x, float& l, float& r) noexcept
    {
        buf[(size_t) pos] = x;
        const double base[2] { 0.0145 * fs, 0.0215 * fs }, depth = 0.0011 * fs, rate[2] { 0.23, 0.31 };
        float v[2];
        for (int k = 0; k < 2; ++k)
        {
            ph[k] += rate[k] / fs; if (ph[k] >= 1.0) ph[k] -= 1.0;
            const double d = base[k] + depth * std::sin (2 * kPi * ph[k]) + 0.4 * depth * std::sin (2 * kPi * ph[k] * 2.7);
            const double rp = (double) pos - d;
            const double fl = std::floor (rp);
            const int i = (int) fl & mask;
            const float f = (float) (rp - fl);
            const float s = buf[(size_t) i] + (buf[(size_t) ((i + 1) & mask)] - buf[(size_t) i]) * f;
            lp[k] += 0.55f * (s - lp[k]);                   // a touch darker than the lead
            v[k] = lp[k];
        }
        pos = (pos + 1) & mask;
        l = v[0]; r = v[1];
    }
private:
    std::vector<float> buf;
    double fs = 48000, ph[2] {};
    int mask = 0, pos = 0;
    float lp[2] {};
};

} // namespace lumen::vox
