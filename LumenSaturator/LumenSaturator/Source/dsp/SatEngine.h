#pragma once
#include "PumpEngine.h"   // SVFp, LR4
#include <cmath>
#include <algorithm>

namespace lumen
{

enum SatMode { Tape = 0, Tube, SoftClip, HardClip, Wavefold };

// Static shapers. Input already multiplied by drive; output roughly within +-1.
inline float shape (int mode, float u) noexcept
{
    switch (mode)
    {
        case Tape:
        {
            const float t = std::tanh (u);
            return t - 0.06f * t * t;                       // soft with a touch of even harmonics
        }
        case Tube:
        {
            // asymmetric: positive half saturates harder -> 2nd harmonic
            return u >= 0.0f ? 1.0f - std::exp (-u) : -0.8f * (1.0f - std::exp (u / 0.8f));
        }
        case SoftClip:
        {
            // cubic soft clip: slope 1 at 0, reaches exactly +-1 (with zero slope) at |u| = 1.5
            const float v = std::clamp (u / 1.5f, -1.0f, 1.0f);
            return 1.5f * (v - v * v * v / 3.0f);
        }
        case HardClip:
            return std::clamp (u, -1.0f, 1.0f);
        case Wavefold:
        default:
            return std::sin (u);                            // unity slope at 0, folds back once |u| > pi/2
    }
}

//==============================================================================
// Three-band saturator running at the (oversampled) rate it is prepared with.
class SatEngine
{
public:
    struct Band { int mode = Tape; float driveDb = 6.0f, mix = 1.0f, levelDb = 0.0f; bool solo = false; };
    struct Params { Band band[3]; float xLow = 150.0f, xHigh = 3500.0f, outDb = 0.0f; bool autoGain = true; };

    void prepare (double rate)
    {
        fs = (float) rate;
        smooth = 1.0f - std::exp (-1.0f / (0.02f * fs));
        loudCoef = 1.0f - std::exp (-1.0f / (0.8f * fs));
        dcCoef = 1.0f - std::exp (-2.0f * 3.14159265f * 8.0f / fs);
        for (auto& c : ch) c = {};
        for (int b = 0; b < 3; ++b) { gDrive[b] = 1.0f; gLevel[b] = 1.0f; gMix[b] = 1.0f; comp[b] = 1.0f; pin[b] = pout[b] = 1.0e-9f; }
        gOut = 1.0f; first = true;
        setCrossovers (150.0f, 3500.0f);
    }

    void setCrossovers (float lo, float hi)
    {
        lo = std::clamp (lo, 20.0f, 0.2f * fs);
        hi = std::clamp (std::max (hi, lo * 1.5f), 40.0f, 0.4f * fs);
        for (auto& c : ch) { c.x1.set (lo, fs); c.x2.set (hi, fs); c.ap.set (hi, fs); }
    }

    void beginBlock (const Params& p) noexcept
    {
        params = p;
        for (int b = 0; b < 3; ++b)
        {
            tDrive[b] = std::pow (10.0f, p.band[b].driveDb / 20.0f);
            tLevel[b] = std::pow (10.0f, p.band[b].levelDb / 20.0f);
            tMix[b]   = std::clamp (p.band[b].mix, 0.0f, 1.0f);
            // auto gain: match each band's wet loudness to its dry loudness (slow, program-dependent)
            tComp[b] = p.autoGain ? std::clamp (std::sqrt (pin[b] / std::max (pout[b], 1.0e-12f)), 0.06f, 4.0f) : 1.0f;
        }
        tOut = std::pow (10.0f, p.outDb / 20.0f);
        anySolo = p.band[0].solo || p.band[1].solo || p.band[2].solo;
        if (first)
        {
            for (int b = 0; b < 3; ++b) { gDrive[b] = tDrive[b]; gLevel[b] = tLevel[b]; gMix[b] = tMix[b]; comp[b] = tComp[b]; }
            gOut = tOut; first = false;
        }
    }

    inline void process (float* l, float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            for (int b = 0; b < 3; ++b)
            {
                gDrive[b] += (tDrive[b] - gDrive[b]) * smooth;
                gLevel[b] += (tLevel[b] - gLevel[b]) * smooth;
                gMix[b]   += (tMix[b]   - gMix[b])   * smooth;
                comp[b]   += (tComp[b]  - comp[b])   * smooth * 0.1f;
            }
            gOut += (tOut - gOut) * smooth;

            float* io[2] = { &l[i], &r[i] };
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[c];
                float lo, rest, mid, hi, a1, a2;
                s.x1.process (*io[c], lo, rest);
                s.x2.process (rest, mid, hi);
                s.ap.process (lo, a1, a2);
                lo = a1 + a2;
                float band[3] = { lo, mid, hi };

                float sum = 0.0f;
                for (int b = 0; b < 3; ++b)
                {
                    const float dry = band[b];
                    float wet = shape (params.band[b].mode, dry * gDrive[b]);
                    // DC blocker (asymmetric modes)
                    s.dcY[b] = wet - s.dcX[b] + (1.0f - dcCoef) * s.dcY[b];
                    s.dcX[b] = wet;
                    wet = s.dcY[b];

                    if (c == 0)   // loudness tracking on the left channel is enough
                    {
                        pin[b]  += (dry * dry - pin[b])  * loudCoef;
                        pout[b] += (wet * wet - pout[b]) * loudCoef;
                    }
                    wet *= comp[b];
                    float y = (dry + (wet - dry) * gMix[b]) * gLevel[b];
                    if (anySolo && ! params.band[b].solo) y = 0.0f;
                    sum += y;
                }
                *io[c] = sum * gOut;
            }
        }
    }

private:
    struct Channel { LR4 x1, x2, ap; float dcX[3] {}, dcY[3] {}; };
    Params params;
    Channel ch[2];
    float fs = 48000.0f, smooth = 0.01f, loudCoef = 1e-5f, dcCoef = 0.001f;
    float tDrive[3] {}, tLevel[3] {}, tMix[3] {}, tComp[3] {}, gDrive[3] {}, gLevel[3] {}, gMix[3] {}, comp[3] {};
    float pin[3] {}, pout[3] {};
    float tOut = 1.0f, gOut = 1.0f;
    bool anySolo = false, first = true;
};

} // namespace lumen
