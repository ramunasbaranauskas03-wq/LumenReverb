#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Resampler.h"

// =====================================================================================
// Lumen Synth Sampler - harmonic resynthesis ("SYNTH" mode)
//
//   A pitched sample (bass / pluck / lead / pad) is analysed once:
//     1. f0 is tracked over time (FFT-based YIN, parabolic refinement)
//     2. the sample is cut into pitch-synchronous single cycles (each made seamlessly
//        periodic by a cross-fade with the previous cycle)
//     3. every cycle is stored as a stack of band-limited tables (half-octave harmonic
//        steps), stereo, together with its time and the sample's own pitch movement
//
//   Playback reads the cycles with a free-running phase at ANY note while time advances
//   at the original speed: the note keeps its length, envelope, movement and harmonic
//   timbre exactly like a synth patch played higher or lower - no "chipmunk" effect.
// =====================================================================================
namespace lumen::sampler
{
struct Resynth
{
    struct Level { int len = 0, harm = 0, offset = 0; };
    bool valid = false;
    int channels = 1, K = 0, stride = 0;
    double rate = 48000, f0 = 0, length = 0;             // source rate, root frequency (Hz), length (samples)
    double voicedFraction = 0;
    std::vector<Level> levels;
    std::vector<double> frameTime;                        // source sample index of every frame (K + 1, last = silent end frame)
    std::vector<float> ratio;                             // f0(t) / f0 at every frame
    std::vector<float> data[2];                           // [frame * stride + level.offset + 1 + i], 1 guard before, 2 after

    const float* table (int k, int l, int ch) const noexcept
    { return data[(size_t) std::min (ch, channels - 1)].data() + (size_t) k * (size_t) stride + (size_t) levels[(size_t) l].offset + 1; }
    int levelFor (double hz, double outRate) const noexcept
    {
        const double hmax = 0.47 * outRate / std::max (1.0, hz);
        for (int l = 0; l < (int) levels.size(); ++l) if (levels[(size_t) l].harm <= hmax) return l;
        return (int) levels.size() - 1;
    }
};

class ResynthAnalyser
{
public:
    static constexpr int kN0 = 2048;          // level-0 table length (harmonics <= 512, 2x oversampled)
    static constexpr int kMaxFrames = 256;

