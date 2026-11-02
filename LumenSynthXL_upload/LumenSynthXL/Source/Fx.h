#pragma once
#include "Common.h"

namespace lxl
{
struct OnePole
{
    float z = 0.0f;
    inline float lp (float x, float a) noexcept { z += a * (x - z); return z; }
    inline float hp (float x, float a) noexcept { return x - lp (x, a); }
    static float coef (float hz, float sr) { return 1.0f - std::exp (-kTwoPi * juce::jmin (hz, sr * 0.45f) / sr); }
};

// ------------------------------------------------------------------ Distortion
class Distortion
{
public:
    void prepare (double sr) { fs = (float) sr; lpL = lpR = hpL = hpR = {}; }
    void process (float* L, float* R, int n, int type, float drive, float tone, float mix)
    {
        const float g = 1.0f + drive * drive * 30.0f;
        const float comp = 1.0f / (1.0f + drive * 1.5f);
        const float lpA = OnePole::coef (1500.0f * std::exp2 (tone * 4.0f), fs);
        const float hpA = OnePole::coef (20.0f + tone * 120.0f, fs);
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                float& s = c == 0 ? L[i] : R[i];
                const float dry = s;
                float x = s * g;
                switch (type)
                {
                    case 0: x = fastTanh (x); break;
                    case 1: x = juce::jlimit (-1.0f, 1.0f, x); break;
                    case 2: x = fastTanh (x + 0.25f * x * x) - fastTanh (0.0f); break;
                    default: { x = x * 0.25f + 0.25f; x = x - std::floor (x); x = 1.0f - 4.0f * std::abs (x - 0.5f); break; }
                }
                auto& lp = c == 0 ? lpL : lpR;
                auto& hp = c == 0 ? hpL : hpR;
                x = hp.hp (lp.lp (x, lpA), hpA) * comp * 1.4f;
                s = dry + mix * (x - dry);
            }
        }
    }
private:
    float fs = 44100.0f;
    OnePole lpL, lpR, hpL, hpR;
};

// ------------------------------------------------------------------ Chorus / Dimension (3-voice stereo)
class Chorus
{
public:
    void prepare (double sr) { fs = (float) sr; for (auto& d : dl) d.setSize ((int) (sr * 0.06) + 4); phase = 0.0f; hpL = hpR = {}; }
    void process (float* L, float* R, int n, float rate, float depth, float mix)
    {
        const float inc = rate / fs;
        static const float base[3] = { 0.0071f, 0.0113f, 0.0157f };
        const float dep = (0.0004f + depth * 0.0038f) * fs;
        const float hpA = OnePole::coef (160.0f, fs);
        const float wet = mix, dry = 1.0f - 0.5f * mix;
        for (int i = 0; i < n; ++i)
        {
            const float l = L[i], r = R[i];
            // keep lows out of the modulated path so bass stays solid
            dl[0].push (hpL.hp (l, hpA)); dl[1].push (hpR.hp (r, hpA));
            float wl = 0.0f, wr = 0.0f;
            for (int v = 0; v < 3; ++v)
            {
                const float ph = phase + (float) v / 3.0f;
                wl += dl[0].read (base[v] * fs + dep * fastSin (ph));
                wr += dl[1].read (base[v] * fs + dep * fastSin (ph + 0.25f));
            }
            L[i] = l * dry + wet * wl * 0.42f;
            R[i] = r * dry + wet * wr * 0.42f;
            phase += inc; if (phase >= 1.0f) phase -= 1.0f;
        }
    }
private:
    float fs = 44100.0f, phase = 0.0f;
    DelayLine dl[2];
    OnePole hpL, hpR;
};

// ------------------------------------------------------------------ Hyper / Dimension (Serum-style)
/** Hyper: 6 detuned, slowly modulated short-delay copies spread across the stereo field.
    Dimension: cross-fed decorrelating delays that widen without chorusing the lows. */
