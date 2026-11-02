#pragma once
#include "Params.h"
#include <complex>

namespace lxl
{
/** Zero-delay-feedback state variable filter (Simper/Zavalishin). */
struct Svf
{
    float ic1 = 0, ic2 = 0;
    float a1 = 0, a2 = 0, a3 = 0, k = 2;
    void reset() { ic1 = ic2 = 0; }
    inline void set (float g, float kk) noexcept
    {
        k = kk;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline void tick (float v0, float& low, float& band, float& high, bool saturate = false) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        if (saturate) { ic1 = 1.6f * fastTanh (ic1 * 0.625f); }
        low = v2; band = v1; high = v0 - k * v1 - v2;
    }
};

/** 4-pole zero-delay ladder (linear core, saturating input). */
struct Ladder
{
    float s[4] {};
    void reset() { for (auto& v : s) v = 0.0f; }
    inline void tick (float x, float g, float k, float& y1, float& y2, float& y3, float& y4, float& u) noexcept
    {
        const float G = g / (1.0f + g);
        const float inv = 1.0f / (1.0f + g);
        const float S = G * G * G * s[0] * inv + G * G * s[1] * inv + G * s[2] * inv + s[3] * inv;
        const float G4 = G * G * G * G;
        u = (x - k * S) / (1.0f + k * G4);
        u = fastTanh (u);
        auto stage = [G] (float in, float& st) { const float v = (in - st) * G; const float y = v + st; st = y + v; return y; };
        y1 = stage (u, s[0]);
        y2 = stage (y1, s[1]);
        y3 = stage (y2, s[2]);
        y4 = stage (y3, s[3]);
    }
};

/** One complete multi-mode filter channel. */
class FilterChannel
{
public:
    /** Allocates for the highest rate this channel will run at. */
    void prepare (double maxSr)
    {
        sampleRate = (float) maxSr;
        comb.setSize ((int) (maxSr / 18.0) + 8);
        reset();
    }
    void setSampleRate (double sr) { sampleRate = (float) sr; reset(); }
    void reset()
    {
        svf.reset(); svf2.reset(); ladder.reset(); comb.clear(); combLp = 0.0f;
        for (auto& f : formants) f.reset();
    }