    // f0 track (FFT based YIN). Returns per-hop f0 (0 = unvoiced) and the hop size.
    static std::vector<double> trackPitch (const SampleSource& s, int& hop, std::vector<double>& energy)
    {
        const double fs = s.rate;
        hop = std::max (64, (int) std::round (fs * 0.005));
        const int tauMax = std::min (4096, (int) (fs / 25.0)), tauMin = std::max (2, (int) (fs / 2500.0));
        int W = 1; while (W < tauMax) W <<= 1;                      // integration window >= longest period
        int nfft = 1; while (nfft < 2 * W + tauMax) nfft <<= 1;
        int order = 0; while ((1 << order) < nfft) ++order;
        juce::dsp::FFT fft (order);
        std::vector<float> a ((size_t) (2 * nfft)), b ((size_t) (2 * nfft));
        const float* L = s.data (0, 0); const float* R = s.data (0, 1);
        auto x = [&] (int i) { return (i < 0 || i >= s.length) ? 0.0f : 0.5f * (L[i] + R[i]); };
        std::vector<double> f0;
        // fft round trip scale
        std::fill (a.begin(), a.end(), 0.0f); a[0] = 1; fft.performRealOnlyForwardTransform (a.data()); fft.performRealOnlyInverseTransform (a.data());
        const double sc = 1.0 / a[0];
        for (int c = 0; c < s.length; c += hop)
        {
            const int st = c - W / 2;
            // a = window (W samples), b = segment (W + tauMax)
            std::fill (a.begin(), a.end(), 0.0f); std::fill (b.begin(), b.end(), 0.0f);
            double e = 0;
            for (int j = 0; j < W; ++j) { a[(size_t) j] = x (st + j); e += (double) a[(size_t) j] * a[(size_t) j]; }
            for (int j = 0; j < W + tauMax; ++j) b[(size_t) j] = x (st + j);
            energy.push_back (e / W);
            if (e / W < 1e-9) { f0.push_back (0); continue; }
            std::vector<double> sq ((size_t) (W + tauMax)); for (int j = 0; j < W + tauMax; ++j) sq[(size_t) j] = (double) b[(size_t) j] * b[(size_t) j];
            fft.performRealOnlyForwardTransform (a.data()); fft.performRealOnlyForwardTransform (b.data());
            // cross-correlation r(tau) = sum a[j] b[j + tau]  ->  conj(A) * B
            for (int k = 0; k < nfft; ++k)
            {
                const float ar = a[(size_t) (2 * k)], ai = a[(size_t) (2 * k + 1)], br = b[(size_t) (2 * k)], bi = b[(size_t) (2 * k + 1)];
                a[(size_t) (2 * k)] = ar * br + ai * bi; a[(size_t) (2 * k + 1)] = ar * bi - ai * br;
            }
            fft.performRealOnlyInverseTransform (a.data());
            // d(tau) = E(window) + E(segment shifted) - 2 r(tau)
            std::vector<double> d ((size_t) tauMax + 2, 0.0), dn ((size_t) tauMax + 2, 1.0);
            double eShift = 0; for (int j = 0; j < W; ++j) eShift += sq[(size_t) j];
            for (int tau = 1; tau <= tauMax; ++tau)
            {
                eShift += sq[(size_t) (W + tau - 1)] - sq[(size_t) (tau - 1)];
                d[(size_t) tau] = std::max (0.0, e + eShift - 2.0 * a[(size_t) tau] * sc);
            }
            double run = 0;
            for (int tau = 1; tau <= tauMax; ++tau) { run += d[(size_t) tau]; dn[(size_t) tau] = run > 0 ? d[(size_t) tau] * tau / run : 1.0; }
            int best = -1;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (dn[(size_t) tau] < 0.2 && dn[(size_t) tau] <= dn[(size_t) tau - 1] && dn[(size_t) tau] <= dn[(size_t) tau + 1])
                {
                    // walk down to the local minimum
                    while (tau + 1 < tauMax && dn[(size_t) tau + 1] < dn[(size_t) tau]) ++tau;
                    best = tau; break;
                }
            if (best < 0) { f0.push_back (0); continue; }
            const double y0 = dn[(size_t) best - 1], y1 = dn[(size_t) best], y2 = dn[(size_t) best + 1], den = y0 - 2 * y1 + y2;
            const double t = best + (std::abs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0);
            f0.push_back (fs / t);
        }
        return f0;
    }

