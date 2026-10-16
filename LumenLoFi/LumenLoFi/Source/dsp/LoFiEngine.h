#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdint>

// =====================================================================================
// Lumen Lo-Fi — retro character processor
//   SATURATE (ADAA, alias-free without oversampling) -> NOISE (+duck/follow) -> WOBBLE (wow/flutter/drift)
//   -> MAGNETIC (wear, head bump, dropouts) -> DIGITAL (rate/bits/vintage filter) -> TONE -> SPACE -> STEREO
//   AGE scales every module from clean (0 %) to destroyed (200 %); FLUX makes wobble/dropouts follow the dynamics.
// =====================================================================================
namespace lumen::lofi
{

struct Params
{
    float age = 1.0f;
    bool satOn = true; int satType = 0; float drive = 6, bias = 0.2f;
    bool noiseOn = true; int noiseType = 0; float noiseDb = -42, noiseTone = 0.5f, noiseDuck = 0;
    bool wobOn = true; float wow = 0.4f, flutter = 0.3f, wobRate = 0.6f, drift = 0.4f, wobStereo = 0.3f;
    bool magOn = true; float wear = 0.4f, bump = 0.4f, dropouts = 0.2f;
    bool digOn = false; float rate = 26000, bits = 12, digFilter = 0.6f;
    bool spaceOn = false; float size = 0.4f, decay = 0.5f, spaceMix = 0.2f;
    float lowCut = 20, highCut = 20000, width = 1; bool monoBass = false;
    float flux = 0, mix = 1, outDb = 0;
};

inline constexpr int kWobLatencyMs = 5;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process (float x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
    void lp (double fs, double f, double q) { const double w = 2 * juce::MathConstants<double>::pi * std::min (f, fs * 0.49) / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al; b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void hp (double fs, double f, double q) { const double w = 2 * juce::MathConstants<double>::pi * std::min (f, fs * 0.49) / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al; b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void bp (double fs, double f, double q) { const double w = 2 * juce::MathConstants<double>::pi * std::min (f, fs * 0.49) / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al; b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void peak (double fs, double f, double q, double db) { const double A = std::pow (10.0, db / 40.0), w = 2 * juce::MathConstants<double>::pi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al / A; b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0; a1 = -2 * c / a0; a2 = (1 - al / A) / a0; }
    void unity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
};

// ---------------- ADAA saturation ----------------
inline double logcosh (double u) { const double a = std::abs (u); return a + std::log1p (std::exp (-2.0 * a)) - 0.6931471805599453; }
struct Shaper
{
    int type = 0; double b = 0;
    double f (double u) const
    {
        switch (type)
        {
            case 0: return std::tanh (u);
            case 1: return std::tanh (u + b) - std::tanh (b);
            case 2: return u / (1.0 + std::abs (u));
            default: return std::clamp (u, -1.0, 1.0);
        }
    }
    double F (double u) const
    {
        switch (type)
        {
            case 0: return logcosh (u);
            case 1: return logcosh (u + b) - std::tanh (b) * u;
            case 2: { const double a = std::abs (u); return a - std::log1p (a); }
            default: { const double a = std::abs (u); return a <= 1.0 ? 0.5 * u * u : a - 0.5; }
        }
    }
};

class LoFiEngine
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        L = (int) std::round (kWobLatencyMs * 0.001 * fs);
        int n = 1; while (n < 4 * L + 16) n <<= 1;
        for (auto& b : wob) b.assign ((size_t) n, 0.0f);
        for (auto& b : dryDl) b.assign ((size_t) n, 0.0f);
        wMask = n - 1; wPos = 0;
        // space: freeverb-lite
        static const int combMs[4] = { 2557, 2903, 3251, 3559 }, apMs[2] = { 556, 441 };
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < 4; ++k) { combs[c][k].assign ((size_t) (combMs[k] * fs / 44100.0 * 1.6 + 40 + c * 23), 0.0f); combPos[c][k] = 0; combLp[c][k] = 0; }
            for (int k = 0; k < 2; ++k) { aps[c][k].assign ((size_t) (apMs[k] * fs / 44100.0 + c * 11 + 5), 0.0f); apPos[c][k] = 0; }
        }
        for (auto& s : st) s = ChanState();
        wowPh = 0; flPh = 0.25; driftV = driftT = 0; driftCnt = 0;
        env = 0; holdPh = 0; drop = DropState(); gainWander = 0; wanderT = 0;
        crackEnv = 0; humPh = 0;
        lastHash = -1;
        outSm = std::pow (10.0f, p.outDb / 20.0f);
    }
    int latency() const noexcept { return wobActive() ? L : 0; }
    void setParams (const Params& np) noexcept { p = np; }

