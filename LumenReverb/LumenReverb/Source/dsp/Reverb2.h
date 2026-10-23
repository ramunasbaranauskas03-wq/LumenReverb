#pragma once
#include <cmath>
#include <algorithm>
#include <vector>
#include <array>
#include <cstdint>

// =====================================================================================
// Lumen Reverb 2 engine
//
//   pre-delay -> input cut -> [bloom diffusers] -> input diffusion (4 allpasses / side)
//      -> 16-line FDN: each line = modulated delay (random-walk LFO, cubic read)
//                                 + in-loop allpass (echo density grows like a real room)
//                                 + 3-band absorbent filter (exact RT60 at low / mid / high)
//                       fast Walsh-Hadamard feedback, orthogonal stereo in / out vectors
//      + early reflections (24 taps / side, pattern per algorithm, scaled by SIZE)
//      + shimmer (dual-grain pitch shifter in the feedback path)
//   -> wet EQ -> bass mono -> width -> CLARITY (multiband ducking against the dry)
//   -> DUCK -> mix
// =====================================================================================
namespace lumen::rv2
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kLines = 16;

// ------------------------------------------------------------------ algorithms
struct Algo
{
    const char* name;
    float lenMin, lenMax;        // FDN line lengths (ms) at size 1
    float apG, apMin, apMax;     // in-loop allpass gain / lengths (ms)
    float modMs;                 // modulation depth (ms) at depth 100 %
    float erStart, erEnd, erAmt; // early reflections (ms at size 1), level
    float erLp;                  // ER air absorption (Hz)
    float bloom;                 // long diffuser amount
    float bright;                // output tilt (dB / decade above 1 kHz)
    float wetGain;               // calibration
};
inline const std::array<Algo, 8>& algos()
{
    static const std::array<Algo, 8> a { {
        { "Hall",      40, 140, 0.50f, 3.0f, 9.0f,  0.55f, 8, 80,  0.55f, 9000, 0.00f,  0.0f, 1.0f },
        { "Plate",     10, 55,  0.65f, 1.5f, 5.0f,  0.45f, 0, 0,   0.00f, 12000, 0.00f, 1.0f, 1.0f },
        { "Room",      12, 55,  0.55f, 1.0f, 4.0f,  0.25f, 2, 35,  1.00f, 10000, 0.00f, 0.0f, 1.0f },
        { "Chamber",   20, 75,  0.55f, 2.0f, 6.0f,  0.30f, 4, 50,  0.80f, 9000, 0.00f,  0.5f, 1.0f },
        { "Cathedral", 60, 220, 0.55f, 4.0f, 12.0f, 0.75f, 15, 140, 0.45f, 6000, 0.35f, -0.5f, 1.0f },
        { "Bloom",     50, 170, 0.60f, 3.0f, 10.0f, 0.85f, 0, 0,   0.00f, 8000, 1.00f,  0.0f, 1.0f },
        { "Shimmer",   50, 160, 0.55f, 3.0f, 10.0f, 0.70f, 10, 70, 0.30f, 8000, 0.25f,  0.5f, 1.0f },
        { "Ambience",  7,  32,  0.55f, 1.0f, 3.5f,  0.22f, 1, 20,  1.20f, 12000, 0.00f, 0.0f, 1.0f },
    } };
    return a;
}

struct Params
{
    int algo = 0;
    float decay = 2.5f, size = 0.6f, preDelayMs = 20, diffusion = 0.8f, early = 0.5f;
    float modDepth = 0.35f, modRate = 0.6f;
    float bassMult = 1.1f, trebleMult = 0.5f, xLow = 250, xHigh = 4000;
    float lowCut = 120, highCut = 14000;
    float width = 1.0f, bassMono = 120;   // 0 = off
    float shimmer = 0, shimmerSemis = 12;
    bool freeze = false;
};

