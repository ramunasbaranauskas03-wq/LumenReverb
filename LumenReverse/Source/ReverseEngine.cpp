#include "ReverseEngine.h"

namespace lumen
{

namespace
{
    struct AlgoDef
    {
        float delaysMs[8];
        float modDepth;      // samples @48k at mod = 1
        float modRate;       // LFO rate multiplier
        float diffBoost;     // added to diffuser gain
        float diffScale;     // diffuser length multiplier
        int   numDiff;
        float dampMul;       // damping freq multiplier
        float shimmerBase;   // always-on shimmer
    };

    const AlgoDef algos[6] =
    {
        // Room
        { { 11.3f, 13.7f, 17.1f, 19.9f, 23.3f, 27.1f, 29.9f, 33.7f },  4.0f, 1.0f, 0.00f, 0.7f, 4, 1.0f, 0.0f },
        // Hall
        { { 37.1f, 43.3f, 53.1f, 61.7f, 71.3f, 79.9f, 89.1f, 97.3f }, 14.0f, 0.8f, 0.05f, 1.0f, 4, 1.0f, 0.0f },
        // Plate
        { { 13.1f, 17.3f, 23.9f, 29.3f, 31.7f, 37.9f, 41.3f, 47.1f },  7.0f, 1.3f, 0.15f, 0.5f, 6, 1.4f, 0.0f },
        // Cathedral
        { { 61.3f, 71.9f, 83.3f, 97.1f, 109.3f, 127.1f, 139.7f, 151.3f }, 18.0f, 0.6f, 0.08f, 1.4f, 4, 0.8f, 0.0f },
        // Shimmer
        { { 41.3f, 47.9f, 59.3f, 67.1f, 73.9f, 83.3f, 91.7f, 101.9f }, 16.0f, 0.7f, 0.08f, 1.1f, 4, 1.2f, 0.45f },
        // Bloom
        { { 47.3f, 59.1f, 67.9f, 79.3f, 89.7f, 101.3f, 113.9f, 127.3f }, 22.0f, 0.5f, 0.15f, 3.0f, 6, 0.9f, 0.0f },
    };

    const float baseDiffMs[6] = { 4.77f, 3.59f, 12.73f, 9.31f, 7.13f, 5.41f };
    const float lfoRates[8]   = { 0.31f, 0.47f, 0.59f, 0.73f, 0.89f, 1.03f, 1.21f, 1.37f };
    const float signL[8]      = { 1, -1, 1, 1, -1, 1, -1, -1 };
    const float signR[8]      = { 1, 1, -1, 1, 1, -1, -1, 1 };
}

void FDNReverb::prepare (double sampleRate)
{
    sr = sampleRate;
    const int maxLine = (int) (sr * 0.40);
    for (auto& l : lines) l.prepare (maxLine);
    preL.prepare ((int) (sr * 0.3));
    preR.prepare ((int) (sr * 0.3));
    for (int i = 0; i < 6; ++i) { diffL[(size_t) i].prepare ((int) (sr * 0.12)); diffR[(size_t) i].prepare ((int) (sr * 0.12)); }
    shifter.prepare (sr);
    reset();
}

void FDNReverb::reset()
{
    for (auto& l : lines) l.clear();
    preL.clear(); preR.clear();
    for (auto& a : diffL) a.clear();
    for (auto& a : diffR) a.clear();
    shifter.clear();
    lp.fill (0.0f);
    for (int i = 0; i < N; ++i) lfoPhase[(size_t) i] = (float) i / (float) N;
    shimLP = 0.0f;
}

void FDNReverb::setParams (const ReverbParams& p)
{
    const auto& a = algos[juce::jlimit (0, 5, p.algo)];
    const float sizeScale = 0.45f + p.size * 1.25f;
    const float rt = juce::jmax (0.1f, p.decay);

    for (int i = 0; i < N; ++i)
    {
        len[(size_t) i]  = (float) (a.delaysMs[i] * 0.001 * sizeScale * sr);
        gain[(size_t) i] = std::pow (10.0f, -3.0f * len[(size_t) i] / (rt * (float) sr));
        lfoInc[(size_t) i] = (float) (lfoRates[i] * a.modRate / sr);
    }

    modDepthSamples = p.mod * a.modDepth * (float) (sr / 48000.0);
    const float damp = juce::jlimit (500.0f, 20000.0f, p.dampHz * a.dampMul);
    dampCoef = std::exp (-juce::MathConstants<float>::twoPi * damp / (float) sr);

    predelaySamples = p.predelayMs * 0.001f * (float) sr;

    numDiff = a.numDiff;
    const float g = juce::jlimit (0.0f, 0.82f, 0.30f + 0.45f * p.diffusion + a.diffBoost);
    const float diffSize = a.diffScale * (0.6f + 0.6f * p.size);
    for (int i = 0; i < 6; ++i)
    {
        const int l = juce::jmax (1, (int) (baseDiffMs[i] * 0.001f * diffSize * (float) sr));
        diffL[(size_t) i].len = l;
        diffR[(size_t) i].len = juce::jmax (1, (int) (l * 1.13f));
        diffL[(size_t) i].g = g;
        diffR[(size_t) i].g = g;
    }

    shimmerAmt = juce::jlimit (0.0f, 1.0f, a.shimmerBase + p.shimmer * (1.0f - a.shimmerBase));
    shimNorm = 1.0f / (1.0f + 0.55f * shimmerAmt);
}

void FDNReverb::process (float inL, float inR, float& outL, float& outR)
{
    // pre-delay
    preL.push (inL); preR.push (inR);
    float xL = predelaySamples > 0.5f ? preL.read (predelaySamples) : inL;
    float xR = predelaySamples > 0.5f ? preR.read (predelaySamples) : inR;

    // input diffusion
    for (int i = 0; i < numDiff; ++i)
    {
        xL = diffL[(size_t) i].process (xL);
        xR = diffR[(size_t) i].process (xR);
    }

    std::array<float, N> d {}, v {};
    float sum = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        auto& ph = lfoPhase[(size_t) i];
        ph += lfoInc[(size_t) i]; if (ph >= 1.0f) ph -= 1.0f;
        const float m = modDepthSamples * std::sin (juce::MathConstants<float>::twoPi * ph);
        d[(size_t) i] = lines[(size_t) i].read (len[(size_t) i] + m);
        lp[(size_t) i] = d[(size_t) i] * (1.0f - dampCoef) + lp[(size_t) i] * dampCoef;
        v[(size_t) i] = lp[(size_t) i] * gain[(size_t) i];
        sum += v[(size_t) i];
    }

