#pragma once
#include "DelayLine.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace lumen
{

constexpr float kPi = 3.14159265358979323846f;

//==============================================================================
struct OnePoleLP
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (float fc, float fs) noexcept { a = std::exp (-2.0f * kPi * fc / fs); }
    void bypass() noexcept { a = 0.0f; }
    inline float process (float x) noexcept { z = x + a * (z - x); return z; }
    void reset() noexcept { z = 0.0f; }
};

// Topology-preserving-transform state variable filter (Simper / Zavalishin).
struct SVF
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1.41421356f;
    float ic1 = 0, ic2 = 0;

    void set (float fc, float fs, float q = 0.70710678f) noexcept
    {
        fc = std::clamp (fc, 10.0f, 0.45f * fs);
        const float g = std::tan (kPi * fc / fs);
        k  = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline void process (float v0, float& low, float& high) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low  = v2;
        high = v0 - k * v1 - v2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }
};

struct Allpass
{
    std::vector<float> buf;
    int idx = 0;

    void prepare (int len) { buf.assign ((size_t) std::max (1, len), 0.0f); idx = 0; }
    void clear() noexcept  { std::fill (buf.begin(), buf.end(), 0.0f); idx = 0; }

    inline float process (float x, float g) noexcept
    {
        const float d = buf[(size_t) idx];
        const float v = x - g * d;
        const float y = d + g * v;
        buf[(size_t) idx] = v;
        if (++idx >= (int) buf.size()) idx = 0;
        return y;
    }
};

//==============================================================================
// Stereo reverb: pre-delay -> early reflections + input diffusion -> 16-line
// modulated feedback delay network (Hadamard mixing) -> wet EQ -> width.
// Returns the wet signal only.
class ReverbEngine
{
public:
    static constexpr int N = 16;
    static constexpr int numTaps = 12;

    struct Params
    {
        float size       = 0.6f;    // 0..1
        float decay      = 2.5f;    // RT60 seconds
        float preDelayMs = 20.0f;
        float dampHz     = 8000.0f;
        float lowCutHz   = 120.0f;
        float highCutHz  = 12000.0f;
        float early      = 0.5f;    // 0..1
        float diffusion  = 0.75f;   // 0..1
        float width      = 1.0f;    // 0..1.5
        float modDepth   = 0.3f;    // 0..1
        float modRate    = 0.6f;    // Hz
        bool  freeze     = false;
    };

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;

        static constexpr float baseMs[N] = { 23.03f, 25.39f, 27.86f, 30.71f, 33.79f, 37.19f, 40.93f, 45.07f,
                                             49.61f, 54.59f, 60.07f, 66.13f, 72.79f, 80.11f, 88.19f, 97.07f };
        for (int i = 0; i < N; ++i)
        {
            baseSamp[i] = baseMs[i] * 0.001f * fs;
            lines[i].prepare ((int) (baseSamp[i] * kMaxScale + 0.003f * fs) + 16);
            rateMult[i] = 0.7f + 0.6f * (float) ((i * 7) % N) / (float) (N - 1);
            lfoRe[i] = std::cos (2.0f * kPi * (float) i / (float) N);
            lfoIm[i] = std::sin (2.0f * kPi * (float) i / (float) N);
        }

        preL.prepare ((int) (0.3f * fs));
        preR.prepare ((int) (0.3f * fs));
        erL.prepare  ((int) (0.12f * fs));
        erR.prepare  ((int) (0.12f * fs));

        static constexpr float tapMsL[numTaps] = { 3.1f, 5.7f, 8.9f, 12.3f, 15.8f, 19.6f, 24.1f, 28.7f, 33.9f, 39.4f, 46.2f, 53.5f };
        static constexpr float tapMsR[numTaps] = { 4.2f, 6.6f, 10.1f, 13.4f, 17.3f, 21.2f, 25.9f, 30.8f, 36.1f, 42.0f, 48.7f, 56.8f };
        static constexpr float signL[numTaps]  = { 1, -1, 1, 1, -1, 1, -1, -1, 1, -1, 1, -1 };
        static constexpr float signR[numTaps]  = { -1, 1, 1, -1, 1, -1, 1, 1, -1, 1, -1, 1 };
        for (int k = 0; k < numTaps; ++k)
        {
            const float g = 0.42f * std::exp (-0.17f * (float) k);
            tapSampL[k] = tapMsL[k] * 0.001f * fs;
            tapSampR[k] = tapMsR[k] * 0.001f * fs;
            tapGainL[k] = g * signL[k];
            tapGainR[k] = g * signR[k];
        }

