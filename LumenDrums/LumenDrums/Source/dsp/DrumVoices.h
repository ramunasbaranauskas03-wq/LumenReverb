#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <array>
#include <memory>
#include <atomic>
#include <algorithm>

// =====================================================================================
// Lumen Drums - voices
//   Every pad synthesises its role (kick, clap, snare, hats, ride, perc, tom, fx) from a model table,
//   or plays a user sample. Shared pad controls: tune, decay, tone (LP <-> HP tilt), snap, drive.
// =====================================================================================
namespace lumen::drums
{
constexpr float kPi = 3.14159265358979f;
constexpr int kPads = 10;
enum Role { RKick, RClap, RSnare, RCHat, ROHat, RRide, RPerc1, RPerc2, RTom, RFx };

inline const juce::StringArray& padNames() { static const juce::StringArray s { "KICK", "CLAP", "SNARE", "CL HAT", "OP HAT", "RIDE", "PERC 1", "PERC 2", "TOM", "FX" }; return s; }
inline int roleOf (int pad) { return pad; }

inline juce::StringArray modelNames (int role)
{
    switch (role)
    {
        case RKick:  return { "909", "808", "Big Room", "Techno", "Hardstyle", "Deep House", "Trap 808 Long" };
        case RClap:  return { "909 Clap", "808 Clap", "Big Room Clap", "Tight Clap", "Snap" };
        case RSnare: return { "909 Snare", "808 Snare", "Rim Shot", "Trap Snare", "Big Room Snare" };
        case RCHat:  return { "808 Closed", "909 Closed", "Trap Hat", "Shaker" };
        case ROHat:  return { "808 Open", "909 Open", "Trap Open", "Open Shaker" };
        case RRide:  return { "909 Ride", "Crash", "Ride Bell" };
        case RPerc1: case RPerc2: return { "Conga", "Bongo", "Rim", "Cowbell", "Clave", "Woodblock", "Zap" };
        case RTom:   return { "909 Tom", "808 Tom", "Floor Tom" };
        default:     return { "Crash", "Reverse Crash", "Zap", "Noise Hit", "Impact" };
    }
}

// ------------------------------------------------------------------------------------
struct SampleData
{
    juce::AudioBuffer<float> buf;     // stereo
    double rate = 48000.0;
    juce::String name;
};

struct PadP
{
    int model = 0; bool useSample = false;
    float level = 1, pan = 0, tune = 0, decay = 0.5f, tone = 0, snap = 0.5f, drive = 0;
    bool reverse = false; float start = 0;
    int choke = 0, out = 0;
};

// tiny helpers ------------------------------------------------------------------------
struct Rng { uint32_t s = 0x1234567u; inline float bip() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * (1.0f / 2147483648.0f); } };