    void process (float* Lp, float* Rp, int n) noexcept
    {
        updateFilters();
        const float a = std::clamp (p.age, 0.0f, 2.0f);
        const bool wobOn = wobActive();
        const float sm = 1.0f - std::exp (-1.0f / (0.02f * (float) fs));
        const float envA = 1.0f - std::exp (-1.0f / (0.005f * (float) fs)), envR = 1.0f - std::exp (-1.0f / (0.12f * (float) fs));
        const float outT = std::pow (10.0f, p.outDb / 20.0f);
        // saturation setup
        const double g = std::pow (10.0, (double) (p.drive * a) / 20.0);
        Shaper sh; sh.type = p.satType; sh.b = 0.6 * p.bias;
        const double norm = 0.5 / std::max (1e-6, std::abs (sh.f (g * 0.5)));
        const bool satOn = p.satOn && a > 0.0f && p.drive * a > 0.05f;
        // noise
        const bool noiseOn = p.noiseOn && a > 0.0f;
        const float nGain = noiseOn ? std::pow (10.0f, p.noiseDb / 20.0f) * std::min (1.0f, a) * (a > 1.0f ? std::pow (10.0f, (a - 1.0f) * 6.0f / 20.0f) : 1.0f) : 0.0f;
        // wobble depth (samples)
        const float wowS = p.wow * a * 0.0022f * (float) fs, flS = p.flutter * a * 0.00012f * (float) fs, drS = p.drift * a * 0.0012f * (float) fs;
        const double wowInc = std::clamp ((double) p.wobRate, 0.05, 6.0) / fs;
        const double flInc = (6.0 + 4.0 * p.wobRate) / fs;
        // digital
        const bool digOn = p.digOn && a > 0.0f;
        const float rateEff = (float) std::exp (std::log ((double) fs) + (std::log ((double) std::max (500.0f, p.rate)) - std::log (fs)) * std::min (1.0f, a));
        const float bitsEff = 24.0f + (std::clamp (p.bits, 1.0f, 24.0f) - 24.0f) * std::min (1.0f, a) - (a > 1.0f ? (a - 1.0f) * 3.0f : 0.0f);
        const double holdInc = rateEff / fs;
        const float q = std::pow (2.0f, std::max (1.0f, bitsEff) - 1.0f);
        // magnetic
        const bool magOn = p.magOn && a > 0.0f;
        const float dropRate = p.dropouts * a * 2.5f;                       // events per second
        for (int i = 0; i < n; ++i)
        {
            float x[2] { Lp[i], Rp[i] };
            const float dry[2] { x[0], x[1] };
            // input envelope (duck / follow / flux)
            const float lvl = std::max (std::abs (x[0]), std::abs (x[1]));
            env += (lvl - env) * (lvl > env ? envA : envR);
            const float envLin = std::clamp ((20.0f * std::log10 (env + 1e-9f) + 50.0f) / 44.0f, 0.0f, 1.0f);
            const float fluxScale = std::max (0.0f, 1.0f + p.flux * (envLin - 0.5f) * 2.0f);

            // ---- SATURATE ----
            if (satOn)
                for (int c = 0; c < 2; ++c)
                {
                    auto& s = st[c];
                    const double u = g * x[c];
                    const double d = u - s.satPrev;
                    const double y = std::abs (d) > 1e-6 ? (sh.F (u) - sh.F (s.satPrev)) / d : sh.f (0.5 * (u + s.satPrev));
                    s.satPrev = u;
                    x[c] = s.dc.process ((float) (y * norm));
                }

            // ---- NOISE ----
            if (noiseOn)
            {
                float nz[2];
                makeNoise (nz);
                const float gN = nGain * (p.noiseDuck < 0.0f ? 1.0f + p.noiseDuck * envLin : (1.0f - p.noiseDuck) + p.noiseDuck * envLin);
                x[0] += nz[0] * gN; x[1] += nz[1] * gN;
            }

            // ---- WOBBLE ----
            for (int c = 0; c < 2; ++c) { wob[c][(size_t) (wPos & wMask)] = x[c]; dryDl[c][(size_t) (wPos & wMask)] = dry[c]; }
            float dryOut[2] { dry[0], dry[1] };
            if (wobOn)
            {
                wowPh += wowInc; if (wowPh >= 1.0) wowPh -= 1.0;
                flPh += flInc; if (flPh >= 1.0) flPh -= 1.0;
                if (--driftCnt <= 0) { driftCnt = (int) (fs * 0.05); driftT = rnd() * 2.0f - 1.0f; }
                driftV += (driftT - driftV) * (float) (1.0 / (fs * 0.3));
                for (int c = 0; c < 2; ++c)
                {
                    const double off = c == 0 ? 0.0 : p.wobStereo * 0.25;
                    const double m = wowS * std::sin (2 * juce::MathConstants<double>::pi * (wowPh + off))
                                   + flS * std::sin (2 * juce::MathConstants<double>::pi * (flPh + off * 1.7))
                                   + drS * driftV * (c == 0 ? 1.0f : 1.0f - p.wobStereo * 0.6f);
                    const double d = std::clamp ((double) L + m * fluxScale, 2.0, 2.0 * L - 2.0);
                    x[c] = readFrac (wob[c], (double) wPos - d);
                    dryOut[c] = dryDl[c][(size_t) ((wPos - L) & wMask)];
                }
            }
            ++wPos;

            // ---- MAGNETIC ----
            if (magOn)
            {
                float dg[2] { 1.0f, 1.0f };
                if (dropRate > 0.0f)
                {
                    if (! drop.active && rnd() < dropRate * fluxScale / (float) fs)
                    {
                        drop.active = true; drop.t = 0;
                        drop.len = (int) (fs * (0.03 + 0.17 * rnd()));
                        drop.depth = std::min (0.95f, (0.25f + 0.6f * rnd()) * std::min (1.0f, p.dropouts * a * 1.5f));
                        drop.stereo = 0.7f + 0.3f * rnd();
                    }
                    if (drop.active)
                    {
                        const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) drop.t / (float) drop.len);
                        dg[0] = 1.0f - drop.depth * w; dg[1] = 1.0f - drop.depth * drop.stereo * w;
                        if (++drop.t >= drop.len) drop.active = false;
                    }
                }
                if (--wanderT <= 0) { wanderT = (int) (fs * 0.1); wanderTarget = (rnd() * 2.0f - 1.0f) * p.wear * a; }
                gainWander += (wanderTarget - gainWander) * (float) (1.0 / (fs * 0.2));
                const float gw = std::pow (10.0f, gainWander * 0.6f / 20.0f);
                for (int c = 0; c < 2; ++c)
                {
                    auto& s = st[c];
                    x[c] = s.bump.process (s.wear2.process (s.wear1.process (x[c]))) * dg[c] * gw;
                }
            }

