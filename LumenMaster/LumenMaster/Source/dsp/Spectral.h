#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"
#include "Targets.h"

// =====================================================================================
// Adaptive spectral shaper ("Smart Balance"):
//   STFT (sqrt-Hann, 75 % overlap). Every frame the music's spectrum (1/6 octave, time
//   smoothed) is compared with the genre target curve; a smooth corrective EQ curve moves
//   the master toward the target, while a fine (1/12 octave) detector tames resonances
//   that stick out of their neighbourhood. Same gain on L and R so the image never moves.
// =====================================================================================
namespace lumen::master
{
struct ShaperParams { float amount = 0.5f, tame = 0.3f, boost = 0.5f, speed = 0.5f, lowHz = 30, highHz = 18000; int target = 0; };

class SpectralShaper
{
public:
    static constexpr int kBands = 60;      // 1/6 octave from 20 Hz
    static constexpr int kFine = 120;      // 1/12 octave

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        N = fs > 100000 ? 8192 : fs > 50000 ? 4096 : 2048;
        H = N / 4;
        int order = 0; while ((1 << order) < N) ++order;
        fft = std::make_unique<juce::dsp::FFT> (order);
        win.resize ((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2 * kPi * (i + 0.5) / N));
        for (int c = 0; c < 2; ++c) { inRing[c].assign ((size_t) N, 0.0f); outAcc[c].assign ((size_t) N, 0.0f); spec[c].assign ((size_t) (2 * N), 0.0f); }
        binGain.assign ((size_t) (N / 2 + 1), 1.0f);
        power.assign ((size_t) (N / 2 + 1), 0.0f);
        // band centres
        for (int b = 0; b < kBands; ++b) bandHz[(size_t) b] = 20.0 * std::pow (2.0, b / 6.0);
        for (int b = 0; b < kFine; ++b) fineHz[(size_t) b] = 20.0 * std::pow (2.0, b / 12.0);
        // overlap-add gain: sum of hann over 4 hops = 2
        olaScale = 0.5f;
        float t[4] {}; std::vector<float> tmp ((size_t) (2 * N), 0.0f); tmp[0] = 1; fft->performRealOnlyForwardTransform (tmp.data()); fft->performRealOnlyInverseTransform (tmp.data()); t[0] = tmp[0];
        invScale = 1.0f / t[0];
        reset();
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c) { std::fill (inRing[c].begin(), inRing[c].end(), 0.0f); std::fill (outAcc[c].begin(), outAcc[c].end(), 0.0f); }
        pos = 0; count = 0;
        level.fill (-100.0f); fine.fill (-100.0f); corr.fill (0.0f);
        std::fill (binGain.begin(), binGain.end(), 1.0f);
        primed = false;
    }
    int latency() const { return N; }

    void setParams (const ShaperParams& p) { prm = p; }

    // processes in place; when active == false the signal is only delayed (state keeps running)
    void process (float* L, float* R, int n, bool active) noexcept
    {
        float* ch[2] { L, R };
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float x = ch[c][i];
                ch[c][i] = outAcc[c][(size_t) pos];
                outAcc[c][(size_t) pos] = 0.0f;
                inRing[c][(size_t) pos] = x;
            }
            pos = (pos + 1) % N;
            if (++count >= H) { count = 0; frame (active); }
        }
    }

    // current correction curve for the display (dB per 1/6-oct band)
    void getCurve (float* dst) const { for (int b = 0; b < kBands; ++b) dst[b] = shown[(size_t) b].load (std::memory_order_relaxed); }
    static double bandFreq (int b) { return 20.0 * std::pow (2.0, b / 6.0); }

