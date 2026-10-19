#pragma once
#include "Wavetables.h"
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <vector>

namespace lumen::synth
{

constexpr float kPiS = 3.14159265358979f;
constexpr int   kTick = 16;          // control-rate period (samples)
constexpr int   kPool = 32;          // voice pool
constexpr int   kMaxUni = 16;
constexpr int   kOsc = 3, kLfo = 4, kSlots = 16, kLut = 256;

enum Src { sOff, sL1, sL2, sL3, sL4, sE1, sE2, sE3, sVel, sMW, sAT, sKey, sM1, sM2, sM3, sM4, sRnd, sBend, kNumSrc };
enum Dst { dOff = 0, dOscBase = 1,      // + 6*osc + { pos, pitch, level, det, warp, pan }
           dPitch = 19, dCutoff, dRes, dDrive, dF2Cut, dF2Res, dSub, dNoise, dPan, dVolume,
           dL1Rate, dL2Rate, dL3Rate, dL4Rate, dWidth, dFltEnv,
           dFxDistDrive, dFxDistMix, dFxHyperMix, dFxChorusMix, dFxPhaserMix, dFxFlangerMix,
           dFxDelayMix, dFxDelayFb, dFxReverbMix, dFxReverbSize, dFxPumpDepth, kNumDst };
constexpr int kNumFxDst = kNumDst - dFxDistDrive;
enum Warp { wOff, wSync, wBend, wSqueeze, wFM, wMirror, wAsym, wQuantize, wRemap };
enum FType { fLadder, fLP12, fLP24, fHP12, fHP24, fBP, fNotch, fCombP, fCombN, fFormant, fPhaser, fMS20, fDiode, fJuno };
enum UMode { uClassic, uSuper, uOct, uOct5, uFifth, uMajor, uMinor };
enum LShape { lSine, lTri, lSawUp, lSawDown, lSquare, lPump, lSH, lSmooth, lCustom };
enum LMode { lmFree, lmRetrig, lmEnv };

struct OscP
{
    bool on = true; const Table* tbl = nullptr; float pos = 0; int warpMode = 0; float warp = 0;
    int oct = 0, semi = 0; float fine = 0, level = 0.75f, pan = 0;
    int uni = 1; float det = 0.25f, blend = 0.75f, width = 0.8f, rand = 1; int umode = 0;
};
struct FiltP { bool on = false; int type = 0; float cutoff = 20000, res = 0.1f, drive = 0, env = 0, key = 0; };
struct ModSlot { int src = 0, dst = 0; float amt = 0; };

struct Patch
{
    OscP osc[kOsc];
    bool subOn = false; int subShape = 0, subOct = 1; float subLevel = 0.6f;
    bool noiseOn = false; float noiseLevel = 0.25f, noiseColor = 0.5f;
    FiltP flt[2];
    int route = 0;     // 0 serial, 1 parallel
    float env[3][4] { { 0.002f, 0.4f, 1, 0.12f }, { 0.001f, 0.4f, 0, 0.12f }, { 0.001f, 0.4f, 0, 0.12f } };
    int lfoShape[kLfo] {}; float lfoRate[kLfo] { 2, 2, 2, 2 }; bool lfoSync[kLfo] { true, true, true, true };
    double lfoBeats[kLfo] { 1, 1, 1, 1 }; int lfoMode[kLfo] { 1, 1, 1, 1 };
    const float* lfoLut[kLfo] {};        // kLut + 1 values, 0..1 (custom shapes)
    ModSlot mod[kSlots];
    float macro[4] {};
    int voices = 8; bool legato = false; float glide = 0; int bend = 2; float velSens = 0.5f;
    float drift = 0;
    float warmth = 0.3f;     // analog voice stage: soft asymmetric saturation + per-voice tolerances
};

//==============================================================================
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    inline uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float uni() noexcept  { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    inline float bip() noexcept  { return uni() * 2.0f - 1.0f; }
};

inline float fastTanh (float x) noexcept
{
    x = std::clamp (x, -3.0f, 3.0f);       // the Pade approximant peaks at exactly 1.0 at |x| = 3
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// JP-8000 super saw detune curve (Szabo 2010), x = detune knob 0..1 -> relative spread
inline float jpDetune (float x) noexcept
{
    const double v = std::clamp ((double) x, 0.0, 1.0);
    static const double c[12] { 0.0030115596, 0.6717417634, -24.1878824391, 404.2703938388, -3425.0836591318, 17019.9518580080,
                                -53046.9642751875, 106649.6679158292, -138150.6761080548, 111363.4808729368, -50818.8652045924, 10028.7312891634 };
    double y = 0.0, p = 1.0;
    for (double k : c) { y += k * p; p *= v; }
    return (float) std::max (0.0, y);
}

inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt)        { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

//==============================================================================
struct EnvCoef { float ac = 1, dc = 0.99f, s = 1, rc = 0.99f; };

struct Env
{
    enum { Idle, Att, Dec, Sus, Rel };
    int st = Idle; float v = 0;
    const EnvCoef* c = nullptr;
    void on() noexcept  { st = Att; }
    void off() noexcept { if (st != Idle) st = Rel; }
    inline float next() noexcept
    {
        switch (st)
        {
            case Att: v += (1.3f - v) * c->ac; if (v >= 1.0f) { v = 1.0f; st = Dec; } break;
            case Dec: v = c->s + (v - c->s) * c->dc; if (std::fabs (v - c->s) < 1.0e-4f) { v = c->s; st = Sus; } break;
            case Sus: v += (c->s - v) * 0.002f; break;
            case Rel: v *= c->rc; v -= 1.0e-7f; if (v <= 1.0e-5f) { v = 0; st = Idle; } break;
            default: break;
        }
        return v;
    }
};

//==============================================================================
// Voice filter (stereo): ZDF ladder, TPT SVF modes, comb +/-, 3-formant vowel filter, 4-stage phaser.
struct VFilter
{
    static constexpr int kComb = 8192;
    int type = 0; float G = 0, beta = 1, k = 0, g = 0, a1 = 0, a2 = 0, a3 = 0, ks = 1.4f, preG = 1, postG = 1;
    float s[2][4] {}, sv[2][4] {};
    // comb
    float combD = 100, combG = 0.5f, combNorm = 1, combDamp = 0.5f, combLp[2] {};
    int combPos = 0;
    std::array<float, kComb> combBuf[2] {};
    // formant
    float fa1[3] {}, fa2[3] {}, fa3[3] {}, fk[3] {}, fGain[3] {}, fs3[2][3][2] {};
    // phaser
    float apG[4] {}, apS[2][4] {}, phFb = 0, phLast[2] {};

    void reset() noexcept
    {
        for (auto& c : s) for (auto& x : c) x = 0;
        for (auto& c : sv) for (auto& x : c) x = 0;
        combLp[0] = combLp[1] = 0;
        for (auto& c : fs3) for (auto& b : c) b[0] = b[1] = 0;
        for (auto& c : apS) for (auto& x : c) x = 0;
        phLast[0] = phLast[1] = 0;
    }

    void resetComb() noexcept { for (auto& b : combBuf) b.fill (0.0f); combPos = 0; }

    static inline void svfCoef (float fc, float q, float fs, float& c1, float& c2, float& c3, float& kk) noexcept
    {
        const float gg = std::tan (kPiS * std::clamp (fc, 15.0f, 0.46f * fs) / fs);
        kk = 1.0f / q;
        c1 = 1.0f / (1.0f + gg * (gg + kk)); c2 = gg * c1; c3 = gg * c2;
    }

    void set (int t, float fc, float res, float drive, float fs) noexcept
    {
        type = t;
        fc = std::clamp (fc, 15.0f, 0.46f * fs);
        res = std::clamp (res, 0.0f, 1.0f);
        preG  = 1.0f + std::clamp (drive, 0.0f, 1.0f) * 9.0f;
        postG = 1.0f / std::sqrt (preG);
        switch (t)
        {
            case fCombP: case fCombN:
            {
                const float f = std::max (fc, std::max (25.0f, fs / (float) (kComb - 4)));
                combD = fs / f;
                combG = 0.15f + 0.83f * std::sqrt (res);
                combNorm = std::sqrt (1.0f - combG * combG) * 1.4f;
                combDamp = 0.35f + 0.6f * (1.0f - std::clamp (drive, 0.0f, 1.0f));
                return;
            }
            case fFormant:
            {
                static const float F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2100, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
                const float pos = std::clamp (std::log2 (fc / 20.0f) / std::log2 (1000.0f), 0.0f, 1.0f) * 4.0f;
                const int a = std::min (3, (int) pos); const float fr = pos - (float) a;
                const float q = 4.0f + 16.0f * res;
                for (int i = 0; i < 3; ++i)
                {
                    const float f = F[a][i] + (F[a + 1][i] - F[a][i]) * fr;
                    svfCoef (f, q, fs, fa1[i], fa2[i], fa3[i], fk[i]);
                    fGain[i] = (i == 0 ? 1.0f : i == 1 ? 0.7f : 0.45f) * 1.8f;
                }
                return;
            }
            case fPhaser:
            {
                static const float spread[4] = { 0.55f, 0.85f, 1.3f, 2.1f };
                for (int i = 0; i < 4; ++i)
                {
                    const float gg = std::tan (kPiS * std::clamp (fc * spread[i], 15.0f, 0.46f * fs) / fs);
                    apG[i] = gg / (1.0f + gg);
                }
                phFb = 0.92f * res;
                return;
            }
            default: break;
        }
        g = std::tan (kPiS * fc / fs);
        G = g / (1.0f + g); beta = 1.0f / (1.0f + g);
        k = 3.95f * res * (0.75f + 0.25f * res);
        if (t == fDiode) k = 7.4f * res * (0.7f + 0.3f * res);
        if (t == fJuno)  k = 3.7f * res;
        if (t == fMS20)  { ks = 2.0f - 1.995f * std::pow (res, 0.5f); a1 = 1.0f / (1.0f + g * (g + ks)); a2 = g * a1; a3 = g * a2; return; }
        ks = 2.0f - 1.96f * std::pow (res, 0.7f);
        a1 = 1.0f / (1.0f + g * (g + ks)); a2 = g * a1; a3 = g * a2;
    }

    inline float svf (float x, float* st, float& lp, float& bp) noexcept
    {
        const float v3 = x - st[1];
        const float v1 = a1 * st[0] + a2 * v3;
        const float v2 = st[1] + a2 * st[0] + a3 * v3;
        st[0] = 2.0f * v1 - st[0]; st[1] = 2.0f * v2 - st[1];
        lp = v2; bp = v1;
        return x - ks * v1 - v2;
    }

    inline float process (float x, int ch) noexcept
    {
        x = 2.0f * fastTanh (0.5f * x * preG) * postG;
        switch (type)
        {
            case fLadder:
            {
                float* st = s[ch];
                const float S = beta * (G * G * G * st[0] + G * G * st[1] + G * st[2] + st[3]);
                const float G4 = G * G * G * G;
                float y = fastTanh ((x - k * S) / (1.0f + k * G4));
                for (int i = 0; i < 4; ++i) { const float v = (y - st[i]) * G; y = v + st[i]; st[i] = y + v; }
                return y * (1.0f + 0.55f * k);
            }
            case fDiode:
            {
                // 3-pole (18 dB) diode-style ladder: squelchy acid resonance, thinner bass when it screams
                float* st = s[ch];
                const float S = beta * (G * G * st[0] + G * st[1] + st[2]);
                const float G3 = G * G * G;
                float y = fastTanh ((x - k * fastTanh (S)) / (1.0f + k * G3));
                for (int i = 0; i < 3; ++i) { const float v = (y - st[i]) * G; y = v + st[i]; st[i] = y + v; if (i == 0) y = fastTanh (y * 1.2f) / 1.2f; }
                return y * (1.0f + 0.3f * k);
            }
            case fJuno:
            {
                // OTA 4-pole: soft per-stage saturation, resonance keeps the bass (no thinning)
                float* st = s[ch];
                const float S = beta * (G * G * G * st[0] + G * G * st[1] + G * st[2] + st[3]);
                const float G4 = G * G * G * G;
                float y = (x - k * S) / (1.0f + k * G4);
                for (int i = 0; i < 4; ++i) { const float v = (fastTanh (y * 0.8f) * 1.25f - st[i]) * G; y = v + st[i]; st[i] = y + v; }
                return y * (1.0f + 0.9f * k);
            }
            case fMS20:
            {
                // Sallen-Key style 12 dB with a clipped resonance loop: aggressive, screaming peak that never blows up
                float* st = sv[ch];
                const float v3 = x - st[1];
                const float v1 = a1 * st[0] + a2 * v3;
                const float v2 = st[1] + a2 * st[0] + a3 * v3;
                st[0] = 2.0f * v1 - st[0]; st[1] = 2.0f * v2 - st[1];
                st[0] = 1.6f * fastTanh (st[0] * 0.625f);
                return v2 * (1.0f + 0.4f * (2.0f - ks));
            }
            case fCombP: case fCombN:
            {
                auto& b = combBuf[ch];
                float rp = (float) combPos - combD; while (rp < 0.0f) rp += (float) kComb;
                const int i0 = (int) rp; const float fr = rp - (float) i0;
                const float d = b[(size_t) (i0 & (kComb - 1))] + fr * (b[(size_t) ((i0 + 1) & (kComb - 1))] - b[(size_t) (i0 & (kComb - 1))]);
                combLp[ch] += combDamp * (d - combLp[ch]);
                const float y = type == fCombP ? x + combG * combLp[ch] : x - combG * combLp[ch];
                b[(size_t) combPos] = fastTanh (y);
                if (ch == 1) combPos = (combPos + 1) & (kComb - 1);
                return y * combNorm;
            }
            case fFormant:
            {
                float out = 0.0f;
                for (int i = 0; i < 3; ++i)
                {
                    float* st = fs3[ch][i];
                    const float v3 = x - st[1];
                    const float v1 = fa1[i] * st[0] + fa2[i] * v3;
                    const float v2 = st[1] + fa2[i] * st[0] + fa3[i] * v3;
                    st[0] = 2.0f * v1 - st[0]; st[1] = 2.0f * v2 - st[1];
                    out += v1 * fk[i] * fGain[i];
                }
                return out;
            }
            case fPhaser:
            {
                float y = x + phFb * phLast[ch];
                for (int i = 0; i < 4; ++i)
                {
                    // TPT one-pole allpass: ap = 2*lp - x
                    const float v = (y - apS[ch][i]) * apG[i];
                    const float lp = v + apS[ch][i];
                    apS[ch][i] = lp + v;
                    y = 2.0f * lp - y;
                }
                phLast[ch] = y;
                return 0.6f * (x + y);
            }
            default: break;
        }
        float lp, bp, hp = svf (x, sv[ch], lp, bp);
        switch (type)
        {
            case fLP12:  return lp;
            case fHP12:  return hp;
            case fBP:    return bp * ks;
            case fNotch: return x - ks * bp;
            case fLP24:  { float lp2, bp2; svf (lp, sv[ch] + 2, lp2, bp2); return lp2; }
            case fHP24:  { float lp2, bp2; return svf (hp, sv[ch] + 2, lp2, bp2); }
            default: return lp;
        }
    }
};

//==============================================================================
struct Shared
{
    const Patch* p = nullptr;
    float fs = 48000, invFs = 1.0f / 48000;
    float mipFs = 48000;     // oscillators are band-limited for the OUTPUT rate, so oversampled drive / filters have headroom
    EnvCoef ec[3];
    float bendSemis = 0, bendNorm = 0, modWheel = 0, aftertouch = 0;
    float glideCoef = 1;
    float lfoGlobal[kLfo] {};
    float lfoInc[kLfo] {};
};

inline float lfoShape (int shape, float ph, float shA, float shB, const float* lut = nullptr) noexcept
{
    switch (shape)
    {
        case lSine:    return std::sin (2.0f * kPiS * ph);
        case lTri:     { float t = ph + 0.25f; t -= std::floor (t); return 1.0f - 4.0f * std::fabs (t - 0.5f); }
        case lSawUp:   return 2.0f * ph - 1.0f;
        case lSawDown: return 1.0f - 2.0f * ph;
        case lSquare:  return ph < 0.5f ? 1.0f : -1.0f;
        case lPump:    { const float x = std::min (1.0f, ph / 0.6f), y = 1.0f - x; return 2.0f * (1.0f - y * y * y) - 1.0f; }
        case lSH:      return shB;
        case lSmooth:  { const float w = 0.5f - 0.5f * std::cos (kPiS * ph); return shA + (shB - shA) * w; }
        case lCustom:
        {
            if (lut == nullptr) return 0.0f;
            const float x = std::clamp (ph, 0.0f, 1.0f) * (float) kLut;
            const int i = std::min ((int) x, kLut - 1); const float f = x - (float) i;
            return 2.0f * (lut[i] + f * (lut[i + 1] - lut[i])) - 1.0f;
        }
        default: return 0.0f;
    }
}

inline bool isBipolarSrc (int s) { return s == sL1 || s == sL2 || s == sL3 || s == sL4 || s == sRnd || s == sBend; }

//==============================================================================
class Voice
{
public:
    bool active = false, gate = false, sustained = false, killing = false;
    int note = 60; float vel = 1; uint64_t age = 0;
    float ampOut = 0;

    void clear() noexcept
    {
        active = gate = sustained = killing = false; ampOut = 0;
        for (auto& e : env) { e.st = Env::Idle; e.v = 0; }
        for (auto& f : filt) { f.reset(); f.resetComb(); }
    }

    void start (int n, float v, float fromPitch, bool glideFrom, const Shared& sh, Rng& rg, uint64_t a)
    {
        const bool wasActive = active && ! killing;
        note = n; vel = v; age = a; active = true; gate = true; sustained = false; killing = false; killGain = 1.0f;
        targetPitch = (float) n;
        curPitch = glideFrom ? fromPitch : (float) n;
        noteRand = rg.uni(); noteRand2 = rg.uni();
        tolGain = 1.0f + 0.06f * rg.bip(); tolBias = 0.04f * rg.bip();
        for (auto& oo : driftV) for (auto& dv : oo) { dv.fixed = rg.bip(); if (! wasActive) { dv.v = rg.bip(); dv.target = rg.bip(); dv.left = 0; } }
        for (int e = 0; e < 3; ++e) { env[e].c = &sh.ec[e]; if (! wasActive) env[e].v = 0; env[e].on(); }
        const Patch& p = *sh.p;
        for (int o = 0; o < kOsc; ++o)
            for (int j = 0; j < kMaxUni; ++j)
                uni[o][j].ph = rg.uni() * p.osc[o].rand;
        for (int l = 0; l < kLfo; ++l)
        {
            if (p.lfoMode[l] != lmFree) lfoPh[l] = 0.0f;
            shA[l] = rg.bip(); shB[l] = rg.bip();
        }
        subPh = 0.0f;
        if (! wasActive)
        {
            for (int f = 0; f < 2; ++f)
            {
                filt[f].reset();
                if (p.flt[f].type == fCombP || p.flt[f].type == fCombN) filt[f].resetComb();
            }
            for (auto& x : fmOut) x = 0.0f;
            for (auto& o : osc) for (auto& c : o.hpS) c[0] = c[1] = 0.0f;
            noiseLp = noiseLp2 = 0.0f;
            warmDc[0] = warmDc[1] = warmY[0] = warmY[1] = 0.0f;
        }
        firstTick = true;
        tick (sh);
    }

    void legatoTo (int n, float v) { note = n; targetPitch = (float) n; vel = v; }
    void release() { gate = false; for (auto& e : env) e.off(); }
    void kill (float fs) { killing = true; gate = false; killStep = 1.0f / (0.003f * fs); }

    void tick (const Shared& sh) noexcept
    {
        const Patch& p = *sh.p;
        curPitch += (targetPitch - curPitch) * sh.glideCoef;

        float lfoV[kLfo];
        for (int l = 0; l < kLfo; ++l)
        {
            const float rateMul = std::exp2 (4.0f * std::clamp (lastDst[dL1Rate + l], -1.0f, 1.0f));
            if (p.lfoMode[l] != lmFree)
            {
                if (! firstTick) lfoPh[l] += sh.lfoInc[l] * rateMul;
                if (p.lfoMode[l] == lmEnv) lfoPh[l] = std::min (lfoPh[l], 0.99999f);
                else if (lfoPh[l] >= 1.0f) { lfoPh[l] -= std::floor (lfoPh[l]); shA[l] = shB[l]; shB[l] = rng.bip(); }
                lfoV[l] = lfoShape (p.lfoShape[l], lfoPh[l], shA[l], shB[l], p.lfoLut[l]);
            }
            else
            {
                const float ph = sh.lfoGlobal[l];
                if (ph < lastGlobal[l]) { shA[l] = shB[l]; shB[l] = rng.bip(); }
                lastGlobal[l] = ph;
                lfoV[l] = lfoShape (p.lfoShape[l], ph, shA[l], shB[l], p.lfoLut[l]);
            }
        }

        float src[kNumSrc] {};
        for (int l = 0; l < kLfo; ++l) src[sL1 + l] = lfoV[l];
        src[sE1] = env[0].v; src[sE2] = env[1].v; src[sE3] = env[2].v;
        src[sVel] = vel; src[sMW] = sh.modWheel; src[sAT] = sh.aftertouch; src[sKey] = ((float) note - 60.0f) / 60.0f;
        for (int m = 0; m < 4; ++m) src[sM1 + m] = p.macro[m];
        src[sRnd] = noteRand * 2.0f - 1.0f; src[sBend] = sh.bendNorm;

        float d[kNumDst] {};
        float volMul = 1.0f;
        for (const auto& s : p.mod)
        {
            if (s.src <= sOff || s.src >= kNumSrc || s.dst <= dOff || s.dst >= dFxDistDrive || std::fabs (s.amt) < 1.0e-9f) continue;
            const float v = src[s.src];
            if (s.dst == dVolume)
            {
                const float u = std::clamp (isBipolarSrc (s.src) ? 0.5f * (v + 1.0f) : v, 0.0f, 1.0f);
                volMul *= s.amt > 0.0f ? (1.0f - s.amt) + s.amt * u : 1.0f + s.amt * u;
            }
            else d[s.dst] += s.amt * v;
        }
        std::copy (std::begin (d), std::end (d), std::begin (lastDst));

        const float basePitch = curPitch + sh.bendSemis + 24.0f * d[dPitch];

        for (int o = 0; o < kOsc; ++o)
        {
            const OscP& op = p.osc[o];
            OscState& os = osc[o];
            const int base = dOscBase + 6 * o;
            const float levelT = op.on && op.tbl != nullptr ? std::clamp (op.level + d[base + 2], 0.0f, 1.0f) : 0.0f;
            setRamp (os.level, os.levelInc, levelT * levelT);
            if ((! op.on || op.tbl == nullptr) && os.level <= 0.0f) { os.n = 0; continue; }
            if (op.tbl == nullptr) { os.n = 0; continue; }

            const float pitch = basePitch + 12.0f * op.oct + op.semi + 0.01f * op.fine + 24.0f * d[base + 1];
            const float freq = 440.0f * std::exp2 ((pitch - 69.0f) / 12.0f);
            const int n = std::clamp (op.uni, 1, kMaxUni);
            os.n = n;
            const float det = std::clamp (op.det + d[base + 3], 0.0f, 1.0f);
            const float spread = 1.2f * det * std::sqrt (det);
            const float width = std::clamp (op.width + d[dWidth], 0.0f, 1.0f);
            float sumSq = 0.0f;
            // stack intervals: unison voices are spread over octaves / fifths / chord tones
            static const int stacks[7][4] { { 0 }, { 0 }, { 0, 12 }, { 0, 7, 12 }, { 0, 7 }, { 0, 4, 7, 12 }, { 0, 3, 7, 12 } };
            static const int stackN[7] { 1, 1, 2, 3, 2, 4, 4 };
            const int um = std::clamp (op.umode, 0, 6), sn = stackN[um];
            const float drift = sh.p->drift;
            const int groups = sn > 1 ? (n + sn - 1) / sn : n;
            for (int j = 0; j < n; ++j)
            {
                // stacks: voices of one interval group share a stereo / detune position, so every interval is heard on both sides
                const int gi = sn > 1 ? j / sn : j;
                const float x = groups <= 1 ? 0.0f : 2.0f * (float) gi / (float) (groups - 1) - 1.0f;
                float off;
                if (um == uSuper)
                {
                    // Roland JP-8000 super saw (Adam Szabo's measurements): fixed uneven voice offsets scaled by the
                    // measured 11th-order detune curve -> the classic trance / big room sound
                    static const float jp[7] { -0.11002313f, -0.06288439f, -0.01952356f, 0.0f, 0.01991221f, 0.06216538f, 0.10745242f };
                    const float fx = (x + 1.0f) * 3.0f; const int i0 = std::min (5, (int) fx); const float fr = fx - (float) i0;
                    const float c = jp[i0] + (jp[i0 + 1] - jp[i0]) * fr;
                    off = 12.0f * std::log2 (std::max (0.05f, 1.0f + c * jpDetune (det)));
                }
                else off = spread * x * (0.55f + 0.45f * std::fabs (x));
                if (sn > 1) off += (float) stacks[um][j % sn];
                // analog drift: slow random wander per unison voice + a fixed per-note offset
                if (drift > 0.0f)
                {
                    auto& dr = driftV[o][j];
                    if (--dr.left <= 0) { dr.target = rng.bip(); dr.left = 150 + (int) (rng.uni() * 600.0f); }
                    dr.v += (dr.target - dr.v) * 0.004f;
                    off += drift * (0.14f * dr.v + 0.06f * dr.fixed);
                }
                uni[o][j].inc = freq * std::exp2 (off / 12.0f) * sh.invFs;
                const bool centre = groups <= 1 || std::fabs (x) < (groups % 2 ? 0.01f : 1.01f / (float) (groups - 1));
                // JP mix curves: the centre saw fades as the side saws come up (keeps the level constant)
                const float gg = um == uSuper ? (centre ? -0.55366f * op.blend + 0.99785f : -0.73764f * op.blend * op.blend + 1.2841f * op.blend + 0.044372f)
                                              : (centre ? 1.0f : op.blend);
                const float pan = std::clamp (op.pan + d[base + 5] + d[dPan] + x * width, -1.0f, 1.0f);
                const float ang = (pan + 1.0f) * 0.25f * kPiS;
                uni[o][j].gl = gg * std::cos (ang) * 1.41421356f;
                uni[o][j].gr = gg * std::sin (ang) * 1.41421356f;
                uni[o][j].gm = gg;
                sumSq += gg * gg;
            }
            os.norm = 1.0f / std::sqrt (std::max (sumSq, 1.0e-6f));
            os.tbl = op.tbl;
            os.hpOn = um == uSuper;
            if (os.hpOn)
            {
                const float gH = std::tan (kPiS * std::min (freq, 0.45f * sh.fs) * sh.invFs);
                os.hpA1 = 1.0f / (1.0f + gH * (gH + 1.41421356f)); os.hpA2 = gH * os.hpA1; os.hpA3 = gH * os.hpA2;
            }
            const int F = op.tbl->frames;
            const float pos = std::clamp (op.pos + d[base + 0], 0.0f, 1.0f);
            const float fp = pos * (float) (F - 1);
            os.f0 = std::min ((int) fp, std::max (0, F - 2));
            os.ff = F > 1 ? fp - (float) os.f0 : 0.0f;
            os.f1 = F > 1 ? os.f0 + 1 : os.f0;
            os.warpMode = op.warpMode;
            os.warp = std::clamp (op.warp + d[base + 4], 0.0f, 1.0f);
            float bwMul = um == uSuper ? 1.0f + 0.11f * jpDetune (det) : std::exp2 (spread / 12.0f);
            switch (os.warpMode)
            {
                case wSync:     bwMul *= 1.0f + 7.0f * os.warp; break;
                case wFM:       bwMul *= 1.0f + 10.0f * os.warp; break;
                case wSqueeze:  bwMul *= 1.0f / std::max (0.04f, 1.0f - 0.96f * os.warp); break;
                case wBend:     bwMul *= 1.0f + 4.0f * os.warp; break;
                case wAsym:     bwMul *= 1.0f + 4.0f * std::fabs (2.0f * os.warp - 1.0f); break;
                case wMirror:   bwMul *= 2.0f; break;
                case wRemap:    bwMul *= 1.0f + 2.0f * os.warp; break;
                default: break;
            }
            os.mip = Table::mipFor (freq * bwMul, sh.mipFs);
            os.sqW = 0.5f - 0.48f * os.warp;
            os.bendA = 8.0f * os.warp;
            os.asymA = (2.0f * os.warp - 1.0f) * 8.0f;
            os.qSteps = std::exp2 (1.0f + (1.0f - os.warp) * 7.0f);
            { const float ratio = 1.0f + 7.0f * os.warp; os.syncEnd = ratio - std::floor (ratio); }
        }

        {
            const float subPitch = basePitch - 12.0f * (float) (2 - std::clamp (p.subOct, 0, 2));
            subInc = 440.0f * std::exp2 ((subPitch - 69.0f) / 12.0f) * sh.invFs;
            const float sl = p.subOn ? std::clamp (p.subLevel + d[dSub], 0.0f, 1.0f) : 0.0f;
            setRamp (subLevel, subLevelInc, sl * sl);
            const float nl = p.noiseOn ? std::clamp (p.noiseLevel + d[dNoise], 0.0f, 1.0f) : 0.0f;
            setRamp (noiseLevel, noiseLevelInc, nl * nl * 0.5f);
            noiseColor = p.noiseColor;
            noiseA  = 1.0f - std::exp (-2.0f * kPiS * 600.0f * sh.invFs);
            noiseA2 = 1.0f - std::exp (-2.0f * kPiS * 4000.0f * sh.invFs);
        }

        for (int f = 0; f < 2; ++f)
        {
            const FiltP& fp = p.flt[f];
            fltOn[f] = fp.on;
            if (! fp.on) continue;
            const float envAmt = fp.env + (f == 0 ? d[dFltEnv] : 0.0f);
            const float oct = envAmt * 8.0f * env[1].v + fp.key * ((float) note - 60.0f) / 12.0f + 8.0f * d[f == 0 ? dCutoff : dF2Cut]
                            + sh.p->drift * 0.3f * (noteRand2 * 2.0f - 1.0f);
            filt[f].set (fp.type, fp.cutoff * std::exp2 (oct), fp.res + d[f == 0 ? dRes : dF2Res], fp.drive + (f == 0 ? d[dDrive] : 0.0f), sh.fs);
        }
        route = p.route;

        warmK = std::clamp (p.warmth, 0.0f, 1.0f);
        warmDrive = 0.6f + 2.4f * warmK;
        warmBias = (0.12f + tolBias) * warmK;
        warmMakeup = 1.0f / std::max (0.2f, fastTanh (warmDrive * 0.7f) / 0.7f);

        const float velG = 1.0f - p.velSens * (1.0f - vel * vel);
        setRamp (gainSm, gainInc, velG * std::clamp (volMul, 0.0f, 2.0f));
        firstTick = false;
    }

    void render (float* L, float* R, int n, const Shared& sh) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            float l = 0.0f, r = 0.0f;
            for (int o = kOsc - 1; o >= 0; --o)
            {
                OscState& os = osc[o];
                os.level += os.levelInc;
                if (os.n == 0) { fmOut[o] = 0.0f; continue; }
                const float* fa = os.tbl->frame (os.f0, os.mip);
                const float* fb = os.tbl->frame (os.f1, os.mip);
                const float ff = os.ff;
                const float fm = o == 0 ? fmOut[1] : o == 1 ? fmOut[2] : fmOut[0];
                float ol = 0.0f, orr = 0.0f, om = 0.0f;
                for (int j = 0; j < os.n; ++j)
                {
                    Uni& u = uni[o][j];
                    float ph = u.ph;
                    switch (os.warpMode)
                    {
                        case wSync:     ph *= 1.0f + 7.0f * os.warp; ph -= std::floor (ph); break;
                        case wBend:     ph = ph * (1.0f + os.bendA) / (1.0f + os.bendA * ph); break;
                        case wSqueeze:  ph = ph < os.sqW ? 0.5f * ph / os.sqW : 0.5f + 0.5f * (ph - os.sqW) / (1.0f - os.sqW); break;
                        case wFM:       ph += 1.6f * os.warp * fm; ph -= std::floor (ph); break;
                        case wMirror:   { const float m = ph < 0.5f ? 2.0f * ph : 2.0f - 2.0f * ph; ph += os.warp * (m * 0.999f - ph); break; }
                        case wAsym:     { const float a = os.asymA;
                                          ph = a >= 0.0f ? ph * (1.0f + a) / (1.0f + a * ph) : 1.0f - (1.0f - ph) * (1.0f - a) / (1.0f - a * (1.0f - ph)); break; }
                        case wQuantize: ph = std::floor (ph * os.qSteps) / os.qSteps; break;
                        case wRemap:    ph -= os.warp * 0.15f * std::sin (2.0f * kPiS * ph); ph -= std::floor (ph); break;
                        default: break;
                    }
                    const float x = ph * (float) Table::kSize;
                    int i0 = (int) x; const float fr = x - (float) i0;
                    i0 &= Table::kSize - 1; const int i1 = (i0 + 1) & (Table::kSize - 1);
                    const float sa = fa[i0] + fr * (fa[i1] - fa[i0]);
                    const float sb = fb[i0] + fr * (fb[i1] - fb[i0]);
                    float s = sa + ff * (sb - sa);
                    if (os.warpMode == wSync && (u.ph < u.inc || u.ph > 1.0f - u.inc))
                    {
                        // band-limited hard sync: polyBLEP the jump at the master reset
                        const float endPh = os.syncEnd * (float) Table::kSize;
                        int e0 = (int) endPh; const float ef = endPh - (float) e0;
                        e0 &= Table::kSize - 1; const int e1 = (e0 + 1) & (Table::kSize - 1);
                        const float va = fa[e0] + ef * (fa[e1] - fa[e0]), vb = fb[e0] + ef * (fb[e1] - fb[e0]);
                        const float endV = va + ff * (vb - va);
                        const float startV = fa[0] + ff * (fb[0] - fa[0]);
                        s += 0.5f * (startV - endV) * polyBlep (u.ph, u.inc);
                    }
                    u.ph += u.inc; if (u.ph >= 1.0f) u.ph -= 1.0f;
                    ol += s * u.gl; orr += s * u.gr; om += s * u.gm;
                }
                const float gg = os.norm * os.level;
                if (os.hpOn)
                {
                    // 12 dB high-pass at the fundamental (TPT SVF, per channel)
                    auto hpf = [&] (float x, float* st)
                    {
                        const float v3 = x - st[1], v1 = os.hpA1 * st[0] + os.hpA2 * v3, v2 = st[1] + os.hpA2 * st[0] + os.hpA3 * v3;
                        st[0] = 2.0f * v1 - st[0]; st[1] = 2.0f * v2 - st[1];
                        return x - 1.41421356f * v1 - v2;
                    };
                    ol = hpf (ol, os.hpS[0]); orr = hpf (orr, os.hpS[1]);
                }
                l += ol * gg; r += orr * gg;
                fmOut[o] = om * os.norm;
            }

            subLevel += subLevelInc;
            if (subLevel > 1.0e-6f)
            {
                float s;
                const float t = subPh;
                switch (sh.p->subShape)
                {
                    case 0:  s = std::sin (2.0f * kPiS * t); break;
                    case 1:  s = 1.0f - 4.0f * std::fabs (t - 0.5f); break;
                    case 2:  s = 2.0f * t - 1.0f - polyBlep (t, subInc); break;
                    default: { float t2 = t + 0.5f; t2 -= std::floor (t2);
                               s = (t < 0.5f ? 1.0f : -1.0f) + polyBlep (t, subInc) - polyBlep (t2, subInc); s *= 0.8f; break; }
                }
                s *= subLevel;
                l += s; r += s;
            }
            subPh += subInc; if (subPh >= 1.0f) subPh -= 1.0f;

            noiseLevel += noiseLevelInc;
            if (noiseLevel > 1.0e-6f)
            {
                const float w = rng.bip();
                noiseLp += noiseA * (w - noiseLp);
                noiseLp2 += noiseA2 * (w - noiseLp2);
                const float c = noiseColor;
                const float nz = c < 0.5f ? noiseLp * 2.5f + (w - noiseLp * 2.5f) * (c * 2.0f) : w + ((w - noiseLp2) * 1.3f - w) * ((c - 0.5f) * 2.0f);
                const float w2 = rng.bip();
                l += nz * noiseLevel; r += (0.7f * nz + 0.3f * w2 * (c > 0.5f ? 1.0f : 0.4f)) * noiseLevel;
            }

            if (fltOn[0] || fltOn[1])
            {
                if (route == 0 || ! (fltOn[0] && fltOn[1]))
                {
                    if (fltOn[0]) { l = filt[0].process (l, 0); r = filt[0].process (r, 1); }
                    if (fltOn[1]) { l = filt[1].process (l, 0); r = filt[1].process (r, 1); }
                }
                else
                {
                    const float l1 = filt[0].process (l, 0), r1 = filt[0].process (r, 1);
                    const float l2 = filt[1].process (l, 0), r2 = filt[1].process (r, 1);
                    l = 0.7f * (l1 + l2); r = 0.7f * (r1 + r2);
                }
            }

            if (warmK > 0.0f)
            {
                // analog voice stage: VCA-style asymmetric soft saturation (even + odd harmonics) with DC blocking
                auto warm = [&] (float x, int c)
                {
                    const float u = x * warmDrive * tolGain + warmBias;
                    const float y = fastTanh (u) - fastTanh (warmBias);
                    const float o = x + warmK * (y * warmMakeup - x);
                    const float d = o - warmDc[c] + 0.9995f * warmY[c]; warmDc[c] = o; warmY[c] = d;
                    return d;
                };
                l = warm (l, 0); r = warm (r, 1);
            }

            const float a = env[0].next();
            env[1].next(); env[2].next();
            gainSm += gainInc;
            float amp = a * gainSm;
            if (killing) { killGain -= killStep; if (killGain <= 0.0f) { killGain = 0.0f; active = false; } amp *= killGain; }
            L[i] += l * amp;
            R[i] += r * amp;
            if (env[0].st == Env::Idle) { active = false; break; }
        }
        ampOut = env[0].v * (killing ? killGain : 1.0f);
    }