class Hyper
{
public:
    void prepare (double sr)
    {
        fs = (float) sr;
        for (auto& d : dl) d.setSize ((int) (sr * 0.05) + 8);
        for (auto& d : dim) d.setSize ((int) (sr * 0.04) + 8);
        for (int v = 0; v < kV; ++v) ph[v] = (float) v / (float) kV;
        hpL = hpR = {};
    }
    void process (float* L, float* R, int n, float rate, float detune, float dimension, float mix)
    {
        static const float base[kV] = { 0.0061f, 0.0089f, 0.0117f, 0.0143f, 0.0171f, 0.0199f };
        static const float rmul[kV] = { 1.0f, 1.13f, 0.87f, 1.29f, 0.79f, 1.41f };
        static const float pan[kV]  = { -1.0f, 1.0f, -0.6f, 0.6f, -0.25f, 0.25f };
        const float dep = (0.0002f + detune * 0.0028f) * fs;
        const float hpA = OnePole::coef (180.0f, fs);
        for (int i = 0; i < n; ++i)
        {
            const float l = L[i], r = R[i];
            const float m = 0.5f * (hpL.hp (l, hpA) + hpR.hp (r, hpA));
            dl[0].push (m);
            float wl = 0.0f, wr = 0.0f;
            for (int v = 0; v < kV; ++v)
            {
                ph[v] += rate * rmul[v] / fs; if (ph[v] >= 1.0f) ph[v] -= 1.0f;
                const float s = dl[0].read (base[v] * fs + dep * fastSin (ph[v]));
                wl += s * (0.5f - 0.5f * pan[v]);
                wr += s * (0.5f + 0.5f * pan[v]);
            }
            wl *= 0.45f; wr *= 0.45f;
            // dimension: short cross-delays
            dim[0].push (l); dim[1].push (r);
            const float dL = dim[1].read (0.0123f * fs) - dim[0].read (0.0187f * fs) * 0.5f;
            const float dR = dim[0].read (0.0141f * fs) - dim[1].read (0.0211f * fs) * 0.5f;
            wl += dimension * dL * 0.6f;
            wr += dimension * dR * 0.6f;
            L[i] = l + mix * wl;
            R[i] = r + mix * wr;
        }
    }
private:
    static constexpr int kV = 6;
    float fs = 44100.0f, ph[kV] {};
    DelayLine dl[1], dim[2];
    OnePole hpL, hpR;
};

// ------------------------------------------------------------------ Phaser
class Phaser
{
public:
    void prepare (double sr) { fs = (float) sr; for (auto& c : st) for (auto& v : c) v = 0.0f; fbL = fbR = 0.0f; phase = 0.0f; }
    void process (float* L, float* R, int n, float rate, float depth, float fb, float mix)
    {
        const float inc = rate / fs;
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float lfo = 0.5f + 0.5f * fastSin (phase + (c == 0 ? 0.0f : 0.25f));
                const float fc = 180.0f * std::exp2 (lfo * depth * 4.5f + 0.2f);
                const float t = std::tan (kPi * juce::jmin (fc, fs * 0.45f) / fs);
                const float a = (t - 1.0f) / (t + 1.0f);
                float& s = c == 0 ? L[i] : R[i];
                float& fbs = c == 0 ? fbL : fbR;
                float x = s + fbs * fb;
                for (int k = 0; k < 6; ++k)
                {
                    const float y = a * x + st[c][k];
                    st[c][k] = x - a * y;
                    x = y;
                }
                fbs = fastTanh (x);
                s = s * (1.0f - mix * 0.5f) + x * mix * 0.5f;
            }
            phase += inc; if (phase >= 1.0f) phase -= 1.0f;
        }
    }
private:
    float fs = 44100.0f, phase = 0.0f, fbL = 0.0f, fbR = 0.0f;
    float st[2][6] {};
};

// ------------------------------------------------------------------ Delay
class StereoDelay
{
public:
    void prepare (double sr) { fs = (float) sr; for (auto& d : dl) d.setSize ((int) (sr * 4.2) + 8); clear(); }
    void clear() { for (auto& d : dl) d.clear(); lpL = lpR = hpL = hpR = {}; curTime = -1.0f; }
    void process (float* L, float* R, int n, float timeSec, float fb, bool pingPong, float tone, float mix)
    {
        const float target = juce::jlimit (0.002f, 4.0f, timeSec) * fs;
        if (curTime < 0.0f) curTime = target;
        const float lpA = OnePole::coef (2000.0f * std::exp2 (tone * 3.3f), fs);
        const float hpA = OnePole::coef (60.0f + (1.0f - tone) * 300.0f, fs);
        const float smooth = 1.0f - std::exp (-1.0f / (0.08f * fs));
        for (int i = 0; i < n; ++i)
        {
            curTime += smooth * (target - curTime);
            const float dLs = dl[0].read (curTime), dRs = dl[1].read (curTime);
            const float wl = hpL.hp (lpL.lp (dLs, lpA), hpA);
            const float wr = hpR.hp (lpR.lp (dRs, lpA), hpA);
            const float inL = L[i], inR = R[i];
            if (pingPong)
            {
                const float mono = 0.5f * (inL + inR);
                dl[0].push (fastTanh (mono + wr * fb));
                dl[1].push (fastTanh (wl * fb));
            }
            else
            {
                dl[0].push (fastTanh (inL + wl * fb));
                dl[1].push (fastTanh (inR + wr * fb));
            }
            L[i] = inL + mix * wl;
            R[i] = inR + mix * wr;
        }
    }
private:
    float fs = 44100.0f, curTime = -1.0f;
    DelayLine dl[2];
    OnePole lpL, lpR, hpL, hpR;
};