// ------------------------------------------------------------------ building blocks
struct Line
{
    std::vector<float> buf; int mask = 0, pos = 0;
    void prepare (int maxLen) { int n = 1; while (n < maxLen + 8) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; pos = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline void push (float x) noexcept { buf[(size_t) pos] = x; pos = (pos + 1) & mask; }
    inline float at (int d) const noexcept { return buf[(size_t) ((pos - d) & mask)]; }
    inline float readLinear (float d) const noexcept
    {
        const int i = (int) d; const float f = d - (float) i;
        const float a = at (i), b = at (i + 1); return a + (b - a) * f;
    }
    inline float readCubic (float d) const noexcept           // d >= 2
    {
        const int i = (int) d; const float f = d - (float) i;
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
};

struct Allpass   // Schroeder allpass with its own (fixed) delay
{
    std::vector<float> buf; int len = 1, pos = 0;
    void prepare (int maxLen) { buf.assign ((size_t) std::max (2, maxLen + 1), 0.0f); len = std::max (1, maxLen); pos = 0; }
    void setLength (int l) { len = std::clamp (l, 1, (int) buf.size() - 1); }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline float process (float x, float g) noexcept
    {
        int rp = pos - len; if (rp < 0) rp += (int) buf.size();
        const float z = buf[(size_t) rp];
        const float w = x + g * z;
        buf[(size_t) pos] = w;
        if (++pos >= (int) buf.size()) pos = 0;
        return z - g * w;
    }
};

struct OnePole   // y = lp(x)
{
    float a = 0, z = 0;
    void setHz (double fs, double hz) { a = (float) std::exp (-2.0 * kPi * std::clamp (hz, 1.0, fs * 0.49) / fs); }
    void reset() { z = 0; }
    inline float lp (float x) noexcept { z = x + a * (z - x); return z; }
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process (float xf) noexcept { const double x = xf, y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) { b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0; }
    static double w0 (double fs, double f) { return 2 * kPi * std::clamp (f, 1.0, fs * 0.49) / fs; }
    void lp (double fs, double f, double q) { const double w = w0 (fs, f), al = std::sin (w) / (2 * q), c = std::cos (w); set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
    void hp (double fs, double f, double q) { const double w = w0 (fs, f), al = std::sin (w) / (2 * q), c = std::cos (w); set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
    void ap (double fs, double f, double q) { const double w = w0 (fs, f), al = std::sin (w) / (2 * q), c = std::cos (w); set (1 - al, -2 * c, 1 + al, 1 + al, -2 * c, 1 - al); }
    void highShelf (double fs, double f, double db)
    {
        const double A = std::pow (10.0, db / 40), w = w0 (fs, f), c = std::cos (w), al = std::sin (w) / (2 * 0.7071), s = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + s), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - s), (A + 1) - (A - 1) * c + s, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - s);
    }
    void unity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
    void copyCoeffs (const Biquad& o) { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }
};

struct LR4   // Linkwitz-Riley split, lo + hi = allpass
{
    Biquad l[2], h[2];
    void set (double fs, double f) { for (auto& b : l) b.lp (fs, f, 0.70710678); for (auto& b : h) b.hp (fs, f, 0.70710678); }
    void reset() { for (auto& b : l) b.reset(); for (auto& b : h) b.reset(); }
    inline void split (float x, float& lo, float& hi) noexcept { lo = l[1].process (l[0].process (x)); hi = h[1].process (h[0].process (x)); }
};

struct BandTree   // 4 phase-coherent bands (split at x2, then x1 / x3 with allpass compensation)
{
    LR4 mid, lo, hi; Biquad apLo, apHi;
    void set (double fs, float x1, float x2, float x3) { mid.set (fs, x2); lo.set (fs, x1); hi.set (fs, x3); apLo.ap (fs, x3, 0.70710678); apHi.ap (fs, x1, 0.70710678); }
    void reset() { mid.reset(); lo.reset(); hi.reset(); apLo.reset(); apHi.reset(); }
    inline void process (float x, float b[4]) noexcept
    {
        float a, c; mid.split (x, a, c);
        lo.split (apLo.process (a), b[0], b[1]);
        hi.split (apHi.process (c), b[2], b[3]);
    }
};

// smooth random modulation (cosine-interpolated random walk) -> no periodic chorus
struct RandomLfo
{
    uint32_t seed = 1; float from = 0, to = 0, phase = 0, step = 0.0001f;
    inline float rnd() noexcept { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) * (2.0f / 16777216.0f) - 1.0f; }
    void init (uint32_t s) { seed = s * 2654435761u + 7u; from = rnd(); to = rnd(); phase = (float) (s % 97) / 97.0f; }
    void setRate (double fs, float hz) { step = (float) (hz / fs); }
    inline float next() noexcept
    {
        phase += step;
        if (phase >= 1.0f) { phase -= 1.0f; from = to; to = rnd(); }
        const float w = 0.5f - 0.5f * std::cos ((float) kPi * phase);
        return from + (to - from) * w;
    }
};

