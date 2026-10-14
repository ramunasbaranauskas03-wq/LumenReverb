#pragma once
#include "DelayLine.h"
#include <cmath>
#include <algorithm>

namespace lumen
{

constexpr float kPiD = 3.14159265358979323846f;

// TPT state-variable filter (Simper / Zavalishin)
struct SVF
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1.41421356f, ic1 = 0, ic2 = 0;

    void set (float fc, float fs, float q = 0.70710678f) noexcept
    {
        fc = std::clamp (fc, 10.0f, 0.45f * fs);
        const float g = std::tan (kPiD * fc / fs);
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

struct AllpassD
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
// Stereo tape-style delay. Feedback loop: delay -> HP/LP -> saturation ->
// diffusion -> feedback gain. Modes: Stereo, Ping-Pong, Mono. Returns wet only.
class DelayEngine
{
public:
    enum Mode { Stereo = 0, PingPong = 1, Mono = 2 };
    static constexpr float kMaxSeconds = 6.0f;

    struct Params
    {
        float timeL      = 0.375f;  // seconds
        float timeR      = 0.375f;  // seconds
        float feedback   = 0.45f;   // 0..1
        int   mode       = Stereo;
        float lowCutHz   = 150.0f;
        float highCutHz  = 8000.0f;
        float drive      = 0.2f;    // 0..1
        float wow        = 0.2f;    // 0..1
        float modRate    = 0.6f;    // Hz
        float diffusion  = 0.0f;    // 0..1
        float width      = 1.0f;    // 0..1.5
        bool  freeze     = false;
    };

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        const int maxLen = (int) (kMaxSeconds * fs) + (int) (0.02f * fs) + 16;
        lineL.prepare (maxLen);
        lineR.prepare (maxLen);

        static constexpr float apMs[2][2] = { { 7.13f, 11.37f }, { 7.71f, 12.09f } };
        for (int k = 0; k < 2; ++k)
        {
            apL[k].prepare ((int) (apMs[0][k] * 0.001f * fs));
            apR[k].prepare ((int) (apMs[1][k] * 0.001f * fs));
        }

        glideCoef  = 1.0f - std::exp (-1.0f / (0.12f * fs));  // tape-like time glide
        smoothCoef = 1.0f - std::exp (-1.0f / (0.03f * fs));
        reset();
        first = true;
    }

    void reset()
    {
        lineL.clear(); lineR.clear();
        for (auto& a : apL) a.clear();
        for (auto& a : apR) a.clear();
        hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset();
        phase = 0.0f; fphase = 0.0f;
    }

    void setParams (const Params& p) noexcept { params = p; }

    void beginBlock() noexcept
    {
        const auto& p = params;
        const float maxD = kMaxSeconds * fs;
        tL = std::clamp (p.timeL * fs, 4.0f, maxD);
        tR = std::clamp (p.timeR * fs, 4.0f, maxD);
        fbTarget     = std::clamp (p.feedback, 0.0f, 1.0f);
        freezeTarget = p.freeze ? 1.0f : 0.0f;
        mode         = p.mode;

        if (first)
        {
            dL = tL; dR = tR; fbSm = fbTarget; freezeSm = freezeTarget;
            first = false;
        }

        hpL.set (p.lowCutHz, fs);  hpR.set (p.lowCutHz, fs);
        lpL.set (p.highCutHz, fs); lpR.set (p.highCutHz, fs);

        const float drive = std::clamp (p.drive, 0.0f, 1.0f);
        satGain = 0.5f + 4.5f * drive * drive;
        satInv  = 1.0f / satGain;

        wowDepth  = std::clamp (p.wow, 0.0f, 1.0f) * 0.0035f * fs;
        flutDepth = std::clamp (p.wow, 0.0f, 1.0f) * 0.00025f * fs;
        phaseInc  = 2.0f * kPiD * std::clamp (p.modRate, 0.01f, 10.0f) / fs;
        fphaseInc = 2.0f * kPiD * 6.3f / fs;
        diffG     = 0.65f * std::clamp (p.diffusion, 0.0f, 1.0f);
        width     = std::clamp (p.width, 0.0f, 1.5f);
    }

    inline void processSample (float inL, float inR, float& outL, float& outR) noexcept
    {
        dL += (tL - dL) * glideCoef;
        dR += (tR - dR) * glideCoef;
        fbSm     += (fbTarget - fbSm) * smoothCoef;
        freezeSm += (freezeTarget - freezeSm) * smoothCoef;

        phase  += phaseInc;  if (phase  > 2.0f * kPiD) phase  -= 2.0f * kPiD;
        fphase += fphaseInc; if (fphase > 2.0f * kPiD) fphase -= 2.0f * kPiD;
        const float wowL = wowDepth * (1.0f + std::sin (phase))        + flutDepth * (1.0f + std::sin (fphase));
        const float wowR = wowDepth * (1.0f + std::sin (phase + 1.7f)) + flutDepth * (1.0f + std::sin (fphase + 2.3f));

        const float yL = lineL.readCubic (dL + wowL);
        const float yR = lineR.readCubic ((mode == Mono ? dL + wowL : dR + wowR));

        // Tone + saturation (bypassed progressively when frozen)
        float lo, hi, zL, zR;
        hpL.process (yL, lo, hi); lpL.process (hi, lo, hi); zL = lo;
        hpR.process (yR, lo, hi); lpR.process (hi, lo, hi); zR = lo;
        // Soft saturation, unity gain for small signals: tanh(x*G)/G
        zL = std::tanh (zL * satGain) * satInv;
        zR = std::tanh (zR * satGain) * satInv;

        if (diffG > 0.001f)
        {
            for (auto& a : apL) zL = a.process (zL, diffG);
            for (auto& a : apR) zR = a.process (zR, diffG);
        }

        const float pL = zL + freezeSm * (yL - zL);
        const float pR = zR + freezeSm * (yR - zR);

        const float g   = fbSm + freezeSm * (1.0f - fbSm);
        const float inG = 1.0f - freezeSm;

        switch (mode)
        {
            case PingPong:
                lineL.push (0.5f * (inL + inR) * inG + g * pR);
                lineR.push (g * pL);
                break;
            case Mono:
            {
                const float m = 0.5f * (inL + inR) * inG + g * pL;
                lineL.push (m);
                lineR.push (m);
                break;
            }
            case Stereo:
            default:
                lineL.push (inL * inG + g * pL);
                lineR.push (inR * inG + g * pR);
                break;
        }

        float wl = pL, wr = (mode == Mono ? pL : pR);
        const float mid  = 0.5f * (wl + wr);
        const float side = 0.5f * (wl - wr) * width;
        outL = mid + side;
        outR = mid - side;
    }

private:
    Params params;
    float fs = 48000.0f;
    bool first = true;

    DelayLine lineL, lineR;
    AllpassD apL[2], apR[2];
    SVF hpL, hpR, lpL, lpR;

    float tL = 1000, tR = 1000, dL = 1000, dR = 1000;
    float fbTarget = 0.45f, fbSm = 0.45f, freezeTarget = 0, freezeSm = 0;
    float glideCoef = 0.001f, smoothCoef = 0.001f;
    float satGain = 0.5f, satInv = 2.0f;
    float wowDepth = 0, flutDepth = 0, phase = 0, phaseInc = 0, fphase = 0, fphaseInc = 0;
    float diffG = 0, width = 1.0f;
    int mode = Stereo;
};

} // namespace lumen