    void seed (uint32_t s) { rng.s = s; }
    float envValue (int e) const noexcept { return env[e].v; }
    float curPitch = 60;

private:
    struct Uni { float ph = 0, inc = 0, gl = 1, gr = 1, gm = 1; };
    struct OscState
    {
        int n = 0, f0 = 0, f1 = 0, mip = 0, warpMode = 0;
        const Table* tbl = nullptr;
        float ff = 0, norm = 1, warp = 0, sqW = 0.5f, bendA = 0, asymA = 0, qSteps = 256, syncEnd = 0;
        float level = 0, levelInc = 0;
        bool hpOn = false; float hpA1 = 0, hpA2 = 0, hpA3 = 0, hpS[2][2] {};
    };

    void setRamp (float& v, float& inc, float target) noexcept
    {
        if (firstTick) { v = target; inc = 0.0f; }
        else inc = (target - v) / (float) kTick;
    }

    Env env[3];
    Uni uni[kOsc][kMaxUni];
    OscState osc[kOsc];
    VFilter filt[2];
    Rng rng;
    float lastDst[kNumDst] {};
    float lfoPh[kLfo] {}, shA[kLfo] {}, shB[kLfo] {}, lastGlobal[kLfo] {};
    float targetPitch = 60, noteRand = 0, noteRand2 = 0;
    struct Drift { float v = 0, target = 0, fixed = 0; int left = 0; };
    Drift driftV[kOsc][kMaxUni];
    float subPh = 0, subInc = 0, subLevel = 0, subLevelInc = 0;
    float noiseLevel = 0, noiseLevelInc = 0, noiseColor = 0.5f, noiseLp = 0, noiseLp2 = 0, noiseA = 0.1f, noiseA2 = 0.4f;
    float gainSm = 1, gainInc = 0;
    float fmOut[kOsc] {};
    float killGain = 1, killStep = 0.01f;
    float tolGain = 1, tolBias = 0, warmK = 0, warmDrive = 1, warmBias = 0, warmMakeup = 1, warmDc[2] {}, warmY[2] {};
    bool fltOn[2] { true, false }, firstTick = true;
    int route = 0;
};