struct Svf2
{
    float g = 0, k = 1.4f, a1 = 0, a2 = 0, a3 = 0, s1 = 0, s2 = 0;
    void set (float fs, float fc, float q) { fc = std::clamp (fc, 15.0f, 0.45f * fs); g = std::tan (kPi * fc / fs); k = 1.0f / q; a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    void reset() { s1 = s2 = 0; }
    inline void tick (float x, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2 * v1 - s1; s2 = 2 * v2 - s2; lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

// model tables ---------------------------------------------------------------------------
struct KickM { float f0, f1, tau, hold, dec, click, clickHz, drive; };
inline const KickM& kickModel (int m)
{
    static const KickM t[] {
        { 260, 52, 0.022f, 0.02f, 0.38f, 0.55f, 3000, 0.25f },   // 909
        { 110, 46, 0.035f, 0.00f, 0.90f, 0.10f, 1500, 0.05f },   // 808
        { 420, 54, 0.014f, 0.04f, 0.34f, 0.85f, 4500, 0.60f },   // Big Room
        { 200, 48, 0.030f, 0.03f, 0.55f, 0.45f, 2500, 0.45f },   // Techno
        { 620, 55, 0.012f, 0.08f, 0.45f, 0.60f, 3500, 1.00f },   // Hardstyle
        { 160, 50, 0.028f, 0.02f, 0.28f, 0.30f, 2000, 0.15f },   // Deep House
        {  90, 44, 0.040f, 0.05f, 1.80f, 0.12f, 1500, 0.20f } }; // Trap 808 long
    return t[std::clamp (m, 0, 6)];
}
// tone + noise (snares, toms, percs)
struct ToneM { float f1, f2, mix2, drop, tau, toneDec, noise, nLo, nHi, noiseDec, square; };
inline ToneM toneModel (int role, int m)
{
    if (role == RSnare)
    {
        static const ToneM t[] {
            { 185, 330, 0.5f, 3, 0.03f, 0.12f, 0.75f, 1500, 9000, 0.20f, 0 },     // 909
            { 238, 476, 0.45f, 2, 0.02f, 0.10f, 0.55f, 2000, 11000, 0.15f, 0 },   // 808
            { 1700, 520, 0.6f, 1, 0.01f, 0.025f, 0.45f, 2500, 9000, 0.03f, 0 },   // Rim shot
            { 200, 350, 0.3f, 4, 0.02f, 0.08f, 1.00f, 2500, 12000, 0.22f, 0 },   // Trap
            { 210, 380, 0.5f, 5, 0.03f, 0.16f, 0.95f, 1200, 10000, 0.30f, 0 } }; // Big Room
        return t[std::clamp (m, 0, 4)];
    }
    if (role == RTom)
    {
        static const ToneM t[] {
            { 150, 225, 0.25f, 5, 0.06f, 0.38f, 0.10f, 800, 5000, 0.08f, 0 },
            { 120, 120, 0.0f, 3, 0.10f, 0.60f, 0.02f, 800, 4000, 0.05f, 0 },
            { 85, 130, 0.2f, 6, 0.08f, 0.55f, 0.12f, 500, 3500, 0.10f, 0 } };
        return t[std::clamp (m, 0, 2)];
    }
    // percs
    static const ToneM t[] {
        { 280, 420, 0.15f, 2, 0.02f, 0.18f, 0.05f, 1000, 6000, 0.02f, 0 },     // conga
        { 420, 630, 0.15f, 2, 0.015f, 0.10f, 0.05f, 1500, 8000, 0.02f, 0 },    // bongo
        { 1700, 520, 0.6f, 1, 0.01f, 0.025f, 0.45f, 2500, 9000, 0.03f, 0 },    // rim
        { 540, 800, 0.85f, 0, 0.01f, 0.25f, 0.0f, 2000, 6000, 0.01f, 1 },     // 808 cowbell (squares)
        { 2500, 2500, 0.0f, 0, 0.01f, 0.03f, 0.0f, 2000, 8000, 0.01f, 0 },    // clave
        { 1200, 1650, 0.3f, 0.5f, 0.01f, 0.05f, 0.1f, 2000, 8000, 0.01f, 0 },  // woodblock
        { 2000, 2000, 0.0f, 36, 0.035f, 0.12f, 0.0f, 2000, 8000, 0.01f, 0 } };// zap
    return t[std::clamp (m, 0, 6)];
}

// ------------------------------------------------------------------------------------
float modelGain (int role, int model);

class Voice
{
public:
    bool active = false;
    void prepare (double sampleRate) { fs = (float) sampleRate; }

    void start (int roleIn, const PadP& pp, float velocity, const SampleData* smp, uint32_t seed)
    {
        role = roleIn; p = pp; vel = velocity; sample = smp; t = 0; n = 0; active = true; fade = 1; fading = false;
        rng.s = seed | 1u;
        ph1 = ph2 = 0; for (auto& m : mph) m = 0;
        tf.reset(); tfR.reset(); nf.reset(); nf2.reset(); cf.reset();
        tuneR = std::pow (2.0f, p.tune / 12.0f);
        decMul = std::pow (4.0f, (p.decay - 0.5f) * 2.0f);          // 0.25x .. 4x
        snapK = p.snap * 2.0f;
        // tone tilt (bypass near the centre)
        toneMode = std::abs (p.tone) < 0.02f ? 0 : p.tone < 0 ? 1 : 2;
        if (toneMode == 1) tf.set (fs, 20000.0f * std::pow (2.0f, p.tone * 7.0f), 0.7f);
        if (toneMode == 2) tf.set (fs, 20.0f * std::pow (2.0f, p.tone * 9.0f), 0.7f);
        tfR = tf;
        stereoSrc = sample != nullptr && p.useSample;
        driveG = 1.0f + 8.0f * p.drive;
        driveN = 1.0f / std::tanh (driveG);
        // pan (equal power) * level * velocity
        const float a = (std::clamp (p.pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
        gl = std::cos (a) * 1.41421356f; gr = std::sin (a) * 1.41421356f;
        amp = p.level * (0.25f + 0.75f * vel) * (calibrating || (sample != nullptr && p.useSample) ? 1.0f : modelGain (role, p.model));
        if (sample != nullptr && p.useSample)
        {
            const int len = sample->buf.getNumSamples();
            spos = p.reverse ? (double) len * (1.0 - p.start) - 1.0 : (double) len * p.start;
            sinc = (sample->rate / fs) * tuneR * (p.reverse ? -1.0 : 1.0);
            return;
        }
        setupSynth();
    }
    void choke() { if (active && ! fading) { fading = true; fadeStep = 1.0f / (0.004f * fs); } }

    // adds into L / R
    void render (float* L, float* R, int count) noexcept
    {
        for (int i = 0; i < count && active; ++i)
        {
            float l, r;
            if (sample != nullptr && p.useSample) { if (! sampleTick (l, r)) { active = false; break; } }
            else { const float m = synthTick(); if (! active) break; l = r = m; }
            // tone tilt
            if (toneMode != 0)
            {
                float lp, bp, hp; tf.tick (l, lp, bp, hp); l = toneMode == 1 ? lp : hp;
                if (stereoSrc) { tfR.tick (r, lp, bp, hp); r = toneMode == 1 ? lp : hp; } else r = l;
            }
            if (p.drive > 0.001f) { l = std::tanh (l * driveG) * driveN; r = std::tanh (r * driveG) * driveN; }
            if (fading) { fade -= fadeStep; if (fade <= 0) { active = false; break; } }
            const float g = amp * fade;
            L[i] += l * g * gl; R[i] += r * g * gr;
            ++n; t += 1.0f / fs;
        }
    }

private:
    void setupSynth()
    {
        switch (role)
        {
            case RKick:
            {
                const auto& k = kickModel (p.model);
                kf0 = k.f0 * tuneR; kf1 = k.f1 * tuneR; ktau = k.tau; khold = k.hold * std::sqrt (decMul); kdec = k.dec * decMul;
                kclick = k.click * snapK; kdrive = k.drive;
                cf.set (fs, k.clickHz, 0.8f);
                endT = khold + kdec * 1.1f + 0.02f;
                break;
            }
            case RClap:
            {
                static const float bp[] { 1150, 1000, 1300, 1500, 2200 }, q[] { 1.4f, 1.1f, 0.9f, 1.8f, 0.8f };
                static const float tail[] { 0.22f, 0.32f, 0.35f, 0.12f, 0.08f }, gap[] { 0.011f, 0.009f, 0.012f, 0.007f, 0.004f };
                static const int bursts[] { 4, 4, 5, 3, 2 };
                const int m = std::clamp (p.model, 0, 4);
                nf.set (fs, bp[m] * tuneR, q[m]); nf2.set (fs, bp[m] * 2.4f * tuneR, 1.0f);
                cTail = tail[m] * decMul; cGap = gap[m]; cBursts = bursts[m]; cWide = m == 2;
                endT = cGap * cBursts + cTail * 1.2f + 0.02f;
                break;
            }
            case RCHat: case ROHat: case RRide:
            {
                const int m = std::clamp (p.model, 0, 3);
                static const float base[6] { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
                float mul = 1.0f, dec = 0.05f, bpf = 10000, hpf = 7000, noise = 0.2f;
                if (role == RCHat) { static const float d[] { 0.045f, 0.06f, 0.03f, 0.05f }, nz[] { 0.15f, 0.55f, 0.25f, 1.0f }; dec = d[m]; noise = nz[m]; }
                if (role == ROHat) { static const float d[] { 0.45f, 0.38f, 0.28f, 0.30f }, nz[] { 0.15f, 0.5f, 0.25f, 1.0f }; dec = d[m]; noise = nz[m]; }
                if (role == RRide) { static const float d[] { 1.2f, 1.8f, 0.9f }, nz[] { 0.35f, 0.9f, 0.05f }, mu[] { 1.6f, 1.2f, 2.2f }, bp_[] { 8000, 6000, 5000 }, hp_[] { 4500, 3500, 2500 };
                                     const int r = std::min (m, 2); dec = d[r]; noise = nz[r]; mul = mu[r]; bpf = bp_[r]; hpf = hp_[r]; }
                if (role != RRide && m == 2) { bpf = 12000; hpf = 9000; }   // trap: brighter, tighter
                for (int k = 0; k < 6; ++k) mf[k] = base[k] * mul * tuneR;
                mNoise = noise; mDec = dec * decMul;
                nf.set (fs, bpf * std::sqrt (tuneR), 1.2f); nf2.set (fs, hpf * std::sqrt (tuneR), 0.7f);
                endT = mDec * 1.2f + 0.01f;
                break;
            }
            case RFx:
            {
                const int m = std::clamp (p.model, 0, 4);
                fxModel = m;
                if (m == 0) { mDec = 1.6f * decMul; for (int k = 0; k < 6; ++k) mf[k] = std::array<float, 6> { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f }[(size_t) k] * 1.25f * tuneR; mNoise = 0.9f; nf.set (fs, 6000, 0.8f); nf2.set (fs, 3500, 0.7f); endT = mDec * 1.2f; }
                if (m == 1) { mDec = 1.4f * decMul; nf.set (fs, 7000, 0.7f); nf2.set (fs, 2500, 0.7f); endT = mDec + 0.03f; }
                if (m == 2) { endT = 0.25f * decMul; }
                if (m == 3) { mDec = 0.5f * decMul; nf.set (fs, 3000 * tuneR, 0.9f); endT = mDec * 1.2f; }
                if (m == 4) { kf0 = 150 * tuneR; kf1 = 35 * tuneR; ktau = 0.05f; kdec = 1.5f * decMul; khold = 0; endT = kdec * 1.1f; mDec = 0.6f * decMul; nf.set (fs, 900, 0.7f); }
                break;
            }
            default:   // snare / tom / perc
            {
                tm = toneModel (role, p.model);
                tf1 = tm.f1 * tuneR; tf2 = tm.f2 * tuneR;
                tDec = tm.toneDec * decMul; nDec = tm.noiseDec * decMul;
                nf.set (fs, std::sqrt (tm.nLo * tm.nHi), 0.6f);
                nf2.set (fs, tm.nLo, 0.7f);
                endT = std::max (tDec, nDec) * 1.25f + 0.01f;
                break;
            }
        }
    }

    static float expDec (float t, float tau60) { return std::exp (-6.9f * t / std::max (0.001f, tau60)); }

    float synthTick() noexcept
    {
        if (t > endT) { active = false; return 0.0f; }
        float out = 0;
        switch (role)
        {
            case RKick:
            {
                const float f = kf1 + (kf0 - kf1) * std::exp (-t / ktau);
                ph1 += f / fs; if (ph1 >= 1) ph1 -= 1;
                float body = std::sin (2 * kPi * ph1);
                const float env = t < khold ? 1.0f : expDec (t - khold, kdec);
                body *= env * std::min (1.0f, t * 4000.0f + 0.2f);
                // click: band-passed noise burst + attack transient
                float lp, bp, hp; cf.tick (rng.bip(), lp, bp, hp);
                const float click = (bp * 1.8f + hp * 0.4f) * std::exp (-t / 0.0025f) * kclick;
                float x = body + click;
                if (kdrive > 0) { const float g = 1.0f + 5.0f * kdrive; x = std::tanh (x * g) / std::tanh (g); }
                out = x * 0.95f;
                break;
            }
            case RClap:
            {
                float env = 0;
                for (int b = 0; b < cBursts; ++b)
                {
                    const float tb = t - b * cGap;
                    if (tb < 0) continue;
                    env += b == cBursts - 1 ? expDec (tb, cTail) : std::exp (-tb / 0.0035f);
                }
                float lp, bp, hp, lp2, bp2, hp2;
                const float w = rng.bip();
                nf.tick (w, lp, bp, hp); nf2.tick (w, lp2, bp2, hp2);
                out = (bp * 2.2f + bp2 * 0.6f) * env * (0.6f + 0.6f * snapK * (t < cGap * cBursts ? 1.0f : 0.5f));
                break;
            }
            case RCHat: case ROHat: case RRide:
            {
                float m = 0;
                for (int k = 0; k < 6; ++k)
                {
                    const float dt = mf[k] / fs; mph[k] += dt; if (mph[k] >= 1) mph[k] -= 1;
                    m += (mph[k] < 0.5f ? 1.0f : -1.0f) + polyBlep (mph[k], dt) - polyBlep (std::fmod (mph[k] + 0.5f, 1.0f), dt);
                }
                m = m / 6.0f * (1.0f - mNoise) + rng.bip() * mNoise;
                float lp, bp, hp, lp2, bp2, hp2;
                nf.tick (m, lp, bp, hp); nf2.tick (bp, lp2, bp2, hp2);
                const float env = expDec (t, mDec) * (1.0f + snapK * 0.8f * std::exp (-t / 0.004f));
                out = hp2 * 2.0f * env;
                break;
            }
            case RFx:
            {
                if (fxModel == 2)
                {
                    const float f = 80.0f + 2400.0f * tuneR * std::exp (-t / 0.04f);
                    ph1 += f / fs; if (ph1 >= 1) ph1 -= 1;
                    out = std::sin (2 * kPi * ph1) * expDec (t, endT) * 0.9f;
                }
                else if (fxModel == 4)
                {
                    const float f = kf1 + (kf0 - kf1) * std::exp (-t / ktau);
                    ph1 += f / fs; if (ph1 >= 1) ph1 -= 1;
                    float lp, bp, hp; nf.tick (rng.bip(), lp, bp, hp);
                    out = std::tanh (1.5f * std::sin (2 * kPi * ph1)) * expDec (t, kdec) + lp * expDec (t, mDec) * 0.7f;
                }
                else if (fxModel == 1)
                {
                    // reverse crash: noise swelling up to the end
                    const float u = std::clamp (t / mDec, 0.0f, 1.0f);
                    float lp, bp, hp, lp2, bp2, hp2; nf.tick (rng.bip(), lp, bp, hp); nf2.tick (hp, lp2, bp2, hp2);
                    out = hp2 * std::pow (u, 3.0f) * (u >= 1.0f ? 0.0f : 1.0f) * 1.4f;
                }
                else
                {
                    float m = 0;
                    if (fxModel == 0)
                        for (int k = 0; k < 6; ++k) { mph[k] += mf[k] / fs; if (mph[k] >= 1) mph[k] -= 1; m += mph[k] < 0.5f ? 1.0f : -1.0f; }
                    m = m / 6.0f * (1.0f - mNoise) + rng.bip() * (fxModel == 0 ? mNoise : 1.0f);
                    float lp, bp, hp, lp2, bp2, hp2; nf.tick (m, lp, bp, hp); nf2.tick (fxModel == 0 ? hp : bp, lp2, bp2, hp2);
                    out = (fxModel == 0 ? hp2 * 1.6f : bp * 2.0f) * expDec (t, mDec);
                }
                break;
            }
            default:
            {
                const float drop = std::pow (2.0f, tm.drop * std::exp (-t / tm.tau) / 12.0f);
                ph1 += tf1 * drop / fs; if (ph1 >= 1) ph1 -= 1;
                ph2 += tf2 * drop / fs; if (ph2 >= 1) ph2 -= 1;
                float a, b;
                if (tm.square > 0.5f) { a = ph1 < 0.5f ? 1.0f : -1.0f; b = ph2 < 0.5f ? 1.0f : -1.0f; a *= 0.6f; b *= 0.6f; }
                else { a = std::sin (2 * kPi * ph1); b = std::sin (2 * kPi * ph2); }
                float tone = (a * (1.0f - tm.mix2) + b * tm.mix2) * expDec (t, tDec);
                if (tm.square > 0.5f) { float lp, bp, hp; nf.tick (tone, lp, bp, hp); tone = bp * 2.5f; }
                float noise = 0;
                if (tm.noise > 0)
                {
                    float lp, bp, hp, lp2, bp2, hp2; const float w = rng.bip();
                    nf.tick (w, lp, bp, hp); nf2.tick (w, lp2, bp2, hp2);
                    noise = (bp * 0.8f + hp2 * 0.5f) * tm.noise * expDec (t, nDec) * (0.7f + 0.6f * snapK);
                }
                const float att = 1.0f + snapK * 1.2f * std::exp (-t / 0.003f);
                out = (tone * 0.9f + noise) * att;
                break;
            }
        }
        return out;
    }

    bool sampleTick (float& l, float& r) noexcept
    {
        const auto& b = sample->buf;
        const int len = b.getNumSamples();
        if (spos < 0 || spos >= len - 1) return false;
        const int i0 = (int) spos; const float fr = (float) (spos - i0);
        const int ch2 = b.getNumChannels() > 1 ? 1 : 0;
        l = b.getSample (0, i0) + fr * (b.getSample (0, i0 + 1) - b.getSample (0, i0));
        r = b.getSample (ch2, i0) + fr * (b.getSample (ch2, i0 + 1) - b.getSample (ch2, i0));
        spos += sinc;
        // decay knob: full length at 100 %, shorter below
        if (p.decay < 0.98f)
        {
            const float lenS = 0.03f + 2.5f * p.decay * p.decay;
            const float e = t < lenS * 0.3f ? 1.0f : expDec (t - lenS * 0.3f, lenS);
            l *= e; r *= e;
            if (e < 1.0e-4f) return false;
        }
        const float att = 1.0f + (snapK - 1.0f) * 0.8f * std::exp (-t / 0.004f);
        l *= att; r *= att;
        return true;
    }

public:
    bool calibrating = false;
private:
    float fs = 48000;
    int role = 0; PadP p; float vel = 1; const SampleData* sample = nullptr;
    float t = 0; int64_t n = 0; Rng rng;
    float fade = 1, fadeStep = 0; bool fading = false;
    float tuneR = 1, decMul = 1, snapK = 1, driveG = 1, driveN = 1, amp = 1, gl = 1, gr = 1; int toneMode = 0;
    Svf2 tf, tfR, nf, nf2, cf; bool stereoSrc = false;
    double spos = 0, sinc = 1;
    float endT = 1;
    // kick / impact
    float kf0 = 200, kf1 = 50, ktau = 0.02f, khold = 0, kdec = 0.4f, kclick = 0.5f, kdrive = 0; float ph1 = 0, ph2 = 0;
    // clap
    float cTail = 0.2f, cGap = 0.01f; int cBursts = 4; bool cWide = false;
    // metal
    float mf[6] {}, mph[6] {}, mNoise = 0.2f, mDec = 0.1f;
    // tone/noise
    ToneM tm {}; float tf1 = 200, tf2 = 300, tDec = 0.1f, nDec = 0.1f;
    int fxModel = 0;
};

// every synth model is calibrated once to the same peak level (-1 dBFS at default settings, full velocity)
inline float modelGain (int role, int model)
{
    static const std::array<std::array<float, 8>, kPads> table = []
    {
        std::array<std::array<float, 8>, kPads> t {};
        for (int r = 0; r < kPads; ++r)
            for (int m = 0; m < 8; ++m)
            {
                t[(size_t) r][(size_t) m] = 1.0f;
                if (m >= modelNames (r).size()) continue;
                Voice v; v.calibrating = true; v.prepare (48000.0);
                PadP q; q.model = m;
                v.start (r, q, 1.0f, nullptr, 1u);
                const int len = r == RFx || r == RRide ? 120000 : 30000;
                std::vector<float> L ((size_t) len, 0.0f), R ((size_t) len, 0.0f);
                v.render (L.data(), R.data(), len);
                float pk = 1.0e-4f; for (float x : L) pk = std::max (pk, std::abs (x));
                t[(size_t) r][(size_t) m] = 0.89f / pk;
            }
        return t;
    }();
    return table[(size_t) std::clamp (role, 0, kPads - 1)][(size_t) std::clamp (model, 0, 7)];
}

// ------------------------------------------------------------------------------------
// One pad: 4 voices round-robin (fast re-hits overlap naturally, oldest is faded)
class Pad
{
public:
    void prepare (double fs) { for (auto& v : voices) { v.prepare (fs); v.active = false; } }
    void trigger (int role, const PadP& p, float vel, const SampleData* s)
    {
        Voice* v = nullptr;
        for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (v == nullptr) { v = &voices[(size_t) next]; next = (next + 1) % (int) voices.size(); }
        // monophonic roles (kick / toms): fade the previous hit to avoid phase-smeared low end
        if (role == RKick || role == RTom) for (auto& c : voices) if (&c != v) c.choke();
        v->start (role, p, vel, s, seed += 0x9E3779B9u);
    }
    void choke() { for (auto& v : voices) v.choke(); }
    void render (float* L, float* R, int n) { for (auto& v : voices) if (v.active) v.render (L, R, n); }
    bool busy() const { for (auto& v : voices) if (v.active) return true; return false; }
private:
    std::array<Voice, 4> voices;
    int next = 0; uint32_t seed = 0xABCDEFu;
};

// ------------------------------------------------------------------------------------
// master: glue compressor (feed-forward, soft knee) + soft clipper
class Master
{
public:
    void prepare (double sampleRate) { fs = (float) sampleRate; env = 0; gr = 1; }
    void process (float* L, float* R, int n, float glue, float clip, float vol) noexcept
    {
        const float thr = std::pow (10.0f, (-6.0f - 18.0f * glue) / 20.0f), ratio = 1.0f + 3.0f * glue;
        const float aA = 1.0f - std::exp (-1.0f / (0.010f * fs)), aR = 1.0f - std::exp (-1.0f / (0.120f * fs));
        const float makeup = std::pow (10.0f, (glue * 6.0f) / 20.0f);
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            if (glue > 0.001f)
            {
                const float x = std::max (std::abs (l), std::abs (r));
                env += (x - env) * (x > env ? aA : aR);
                float g = 1.0f;
                if (env > thr) g = std::pow (env / thr, 1.0f / ratio - 1.0f);
                gr = g;
                l *= g * makeup; r *= g * makeup;
            }
            l *= vol; r *= vol;
            if (clip > 0.001f)
            {
                // drive into a soft clipper: louder, rounder transients (classic drum-bus clipping)
                const float k = 1.0f + 3.0f * clip;
                l = 0.98f * std::tanh (l * k); r = 0.98f * std::tanh (r * k);
            }
            // safety ceiling
            L[i] = std::clamp (l, -1.0f, 1.0f); R[i] = std::clamp (r, -1.0f, 1.0f);
        }
    }
    float reduction() const { return gr; }
private:
    float fs = 48000, env = 0, gr = 1;
};

} // namespace lumen::drums
