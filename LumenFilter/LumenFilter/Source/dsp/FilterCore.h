#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>

// Filter core shared with Lumen Synth (verified there): ZDF ladder, TPT SVF modes, comb, formant, phaser.
namespace lumen::flt
{
constexpr float kPiS = 3.14159265358979f;
enum FType { fLadder, fLP12, fLP24, fHP12, fHP24, fBP, fNotch, fCombP, fCombN, fFormant, fPhaser };
enum LShape { lSine, lTri, lSawUp, lSawDown, lSquare, lPump, lSH, lSmooth, lCustom };
constexpr int kLut = 256;

inline float fastTanh (float x) noexcept
{
    x = std::clamp (x, -3.0f, 3.0f);       // the Pade approximant peaks at exactly 1.0 at |x| = 3
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

//==============================================================================
// Voice filter (stereo): ZDF ladder, TPT SVF modes, comb +/-, 3-formant vowel filter, 4-stage phaser.
struct VFilter
{
    static constexpr int kComb = 8192;
    float compG = 1.0f;
    int type = 0; float G = 0, beta = 1, k = 0, g = 0, a1 = 0, a2 = 0, a3 = 0, ks = 1.4f, preG = 1, postG = 1;
    float s[2][4] {}, sv[2][4] {};
    // comb
    float combD = 100, combG = 0.5f, combNorm = 1, combDamp = 0.5f, combLp[2] {};
    int combPos[2] {};
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

    void resetComb() noexcept { for (auto& b : combBuf) b.fill (0.0f); combPos[0] = combPos[1] = 0; }

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
        compG = 1.0f + k * (1.0f - 0.5f * res);     // unity passband at low resonance, classic bass loss at high
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
                return y * compG;
            }
            case fCombP: case fCombN:
            {
                auto& b = combBuf[ch];
                float rp = (float) combPos[ch] - combD; while (rp < 0.0f) rp += (float) kComb;
                const int i0 = (int) rp; const float fr = rp - (float) i0;
                const float d = b[(size_t) (i0 & (kComb - 1))] + fr * (b[(size_t) ((i0 + 1) & (kComb - 1))] - b[(size_t) (i0 & (kComb - 1))]);
                combLp[ch] += combDamp * (d - combLp[ch]);
                const float y = type == fCombP ? x + combG * combLp[ch] : x - combG * combLp[ch];
                b[(size_t) combPos[ch]] = fastTanh (y);
                combPos[ch] = (combPos[ch] + 1) & (kComb - 1);
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


} // namespace lumen::flt