    /** Coefficients are computed per call; cutoff in Hz, reso 0..1, drive 0..1. */
    inline float process (float x, int type, float cutoff, float reso, float drive) noexcept
    {
        const float nyq = sampleRate * 0.49f;
        cutoff = juce::jlimit (16.0f, nyq * 0.98f, cutoff);
        const float pre = 1.0f + drive * 7.0f;
        const float post = 1.0f / (1.0f + drive * 2.2f);
        if (drive > 0.001f) x = fastTanh (x * pre) * (1.0f + drive * 1.2f);

        const float g = std::tan (kPi * cutoff / sampleRate);
        float y = 0.0f;
        switch (type)
        {
            case FLp12: case FHp12: case FBand: case FNotch: case FAnalogLp:
            {
                const float q = reso * reso;
                const float kk = type == FAnalogLp ? 2.0f - 2.08f * reso : 2.0f - 1.96f * q;
                svf.set (g, juce::jmax (0.02f, kk));
                float lo, bp, hi;
                svf.tick (x, lo, bp, hi, type == FAnalogLp);
                if (type == FLp12 || type == FAnalogLp) y = lo;
                else if (type == FHp12) y = hi;
                else if (type == FBand) y = bp * juce::jmax (0.02f, kk);
                else y = lo + hi;
                if (type == FAnalogLp) y = fastTanh (y * 1.15f);
                break;
            }
            case FLp24: case FHp24:
            {
                const float k = reso * 3.95f;
                float y1, y2, y3, y4, u;
                ladder.tick (x * (1.0f + k * 0.35f), g, k, y1, y2, y3, y4, u);
                y = type == FLp24 ? y4 : (u - 4.0f * y1 + 6.0f * y2 - 4.0f * y3 + y4);
                break;
            }
            case FDoubleNotch:
            {
                const float spread = 1.25f + reso * 1.2f;
                const float ga = std::tan (kPi * juce::jlimit (16.0f, nyq * 0.98f, cutoff / spread) / sampleRate);
                const float gb = std::tan (kPi * juce::jlimit (16.0f, nyq * 0.98f, cutoff * spread) / sampleRate);
                const float kk = 1.4f - reso * 1.1f;
                float lo, bp, hi;
                svf.set (ga, kk); svf.tick (x, lo, bp, hi); const float n1 = lo + hi;
                svf2.set (gb, kk); svf2.tick (n1, lo, bp, hi); y = lo + hi;
                break;
            }
            case FCombPlus: case FCombMinus:
            {
                const float delay = sampleRate / cutoff;
                const float fb = (type == FCombPlus ? 1.0f : -1.0f) * (0.2f + reso * 0.77f);
                const float d = comb.read (delay);
                combLp += 0.6f * (d - combLp);
                const float v = x + fb * combLp;
                comb.push (fastTanh (v));
                y = (x + v) * 0.5f;
                break;
            }
            case FFormant:
            default:
            {
                // cutoff sweeps through vowels A-E-I-O-U
                static const float F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2290, 3010 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
                const float pos = juce::jlimit (0.0f, 3.999f, (std::log2 (cutoff / 20.0f) / std::log2 (1000.0f)) * 4.0f);
                const int i = (int) pos;
                const float t = pos - (float) i;
                const float kk = 0.5f - reso * 0.42f;
                static const float gains[3] = { 1.0f, 0.7f, 0.45f };
                for (int f = 0; f < 3; ++f)
                {
                    const float fc = juce::jmin (nyq * 0.95f, F[i][f] + t * (F[i + 1][f] - F[i][f]));
                    formants[f].set (std::tan (kPi * fc / sampleRate), kk);
                    float lo, bp, hi;
                    formants[f].tick (x, lo, bp, hi);
                    y += bp * gains[f] * kk * 2.2f;
                }
                break;
            }
        }
        return y * post;
    }

private:
    float sampleRate = 44100.0f;
    Svf svf, svf2;
    Ladder ladder;
    DelayLine comb;
    float combLp = 0.0f;
    Svf formants[3];
};

/** Analog magnitude response used by the UI to draw the filter curve. */
inline float filterMagnitudeDb (int type, float cutoff, float reso, float freq)
{
    const float w = freq / cutoff;
    auto mag2 = [] (float re, float im) { return re * re + im * im; };
    switch (type)
    {
        case FLp12: case FAnalogLp: case FHp12: case FBand: case FNotch:
        {
            const float k = juce::jmax (0.02f, 2.0f - 1.96f * reso * reso);
            const float dRe = 1.0f - w * w, dIm = k * w;
            const float d = mag2 (dRe, dIm);
            float n = 1.0f;
            if (type == FHp12) n = w * w * w * w;
            else if (type == FBand) n = (k * w) * (k * w);
            else if (type == FNotch) n = (1.0f - w * w) * (1.0f - w * w);
            return 10.0f * std::log10 (juce::jmax (1.0e-12f, n / d));
        }
        case FLp24: case FHp24:
        {
            const float k = reso * 3.95f;
            // (1 + jw)^4 + k
            std::complex<float> s (1.0f, w);
            auto p = s * s * s * s + std::complex<float> (k, 0.0f);
            const float w4 = w * w * w * w;
            const float num = (1.0f + k * 0.35f) * (1.0f + k * 0.35f) * (type == FHp24 ? w4 * w4 : 1.0f);
            const float r = num / std::norm (p);
            return 10.0f * std::log10 (juce::jmax (1.0e-12f, r));
        }
        case FDoubleNotch:
        {
            const float sp = 1.25f + reso * 1.2f, k = 1.4f - reso * 1.1f;
            auto notch = [&] (float ww) { const float a = 1.0f - ww * ww; return (a * a) / (a * a + (k * ww) * (k * ww)); };
            return 10.0f * std::log10 (juce::jmax (1.0e-12f, notch (w * sp) * notch (w / sp)));
        }
        case FCombPlus: case FCombMinus:
        {
            const float fb = (type == FCombPlus ? 1.0f : -1.0f) * (0.2f + reso * 0.77f);
            const float ph = kTwoPi * w;
            const std::complex<float> z = std::polar (1.0f, -ph);
            const auto h = 0.5f * (1.0f + 1.0f / (1.0f - fb * z));
            return 20.0f * std::log10 (juce::jmax (1.0e-6f, std::abs (h) * 0.6f));
        }
        default:
        {
            const float k = 0.5f - reso * 0.42f;
            float sum = 0.0f;
            const float base[3] = { 1.0f, 2.2f, 3.6f };
            for (int i = 0; i < 3; ++i)
            {
                const float ww = freq / (cutoff * base[i] * 0.6f);
                const float d = (1.0f - ww * ww) * (1.0f - ww * ww) + (k * ww) * (k * ww);
                sum += (k * ww) * (k * ww) / d * (1.0f - 0.3f * (float) i);
            }
            return 10.0f * std::log10 (juce::jmax (1.0e-12f, sum));
        }
    }
}
} // namespace lxl
