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
constexpr int   kPool = 32;          // voice pool (polyphony limit + room for fading voices)
constexpr int   kMaxUni = 16;

enum Src { sOff, sL1, sL2, sE2, sE3, sVel, sMW, sAT, sKey, sM1, sM2, sM3, sM4, sRnd, kNumSrc };
enum Dst { dOff, dAPos, dAPitch, dALevel, dADet, dAWarp, dBPos, dBPitch, dBLevel, dBDet, dBWarp,
           dPitch, dCutoff, dRes, dDrive, dSub, dNoise, dPan, dVolume, dL1Rate, dL2Rate, dWidth, kNumDst };
enum Warp { wOff, wSync, wBend, wSqueeze, wFM };
enum FType { fLadder, fLP12, fLP24, fHP12, fHP24, fBP, fNotch };
enum LShape { lSine, lTri, lSawUp, lSawDown, lSquare, lPump, lSH, lSmooth };

struct OscP
{
    bool on = true; int table = 1; float pos = 0; int warpMode = 0; float warp = 0;
    int oct = 0, semi = 0; float fine = 0, level = 0.75f, pan = 0;
    int uni = 1; float det = 0.25f, blend = 0.75f, width = 0.8f, rand = 1;
};
struct ModSlot { int src = 0, dst = 0; float amt = 0; };

struct Patch
{
    OscP osc[2];
    bool subOn = false; int subShape = 0, subOct = 1; float subLevel = 0.6f;
    bool noiseOn = false; float noiseLevel = 0.25f, noiseColor = 0.5f;
    bool fltOn = true; int fltType = 0; float cutoff = 20000, res = 0.1f, drive = 0, fltEnv = 0, keytrack = 0;
    float env[3][4] { { 0.002f, 0.4f, 1, 0.12f }, { 0.001f, 0.4f, 0, 0.12f }, { 0.001f, 0.4f, 0, 0.12f } };
    int lfoShape[2] { 0, 0 }; float lfoRate[2] { 2, 2 }; bool lfoSync[2] { true, true }; int lfoDiv[2] { 6, 6 }; bool lfoRetrig[2] { true, true };
    double lfoBeats[2] { 1, 1 };
    ModSlot mod[8];
    float macro[4] { 0, 0, 0, 0 };
    int voices = 8; bool legato = false; float glide = 0; int bend = 2; float velSens = 0.5f;
};

//==============================================================================
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    inline uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float uni() noexcept  { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // 0..1
    inline float bip() noexcept  { return uni() * 2.0f - 1.0f; }
};