    float shim = 0.0f;
    if (shimmerAmt > 0.001f)
    {
        shim = shifter.process (sum * 0.25f);
        shimLP += 0.35f * (shim - shimLP);   // soften grain edges
        shim = shimLP * shimmerAmt * 0.55f;
    }

    const float h = 2.0f / (float) N;
    for (int i = 0; i < N; ++i)
    {
        float u = (v[(size_t) i] - h * sum) * shimNorm + ((i & 1) ? -shim : shim);
        u += (i % 2 == 0) ? xL * 0.5f : xR * 0.5f;
        // gentle safety saturation in the loop
        if (std::abs (u) > 1.5f) u = 1.5f * std::tanh (u / 1.5f);
        lines[(size_t) i].push (u);
    }

    float oL = 0.0f, oR = 0.0f;
    for (int i = 0; i < N; ++i) { oL += d[(size_t) i] * signL[i]; oR += d[(size_t) i] * signR[i]; }
    outL = oL * 0.35f;
    outR = oR * 0.35f;
}

// ============================================================================

float Renderer::envelopeAt (float frac, const RenderSettings& s, int swellLen, int pos)
{
    float e = s.rise <= 0.001f ? 1.0f : std::pow (juce::jlimit (0.0f, 1.0f, frac), s.rise);
    const int fadeIn = (int) (s.tailFadeMs * 0.001 * s.sampleRate);
    if (fadeIn > 1 && pos < fadeIn)
        e *= 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) pos / (float) fadeIn);
    juce::ignoreUnused (swellLen);
    return e;
}