        static constexpr float apMs[2][4] = { { 4.77f, 3.59f, 12.73f, 9.30f }, { 4.93f, 3.71f, 12.31f, 9.83f } };
        for (int k = 0; k < 4; ++k)
        {
            apL[k].prepare ((int) (apMs[0][k] * 0.001f * fs));
            apR[k].prepare ((int) (apMs[1][k] * 0.001f * fs));
        }

        reset();
        first = true;
    }

    void reset()
    {
        for (auto& l : lines) l.clear();
        for (auto& d : damp)  d.reset();
        preL.clear(); preR.clear(); erL.clear(); erR.clear();
        for (auto& a : apL) a.clear();
        for (auto& a : apR) a.clear();
        hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset();
        dcxL = dcyL = dcxR = dcyR = 0.0f;
    }

    void setParams (const Params& p) noexcept { params = p; }

    // Call once per block before processSample().
    void beginBlock (int numSamples) noexcept
    {
        const auto& p = params;
        sizeTarget = 0.4f + 1.2f * std::clamp (p.size, 0.0f, 1.0f);
        preTarget  = std::clamp (p.preDelayMs, 0.0f, 250.0f) * 0.001f * fs;
        injectTarget = p.freeze ? 0.0f : 1.0f;

        if (first)
        {
            sizeSm = sizeTarget; preSm = preTarget; decaySm = p.decay;
            injectSm = injectTarget; earlySm = p.early;
            first = false;
        }

        const float blockCoef = 1.0f - std::exp (-(float) numSamples / (0.08f * fs));
        decaySm += (p.decay - decaySm) * blockCoef;
        earlySm += (p.early - earlySm) * blockCoef;
        smoothCoef = 1.0f - std::exp (-1.0f / (0.12f * fs));
        fastCoef   = 1.0f - std::exp (-1.0f / (0.02f * fs));

        const float rt = std::max (0.05f, decaySm) * kRtCal; // calibrated against measured T30
        for (int i = 0; i < N; ++i)
        {
            const float d = baseSamp[i] * sizeSm;
            gain[i] = p.freeze ? 1.0f : std::pow (10.0f, -3.0f * d / (rt * fs));
            if (p.freeze) damp[i].bypass();
            else          damp[i].setCutoff (std::clamp (p.dampHz, 500.0f, 0.45f * fs), fs);

            const float w = 2.0f * kPi * p.modRate * rateMult[i] / fs;
            lfoCos[i] = std::cos (w);
            lfoSin[i] = std::sin (w);
            const float mag = std::sqrt (lfoRe[i] * lfoRe[i] + lfoIm[i] * lfoIm[i]);
            lfoRe[i] /= mag; lfoIm[i] /= mag;
        }

        modSamp = std::clamp (p.modDepth, 0.0f, 1.0f) * 0.0006f * fs;
        diffG   = 0.72f * std::clamp (p.diffusion, 0.0f, 1.0f);
        width   = std::clamp (p.width, 0.0f, 1.5f);

        hpL.set (p.lowCutHz, fs); hpR.set (p.lowCutHz, fs);
        lpL.set (p.highCutHz, fs); lpR.set (p.highCutHz, fs);
    }

    inline void processSample (float inL, float inR, float& outL, float& outR) noexcept
    {
        sizeSm   += (sizeTarget - sizeSm) * smoothCoef;
        preSm    += (preTarget - preSm) * smoothCoef;
        injectSm += (injectTarget - injectSm) * fastCoef;

        // DC blocker
        const float xl = inL - dcxL + 0.995f * dcyL; dcxL = inL; dcyL = xl;
        const float xr = inR - dcxR + 0.995f * dcyR; dcxR = inR; dcyR = xr;

        // Pre-delay
        preL.push (xl); preR.push (xr);
        const float pl = preL.readLinear (1.0f + preSm) * injectSm;
        const float pr = preR.readLinear (1.0f + preSm) * injectSm;

        // Early reflections
        erL.push (pl); erR.push (pr);
        float eL = 0.0f, eR = 0.0f;
        for (int k = 0; k < numTaps; ++k)
        {
            eL += tapGainL[k] * erL.readLinear (tapSampL[k] * sizeSm);
            eR += tapGainR[k] * erR.readLinear (tapSampR[k] * sizeSm);
        }
        const float eMixL = eL + 0.25f * eR;
        const float eMixR = eR + 0.25f * eL;

        // Input diffusion
        float dl = pl, dr = pr;
        for (int k = 0; k < 4; ++k)
        {
            dl = apL[k].process (dl, diffG);
            dr = apR[k].process (dr, diffG);
        }

        // Feedback delay network
        float x[N];
        for (int i = 0; i < N; ++i)
        {
            const float re = lfoRe[i] * lfoCos[i] - lfoIm[i] * lfoSin[i];
            const float im = lfoRe[i] * lfoSin[i] + lfoIm[i] * lfoCos[i];
            lfoRe[i] = re; lfoIm[i] = im;
            x[i] = lines[i].readCubic (baseSamp[i] * sizeSm + modSamp * (1.0f + im));
        }

        float lateL = 0.0f, lateR = 0.0f;
        for (int i = 0; i < N; i += 2)
        {
            lateL += kOutSign[i] * x[i];
            lateR += kOutSign[i + 1] * x[i + 1];
        }

        for (int i = 0; i < N; ++i)
            x[i] = damp[i].process (x[i]) * gain[i];

        hadamard (x);

        for (int i = 0; i < N; ++i)
        {
            const float in = (i & 1) ? dr : dl;
            lines[i].push (x[i] + kInject * kInSign[i] * in);
        }

        // Sum, EQ, width
        float wl = earlySm * eMixL + kLateGain * lateL;
        float wr = earlySm * eMixR + kLateGain * lateR;

        float lo, hi;
        hpL.process (wl, lo, hi); wl = hi;
        hpR.process (wr, lo, hi); wr = hi;
        lpL.process (wl, lo, hi); wl = lo;
        lpR.process (wr, lo, hi); wr = lo;

        const float mid  = 0.5f * (wl + wr);
        const float side = 0.5f * (wl - wr) * width;
        outL = mid + side;
        outR = mid - side;
    }