    static std::shared_ptr<Resynth> analyse (const SampleSource& s)
    {
        auto r = std::make_shared<Resynth>();
        r->channels = s.channels; r->rate = s.rate; r->length = s.length;
        if (s.length < 512) return r;
        int hop = 256; std::vector<double> en;
        auto f0 = trackPitch (s, hop, en);
        const int H = (int) f0.size();
        // root: energy-weighted median of log f0 over voiced hops
        std::vector<std::pair<double, double>> v; double eAll = 0, eV = 0;
        for (int i = 0; i < H; ++i) { eAll += en[(size_t) i]; if (f0[(size_t) i] > 0) { v.push_back ({ std::log2 (f0[(size_t) i]), en[(size_t) i] }); eV += en[(size_t) i]; } }
        r->voicedFraction = eAll > 0 ? eV / eAll : 0;
        if (v.empty() || r->voicedFraction < 0.5) return r;
        std::sort (v.begin(), v.end());
        double acc = 0, root = v.front().first;
        for (auto& p : v) { acc += p.second; if (acc >= 0.5 * eV) { root = p.first; break; } }
        r->f0 = std::pow (2.0, root);
        // clean the track: octave errors -> nearest octave of the root, unvoiced -> hold neighbours, median of 5
        double last = r->f0;
        for (auto& f : f0) { if (f <= 0) f = last; else last = f; }
        { auto cp = f0; for (int i = 0; i < H; ++i) { double w[5]; int n = 0; for (int j = i - 2; j <= i + 2; ++j) w[n++] = cp[(size_t) std::clamp (j, 0, H - 1)]; std::sort (w, w + 5); f0[(size_t) i] = w[2]; } }
        // cycle starts by integrating the phase
        std::vector<double> starts; starts.push_back (0.0);
        {
            double ph = 0;
            for (int i = 0; i < s.length; ++i)
            {
                const double hp = (double) i / hop; const int h0 = std::min (H - 1, (int) hp); const int h1 = std::min (H - 1, h0 + 1); const double t = hp - h0;
                const double f = f0[(size_t) h0] + (f0[(size_t) h1] - f0[(size_t) h0]) * t;
                const double inc = f / s.rate;
                if (ph + inc >= 1.0) { starts.push_back (i + (1.0 - ph) / inc); ph = ph + inc - 1.0; } else ph += inc;
            }
        }
        if (starts.size() < 3) return r;
        const int C = (int) starts.size() - 1;               // complete cycles
        const int K = std::min (kMaxFrames, C);
        // levels
        for (int l = 0;; ++l)
        {
            const int h = (int) std::floor (512.0 / std::pow (2.0, l * 0.5));
            if (h < 1) break;
            int len = 8; while (len < 4 * h) len <<= 1;
            Resynth::Level lv; lv.len = len; lv.harm = h; lv.offset = r->stride;
            r->stride += len + 3;
            r->levels.push_back (lv);
            if (h == 1) break;
        }
        r->K = K;
        for (int c = 0; c < r->channels; ++c) r->data[c].assign ((size_t) (K + 1) * (size_t) r->stride, 0.0f);
        r->frameTime.resize ((size_t) K + 1); r->ratio.resize ((size_t) K + 1);
        int order = 0; while ((1 << order) < kN0) ++order;
        juce::dsp::FFT fft0 (order);
        std::vector<std::unique_ptr<juce::dsp::FFT>> lf;
        for (auto& lv : r->levels) { int o = 0; while ((1 << o) < lv.len) ++o; lf.push_back (std::make_unique<juce::dsp::FFT> (o)); }
        std::vector<float> w ((size_t) (2 * kN0)), t2;
        // round-trip scales
        auto rt = [] (juce::dsp::FFT& f, int n) { std::vector<float> q ((size_t) (2 * n), 0.0f); q[0] = 1; f.performRealOnlyForwardTransform (q.data()); f.performRealOnlyInverseTransform (q.data()); return 1.0f / q[0]; };
        const float sc0 = rt (fft0, kN0);
        std::vector<float> lsc; for (size_t l = 0; l < r->levels.size(); ++l) lsc.push_back (rt (*lf[l], r->levels[l].len));
        for (int k = 0; k < K; ++k)
        {
            const int c = K == C ? k : (int) std::lround ((double) k * (C - 1) / std::max (1, K - 1));
            const double t0 = starts[(size_t) c], P = starts[(size_t) c + 1] - t0;
            r->frameTime[(size_t) k] = t0;
            const double hp = t0 / hop; const int h0 = std::min (H - 1, (int) hp);
            r->ratio[(size_t) k] = (float) (f0[(size_t) h0] / r->f0);
            // one seamless cycle per channel, kN0 points (cross-fade with the previous cycle makes it periodic)
            std::vector<float> cyc[2]; cyc[0].resize (kN0); cyc[1].resize (kN0);
            for (int n = 0; n < kN0; ++n)
            {
                const double u = (double) n / kN0, pos = t0 + u * P;
                float a0, b0, a1, b1;
                SincReader::read (s, pos, P / kN0, a0, b0);
                SincReader::read (s, pos - P, P / kN0, a1, b1);
                cyc[0][(size_t) n] = (float) ((1.0 - u) * a0 + u * a1);
                cyc[1][(size_t) n] = (float) ((1.0 - u) * b0 + u * b1);
            }
            for (int ch = 0; ch < r->channels; ++ch)
            {
                for (int n = 0; n < kN0; ++n) w[(size_t) n] = cyc[ch][(size_t) n];
                std::fill (w.begin() + kN0, w.end(), 0.0f);
                fft0.performRealOnlyForwardTransform (w.data(), true);
                for (size_t l = 0; l < r->levels.size(); ++l)
                {
                    const auto& lv = r->levels[l];
                    t2.assign ((size_t) (2 * lv.len), 0.0f);
                    const float g = (float) lv.len / kN0 * sc0 * lsc[l] / sc0;
                    for (int h = 0; h <= std::min (lv.harm, lv.len / 2 - 1); ++h)
                    {
                        t2[(size_t) (2 * h)] = w[(size_t) (2 * h)] * g; t2[(size_t) (2 * h + 1)] = w[(size_t) (2 * h + 1)] * g;
                        if (h > 0) { t2[(size_t) (2 * (lv.len - h))] = w[(size_t) (2 * h)] * g; t2[(size_t) (2 * (lv.len - h) + 1)] = -w[(size_t) (2 * h + 1)] * g; }
                    }
                    lf[l]->performRealOnlyInverseTransform (t2.data());
                    float* dst = r->data[ch].data() + (size_t) k * r->stride + lv.offset;
                    for (int i = 0; i < lv.len; ++i) dst[1 + i] = t2[(size_t) i];
                    dst[0] = dst[lv.len]; dst[lv.len + 1] = dst[1]; dst[lv.len + 2] = dst[2];
                }
            }
        }
        // silent end frame one period after the last cycle
        r->frameTime[(size_t) K] = std::min ((double) s.length, starts[(size_t) C]) + (starts[(size_t) C] - starts[(size_t) C - 1]);
        r->ratio[(size_t) K] = r->ratio[(size_t) K - 1];
        if (r->frameTime[(size_t) K] <= r->frameTime[(size_t) K - 1]) r->frameTime[(size_t) K] = r->frameTime[(size_t) K - 1] + 1;
        r->valid = true;
        return r;
    }
};

// Plays a Resynth at any frequency: free-running phase, time at its own speed
struct ResynthReader
{
    double phase = 0, tau = 0; int k = 0, level = 0;
    void start (double startTau, double startPhase) { tau = startTau; phase = startPhase - std::floor (startPhase); k = 0; }