std::unique_ptr<RenderResult> Renderer::render (const juce::AudioBuffer<float>& source, const RenderSettings& s)
{
    auto res = std::make_unique<RenderResult>();
    const int Ls = source.getNumSamples();
    const int Lw = juce::jmax (64, s.swellSamples);
    if (Ls <= 0) return res;

    const int total = Lw + Ls;
    res->audio.setSize (2, total);
    res->audio.clear();
    res->swellLen = Lw;
    res->srcLen = Ls;

    auto srcSample = [&] (int ch, int i) -> float
    {
        const int c = juce::jmin (ch, source.getNumChannels() - 1);
        return source.getSample (c, i);
    };

    FDNReverb verb;
    verb.prepare (s.sampleRate);
    verb.setParams (s.reverb);

    auto* outL = res->audio.getWritePointer (0);
    auto* outR = res->audio.getWritePointer (1);

    if (s.mode == 0)
    {
        // Reverse Tail: reverb the forward source, flip the tail so it swells into the hit
        std::vector<float> wL ((size_t) Lw), wR ((size_t) Lw);
        for (int n = 0; n < Lw; ++n)
        {
            const float iL = n < Ls ? srcSample (0, n) : 0.0f;
            const float iR = n < Ls ? srcSample (1, n) : 0.0f;
            verb.process (iL, iR, wL[(size_t) n], wR[(size_t) n]);
        }
        for (int t = 0; t < Lw; ++t)
        {
            outL[t] = wL[(size_t) (Lw - 1 - t)];
            outR[t] = wR[(size_t) (Lw - 1 - t)];
        }
    }
    else
    {
        // Pre-Verb: reverse source -> reverb -> reverse (ghost swell that flows through the phrase)
        const int wetLen = total;
        std::vector<float> wL ((size_t) wetLen), wR ((size_t) wetLen);
        for (int n = 0; n < wetLen; ++n)
        {
            const float iL = n < Ls ? srcSample (0, Ls - 1 - n) : 0.0f;
            const float iR = n < Ls ? srcSample (1, Ls - 1 - n) : 0.0f;
            verb.process (iL, iR, wL[(size_t) n], wR[(size_t) n]);
        }
        for (int t = 0; t < wetLen; ++t)
        {
            outL[t] = wL[(size_t) (wetLen - 1 - t)];
            outR[t] = wR[(size_t) (wetLen - 1 - t)];
        }
    }

    // --- filters: static high-pass + sweeping low-pass across the swell ---
    {
        juce::dsp::ProcessSpec spec { s.sampleRate, 32, 2 };
        juce::dsp::StateVariableTPTFilter<float> hp, lpf;
        hp.prepare (spec);  lpf.prepare (spec);
        hp.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        lpf.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        const float nyq = (float) (s.sampleRate * 0.45);
        const float hc = juce::jmin (s.highCut, nyq);
        hp.setCutoffFrequency (juce::jlimit (10.0f, nyq, s.lowCut));
        hp.setResonance (0.7071f);
        lpf.setResonance (0.7071f + 0.25f * s.sweep);
        const float logHi = std::log (hc);
        const float logLo = std::log (juce::jmax (120.0f, hc * 0.02f));
        const bool useHP = s.lowCut > 20.5f;
        const bool useLP = hc < nyq - 1.0f || s.sweep > 0.001f;

        for (int n = 0; n < total; ++n)
        {
            if (useLP && (n & 31) == 0)
            {
                float f = hc;
                if (s.sweep > 0.001f && n < Lw)
                {
                    const float frac = (float) n / (float) Lw;
                    const float start = logHi + (logLo - logHi) * s.sweep;
                    f = std::exp (start + (logHi - start) * std::pow (frac, 1.5f));
                }
                lpf.setCutoffFrequency (juce::jlimit (40.0f, nyq, f));
            }
            for (int ch = 0; ch < 2; ++ch)
            {
                float x = res->audio.getSample (ch, n);
                if (useHP) x = hp.processSample (ch, x);
                if (useLP) x = lpf.processSample (ch, x);
                res->audio.setSample (ch, n, x);
            }
        }
    }

    // --- swell envelope ---
    for (int t = 0; t < Lw; ++t)
    {
        const float e = envelopeAt ((float) (t + 1) / (float) Lw, s, Lw, t);
        outL[t] *= e; outR[t] *= e;
    }
    // In tail mode, a short fade at the boundary keeps the hand-off click-free
    if (s.mode == 0)
    {
        const int f = juce::jmin (Lw / 4, (int) (0.002 * s.sampleRate));
        for (int i = 0; i < f; ++i)
        {
            const float g = (float) i / (float) f;
            outL[Lw - 1 - i] *= g; outR[Lw - 1 - i] *= g;
        }
    }

    // --- level ---
    float peak = res->audio.getMagnitude (0, total);
    float wetGain = 2.0f;
    if (s.normalize && peak > 1.0e-6f) wetGain = 0.891f / peak;
    else if (peak * wetGain > 0.98f)   wetGain = 0.98f / peak;
    res->audio.applyGain (wetGain);

    // --- source blend (forward source after the swell) ---
    if (s.sourceBlend > 0.0001f)
    {
        const int fadeIn = juce::jmax (1, (int) (s.sourceFadeMs * 0.001 * s.sampleRate));
        const int fadeOut = juce::jmin (Ls / 4, (int) (0.01 * s.sampleRate));
        for (int i = 0; i < Ls; ++i)
        {
            float g = s.sourceBlend;
            if (i < fadeIn) g *= 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) i / (float) fadeIn);
            if (i >= Ls - fadeOut && fadeOut > 0) g *= (float) (Ls - 1 - i) / (float) fadeOut;
            outL[Lw + i] += srcSample (0, i) * g;
            outR[Lw + i] += srcSample (1, i) * g;
        }
    }

    // --- final safety: keep swell + blended source below 0 dBFS ---
    {
        const float pk = res->audio.getMagnitude (0, total);
        if (pk > 0.98f) res->audio.applyGain (0.98f / pk);
    }

    // --- display data ---
    const int cols = 1200;
    res->numCols = cols;
    res->peaks.assign ((size_t) cols * 2, 0.0f);
    for (int c = 0; c < cols; ++c)
    {
        const int a = (int) ((int64_t) c * total / cols);
        const int b = juce::jmax (a + 1, (int) ((int64_t) (c + 1) * total / cols));
        float mn = 0.0f, mx = 0.0f;
        for (int i = a; i < b && i < total; ++i)
        {
            const float m = 0.5f * (outL[i] + outR[i]);
            mn = juce::jmin (mn, m); mx = juce::jmax (mx, m);
        }
        res->peaks[(size_t) c * 2] = mn;
        res->peaks[(size_t) c * 2 + 1] = mx;
    }
    res->envelope.resize (256);
    for (int i = 0; i < 256; ++i)
    {
        const float frac = (float) (i + 1) / 256.0f;
        res->envelope[(size_t) i] = envelopeAt (frac, s, Lw, (int) (frac * (float) Lw));
    }
    return res;
}

} // namespace lumen