            // ---- DIGITAL ----
            if (digOn)
            {
                for (int c = 0; c < 2; ++c) x[c] = st[c].aa4.process (st[c].aa3.process (st[c].aa2.process (st[c].aa1.process (x[c]))));
                holdPh += holdInc;
                if (holdPh >= 1.0 || holdInc >= 0.999) { holdPh -= std::floor (holdPh); holdV[0] = x[0]; holdV[1] = x[1]; }
                for (int c = 0; c < 2; ++c)
                {
                    float v = holdV[c];
                    if (bitsEff < 23.5f) v = std::round (v * q) / q;
                    x[c] = st[c].img.process (v);
                }
            }

            // ---- TONE ----
            for (int c = 0; c < 2; ++c) x[c] = st[c].lc2.process (st[c].lc1.process (st[c].hc2.process (st[c].hc1.process (x[c]))));

            // ---- SPACE ----
            if (p.spaceOn && a > 0.0f && p.spaceMix > 0.0f)
            {
                const float in = 0.5f * (x[0] + x[1]) * 0.25f;
                const float fb = 0.6f + 0.36f * p.decay, damp = 0.45f;
                for (int c = 0; c < 2; ++c)
                {
                    float acc = 0;
                    for (int k = 0; k < 4; ++k)
                    {
                        auto& cb = combs[c][k];
                        const int len = std::max (8, (int) (cb.size() * (0.35f + 0.65f * p.size)));
                        int& pos = combPos[c][k];
                        const float o = cb[(size_t) pos];
                        combLp[c][k] = o * (1.0f - damp) + combLp[c][k] * damp;
                        cb[(size_t) pos] = in + combLp[c][k] * fb;
                        if (++pos >= len) pos = 0;
                        acc += o;
                    }
                    for (int k = 0; k < 2; ++k)
                    {
                        auto& ap = aps[c][k];
                        int& pos = apPos[c][k];
                        const float bo = ap[(size_t) pos];
                        ap[(size_t) pos] = acc + bo * 0.5f;
                        acc = bo - acc;
                        if (++pos >= (int) ap.size()) pos = 0;
                    }
                    x[c] += acc * p.spaceMix * std::min (1.0f, a);
                }
            }