//==============================================================================
class Engine
{
public:
    void prepare (double sampleRate, double outputRate = 0.0)
    {
        sh.fs = (float) sampleRate; sh.invFs = 1.0f / sh.fs;
        sh.mipFs = (float) (outputRate > 0.0 ? outputRate : sampleRate);
        (void) WavetableBank::get();
        for (int i = 0; i < kPool; ++i) { voices[i].clear(); voices[i].seed (0x1234567u + 7919u * (uint32_t) i); }
        tickLeft = 0; lastPitch = -1.0f; stackN = 0;
        std::fill (std::begin (held), std::end (held), false);
    }

    void setPatch (const Patch& p) noexcept
    {
        patch = p;
        sh.p = &patch;
        for (int e = 0; e < 3; ++e)
        {
            const float a = std::max (p.env[e][0], e == 0 ? 0.0006f : 0.0f);
            const float d = std::max (p.env[e][1], 0.001f);
            const float r = std::max (p.env[e][3], e == 0 ? 0.003f : 0.001f);
            auto& c = sh.ec[e];
            c.ac = a <= 0.0f ? 1.0f : 1.0f - std::exp (-1.466f / (a * sh.fs));
            c.dc = std::exp (-4.6f / (d * sh.fs));
            c.s  = std::clamp (p.env[e][2], 0.0f, 1.0f);
            c.rc = std::exp (-6.9f / (r * sh.fs));
        }
        sh.glideCoef = p.glide <= 0.0005f ? 1.0f : 1.0f - std::exp (-(float) kTick * 4.6f / (p.glide * sh.fs));
    }