// dual-grain pitch shifter (for shimmer)
struct PitchShifter
{
    std::vector<float> buf; int pos = 0, W = 4096; float ph = 0, ratio = 2;
    void prepare (double fs) { W = 1; while (W < fs * 0.08) W <<= 1; buf.assign ((size_t) (2 * W), 0.0f); pos = 0; ph = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void setSemis (float s) { ratio = std::pow (2.0f, s / 12.0f); }
    inline float process (float x) noexcept
    {
        const int n = (int) buf.size();
        buf[(size_t) pos] = x;
        ph += (1.0f - ratio) / (float) W; ph -= std::floor (ph);
        float y = 0;
        for (int g = 0; g < 2; ++g)
        {
            float p = ph + 0.5f * (float) g; p -= std::floor (p);
            const float d = 2.0f + p * (float) (W - 4);
            float rp = (float) pos - d; if (rp < 0) rp += (float) n;
            const int i = (int) rp; const float f = rp - (float) i;
            const float a = buf[(size_t) i], b = buf[(size_t) ((i + 1) % n)];
            const float win = std::sin ((float) kPi * p);
            y += (a + (b - a) * f) * win * win;
        }
        if (++pos >= n) pos = 0;
        return y;
    }
};

// =====================================================================================
class Reverb
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const double maxLenMs = 220.0 * kMaxScale + 4.0;
        for (int i = 0; i < kLines; ++i)
        {
            lines[i].prepare ((int) (maxLenMs * 0.001 * fs) + 64);
            loopAp[i].prepare ((int) (12.0 * kMaxScale * 0.001 * fs) + 4);
            lfo[i].init ((uint32_t) (i + 1) * 7919u);
        }
        pre[0].prepare ((int) (0.52 * fs)); pre[1].prepare ((int) (0.52 * fs));
        er[0].prepare ((int) (0.2 * kMaxScale * fs)); er[1].prepare ((int) (0.2 * kMaxScale * fs));
        static const double diffMs[2][4] { { 1.31, 2.27, 3.73, 5.91 }, { 1.43, 2.51, 3.97, 6.37 } };
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) { inAp[c][k].prepare ((int) (diffMs[c][k] * 0.001 * fs) + 1); }
        static const double bloomMs[2][4] { { 37.1, 53.3, 79.7, 113.9 }, { 41.3, 59.9, 83.1, 107.3 } };
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) { bloomAp[c][k].prepare ((int) (bloomMs[c][k] * kMaxScale * 0.001 * fs) + 1); bloomBase[c][k] = (float) (bloomMs[c][k] * 0.001 * fs); }
        shift.prepare (fs);
        current = -1; first = true;
        reset();
    }

    void reset()
    {
        for (auto& l : lines) l.clear();
        for (auto& a : loopAp) a.clear();
        for (auto& d : pre) d.clear();
        for (auto& e : er) e.clear();
        for (auto& c : inAp) for (auto& a : c) a.clear();
        for (auto& c : bloomAp) for (auto& a : c) a.clear();
        for (auto& o : lowSt) o.reset();
        for (auto& o : highSt) o.reset();
        for (auto& o : erLp) o.reset();
        for (auto* b : { &inHp[0], &inHp[1], &inLp[0], &inLp[1], &outHp[0], &outHp[1], &outLp[0], &outLp[1], &tilt[0], &tilt[1] }) b->reset();
        for (auto& m : monoSplit) m.reset();
        shift.clear(); shimLp.reset(); shimHp.reset();
        std::fill (std::begin (state), std::end (state), 0.0f);
    }

    void setParams (const Params& np) noexcept
    {
        p = np;
        p.algo = std::clamp (p.algo, 0, (int) algos().size() - 1);
        const auto& a = algos()[(size_t) p.algo];
        if (p.algo != current) { current = p.algo; buildAlgo (a); }
        sizeT = 0.25f + 0.95f * std::clamp (p.size, 0.0f, 1.0f);
        preT = (float) (std::clamp (p.preDelayMs, 0.0f, 500.0f) * 0.001 * fs);
        if (first) { sizeSm = sizeT; preSm = preT; first = false; injectSm = p.freeze ? 0.0f : 1.0f; }
        // wet filters
        for (int c = 0; c < 2; ++c)
        {
            inHp[c].hp (fs, std::clamp (p.lowCut * 0.5f, 10.0f, 600.0f), 0.6); inLp[c].lp (fs, std::clamp (p.highCut * 1.5f, 2000.0f, (float) (fs * 0.45)), 0.6);
            outHp[c].hp (fs, std::clamp (p.lowCut, 10.0f, 2000.0f), 0.70710678); outLp[c].lp (fs, std::clamp (p.highCut, 500.0f, (float) (fs * 0.45)), 0.70710678);
            tilt[c].highShelf (fs, 2500, a.bright * 1.5);
            erLp[c].setHz (fs, a.erLp);
            monoSplit[c].set (fs, std::max (20.0f, p.bassMono));
        }
        shift.setSemis (p.shimmerSemis);
        shimLp.setHz (fs, 7000); shimHp.hp (fs, 300, 0.7);
        for (int i = 0; i < kLines; ++i) lfo[i].setRate (fs, std::clamp (p.modRate, 0.02f, 8.0f) * (0.6f + 0.8f * (float) ((i * 5) % kLines) / (float) kLines));
        modT = std::clamp (p.modDepth, 0.0f, 1.0f) * a.modMs * 0.001f * (float) fs;
        diffG = 0.75f * std::clamp (p.diffusion, 0.0f, 1.0f);
        loopApG = a.apG * (0.4f + 0.6f * std::clamp (p.diffusion, 0.0f, 1.0f));
        updateDecay();
    }

    // stereo in -> stereo wet out (sample loop)
    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        const auto& a = algos()[(size_t) current];
        const float sc = 1.0f - (float) std::exp (-1.0 / (0.25 * fs));       // size / pre-delay glide
        const float fc = 1.0f - (float) std::exp (-1.0 / (0.03 * fs));
        const float injT = p.freeze ? 0.0f : 1.0f;
        const float shimAmt = p.freeze ? 0.0f : std::clamp (p.shimmer, 0.0f, 1.0f);
        const float bloom = a.bloom;
        const float erAmt = a.erAmt * std::clamp (p.early, 0.0f, 1.5f);
        const float w = std::clamp (p.width, 0.0f, 1.5f);
        for (int s = 0; s < n; ++s)
        {
            sizeSm += (sizeT - sizeSm) * sc; preSm += (preT - preSm) * sc; injectSm += (injT - injectSm) * fc;
            if ((s & 31) == 0) { lenScale = sizeSm; updateLoopLengths(); }

            // pre-delay + input band limit (the reverb is not fed mud / fizz)
            float xl = inHp[0].process (inLp[0].process (inL[s])), xr = inHp[1].process (inLp[1].process (inR[s]));
            pre[0].push (xl); pre[1].push (xr);
            const float pd = std::max (1.0f, preSm);
            xl = pre[0].readLinear (pd) * injectSm; xr = pre[1].readLinear (pd) * injectSm;

            // early reflections
            er[0].push (xl); er[1].push (xr);
            float el = 0, erv = 0;
            for (int k = 0; k < kEr; ++k)
            {
                const float d = std::max (1.0f, erDelay[k] * sizeSm);
                el += erGainL[k] * er[0].readLinear (d) + erCross[k] * er[1].readLinear (d * 1.07f);
                erv += erGainR[k] * er[1].readLinear (d * 1.031f) + erCross[k] * er[0].readLinear (d * 0.97f);
            }
            el = erLp[0].lp (el); erv = erLp[1].lp (erv);

            // shimmer: pitched copy of the tail back into the input
            float shim = 0;
            if (shimAmt > 0.0005f || shimActive > 0)
            {
                const float t = 0.5f * (state[0] + state[1]);
                shim = shimHp.process (shimLp.lp (shift.process (t)));
                shim = std::tanh (shim * 1.5f) * (0.66f * shimAmt);
                shimActive = shimAmt > 0.0005f ? 4096 : shimActive - 1;
            }

            // input diffusion (+ bloom: long diffusers -> slow swell)
            float dl = xl + shim, dr = xr + shim;
            if (bloom > 0)
            {
                float bl = dl, br = dr;
                for (int k = 0; k < 4; ++k) { bl = bloomAp[0][k].process (bl, bloomG); br = bloomAp[1][k].process (br, bloomG); }
                dl = dl * (1.0f - bloom) + bl * bloom; dr = dr * (1.0f - bloom) + br * bloom;
            }
            for (int k = 0; k < 4; ++k) { dl = inAp[0][k].process (dl, diffG); dr = inAp[1][k].process (dr, diffG); }

            // FDN
            float x[kLines];
            for (int i = 0; i < kLines; ++i)
                x[i] = lines[i].readCubic (len[i] + modT * (1.0f + lfo[i].next()));
            float lateL = 0, lateR = 0;
            for (int i = 0; i < kLines; ++i) { lateL += outA[i] * x[i]; lateR += outB[i] * x[i]; }
            for (int i = 0; i < kLines; ++i)
            {
                float y = loopAp[i].process (x[i], loopApG);
                // absorbent filter: low shelf / high shelf around the mid decay gain
                const float lo = lowSt[i].lp (y), hiPart = y - highSt[i].lp (y);
                y = y + (lowG[i] - 1.0f) * lo + (highG[i] - 1.0f) * hiPart;
                x[i] = y * midG[i];
            }
            hadamard (x);
            for (int i = 0; i < kLines; ++i) lines[i].push (x[i] + kInj * (inA[i] * dl + inB[i] * dr));
            state[0] = lateL; state[1] = lateR;

            // wet sum, colour, EQ
            float wl = kLate * a.wetGain * lateL + erAmt * kErGain * el;
            float wr = kLate * a.wetGain * lateR + erAmt * kErGain * erv;
            wl = tilt[0].process (wl); wr = tilt[1].process (wr);
            wl = outLp[0].process (outHp[0].process (wl)); wr = outLp[1].process (outHp[1].process (wr));
            // bass mono (keeps the low end of the reverb mono-safe)
            if (p.bassMono > 1.0f)
            {
                float ll, lh, rl, rh; monoSplit[0].split (wl, ll, lh); monoSplit[1].split (wr, rl, rh);
                const float m = 0.5f * (ll + rl); wl = m + lh; wr = m + rh;
            }
            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * w;
            outL[s] = (mid + side) * normGain; outR[s] = (mid - side) * normGain;
        }
    }

    // analysis helpers (UI / tests)
    float rtAt (double f) const
    {
        const double lo = p.decay * p.bassMult, mid = p.decay, hi = p.decay * p.trebleMult;
        const double wl = 1.0 / (1.0 + std::pow (f / p.xLow, 2.0)), wh = 1.0 / (1.0 + std::pow (p.xHigh / f, 2.0));
        return (float) (mid + (lo - mid) * wl + (hi - mid) * wh);
    }
    const Params& params() const noexcept { return p; }