            // ---- STEREO ----
            {
                float m = 0.5f * (x[0] + x[1]), s = 0.5f * (x[0] - x[1]);
                s *= p.width;
                if (p.monoBass) s = st[0].sideHp2.process (st[0].sideHp1.process (s));
                x[0] = m + s; x[1] = m - s;
            }

            outSm += (outT - outSm) * sm;
            Lp[i] = (dryOut[0] + (x[0] - dryOut[0]) * p.mix) * outSm;
            Rp[i] = (dryOut[1] + (x[1] - dryOut[1]) * p.mix) * outSm;
            meterNoise = nGain;
        }
        wobblePos = (float) (wowPh);
        dropNow = drop.active ? drop.depth : 0.0f;
    }

    // UI
    float wobblePos = 0, dropNow = 0, meterNoise = 0;
    float speedNow() const noexcept { return (float) wowPh; }

private:
    struct ChanState
    {
        double satPrev = 0;
        Biquad dc, wear1, wear2, bump, aa1, aa2, aa3, aa4, img, lc1, lc2, hc1, hc2, sideHp1, sideHp2, nTone, nHp, crackBp;
        float pink[3] {};
    };
    struct DropState { bool active = false; int t = 0, len = 1; float depth = 0, stereo = 1; };

    bool wobActive() const noexcept { return p.wobOn && p.age > 0.0f && (p.wow > 0.0f || p.flutter > 0.0f || p.drift > 0.0f); }

    float rnd() noexcept { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; }
    float gauss() noexcept { return (rnd() + rnd() + rnd() + rnd() - 2.0f) * 1.73f; }

    float readFrac (const std::vector<float>& b, double pos) const noexcept
    {
        const double fl = std::floor (pos);
        const int64_t i = (int64_t) fl; const float f = (float) (pos - fl);
        auto at = [&] (int64_t j) { return b[(size_t) (j & wMask)]; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    void makeNoise (float* out) noexcept
    {
        switch (p.noiseType)
        {
            case 0:   // tape hiss: pinkish, band-limited, decorrelated stereo
                for (int c = 0; c < 2; ++c)
                {
                    auto& s = st[c];
                    const float w = gauss();
                    s.pink[0] = 0.99765f * s.pink[0] + w * 0.0990460f; s.pink[1] = 0.96300f * s.pink[1] + w * 0.2965164f; s.pink[2] = 0.57000f * s.pink[2] + w * 1.0526913f;
                    const float pk = (s.pink[0] + s.pink[1] + s.pink[2] + w * 0.1848f) * 0.25f;
                    out[c] = s.nTone.process (s.nHp.process (pk)) * 1.6f;
                }
                break;
            case 1: case 2:   // vinyl crackle / dust
            {
                const float rate = p.noiseType == 1 ? 9.0f : 90.0f;
                if (rnd() < rate / (float) fs)
                {
                    const float r = std::max (0.02f, rnd());
                    crackAmp = (p.noiseType == 1 ? 2.2f : 0.7f) * std::min (6.0f, 0.15f / r) * (rnd() < 0.5f ? -1.0f : 1.0f);
                    crackEnv = 1.0f; crackPan = rnd();
                }
                const float click = crackEnv * crackAmp * gauss();
                crackEnv *= p.noiseType == 1 ? 0.93f : 0.8f;
                // surface rumble / bed
                const float bed = gauss() * 0.035f;
                for (int c = 0; c < 2; ++c)
                {
                    auto& s = st[c];
                    const float panG = c == 0 ? 1.0f - 0.6f * crackPan : 0.4f + 0.6f * crackPan;
                    out[c] = s.nTone.process (s.crackBp.process (click * panG) + s.nHp.process (bed));
                }
                break;
            }
            default:  // hum 50 / 60 Hz with harmonics and buzz
            {
                const double f0 = p.noiseType == 3 ? 50.0 : 60.0;
                humPh += f0 / fs; if (humPh >= 1.0) humPh -= 1.0;
                const double ph = 2 * juce::MathConstants<double>::pi * humPh;
                float h = (float) (std::sin (ph) + 0.5 * std::sin (2 * ph) + 0.3 * std::sin (3 * ph) + 0.15 * std::sin (5 * ph));
                h += (float) (0.4 * (std::abs (std::sin (ph)) - 0.6366)) * (0.3f + 1.4f * p.noiseTone);
                out[0] = out[1] = h * 0.45f;
                break;
            }
        }
    }

    void updateFilters()
    {
        const float a = std::clamp (p.age, 0.0f, 2.0f);
        const float h = p.wear * 13.1f + p.bump * 7.7f + p.lowCut * 0.37f + p.highCut * 0.011f + a * 3.3f + p.rate * 0.0013f + p.digFilter * 5.1f
                      + p.noiseTone * 2.9f + (float) p.noiseType * 17.0f + (p.monoBass ? 1.0f : 0.0f) + (p.digOn ? 0.5f : 0.0f);
        if (std::abs (h - lastHash) < 1e-4f) return;
        lastHash = h;
        const float wearEff = std::min (1.0f, p.wear * a);
        const double hf = 20000.0 * std::pow (2500.0 / 20000.0, (double) wearEff);
        const double bumpDb = 7.0 * p.bump * std::min (1.5f, a);
        const double rateEff = std::exp (std::log (fs) + (std::log ((double) std::max (500.0f, p.rate)) - std::log (fs)) * std::min (1.0f, a));
        const double lc = 20.0 * std::pow ((double) std::max (20.0f, p.lowCut) / 20.0, (double) std::min (1.0f, a));
        const double hcT = 20000.0 * std::pow ((double) std::min (20000.0f, p.highCut) / 20000.0, (double) std::min (1.0f, a));
        for (int c = 0; c < 2; ++c)
        {
            auto& s = st[c];
            s.dc.hp (fs, 6.0, 0.7071);
            if (hf < 19000.0) { s.wear1.lp (fs, hf, 0.5412); s.wear2.lp (fs, hf, 1.3066); } else { s.wear1.unity(); s.wear2.unity(); }
            if (bumpDb > 0.05) s.bump.peak (fs, 75.0, 0.9, bumpDb); else s.bump.unity();
            // vintage sampler: anti-alias in front (amount), imaging softened behind
            const double nyq = rateEff * 0.5;
            if (p.digFilter > 0.01f && rateEff < fs * 0.95)
            {
                const double fc = std::min (fs * 0.45, nyq * (0.85 + 1.6 * (1.0 - p.digFilter)));
                s.aa1.lp (fs, fc, 0.5412); s.aa2.lp (fs, fc, 1.3066); s.aa3.lp (fs, fc, 0.5412); s.aa4.lp (fs, fc, 1.3066);
                s.img.lp (fs, std::min (fs * 0.45, nyq * (1.0 + 2.0 * (1.0 - p.digFilter))), 0.7071);
            }
            else { s.aa1.unity(); s.aa2.unity(); s.aa3.unity(); s.aa4.unity(); s.img.unity(); }
            if (lc > 21.0) { s.lc1.hp (fs, lc, 0.5412); s.lc2.hp (fs, lc, 1.3066); } else { s.lc1.unity(); s.lc2.unity(); }
            if (hcT < 19500.0) { s.hc1.lp (fs, hcT, 0.5412); s.hc2.lp (fs, hcT, 1.3066); } else { s.hc1.unity(); s.hc2.unity(); }
            s.sideHp1.hp (fs, 150.0, 0.5412); s.sideHp2.hp (fs, 150.0, 1.3066);
            // noise colour
            const double nt = 2500.0 * std::pow (16000.0 / 2500.0, (double) p.noiseTone);
            s.nTone.lp (fs, nt, 0.7071);
            if (p.noiseType == 0) s.nHp.hp (fs, 250.0, 0.7071); else s.nHp.lp (fs, 35.0, 0.7071);
            s.crackBp.bp (fs, 2800.0, 0.6);
        }
    }

    Params p;
    double fs = 48000;
    int L = 240;
    std::vector<float> wob[2], dryDl[2];
    int64_t wMask = 0, wPos = 0;
    std::vector<float> combs[2][4], aps[2][2];
    int combPos[2][4] {}, apPos[2][2] {};
    float combLp[2][4] {};
    ChanState st[2];
    double wowPh = 0, flPh = 0, holdPh = 0, humPh = 0;
    float driftV = 0, driftT = 0; int driftCnt = 0;
    float env = 0, holdV[2] {};
    DropState drop;
    float gainWander = 0, wanderTarget = 0; int wanderT = 0;
    float crackEnv = 0, crackAmp = 0, crackPan = 0.5f;
    float lastHash = -1, outSm = 1;
    uint32_t seed = 0x2468ACEu;
};

} // namespace lumen::lofi