    void setTransport (double bpmIn, bool playingIn, double ppqIn) noexcept { bpm = bpmIn > 1 ? bpmIn : 120.0; playing = playingIn; ppq = ppqIn; }

    void beginBlock() noexcept
    {
        for (int l = 0; l < kLfo; ++l)
        {
            const double hz = patch.lfoSync[l] ? bpm / 60.0 / patch.lfoBeats[l] : (double) patch.lfoRate[l];
            sh.lfoInc[l] = (float) (hz * kTick / sh.fs);
            if (patch.lfoSync[l] && playing)
            {
                const double ph = ppq / patch.lfoBeats[l];
                globalPh[l] = (float) (ph - std::floor (ph));
            }
        }
    }

    // Global modulation (sources that exist without a voice) for the FX destinations.
    // Values of the sources that exist without a voice (LFOs on their global clock, macros, wheels...)
    void globalSources (float* src) noexcept
    {
        for (int i = 0; i < kNumSrc; ++i) src[i] = 0.0f;
        for (int l = 0; l < kLfo; ++l)
        {
            if (globalPh[l] < gLast[l]) { gShA[l] = gShB[l]; gShB[l] = rng.bip(); }
            gLast[l] = globalPh[l];
            src[sL1 + l] = lfoShape (patch.lfoShape[l], globalPh[l], gShA[l], gShB[l], patch.lfoLut[l]);
        }
        Voice* v = newestVoice();
        if (v != nullptr) { src[sVel] = v->vel; src[sKey] = ((float) v->note - 60.0f) / 60.0f; }
        src[sMW] = sh.modWheel; src[sAT] = sh.aftertouch; src[sBend] = sh.bendNorm;
        for (int m = 0; m < 4; ++m) src[sM1 + m] = patch.macro[m];
        if (v != nullptr) { src[sE1] = v->envValue (0); src[sE2] = v->envValue (1); src[sE3] = v->envValue (2); }
    }