// ------------------------------------------------------------------ Reverb (stereo diffused 8-line FDN, modulated)
class FdnReverb
{
public:
    void prepare (double sr)
    {
        fs = (float) sr;
        for (auto& l : lines) l.setSize ((int) (0.35 * sr) + 32);
        for (auto& c : ap) for (auto& a : c) a.setSize ((int) (0.03 * sr) + 8);
        for (auto& p : pre) p.setSize ((int) (0.25 * sr) + 8);
        clear();
    }
    void clear()
    {
        for (auto& l : lines) l.clear();
        for (auto& c : ap) for (auto& a : c) a.clear();
        for (auto& p : pre) p.clear();
        for (auto& d : damp) d = {};
        for (auto& d : lowcut) d = {};
        for (int i = 0; i < 8; ++i) modPh[i] = (float) i * 0.125f;
        inHpL = inHpR = {};
    }
    void process (float* L, float* R, int n, float size, float decay, float dampAmt, float preMs, float width, float mix)
    {
        static const float baseLen[8] = { 0.0453f, 0.0527f, 0.0601f, 0.0679f, 0.0751f, 0.0829f, 0.0907f, 0.0983f };
        static const float apLen[2][4] = { { 0.0047f, 0.0071f, 0.0109f, 0.0157f }, { 0.0053f, 0.0079f, 0.0119f, 0.0167f } };
        static const float modRate[8] = { 0.31f, 0.43f, 0.57f, 0.71f, 0.37f, 0.53f, 0.67f, 0.79f };
        const float scale = 0.3f + size * 1.6f;
        float len[8], g[8];
        for (int i = 0; i < 8; ++i)
        {
            len[i] = baseLen[i] * scale * fs;
            g[i] = std::pow (10.0f, -3.0f * (len[i] / fs) / juce::jmax (0.1f, decay));
        }
        const float dampA = OnePole::coef (1200.0f + (1.0f - dampAmt) * 15000.0f, fs);
        const float lcA = OnePole::coef (90.0f, fs);
        const float inHp = OnePole::coef (180.0f, fs);
        const float preS = juce::jmax (1.0f, preMs * 0.001f * fs);
        const float modDepth = 0.0009f * fs;
        const float apScale = 0.5f + size * 0.8f;

        for (int s = 0; s < n; ++s)
        {
            pre[0].push (inHpL.hp (L[s], inHp));
            pre[1].push (inHpR.hp (R[s], inHp));
            float x[2] = { pre[0].read (preS), pre[1].read (preS) };
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 4; ++i)
                {
                    const float d = ap[c][i].read (apLen[c][i] * fs * apScale);
                    const float v = x[c] + 0.6f * d;
                    ap[c][i].push (v);
                    x[c] = d - 0.6f * v;
                }
            float o[8];
            for (int i = 0; i < 8; ++i)
            {
                modPh[i] += modRate[i] / fs; if (modPh[i] >= 1.0f) modPh[i] -= 1.0f;
                o[i] = lines[i].read (len[i] + modDepth * (1.0f + fastSin (modPh[i])));
            }
            float h[8];
            for (int i = 0; i < 8; ++i) h[i] = o[i];
            for (int step = 1; step < 8; step <<= 1)
                for (int i = 0; i < 8; i += step * 2)
                    for (int j = i; j < i + step; ++j)
                    {
                        const float a = h[j], b = h[j + step];
                        h[j] = a + b; h[j + step] = a - b;
                    }
            const float norm = 0.35355339f;
            for (int i = 0; i < 8; ++i)
            {
                float v = damp[i].lp (h[i] * norm * g[i], dampA);
                v -= lowcut[i].lp (v, lcA) * 0.5f;
                lines[i].push (v + x[i & 1] * ((i >> 1) & 1 ? -0.5f : 0.5f));
            }
            const float wl = (o[0] - o[2] + o[4] - o[6]) * 0.6f;
            const float wr = (o[1] - o[3] + o[5] - o[7]) * 0.6f;
            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
            L[s] = L[s] * (1.0f - mix * 0.25f) + (mid + side) * mix;
            R[s] = R[s] * (1.0f - mix * 0.25f) + (mid - side) * mix;
        }
    }