private:
    static constexpr float kMaxScale = 1.2f, kInj = 0.25f, kLate = 0.55f, kErGain = 0.9f;
    static constexpr int kEr = 24;

    static inline void hadamard (float* x) noexcept
    {
        for (int h = 1; h < kLines; h <<= 1)
            for (int i = 0; i < kLines; i += h * 2)
                for (int j = i; j < i + h; ++j) { const float a = x[j], b = x[j + h]; x[j] = a + b; x[j + h] = a - b; }
        for (int i = 0; i < kLines; ++i) x[i] *= 0.25f;
    }

    void buildAlgo (const Algo& a)
    {
        // line lengths: log-spaced, de-correlated by a fixed irrational jitter
        for (int i = 0; i < kLines; ++i)
        {
            const double t = (double) i / (kLines - 1);
            const double ms = a.lenMin * std::pow ((double) a.lenMax / a.lenMin, t) * (1.0 + 0.07 * std::sin (i * 2.399963));
            baseLen[i] = (float) (ms * 0.001 * fs);
            const double ap = a.apMin + (a.apMax - a.apMin) * std::fmod (i * 0.618034, 1.0);
            baseAp[i] = (float) (ap * 0.001 * fs);
        }
        // shuffle so that neighbouring Hadamard partners have very different lengths
        static const int order[kLines] { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
        float tmp[kLines]; for (int i = 0; i < kLines; ++i) tmp[i] = baseLen[order[i]];
        std::copy (tmp, tmp + kLines, baseLen);
        // orthogonal in / out vectors (rows of a 16x16 Hadamard matrix)
        auto hrow = [] (int r, int c) { unsigned v = (unsigned) (r & c), bits = 0; while (v) { bits += v & 1u; v >>= 1; } return (bits & 1u) ? -1.0f : 1.0f; };
        for (int i = 0; i < kLines; ++i) { inA[i] = hrow (3, i); inB[i] = hrow (5, i); outA[i] = hrow (6, i) * 0.25f; outB[i] = hrow (9, i) * 0.25f; }
        // early reflections: deterministic pseudo-random pattern, density grows with time, gains fall
        uint32_t seed = 0x9e3779b9u ^ (uint32_t) (current * 7919);
        auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; };
        for (int k = 0; k < kEr; ++k)
        {
            const float t = std::sqrt (((float) k + rnd()) / (float) kEr);
            const float ms = a.erStart + (a.erEnd - a.erStart) * t;
            erDelay[k] = ms * 0.001f * (float) fs;
            const float g = std::exp (-2.2f * t) * (0.6f + 0.4f * rnd());
            erGainL[k] = g * (rnd() < 0.5f ? -1.0f : 1.0f) * (0.6f + 0.4f * rnd());
            erGainR[k] = g * (rnd() < 0.5f ? -1.0f : 1.0f) * (0.6f + 0.4f * rnd());
            erCross[k] = 0.25f * g * (rnd() < 0.5f ? -1.0f : 1.0f);
        }
        float e = 0; for (int k = 0; k < kEr; ++k) e += erGainL[k] * erGainL[k] + erCross[k] * erCross[k];
        const float n = 1.0f / std::sqrt (std::max (1e-6f, e));
        for (int k = 0; k < kEr; ++k) { erGainL[k] *= n; erGainR[k] *= n; erCross[k] *= n; }
        lenScale = sizeSm; updateLoopLengths();
    }

    void updateLoopLengths() noexcept
    {
        for (int i = 0; i < kLines; ++i) { len[i] = std::max (4.0f, baseLen[i] * lenScale); loopAp[i].setLength ((int) (baseAp[i] * lenScale)); }
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) bloomAp[c][k].setLength ((int) (bloomBase[c][k] * lenScale));
        if (std::abs (lenScale - decayScale) > 0.002f) updateDecay();
    }

    void updateDecay() noexcept
    {
        decayScale = lenScale;
        const double rtMid = std::max (0.05, (double) p.decay) * kRtCal;
        const double rtLo = rtMid * std::clamp ((double) p.bassMult, 0.1, 4.0), rtHi = rtMid * std::clamp ((double) p.trebleMult, 0.02, 2.0);
        for (int i = 0; i < kLines; ++i)
        {
            const double L = (double) len[i] + (double) baseAp[i] * lenScale;
            auto g = [&] (double rt) { return std::pow (10.0, -3.0 * L / (rt * fs)); };
            if (p.freeze) { midG[i] = 1.0f; lowG[i] = 1.0f; highG[i] = 1.0f; }
            else
            {
                const double gm = g (rtMid);
                midG[i] = (float) gm; lowG[i] = (float) (g (rtLo) / gm); highG[i] = (float) (g (rtHi) / gm);
            }
            lowSt[i].setHz (fs, std::clamp ((double) p.xLow, 40.0, 2000.0));
            highSt[i].setHz (fs, std::clamp ((double) p.xHigh, 500.0, fs * 0.45));
        }
        // keep the wet level roughly constant across decay times (long tails would otherwise get much louder)
        bloomG = (float) (0.62 * std::clamp ((double) p.decay / 3.0, 0.25, 1.0));
        normGain = (float) (1.0 / std::pow (std::max (0.2, (double) p.decay) / 2.0, 0.25));
        if (p.freeze) normGain = 1.0f;
    }

    static constexpr double kRtCal = 1.0;

    double fs = 48000; Params p; int current = -1; bool first = true;
    Line lines[kLines]; Allpass loopAp[kLines]; RandomLfo lfo[kLines];
    OnePole lowSt[kLines], highSt[kLines];
    float baseLen[kLines] {}, baseAp[kLines] {}, len[kLines] {}, midG[kLines] {}, lowG[kLines] {}, highG[kLines] {};
    float inA[kLines] {}, inB[kLines] {}, outA[kLines] {}, outB[kLines] {};
    Line pre[2], er[2]; Allpass inAp[2][4], bloomAp[2][4]; float bloomBase[2][4] {};
    float erDelay[kEr] {}, erGainL[kEr] {}, erGainR[kEr] {}, erCross[kEr] {};
    OnePole erLp[2];
    Biquad inHp[2], inLp[2], outHp[2], outLp[2], tilt[2];
    LR4 monoSplit[2];
    PitchShifter shift; OnePole shimLp; Biquad shimHp; int shimActive = 0;
    float state[2] {};
    float sizeT = 1, sizeSm = 1, preT = 0, preSm = 0, injectSm = 1, lenScale = 1, decayScale = -1, modT = 0, diffG = 0.6f, loopApG = 0.5f, normGain = 1, bloomG = 0.62f;
};

} // namespace lumen::rv2