    void globalFxMods (const float* src, float* out) noexcept
    {
        for (int i = 0; i < kNumFxDst; ++i) out[i] = 0.0f;
        for (const auto& s : patch.mod)
            if (s.src > sOff && s.src < kNumSrc && s.dst >= dFxDistDrive && s.dst < kNumDst)
                out[s.dst - dFxDistDrive] += s.amt * src[s.src];
    }

    void noteOn (int note, float vel)
    {
        const bool mono = patch.voices <= 1;
        held[note & 127] = true;
        pushStack (note);
        ++counter;
        const bool glideOK = patch.glide > 0.0005f && lastPitch >= 0.0f;
        if (mono)
        {
            Voice* v = newestVoice();
            if (v != nullptr && v->gate && patch.legato)
            {
                v->legatoTo (note, vel);
                if (patch.glide <= 0.0005f) v->curPitch = (float) note;
            }
            else if (v != nullptr && v->active && ! v->killing)
                v->start (note, vel, glideOK ? v->curPitch : (float) note, glideOK, sh, rng, counter);
            else
                freeVoice()->start (note, vel, lastPitch, glideOK, sh, rng, counter);
            lastPitch = (float) note;
            return;
        }
        int gated = 0; Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.active && v.gate && ! v.killing) { ++gated; if (oldest == nullptr || v.age < oldest->age) oldest = &v; }
        if (gated >= patch.voices && oldest != nullptr) oldest->kill (sh.fs);
        freeVoice()->start (note, vel, lastPitch, glideOK, sh, rng, counter);
        lastPitch = (float) note;
    }

    void noteOff (int note)
    {
        held[note & 127] = false;
        removeStack (note);
        if (patch.voices <= 1)
        {
            Voice* v = newestVoice();
            if (v == nullptr || v->note != note) return;
            if (stackN > 0)
            {
                const int back = stack[stackN - 1];
                if (patch.legato) v->legatoTo (back, v->vel);
                else v->start (back, v->vel, v->curPitch, patch.glide > 0.0005f, sh, rng, ++counter);
                lastPitch = (float) back;
                return;
            }
            if (sustainPedal) v->sustained = true; else v->release();
            return;
        }
        for (auto& v : voices)
            if (v.active && v.gate && ! v.killing && v.note == note)
            {
                if (sustainPedal) v.sustained = true; else v.release();
            }
    }

    void setSustain (bool on)
    {
        sustainPedal = on;
        if (! on)
            for (auto& v : voices)
                if (v.active && v.sustained) { v.sustained = false; if (! held[v.note & 127]) v.release(); }
    }
    void allNotesOff() { for (auto& v : voices) if (v.active) v.release(); stackN = 0; std::fill (std::begin (held), std::end (held), false); }
    void setPitchBend (float b) noexcept { sh.bendNorm = b; }
    void setModWheel (float m) noexcept  { sh.modWheel = m; }
    void setAftertouch (float a) noexcept { sh.aftertouch = a; }

    void render (float* L, float* R, int n) noexcept
    {
        sh.bendSemis = sh.bendNorm * (float) patch.bend;
        int done = 0;
        while (done < n)
        {
            if (tickLeft == 0)
            {
                for (int l = 0; l < kLfo; ++l) { globalPh[l] += sh.lfoInc[l]; globalPh[l] -= std::floor (globalPh[l]); sh.lfoGlobal[l] = globalPh[l]; }
                for (auto& v : voices) if (v.active) v.tick (sh);
                tickLeft = kTick;
            }
            const int m = std::min (n - done, tickLeft);
            for (auto& v : voices) if (v.active) v.render (L + done, R + done, m, sh);
            tickLeft -= m; done += m;
        }
    }

    int activeVoices() const noexcept { int c = 0; for (auto& v : voices) c += v.active ? 1 : 0; return c; }
    void voiceLevels (float* out16) const noexcept
    {
        int k = 0;
        for (auto& v : voices) if (v.active && k < 16) out16[k++] = v.ampOut;
        for (; k < 16; ++k) out16[k] = 0.0f;
    }
    float globalLfoPhase (int l) const noexcept { return globalPh[l]; }