private:
    void frame (bool active) noexcept
    {
        // gather the last N samples (oldest at pos), window, FFT
        for (int c = 0; c < 2; ++c)
        {
            auto& s = spec[c];
            for (int i = 0; i < N; ++i) s[(size_t) i] = inRing[c][(size_t) ((pos + i) % N)] * win[(size_t) i];
            std::fill (s.begin() + N, s.end(), 0.0f);
            fft->performRealOnlyForwardTransform (s.data(), true);
        }
        const int nb = N / 2 + 1;
        double total = 0;
        for (int k = 0; k < nb; ++k)
        {
            const float a = spec[0][(size_t) (2 * k)], b = spec[0][(size_t) (2 * k + 1)], c2 = spec[1][(size_t) (2 * k)], d = spec[1][(size_t) (2 * k + 1)];
            power[(size_t) k] = a * a + b * b + c2 * c2 + d * d;
            total += power[(size_t) k];
        }
        const bool silent = total < 1e-9 * N;      // below about -90 dBFS: keep the last curve
        const double binHz = fs / N;
        // band levels (triangular in log frequency), time smoothing
        const double frameSec = (double) H / fs;
        const double att = std::exp (-frameSec / (0.08 + 0.6 * (1.0 - prm.speed))), rel = std::exp (-frameSec / (0.25 + 1.5 * (1.0 - prm.speed)));
        auto bandLevel = [&] (double fc, double widthOct)
        {
            const double lo = fc * std::pow (2.0, -widthOct), hi = fc * std::pow (2.0, widthOct);
            const int k0 = std::max (1, (int) std::floor (lo / binHz)), k1 = std::min (nb - 1, (int) std::ceil (hi / binHz));
            double s = 0, wsum = 0;
            for (int k = k0; k <= k1; ++k)
            {
                const double f = k * binHz, d = std::abs (std::log2 (f / fc)) / widthOct;
                const double w = std::max (0.0, 1.0 - d);
                s += w * power[(size_t) k]; wsum += w;
            }
            if (wsum <= 0) { const int k = std::clamp ((int) std::round (fc / binHz), 1, nb - 1); return 10.0 * std::log10 (power[(size_t) k] + 1e-20); }
            return 10.0 * std::log10 (s / wsum + 1e-20);
        };
        if (! silent)
        {
            for (int b = 0; b < kBands; ++b)
            {
                const float v = (float) bandLevel (bandHz[(size_t) b], 1.0 / 6.0);
                auto& L = level[(size_t) b];
                if (! primed) L = v; else L = (float) (v > L ? v + (L - v) * att : v + (L - v) * rel);
            }
            for (int b = 0; b < kFine; ++b)
            {
                const float v = (float) bandLevel (fineHz[(size_t) b], 1.0 / 12.0);
                auto& F = fine[(size_t) b];
                if (! primed) F = v; else F = (float) (v > F ? v + (F - v) * att : v + (F - v) * rel);
            }
            primed = true;
            // ---- corrective curve toward the target
            const auto& tg = targetCurve (prm.target);
            double offs = 0, wsum = 0;
            for (int b = 0; b < kBands; ++b)
            {
                const double f = bandHz[(size_t) b];
                if (f < 60 || f > 12000) continue;
                offs += level[(size_t) b] - targetDb (tg, f); wsum += 1;
            }
            offs = wsum > 0 ? offs / wsum : 0;
            std::array<float, kBands> want {};
            for (int b = 0; b < kBands; ++b)
            {
                const double f = bandHz[(size_t) b];
                double d = level[(size_t) b] - targetDb (tg, f) - offs;      // + = too loud
                double g = -prm.amount * 0.6 * d;
                g = std::clamp (g, -6.0, 4.5 * prm.boost);
                // resonance tamer: fine band vs its half-octave neighbourhood
                double worst = 0;
                for (int k = std::max (0, 2 * b - 1); k <= std::min (kFine - 1, 2 * b + 1); ++k)     // fine bands within 1/12 oct of this band
                {
                    if (std::abs (std::log2 (fineHz[(size_t) k] / f)) > 1.0 / 12.0 + 1e-9) continue;
                    double nsum = 0; int nn = 0;
                    for (int j = std::max (0, k - 6); j <= std::min (kFine - 1, k + 6); ++j) if (std::abs (j - k) > 1) { nsum += fine[(size_t) j]; ++nn; }
                    const double ex = fine[(size_t) k] - (nn ? nsum / nn : fine[(size_t) k]) - 3.0;
                    worst = std::max (worst, ex);
                }
                g -= prm.tame * std::min (8.0, worst) * 0.9;
                // frequency range + fades at the edges
                const double fl = std::log2 (f / std::max (20.0f, prm.lowHz)), fh = std::log2 (std::max (100.0f, prm.highHz) / f);
                const double edge = std::clamp (std::min (fl, fh) * 2.0 + 1.0, 0.0, 1.0);
                want[(size_t) b] = (float) (std::clamp (g, -9.0, 6.0) * edge);
            }
            // level neutral: remove the average change across the musical range so loudness stays put
            {
                double m = 0; int k = 0;
                for (int b = 0; b < kBands; ++b) { const double f = bandHz[(size_t) b]; if (f >= 60 && f <= 10000) { m += want[(size_t) b]; ++k; } }
                m = k ? m / k : 0;
                for (auto& x : want) x = (float) std::clamp ((double) x - m, -9.0, 6.0);
            }
            // smooth across frequency (3-tap) and time
            const double ts = std::exp (-frameSec / (0.05 + 0.4 * (1.0 - prm.speed)));
            for (int b = 0; b < kBands; ++b)
            {
                const float a = want[(size_t) std::max (0, b - 1)], m = want[(size_t) b], z = want[(size_t) std::min (kBands - 1, b + 1)];
                const float sm = 0.25f * a + 0.5f * m + 0.25f * z;
                corr[(size_t) b] = (float) (sm + (corr[(size_t) b] - sm) * ts);
            }
            for (int b = 0; b < kBands; ++b) shown[(size_t) b].store (active ? corr[(size_t) b] : 0.0f, std::memory_order_relaxed);
            // per-bin gains (interpolated in log frequency)
            for (int k = 0; k < nb; ++k)
            {
                const double f = std::max (1.0, k * binHz);
                const double bp = std::clamp (6.0 * std::log2 (f / 20.0), 0.0, (double) kBands - 1.001);
                const int b0 = (int) bp; const double t = bp - b0;
                double db = corr[(size_t) b0] + (corr[(size_t) b0 + 1] - corr[(size_t) b0]) * t;
                if (f < 20.0 || f > 20000.0) db = 0;
                binGain[(size_t) k] = (float) std::pow (10.0, db / 20.0);
            }
        }
        // apply, inverse FFT, synthesis window, overlap-add
        for (int c = 0; c < 2; ++c)
        {
            auto& s = spec[c];
            if (active)
                for (int k = 0; k < nb; ++k) { s[(size_t) (2 * k)] *= binGain[(size_t) k]; s[(size_t) (2 * k + 1)] *= binGain[(size_t) k]; }
            for (int k = 1; k < N / 2; ++k) { s[(size_t) (2 * (N - k))] = s[(size_t) (2 * k)]; s[(size_t) (2 * (N - k) + 1)] = -s[(size_t) (2 * k + 1)]; }
            fft->performRealOnlyInverseTransform (s.data());
            for (int i = 0; i < N; ++i)
                outAcc[c][(size_t) ((pos + i) % N)] += s[(size_t) i] * invScale * win[(size_t) i] * olaScale;
        }
    }

    double fs = 48000; int N = 2048, H = 512, pos = 0, count = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> win, inRing[2], outAcc[2], spec[2], binGain, power;
    std::array<double, kBands> bandHz {}; std::array<double, kFine> fineHz {};
    std::array<float, kBands> level {}, corr {}; std::array<float, kFine> fine {};
    std::array<std::atomic<float>, kBands> shown {};
    float olaScale = 0.5f, invScale = 1.0f; bool primed = false;
    ShaperParams prm;
};
} // namespace lumen::master