private:
    float fs = 44100.0f, modPh[8] {};
    DelayLine lines[8], ap[2][4], pre[2];
    OnePole damp[8], lowcut[8], inHpL, inHpR;
};

// ------------------------------------------------------------------ Tape wobble
class TapeWobble
{
public:
    void prepare (double sr) { fs = (float) sr; for (auto& d : dl) d.setSize ((int) (sr * 0.06) + 8); wow = flt = 0.0f; rnd = 0.0f; lpL = lpR = {}; }
    void process (float* L, float* R, int n, float wowAmt, float flutterAmt, float sat, float mix)
    {
        const float base = 0.012f * fs;
        const float lpA = OnePole::coef (16000.0f - sat * 7000.0f, fs);
        const float drive = 1.0f + sat * 3.0f;
        for (int i = 0; i < n; ++i)
        {
            wow += 0.6f / fs; if (wow >= 1.0f) { wow -= 1.0f; rndTarget = rng.bip(); }
            flt += 9.0f / fs; if (flt >= 1.0f) flt -= 1.0f;
            rnd += 0.0005f * (rndTarget - rnd);
            const float mod = wowAmt * 0.0045f * fs * (0.7f * fastSin (wow) + 0.3f * rnd)
                            + flutterAmt * 0.00035f * fs * fastSin (flt);
            for (int c = 0; c < 2; ++c)
            {
                float& s = c == 0 ? L[i] : R[i];
                dl[c].push (s);
                float w = dl[c].read (base + mod);
                w = fastTanh (w * drive) / (drive > 1.0f ? fastTanh (drive) : 1.0f);
                w = (c == 0 ? lpL : lpR).lp (w, lpA);
                s = s + mix * (w - s);
            }
        }
    }
private:
    float fs = 44100.0f, wow = 0.0f, flt = 0.0f, rnd = 0.0f, rndTarget = 0.0f;
    DelayLine dl[2];
    OnePole lpL, lpR;
    Rng rng { 4242u };
};
} // namespace lxl