private:
    Voice* freeVoice() noexcept
    {
        for (auto& v : voices) if (! v.active) return &v;
        Voice* best = &voices[0];
        for (auto& v : voices) if ((! v.gate && best->gate) || (v.gate == best->gate && v.age < best->age)) best = &v;
        best->active = false;
        return best;
    }
    Voice* newestVoice() noexcept
    {
        Voice* best = nullptr;
        for (auto& v : voices) if (v.active && ! v.killing && (best == nullptr || v.age > best->age)) best = &v;
        return best;
    }
    void pushStack (int n) { removeStack (n); if (stackN < 128) stack[stackN++] = n; }
    void removeStack (int n) { int w = 0; for (int i = 0; i < stackN; ++i) if (stack[i] != n) stack[w++] = stack[i]; stackN = w; }

    Patch patch;
    Shared sh;
    Voice voices[kPool];
    Rng rng;
    uint64_t counter = 0;
    int tickLeft = 0;
    float lastPitch = -1.0f;
    bool held[128] {};
    int stack[128] {}; int stackN = 0;
    bool sustainPedal = false;
    double bpm = 120.0, ppq = 0.0; bool playing = false;
    float globalPh[kLfo] {}, gLast[kLfo] {}, gShA[kLfo] {}, gShB[kLfo] {};
};

} // namespace lumen::synth
