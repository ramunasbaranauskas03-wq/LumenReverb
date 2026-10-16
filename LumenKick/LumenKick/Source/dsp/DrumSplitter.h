#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <functional>
#include <random>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

// =====================================================================================
// DRUM SPLIT: drum loop -> Kick / Snare-Clap / Hats-Cymbals / Perc-Other
//   1. STFT magnitude of the mono sum
//   2. onset detection (band-wise spectral flux, adaptive threshold) + per-onset class likelihoods
//   3. score-informed NMF (KL): spectral templates start from per-class priors and stay near them,
//      activations start from the detected onsets of each class
//   4. soft (Wiener-like) masks per class, applied to each channel's STFT -> stereo stems
//   The masks sum to 1, so Kick + Snare + Hats + Perc == the original loop (exactly).
// =====================================================================================
namespace lumen::split
{

enum Class { Kick, Snare, Hats, Perc, kClasses };
inline const char* className (int c) { static const char* n[] { "KICK", "SNARE / CLAP", "HATS / CYMBALS", "PERC / OTHER" }; return n[std::clamp (c, 0, 3)]; }

struct Onset { int sample = 0; int cls = 0; unsigned classes = 0; float strength = 0; };
struct Result
{
    bool ok = false;
    juce::String message;
    double rate = 48000;
    juce::AudioBuffer<float> stems[kClasses];
    std::vector<Onset> onsets;
};

// hard-ish frequency support of each class (1 inside, small outside)
inline float support (int c, float f)
{
    auto band = [f] (float lo, float hi) { return f >= lo && f <= hi; };
    switch (c)
    {
        case Kick:  return band (20.0f, 450.0f) ? 1.0f : band (450.0f, 6000.0f) ? 0.25f : 0.02f;
        case Snare: return band (160.0f, 14000.0f) ? 1.0f : band (110.0f, 160.0f) ? 0.15f : 0.003f;
        case Hats:  return band (2500.0f, 24000.0f) ? 1.0f : band (1200.0f, 2500.0f) ? 0.3f : 0.01f;
        default:    return band (150.0f, 7000.0f) ? 1.0f : 0.05f;
    }
}

inline float prior (int c, float f)
{
    const float lf = std::log2 (std::max (10.0f, f));
    auto g = [lf] (float centre, float width) { const float d = (lf - std::log2 (centre)) / width; return std::exp (-0.5f * d * d); };
    switch (c)
    {
        case Kick:  return g (58.0f, 0.75f) + 0.15f * g (110.0f, 0.5f) + 0.03f * g (3500.0f, 0.8f);
        case Snare: return 0.5f * g (220.0f, 0.45f) + g (2500.0f, 1.3f) + 0.25f * g (6000.0f, 0.8f);
        case Hats:  return 1.0f / (1.0f + std::exp (-3.5f * (lf - std::log2 (6500.0f)))) + 0.05f * g (3500.0f, 0.6f);
        default:    return 0.8f * g (600.0f, 1.0f) + 0.3f * g (1500.0f, 0.8f);
    }
}

// sensitivity 0..1 (onsets), separation 0..1 (mask hardness)
inline Result splitDrums (const juce::AudioBuffer<float>& in, double rate, float sensitivity, float separation,
                          const std::function<bool (float)>& progress)
{
    Result res; res.rate = rate;
    const int nCh = std::max (1, std::min (2, in.getNumChannels()));
    const int n = in.getNumSamples();
    if (n < 2048) { res.message = "file too short"; return res; }
    const int N = rate > 60000.0 ? 4096 : 2048, hop = N / 4, F = N / 2 + 1, order = (int) std::round (std::log2 ((double) N));
    const int T = (n + 2 * N) / hop + 1;
    juce::dsp::FFT fft (order);
    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) N);
    std::vector<float> buf ((size_t) N * 2);
    auto frameOf = [&] (int ch, int t)      // channel -1 = mono sum
    {
        const int start = t * hop - N;
        for (int i = 0; i < N; ++i)
        {
            const int s = start + i;
            float v = 0;
            if (s >= 0 && s < n)
            {
                if (ch < 0) { for (int c = 0; c < nCh; ++c) v += in.getSample (c, s); v /= (float) nCh; }
                else v = in.getSample (std::min (ch, in.getNumChannels() - 1), s);
            }
            buf[(size_t) i] = v * win[(size_t) i];
        }
        std::fill (buf.begin() + N, buf.end(), 0.0f);
        fft.performRealOnlyForwardTransform (buf.data(), true);
    };
    // 1. magnitude
    std::vector<float> V ((size_t) F * (size_t) T);
    auto at = [F] (std::vector<float>& m, int f, int t) -> float& { return m[(size_t) t * (size_t) F + (size_t) f]; };
    for (int t = 0; t < T; ++t)
    {
        frameOf (-1, t);
        for (int f = 0; f < F; ++f) at (V, f, t) = std::sqrt (buf[(size_t) (2 * f)] * buf[(size_t) (2 * f)] + buf[(size_t) (2 * f + 1)] * buf[(size_t) (2 * f + 1)]) + 1e-9f;
        if (progress && (t & 63) == 0 && ! progress (0.15f * (float) t / (float) T)) { res.message = "cancelled"; return res; }
    }
    // 2. onsets: band flux
    const float binHz = (float) (rate / N);
    const float edges[5] { 25.0f, 160.0f, 500.0f, 4500.0f, (float) (rate * 0.5) };
    std::vector<std::array<float, 4>> bflux ((size_t) T);
    std::vector<float> flux ((size_t) T, 0.0f);
    for (int t = 1; t < T; ++t)
    {
        std::array<float, 4> bf {};
        for (int f = 1; f < F; ++f)
        {
            const float hz = f * binHz;
            const float d = std::log1p (100.0f * at (V, f, t)) - std::log1p (100.0f * at (V, f, t - 1));
            if (d <= 0) continue;
            for (int b = 0; b < 4; ++b) if (hz >= edges[b] && hz < edges[b + 1]) { bf[(size_t) b] += d; break; }
        }
        // normalise by band width (bins)
        for (int b = 0; b < 4; ++b) bf[(size_t) b] /= std::max (1.0f, (edges[b + 1] - edges[b]) / binHz);
        bflux[(size_t) t] = bf;
        flux[(size_t) t] = bf[0] + bf[1] + bf[2] + bf[3];
    }
    std::vector<int> onT;
    {
        const float delta = 0.02f + 0.25f * (1.0f - std::clamp (sensitivity, 0.0f, 1.0f));
        float fmax = 1e-9f; for (float v : flux) fmax = std::max (fmax, v);
        const int minGap = std::max (1, (int) (0.03 * rate / hop));
        int last = -1000;
        for (int t = 2; t < T - 2; ++t)
        {
            float med[9]; int m = 0;
            for (int k = t - 4; k <= t + 4; ++k) med[m++] = flux[(size_t) std::clamp (k, 0, T - 1)];
            std::nth_element (med, med + 4, med + 9);
            const float thr = med[4] + delta * fmax;
            if (flux[(size_t) t] > thr && flux[(size_t) t] >= flux[(size_t) t - 1] && flux[(size_t) t] >= flux[(size_t) t + 1] && t - last >= minGap)
            { onT.push_back (t); last = t; }
        }
    }
    // per-onset class weights
    std::array<float, 4> bmax {};
    for (int t : onT) for (int b = 0; b < 4; ++b) bmax[(size_t) b] = std::max (bmax[(size_t) b], bflux[(size_t) t][(size_t) b] + bflux[(size_t) std::min (T - 1, t + 1)][(size_t) b]);
    std::vector<std::array<float, kClasses>> ow;
    for (int t : onT)
    {
        std::array<float, 4> s {};
        for (int b = 0; b < 4; ++b) s[(size_t) b] = (bflux[(size_t) t][(size_t) b] + bflux[(size_t) std::min (T - 1, t + 1)][(size_t) b]) / std::max (1e-9f, bmax[(size_t) b]);
        std::array<float, kClasses> w {};
        w[Kick] = s[0] * (1.0f - 0.4f * s[3]);
        // snare / clap: mid AND upper-mid noise; a kick's onset click has no air above 4.5 kHz, a hat has more air than body
        w[Snare] = std::min (s[2], 1.8f * s[3]) * (0.5f + 0.5f * s[1]) * (1.0f - 0.5f * std::max (0.0f, s[3] - s[2]));
        w[Hats] = s[3] * (1.0f - 0.7f * s[0]) * (1.0f - 0.4f * s[1]);
        w[Perc] = 0.6f * s[1] * (1.0f - s[3]) * (1.0f - s[0]);
        const float mx = std::max ({ w[0], w[1], w[2], w[3], 1e-6f });
        for (auto& v : w) v = v / mx > 0.35f ? v / mx : 0.05f * v / mx;
        ow.push_back (w);
    }
    // 3. score-informed NMF, 2 components per class
    // components: 4 for the kick (its pitch sweep needs several spectra), 2 for the others
    static const int cc[] { Kick, Kick, Kick, Kick, Snare, Snare, Hats, Hats, Perc, Perc };
    const int K = 10;
    std::vector<float> W ((size_t) F * (size_t) K), H ((size_t) K * (size_t) T), P ((size_t) F * kClasses);
    std::mt19937 rng (7); std::uniform_real_distribution<float> ur (0.85f, 1.15f);
    std::vector<float> S ((size_t) F * kClasses);
    for (int c = 0; c < kClasses; ++c)
        for (int f = 0; f < F; ++f)
        {
            const float hz = std::max (1.0f, f * binHz);
            S[(size_t) f * kClasses + (size_t) c] = support (c, hz);
            P[(size_t) f * kClasses + (size_t) c] = (prior (c, hz) + 1e-3f) * support (c, hz);
        }
    for (int k = 0; k < K; ++k)
    {
        double s = 0;
        for (int f = 0; f < F; ++f)
        {
            const float hz = std::max (1.0f, f * binHz);
            float v = P[(size_t) f * kClasses + (size_t) cc[k]] * (k % 2 ? ur (rng) : 1.0f);
            if (k < 4)
            {
                // kick sweep stages: 300 / 160 / 90 / 55 Hz
                static const float kc[] { 300.0f, 160.0f, 90.0f, 55.0f };
                const float d = std::log2 (hz / kc[k]) / 0.5f;
                v = (std::exp (-0.5f * d * d) + 1e-3f) * support (Kick, hz);
            }
            W[(size_t) f * (size_t) K + (size_t) k] = v; s += v;
        }
        for (int f = 0; f < F; ++f) W[(size_t) f * (size_t) K + (size_t) k] /= (float) s;
    }
    const float tauF[kClasses] { 0.20f, 0.16f, 0.07f, 0.12f };
    double vsum = 0; for (float v : V) vsum += v;
    const float hScale = (float) (vsum / T);
    std::vector<float> A ((size_t) kClasses * (size_t) T, 0.03f);
    for (int c = 0; c < kClasses; ++c)
    {
        const float tl = 2.5f * tauF[c] * (float) (rate / hop);
        for (size_t o = 0; o < onT.size(); ++o)
            for (int t = std::max (0, onT[o] - 1); t < T && t < onT[o] + (int) (6 * tl); ++t)
            {
                const float v = ow[o][(size_t) c] * std::exp (-(float) std::max (0, t - onT[o]) / tl);
                float& a = A[(size_t) c * (size_t) T + (size_t) t]; a = std::max (a, std::min (1.0f, 0.03f + v));
            }
    }
    for (int k = 0; k < K; ++k)
    {
        const int c = cc[k];
        const float tau = tauF[c] * (float) (rate / hop) * (k < 4 ? 0.3f + 0.4f * (float) k : 1.0f);
        for (int t = 0; t < T; ++t)
        {
            float h = 0.01f;
            for (size_t o = 0; o < onT.size(); ++o) { const int dt = t - onT[o]; if (dt >= 0 && dt < 8 * tau) h += ow[o][(size_t) c] * std::exp (-dt / tau); }
            H[(size_t) k * (size_t) T + (size_t) t] = h * hScale * 0.3f;
        }
    }
    std::vector<float> WH ((size_t) F * (size_t) T), R ((size_t) F * (size_t) T);
    auto computeWH = [&]
    {
        for (int t = 0; t < T; ++t)
            for (int f = 0; f < F; ++f)
            {
                float s = 1e-12f;
                const float* w = &W[(size_t) f * (size_t) K];
                for (int k = 0; k < K; ++k) s += w[k] * H[(size_t) k * (size_t) T + (size_t) t];
                at (WH, f, t) = s;
            }
    };
    const int iters = 40;
    for (int it = 0; it < iters; ++it)
    {
        computeWH();
        for (size_t i = 0; i < R.size(); ++i) R[i] = V[i] / WH[i];
        // H update
        for (int k = 0; k < K; ++k)
        {
            double wsum = 0; for (int f = 0; f < F; ++f) wsum += W[(size_t) f * (size_t) K + (size_t) k];
            for (int t = 0; t < T; ++t)
            {
                double num = 0;
                const float* r = &R[(size_t) t * (size_t) F];
                for (int f = 0; f < F; ++f) num += W[(size_t) f * (size_t) K + (size_t) k] * r[f];
                H[(size_t) k * (size_t) T + (size_t) t] *= (float) (num / std::max (1e-12, wsum)) * A[(size_t) cc[k] * (size_t) T + (size_t) t];
            }
        }
        computeWH();
        for (size_t i = 0; i < R.size(); ++i) R[i] = V[i] / WH[i];
        // W update (+ pull towards the class prior)
        for (int k = 0; k < K; ++k)
        {
            double hsum = 0; for (int t = 0; t < T; ++t) hsum += H[(size_t) k * (size_t) T + (size_t) t];
            double colSum = 0;
            for (int f = 0; f < F; ++f)
            {
                double num = 0;
                for (int t = 0; t < T; ++t) num += at (R, f, t) * H[(size_t) k * (size_t) T + (size_t) t];
                float& w = W[(size_t) f * (size_t) K + (size_t) k];
                w *= (float) (num / std::max (1e-12, hsum));
                w = w * S[(size_t) f * kClasses + (size_t) cc[k]] + 1e-15f;
                colSum += w;
            }
            for (int f = 0; f < F; ++f) W[(size_t) f * (size_t) K + (size_t) k] /= (float) colSum;
            for (int t = 0; t < T; ++t) H[(size_t) k * (size_t) T + (size_t) t] *= (float) colSum;
        }
        if (progress && ! progress (0.15f + 0.65f * (float) (it + 1) / (float) iters)) { res.message = "cancelled"; return res; }
    }
    // 4. masks + resynthesis
    const float pw = 1.0f + 2.0f * std::clamp (separation, 0.0f, 1.0f);
    for (auto& s : res.stems) { s.setSize (nCh, n); s.clear(); }
    std::vector<float> ybuf ((size_t) N * 2), mask ((size_t) F * kClasses);
    std::vector<float> acc[kClasses];
    std::array<double, kClasses> onsetE {};
    for (int ch = 0; ch < nCh; ++ch)
    {
        for (auto& a : acc) a.assign ((size_t) (n + 4 * N), 0.0f);
        for (int t = 0; t < T; ++t)
        {
            // masks for this frame
            for (int f = 0; f < F; ++f)
            {
                float sc[kClasses] {}; float tot = 1e-20f;
                for (int k = 0; k < K; ++k) sc[cc[k]] += W[(size_t) f * (size_t) K + (size_t) k] * H[(size_t) k * (size_t) T + (size_t) t];
                for (int c = 0; c < kClasses; ++c) { sc[c] = std::pow (sc[c] + 1e-20f, pw); tot += sc[c]; }
                for (int c = 0; c < kClasses; ++c) mask[(size_t) f * kClasses + (size_t) c] = sc[c] / tot;
            }
            frameOf (ch, t);
            for (int c = 0; c < kClasses; ++c)
            {
                for (int f = 0; f < F; ++f) { const float m = mask[(size_t) f * kClasses + (size_t) c]; ybuf[(size_t) (2 * f)] = buf[(size_t) (2 * f)] * m; ybuf[(size_t) (2 * f + 1)] = buf[(size_t) (2 * f + 1)] * m; }
                std::fill (ybuf.begin() + 2 * F, ybuf.end(), 0.0f);
                fft.performRealOnlyInverseTransform (ybuf.data());
                const int start = t * hop;                  // acc is offset by N (frame start = t*hop - N)
                for (int i = 0; i < N; ++i) acc[c][(size_t) (start + i)] += ybuf[(size_t) i] * win[(size_t) i] * (2.0f / 3.0f);
            }
            if (progress && (t & 63) == 0 && ! progress (0.8f + 0.2f * ((float) ch + (float) t / (float) T) / (float) nCh)) { res.message = "cancelled"; return res; }
        }
        for (int c = 0; c < kClasses; ++c) res.stems[c].copyFrom (ch, 0, acc[c].data() + N, n);
    }
    // onset labels from the stems: a class is present when its stem is loud at the hit,
    // relative to the other stems OR relative to that stem's own typical hit level
    std::vector<std::array<float, kClasses>> en (onT.size());
    std::array<float, kClasses> enMax {};
    for (size_t o = 0; o < onT.size(); ++o)
    {
        const int s0 = std::clamp (onT[o] * hop - N + N / 2 - hop / 2, 0, n - 1);
        for (int c = 0; c < kClasses; ++c)
        {
            // energy RISE at the hit (a ringing tail of an earlier hit doesn't count)
            const int w = (int) (0.03 * rate);
            double e = 0, pre = 0;
            for (int i = s0; i < std::min (n, s0 + w); ++i) e += (double) res.stems[c].getSample (0, i) * res.stems[c].getSample (0, i);
            for (int i = std::max (0, s0 - w); i < s0; ++i) pre += (double) res.stems[c].getSample (0, i) * res.stems[c].getSample (0, i);
            const float rise = (float) std::max (0.0, e - 1.5 * pre);
            en[o][(size_t) c] = rise; enMax[(size_t) c] = std::max (enMax[(size_t) c], rise);
        }
    }
    for (size_t o = 0; o < onT.size(); ++o)
    {
        Onset on; on.sample = std::clamp (onT[o] * hop - N + N / 2 - hop / 2, 0, n - 1);
        float best = 0;
        for (int c = 0; c < kClasses; ++c) if (en[o][(size_t) c] > best) { best = en[o][(size_t) c]; on.cls = c; }
        for (int c = 0; c < kClasses; ++c)
            if (best > 0 && (en[o][(size_t) c] > 0.2f * best || en[o][(size_t) c] > 0.3f * enMax[(size_t) c])) on.classes |= 1u << c;
        on.strength = flux[(size_t) onT[o]];
        res.onsets.push_back (on);
    }
    (void) onsetE;
    int counts[kClasses] {}; for (auto& o : res.onsets) for (int c = 0; c < kClasses; ++c) if ((o.classes >> c) & 1u) ++counts[c];
    res.ok = true;
    res.message = juce::String ((int) res.onsets.size()) + " hits: " + juce::String (counts[0]) + " kick, " + juce::String (counts[1]) + " snare, "
                + juce::String (counts[2]) + " hats, " + juce::String (counts[3]) + " perc";
    if (progress) progress (1.0f);
    return res;
}

} // namespace lumen::split