namespace lxl
{
// ------------------------------------------------------------------ biquad helper (RBJ)
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float tick (float x) noexcept { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0.0f; }
    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0); a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }
    void lowShelf (double fs, double f, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / fs, c = std::cos (w), sn = std::sin (w);
        const double al = sn / 2.0 * std::sqrt (2.0), sq = 2.0 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
             (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void highShelf (double fs, double f, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / fs, c = std::cos (w), sn = std::sin (w);
        const double al = sn / 2.0 * std::sqrt (2.0), sq = 2.0 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
             (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
    void peak (double fs, double f, double db, double q)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / fs, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void highPass (double fs, double f, double q = 0.7071)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / fs, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void lowPass (double fs, double f, double q = 0.7071)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / fs, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
};

// ------------------------------------------------------------------ EQ: low cut, low shelf, mid bell, high shelf
class Eq
{
public:
    void prepare (double sr) { fs = sr; for (auto& c : f) for (auto& b : c) b.reset(); last[0] = -1.0f; }
    void process (float* L, float* R, int n, float hp, float low, float midF, float mid, float high)
    {
        if (hp != last[0] || low != last[1] || midF != last[2] || mid != last[3] || high != last[4])
        {
            for (int c = 0; c < 2; ++c)
            {
                f[c][0].highPass (fs, hp, 0.7071);
                f[c][1].lowShelf (fs, 110.0, low);
                f[c][2].peak (fs, midF, mid, 0.9);
                f[c][3].highShelf (fs, 7000.0, high);
            }
            last[0] = hp; last[1] = low; last[2] = midF; last[3] = mid; last[4] = high;
        }
        const bool useHp = hp > 21.0f;
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
            {
                float& x = c == 0 ? L[i] : R[i];
                float y = x;
                if (useHp) y = f[c][0].tick (y);
                if (low != 0.0f) y = f[c][1].tick (y);
                if (mid != 0.0f) y = f[c][2].tick (y);
                if (high != 0.0f) y = f[c][3].tick (y);
                x = y;
            }
    }
private:
    double fs = 44100.0;
    Biquad f[2][4];
    float last[5] { -1, 0, 0, 0, 0 };
};

// ------------------------------------------------------------------ OTT-style 3-band upward/downward compressor
class Ott
{
public:
    void prepare (double sr)
    {
        fs = (float) sr;
        for (int c = 0; c < 2; ++c)
        {
            lo1[c].lowPass (sr, 88.0, 0.7071); lo2[c].lowPass (sr, 88.0, 0.7071);
            hi1[c].highPass (sr, 88.0, 0.7071); hi2[c].highPass (sr, 88.0, 0.7071);
            mlo1[c].lowPass (sr, 2500.0, 0.7071); mlo2[c].lowPass (sr, 2500.0, 0.7071);
            mhi1[c].highPass (sr, 2500.0, 0.7071); mhi2[c].highPass (sr, 2500.0, 0.7071);
            // allpass compensation of the low band through the 2.5k split
            ap1[c].lowPass (sr, 2500.0, 0.7071); ap2[c].lowPass (sr, 2500.0, 0.7071);
            ap3[c].highPass (sr, 2500.0, 0.7071); ap4[c].highPass (sr, 2500.0, 0.7071);
        }
        for (auto& e : env) e = 0.0f;
        for (auto& g : gainDb) g = 0.0f;
    }

    void process (float* L, float* R, int n, float depth, float time, float upAmt, float downAmt, float outDb)
    {
        // calibrated so a typical -18 dBFS band comes out at about the same level (auto makeup),
        // louder material is squashed hard, quiet detail and tails are lifted
        static const float thrDown[3] = { -24.0f, -23.0f, -22.0f };
        static const float thrUp[3]   = { -36.0f, -35.0f, -34.0f };
        static const float bandGain[3] = { 8.5f, 8.0f, 9.0f };
        const float att = std::exp (-1.0f / (fs * (0.0005f + 0.012f * time)));
        const float rel = std::exp (-1.0f / (fs * (0.02f + 0.25f * time)));
        const float out = dbToGain (outDb);
        for (int i = 0; i < n; ++i)
        {
            float band[3][2];
            for (int c = 0; c < 2; ++c)
            {
                const float x = c == 0 ? L[i] : R[i];
                const float low = lo2[c].tick (lo1[c].tick (x));
                const float rest = hi2[c].tick (hi1[c].tick (x));
                const float mid = mlo2[c].tick (mlo1[c].tick (rest));
                const float high = mhi2[c].tick (mhi1[c].tick (rest));
                const float lowAp = ap2[c].tick (ap1[c].tick (low)) + ap4[c].tick (ap3[c].tick (low));
                band[0][c] = lowAp; band[1][c] = mid; band[2][c] = high;
            }
            float y[2] = { 0.0f, 0.0f };
            // dry signal taken from the band sum so the depth blend has no crossover phase cancellation
            const float dry[2] = { band[0][0] + band[1][0] + band[2][0], band[0][1] + band[1][1] + band[2][1] };
            for (int b = 0; b < 3; ++b)
            {
                const float lv = juce::jmax (std::abs (band[b][0]), std::abs (band[b][1]));
                env[b] = lv > env[b] ? att * env[b] + (1.0f - att) * lv : rel * env[b] + (1.0f - rel) * lv;
                const float db = juce::Decibels::gainToDecibels (env[b] + 1.0e-9f, -120.0f);
                float g = 0.0f;
                if (db > thrDown[b]) g -= (db - thrDown[b]) * (1.0f - 1.0f / 66.7f) * downAmt;
                else if (db < thrUp[b] && db > -75.0f) g += juce::jmin (18.0f, (thrUp[b] - db) * (1.0f - 1.0f / 4.17f)) * upAmt;
                g += bandGain[b];
                gainDb[b] = g;
                const float lin = dbToGain (g);
                y[0] += band[b][0] * lin;
                y[1] += band[b][1] * lin;
            }
            L[i] = (dry[0] + depth * (y[0] - dry[0])) * out;
            R[i] = (dry[1] + depth * (y[1] - dry[1])) * out;
        }
    }
private:
    float fs = 44100.0f;
    Biquad lo1[2], lo2[2], hi1[2], hi2[2], mlo1[2], mlo2[2], mhi1[2], mhi2[2], ap1[2], ap2[2], ap3[2], ap4[2];
    float env[3] {}, gainDb[3] {};
};

// ------------------------------------------------------------------ Stereo utility: width + bass mono
class StereoUtility
{
public:
    void prepare (double sr) { fs = sr; for (auto& b : side) b.reset(); lastF = -1.0f; }
    void process (float* L, float* R, int n, float width, float monoFreq)
    {
        const bool mono = monoFreq > 21.0f;
        if (mono && monoFreq != lastF) { side[0].highPass (fs, monoFreq, 0.7071); side[1].highPass (fs, monoFreq, 0.7071); lastF = monoFreq; }
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            float s = 0.5f * (L[i] - R[i]) * width;
            if (mono) s = side[1].tick (side[0].tick (s));
            L[i] = m + s;
            R[i] = m - s;
        }
    }
private:
    double fs = 44100.0;
    Biquad side[2];
    float lastF = -1.0f;
};
} // namespace lxl