private:
    static inline void hadamard (float* x) noexcept
    {
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h * 2)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[j], b = x[j + h];
                    x[j] = a + b;
                    x[j + h] = a - b;
                }
        for (int i = 0; i < N; ++i)
            x[i] *= 0.25f;
    }

    static constexpr float kMaxScale = 1.6f;
    static constexpr float kInject   = 0.35f;
    static constexpr float kLateGain = 0.6f;
    static constexpr float kRtCal    = 1.12f;
    static constexpr float kOutSign[N] = { 1, 1, -1, 1, -1, -1, 1, -1, 1, -1, -1, 1, -1, 1, 1, -1 };
    static constexpr float kInSign[N]  = { 1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, -1, -1, 1, -1 };

    Params params;
    float fs = 48000.0f;
    bool first = true;

    DelayLine lines[N];
    OnePoleLP damp[N];
    float baseSamp[N] {}, gain[N] {}, rateMult[N] {};
    float lfoRe[N] {}, lfoIm[N] {}, lfoCos[N] {}, lfoSin[N] {};

    DelayLine preL, preR, erL, erR;
    float tapSampL[numTaps] {}, tapSampR[numTaps] {}, tapGainL[numTaps] {}, tapGainR[numTaps] {};
    Allpass apL[4], apR[4];
    SVF hpL, hpR, lpL, lpR;

    float sizeTarget = 1.0f, sizeSm = 1.0f, preTarget = 0.0f, preSm = 0.0f;
    float injectTarget = 1.0f, injectSm = 1.0f, decaySm = 2.5f, earlySm = 0.5f;
    float smoothCoef = 0.001f, fastCoef = 0.001f;
    float modSamp = 0.0f, diffG = 0.5f, width = 1.0f;
    float dcxL = 0, dcyL = 0, dcxR = 0, dcyR = 0;
};

} // namespace lumen