inline float fastTanh (float x) noexcept
{
    x = std::clamp (x, -4.5f, 4.5f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
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
// Voice filter: zero-delay-feedback ladder (with saturating feedback) and TPT SVF modes. Stereo.
struct VFilter
{
    int type = 0; float G = 0, beta = 1, k = 0, g = 0, a1 = 0, a2 = 0, a3 = 0, ks = 1.4f, preG = 1, postG = 1;
    float s[2][4] {};
    float sv[2][4] {};

    void reset() noexcept { for (auto& c : s) for (auto& x : c) x = 0; for (auto& c : sv) for (auto& x : c) x = 0; }

    void set (int t, float fc, float res, float drive, float fs) noexcept
    {
        type = t;
        fc = std::clamp (fc, 15.0f, 0.46f * fs);
        g = std::tan (kPiS * fc / fs);
        G = g / (1.0f + g); beta = 1.0f / (1.0f + g);
        res = std::clamp (res, 0.0f, 1.0f);
        k = 3.95f * res * (0.75f + 0.25f * res);
        ks = 2.0f - 1.96f * std::pow (res, 0.7f);                     // SVF damping
        a1 = 1.0f / (1.0f + g * (g + ks)); a2 = g * a1; a3 = g * a2;
        preG  = 1.0f + std::clamp (drive, 0.0f, 1.0f) * 9.0f;
        postG = 1.0f / std::sqrt (preG);
    }

    inline float svf (float x, float* st, float& lp, float& bp) noexcept
    {
        const float v3 = x - st[1];
        const float v1 = a1 * st[0] + a2 * v3;
        const float v2 = st[1] + a2 * st[0] + a3 * v3;
        st[0] = 2.0f * v1 - st[0]; st[1] = 2.0f * v2 - st[1];
        lp = v2; bp = v1;
        return x - ks * v1 - v2;     // hp
    }

    inline float process (float x, int ch) noexcept
    {
        x = 2.0f * fastTanh (0.5f * x * preG) * postG;
        if (type == fLadder)
        {
            float* st = s[ch];
            const float S = beta * (G * G * G * st[0] + G * G * st[1] + G * st[2] + st[3]);
            const float G4 = G * G * G * G;
            float u = (x - k * S) / (1.0f + k * G4);
            u = fastTanh (u);
            float y = u;
            for (int i = 0; i < 4; ++i)
            {
                const float v = (y - st[i]) * G;
                y = v + st[i];
                st[i] = y + v;
            }
            return y * (1.0f + 0.55f * k);
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
struct Shared   // per-block values every voice reads
{
    const Patch* p = nullptr;
    float fs = 48000, invFs = 1.0f / 48000;
    EnvCoef ec[3];
    float bendSemis = 0, modWheel = 0, aftertouch = 0;
    float glideCoef = 1;
    float lfoGlobal[2] { 0, 0 };        // phase for free (non-retrig) LFOs
    float lfoInc[2] { 0, 0 };           // phase increment per tick (before rate modulation)
};

inline float lfoShape (int shape, float ph, float shA, float shB) noexcept
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
        default: return 0.0f;
    }
}

//==============================================================================
class Voice
{
public:
    bool active = false, gate = false, sustained = false, killing = false;
    int note = 60; float vel = 1; uint64_t age = 0;
    float ampOut = 0;          // last envelope value (for UI LEDs)

    void start (int n, float v, float fromPitch, bool glideFrom, const Shared& sh, Rng& rg, uint64_t a)
    {
        const bool wasActive = active && ! killing;
        note = n; vel = v; age = a; active = true; gate = true; sustained = false; killing = false; killGain = 1.0f;
        targetPitch = (float) n;
        curPitch = glideFrom ? fromPitch : (float) n;
        noteRand = rg.uni();
        for (int e = 0; e < 3; ++e) { env[e].c = &sh.ec[e]; if (! wasActive) env[e].v = 0; env[e].on(); }
        const Patch& p = *sh.p;
        for (int o = 0; o < 2; ++o)
            for (int j = 0; j < kMaxUni; ++j)
                uni[o][j].ph = rg.uni() * p.osc[o].rand;
        for (int l = 0; l < 2; ++l)
        {
            if (p.lfoRetrig[l]) lfoPh[l] = 0.0f;
            shA[l] = rg.bip(); shB[l] = rg.bip();
        }
        subPh = 0.0f;
        if (! wasActive) { filt.reset(); fmA = fmB = 0.0f; noiseLp = noiseLp2 = 0.0f; }
        firstTick = true;
        tick (sh);
    }

    void legatoTo (int n, float v) { note = n; targetPitch = (float) n; vel = v; }
    void release() { gate = false; for (auto& e : env) e.off(); }
    void kill (float fs) { killing = true; gate = false; killStep = 1.0f / (0.003f * fs); }

    // ---- control-rate update ----
    void tick (const Shared& sh) noexcept
    {
        const Patch& p = *sh.p;
        curPitch += (targetPitch - curPitch) * sh.glideCoef;

        // LFOs (rate modulation uses last tick's matrix result)
        float lfoV[2];
        for (int l = 0; l < 2; ++l)
        {
            const float rateMul = std::exp2 (4.0f * std::clamp (lastDst[l == 0 ? dL1Rate : dL2Rate], -1.0f, 1.0f));
            if (p.lfoRetrig[l])
            {
                if (! firstTick) lfoPh[l] += sh.lfoInc[l] * rateMul;
                if (lfoPh[l] >= 1.0f) { lfoPh[l] -= std::floor (lfoPh[l]); shA[l] = shB[l]; shB[l] = rng.bip(); }
                lfoV[l] = lfoShape (p.lfoShape[l], lfoPh[l], shA[l], shB[l]);
            }
            else
            {
                const float ph = sh.lfoGlobal[l];
                if (ph < lastGlobal[l]) { shA[l] = shB[l]; shB[l] = rng.bip(); }
                lastGlobal[l] = ph;
                lfoV[l] = lfoShape (p.lfoShape[l], ph, shA[l], shB[l]);
            }
        }

        float src[kNumSrc] {};
        src[sL1] = lfoV[0]; src[sL2] = lfoV[1]; src[sE2] = env[1].v; src[sE3] = env[2].v;
        src[sVel] = vel; src[sMW] = sh.modWheel; src[sAT] = sh.aftertouch; src[sKey] = ((float) note - 60.0f) / 60.0f;
        for (int m = 0; m < 4; ++m) src[sM1 + m] = p.macro[m];
        src[sRnd] = noteRand * 2.0f - 1.0f;

        float d[kNumDst] {};
        float volMul = 1.0f;
        for (const auto& s : p.mod)
        {
            if (s.src <= sOff || s.src >= kNumSrc || s.dst <= dOff || s.dst >= kNumDst || std::fabs (s.amt) < 1.0e-9f) continue;
            const float v = src[s.src];
            if (s.dst == dVolume)
            {
                const bool bip = (s.src == sL1 || s.src == sL2 || s.src == sRnd);
                const float u = std::clamp (bip ? 0.5f * (v + 1.0f) : v, 0.0f, 1.0f);
                volMul *= s.amt > 0.0f ? (1.0f - s.amt) + s.amt * u : 1.0f + s.amt * u;
            }
            else d[s.dst] += s.amt * v;
        }
        std::copy (std::begin (d), std::end (d), std::begin (lastDst));

        const float basePitch = curPitch + sh.bendSemis + 24.0f * d[dPitch];

        // ---- oscillators ----
        for (int o = 0; o < 2; ++o)
        {
            const OscP& op = p.osc[o];
            OscState& os = osc[o];
            os.on = op.on;
            const int base = o == 0 ? dAPos : dBPos;     // A/B dst blocks have the same layout
            const float levelT = op.on ? std::clamp (op.level + d[base + 2], 0.0f, 1.0f) : 0.0f;
            setRamp (os.level, os.levelInc, levelT * levelT);     // squared: perceptual level knob
            if (! op.on && os.level <= 0.0f) { os.n = 0; continue; }

            const float pitch = basePitch + 12.0f * op.oct + op.semi + 0.01f * op.fine + 24.0f * d[base + 1];
            const float freq = 440.0f * std::exp2 ((pitch - 69.0f) / 12.0f);
            const int n = std::clamp (op.uni, 1, kMaxUni);
            os.n = n;
            const float det = std::clamp (op.det + d[base + 3], 0.0f, 1.0f);
            const float spread = 1.2f * det * std::sqrt (det);                    // semitones at the outer voices
            const float width = std::clamp (op.width + d[dWidth], 0.0f, 1.0f);
            const float blend = op.blend;
            float sumSq = 0.0f;
            for (int j = 0; j < n; ++j)
            {
                const float x = n == 1 ? 0.0f : 2.0f * (float) j / (float) (n - 1) - 1.0f;
                const float off = spread * x * (0.55f + 0.45f * std::fabs (x));
                uni[o][j].inc = freq * std::exp2 (off / 12.0f) * sh.invFs;
                const bool centre = n == 1 || std::fabs (x) < (n % 2 ? 0.01f : 1.01f / (float) (n - 1));
                const float g = centre ? 1.0f : blend;
                const float pan = std::clamp (op.pan + d[dPan] + x * width, -1.0f, 1.0f);
                const float ang = (pan + 1.0f) * 0.25f * kPiS;
                uni[o][j].gl = g * std::cos (ang) * 1.41421356f;
                uni[o][j].gr = g * std::sin (ang) * 1.41421356f;
                uni[o][j].gm = g;
                sumSq += g * g;
            }
            os.norm = 1.0f / std::sqrt (std::max (sumSq, 1.0e-6f));
            os.monoNorm = os.norm;

            os.table = std::clamp (op.table, 0, WavetableBank::numTables() - 1);
            const float pos = std::clamp (op.pos + d[base + 0], 0.0f, 1.0f);
            const float fp = pos * (float) (WavetableBank::kFrames - 1);
            os.f0 = std::min ((int) fp, WavetableBank::kFrames - 2);
            os.ff = fp - (float) os.f0;
            os.warpMode = op.warpMode;
            os.warp = std::clamp (op.warp + d[base + 4], 0.0f, 1.0f);
            float bwMul = std::exp2 (spread / 12.0f);
            if (os.warpMode == wSync) bwMul *= 1.0f + 7.0f * os.warp;
            if (os.warpMode == wFM)   bwMul *= 1.0f + 10.0f * os.warp;
            if (os.warpMode == wSqueeze) bwMul *= 1.0f / std::max (0.04f, 1.0f - 0.96f * os.warp);
            if (os.warpMode == wBend) bwMul *= 1.0f + 4.0f * os.warp;
            os.mip = WavetableBank::mipFor (freq * bwMul, sh.fs);
            os.sqW = 0.5f - 0.48f * os.warp;
            os.bendA = 8.0f * os.warp;
        }

        // ---- sub / noise ----
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

        // ---- filter ----
        fltOn = p.fltOn;
        if (fltOn)
        {
            const float oct = p.fltEnv * 8.0f * env[1].v + p.keytrack * ((float) note - 60.0f) / 12.0f + 8.0f * d[dCutoff];
            const float fc = p.cutoff * std::exp2 (oct);
            filt.set (p.fltType, fc, p.res + d[dRes], p.drive + d[dDrive], sh.fs);
        }

        // ---- amp ----
        const float velG = 1.0f - p.velSens * (1.0f - vel * vel);
        setRamp (gainSm, gainInc, velG * std::clamp (volMul, 0.0f, 2.0f));
        firstTick = false;
    }

    // ---- audio-rate render ----
    void render (float* L, float* R, int n, const Shared& sh, const WavetableBank& bank) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            float l = 0.0f, r = 0.0f;
            // Osc B first (FM source for A is B's current sample; B's FM source is A's previous sample)
            for (int o = 1; o >= 0; --o)
            {
                OscState& os = osc[o];
                os.level += os.levelInc;
                if (os.n == 0) { if (o == 1) fmB = 0.0f; else fmA = 0.0f; continue; }
                const float* fa = bank.frame (os.table, os.f0, os.mip);
                const float* fb = bank.frame (os.table, os.f0 + 1, os.mip);
                const float ff = os.ff;
                const float fm = o == 0 ? fmB : fmA;
                float ol = 0.0f, orr = 0.0f, om = 0.0f;
                for (int j = 0; j < os.n; ++j)
                {
                    Uni& u = uni[o][j];
                    float ph = u.ph;
                    switch (os.warpMode)
                    {
                        case wSync:    { ph *= 1.0f + 7.0f * os.warp; ph -= std::floor (ph); break; }
                        case wBend:    { ph = ph * (1.0f + os.bendA) / (1.0f + os.bendA * ph); break; }
                        case wSqueeze: { ph = ph < os.sqW ? 0.5f * ph / os.sqW : 0.5f + 0.5f * (ph - os.sqW) / (1.0f - os.sqW); break; }
                        case wFM:      { ph += 1.6f * os.warp * fm; ph -= std::floor (ph); break; }
                        default: break;
                    }
                    const float x = ph * (float) WavetableBank::kSize;
                    int i0 = (int) x; const float fr = x - (float) i0;
                    i0 &= WavetableBank::kSize - 1; const int i1 = (i0 + 1) & (WavetableBank::kSize - 1);
                    const float sa = fa[i0] + fr * (fa[i1] - fa[i0]);
                    const float sb = fb[i0] + fr * (fb[i1] - fb[i0]);
                    const float s = sa + ff * (sb - sa);
                    u.ph += u.inc; if (u.ph >= 1.0f) u.ph -= 1.0f;
                    ol += s * u.gl; orr += s * u.gr; om += s * u.gm;
                }
                const float g = os.norm * os.level;
                l += ol * g; r += orr * g;
                if (o == 1) fmB = om * os.monoNorm; else fmA = om * os.monoNorm;
            }

            // Sub (direct, mono)
            subLevel += subLevelInc;
            if (subLevel > 1.0e-6f)
            {
                float s;
                const float t = subPh;
                switch (sh.p->subShape)
                {
                    case 0:  s = std::sin (2.0f * kPiS * t); break;
                    case 1:  s = 1.0f - 4.0f * std::fabs (t - 0.5f); break;          // phase-offset triangle
                    case 2:  s = 2.0f * t - 1.0f - polyBlep (t, subInc); break;
                    default: { float t2 = t + 0.5f; t2 -= std::floor (t2);
                               s = (t < 0.5f ? 1.0f : -1.0f) + polyBlep (t, subInc) - polyBlep (t2, subInc); s *= 0.8f; break; }
                }
                s *= subLevel;
                l += s; r += s;
            }
            subPh += subInc; if (subPh >= 1.0f) subPh -= 1.0f;

            // Noise
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

            if (fltOn) { l = filt.process (l, 0); r = filt.process (r, 1); }

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

private:
    struct Uni { float ph = 0, inc = 0, gl = 1, gr = 1, gm = 1; };
    struct OscState
    {
        bool on = false; int n = 0, table = 0, f0 = 0, mip = 0, warpMode = 0;
        float ff = 0, norm = 1, monoNorm = 1, warp = 0, sqW = 0.5f, bendA = 0;
        float level = 0, levelInc = 0;
    };

    // Linear ramp to `target` across one tick (instant on the very first tick).
    void setRamp (float& v, float& inc, float target) noexcept
    {
        if (firstTick) { v = target; inc = 0.0f; }
        else inc = (target - v) / (float) kTick;
    }

    Env env[3];
    Uni uni[2][kMaxUni];
    OscState osc[2];
    VFilter filt;
    Rng rng;
    float lastDst[kNumDst] {};
    float lfoPh[2] { 0, 0 }, shA[2] { 0, 0 }, shB[2] { 0, 0 }, lastGlobal[2] { 0, 0 };
    float curPitch = 60, targetPitch = 60, noteRand = 0;
    float subPh = 0, subInc = 0, subLevel = 0, subLevelInc = 0;
    float noiseLevel = 0, noiseLevelInc = 0, noiseColor = 0.5f, noiseLp = 0, noiseLp2 = 0, noiseA = 0.1f, noiseA2 = 0.4f;
    float gainSm = 1, gainInc = 0;
    float fmA = 0, fmB = 0;
    float killGain = 1, killStep = 0.01f;
    bool fltOn = true, firstTick = true;

    friend class Engine;
public:
    void seed (uint32_t s) { rng.s = s; }
};

//==============================================================================
class Engine
{
public:
    void prepare (double sampleRate)
    {
        sh.fs = (float) sampleRate; sh.invFs = 1.0f / sh.fs;
        (void) WavetableBank::get();
        for (int i = 0; i < kPool; ++i) { voices[i] = Voice(); voices[i].seed (0x1234567u + 7919u * (uint32_t) i); }
        tickLeft = 0; heldCount = 0; lastPitch = -1.0f;
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
    double getBpm() const noexcept { return bpm; }

    void beginBlock (int /*numSamples*/) noexcept
    {
        for (int l = 0; l < 2; ++l)
        {
            const double hz = patch.lfoSync[l] ? bpm / 60.0 / divBeatsCache (l) : (double) patch.lfoRate[l];
            sh.lfoInc[l] = (float) (hz * kTick / sh.fs);
            if (patch.lfoSync[l] && playing)
            {
                const double ph = ppq / divBeatsCache (l);
                globalPh[l] = (float) (ph - std::floor (ph));
            }
        }
    }

    // MIDI-ish controls
    void noteOn (int note, float vel)
    {
        const bool mono = patch.voices <= 1;
        held[note & 127] = true;
        pushStack (note);
        ++counter;
        if (mono)
        {
            Voice* v = monoVoice();
            const bool glideOK = patch.glide > 0.0005f && lastPitch >= 0.0f;
            if (v != nullptr && v->gate && patch.legato)
            {
                v->legatoTo (note, vel);
                if (patch.glide <= 0.0005f) v->curPitch = (float) note;
            }
            else
            {
                if (v != nullptr && v->active && ! v->killing)
                {
                    // retrigger the same voice (no click: envelopes restart from their current level)
                    v->start (note, vel, glideOK ? v->curPitch : (float) note, glideOK, sh, rng, counter);
                }
                else
                {
                    v = freeVoice();
                    v->start (note, vel, lastPitch, glideOK, sh, rng, counter);
                }
            }
            lastPitch = (float) note;
            return;
        }

        int gated = 0; Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.active && v.gate && ! v.killing) { ++gated; if (oldest == nullptr || v.age < oldest->age) oldest = &v; }
        if (gated >= patch.voices && oldest != nullptr) oldest->kill (sh.fs);
        // also limit releasing tails so the pool never fills
        Voice* v = freeVoice();
        const bool glideOK = patch.glide > 0.0005f && lastPitch >= 0.0f;
        v->start (note, vel, lastPitch, glideOK, sh, rng, counter);
        lastPitch = (float) note;
    }

    void noteOff (int note)
    {
        held[note & 127] = false;
        removeStack (note);
        if (patch.voices <= 1)
        {
            Voice* v = monoVoice();
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
    void panic()       { for (auto& v : voices) v.active = false; stackN = 0; std::fill (std::begin (held), std::end (held), false); }
    void setPitchBend (float b) noexcept { bendNorm = b; }
    void setModWheel (float m) noexcept  { sh.modWheel = m; }
    void setAftertouch (float a) noexcept { sh.aftertouch = a; }

    // Render voices (adds into L/R). MIDI is applied between render calls by the caller.
    void render (float* L, float* R, int n) noexcept
    {
        const auto& bank = WavetableBank::get();
        sh.bendSemis = bendNorm * (float) patch.bend;
        int done = 0;
        while (done < n)
        {
            if (tickLeft == 0)
            {
                for (int l = 0; l < 2; ++l) { globalPh[l] += sh.lfoInc[l]; globalPh[l] -= std::floor (globalPh[l]); sh.lfoGlobal[l] = globalPh[l]; }
                for (auto& v : voices) if (v.active) v.tick (sh);
                tickLeft = kTick;
            }
            const int m = std::min (n - done, tickLeft);
            for (auto& v : voices) if (v.active) v.render (L + done, R + done, m, sh, bank);
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

private:
    double divBeatsCache (int l) const noexcept { return patch.lfoBeats[l]; }

    Voice* freeVoice() noexcept
    {
        for (auto& v : voices) if (! v.active) return &v;
        Voice* best = &voices[0];                          // pool full: steal the quietest releasing / oldest
        for (auto& v : voices) if ((! v.gate && best->gate) || (v.gate == best->gate && v.age < best->age)) best = &v;
        best->active = false;
        return best;
    }
    Voice* monoVoice() noexcept
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
    int tickLeft = 0, heldCount = 0;
    float lastPitch = -1.0f, bendNorm = 0.0f;
    bool held[128] {};
    int stack[128] {}; int stackN = 0;
    bool sustainPedal = false;
    double bpm = 120.0, ppq = 0.0; bool playing = false;
    float globalPh[2] { 0, 0 };
};

} // namespace lumen::synth