    // returns false when the sound has ended
    inline bool read (const Resynth& r, double hz, double outRate, double timeStep, float& outL, float& outR, float& curRatio) noexcept
    {
        const int K = r.K;
        if (tau >= r.frameTime[(size_t) K]) return false;
        while (k + 1 < K && tau >= r.frameTime[(size_t) k + 1]) ++k;
        while (k > 0 && tau < r.frameTime[(size_t) k]) --k;
        const double t0 = r.frameTime[(size_t) k], t1 = r.frameTime[(size_t) k + 1];
        const float w = (float) std::clamp ((tau - t0) / std::max (1e-9, t1 - t0), 0.0, 1.0);
        curRatio = r.ratio[(size_t) k] + (r.ratio[(size_t) k + 1] - r.ratio[(size_t) k]) * w;
        const double f = hz * curRatio;
        level = r.levelFor (f, outRate);
        const int len = r.levels[(size_t) level].len;
        const double x = phase * len; const int i = (int) x; const float fr = (float) (x - i);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* a = r.table (k, level, ch); const float* b = r.table (k + 1, level, ch);
            const float va = herm (a, i, fr), vb = herm (b, i, fr);
            (ch == 0 ? outL : outR) = va + (vb - va) * w;
        }
        phase += f / outRate; phase -= std::floor (phase);
        tau += timeStep;
        return true;
    }
    static inline float herm (const float* t, int i, float f) noexcept
    {
        const float xm1 = t[i - 1], x0 = t[i], x1 = t[i + 1], x2 = t[i + 2];
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
};
} // namespace lumen::sampler
