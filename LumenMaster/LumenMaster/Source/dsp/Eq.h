#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Common.h"

// =====================================================================================
// Mastering EQ: 6 bands, each Stereo / Mid / Side.
//   Natural phase: analog-matched biquads in the M/S domain.
//   Linear phase:  a zero-phase FIR built from the exact analog magnitude of all bands
//                  (separate filters for Mid and Side), run by a uniformly partitioned
//                  FFT convolver. New filters are built off the audio thread and
//                  cross-faded in, so automation never clicks.
// =====================================================================================
namespace lumen::master
{
struct EqBand
{
    bool on = false; int type = Bell; double freq = 1000, gain = 0, q = 0.7; int slope = 1; int chan = ChStereo;
    bool operator== (const EqBand& o) const { return on == o.on && type == o.type && freq == o.freq && gain == o.gain && q == o.q && slope == o.slope && chan == o.chan; }
    bool operator!= (const EqBand& o) const { return ! (*this == o); }
    bool active() const { return on && (type == LowCut || type == HighCut || std::abs (gain) > 1e-4); }
};
static constexpr int kEqBands = 6;
using EqSet = std::array<EqBand, kEqBands>;

// combined magnitude of all bands for one channel role (0 = mid, 1 = side)
inline double eqMagnitude (const EqSet& s, double f, int role)
{
    double m = 1.0;
    for (auto& b : s)
        if (b.active() && (b.chan == ChStereo || b.chan == (role == 0 ? ChMid : ChSide)))
            m *= analogMag (b.type, f, b.freq, b.q, b.gain, b.slope);
    return m;
}

// ------------------------------------------------------------------------------------
class PartitionedConvolver   // 2 channels, block B, latency B
{
public:
    struct IR { int parts = 0; std::vector<std::complex<float>> h[2]; };   // [part * (B+1) + bin]

    ~PartitionedConvolver() { delete cur; delete pending.exchange (nullptr); delete garbage.exchange (nullptr); }

    void prepare (int blockSize, int maxParts)
    {
        B = blockSize; maxP = maxParts;
        int order = 0; while ((1 << order) < 2 * B) ++order;
        fft = std::make_unique<juce::dsp::FFT> (order);
        bins = B + 1;
        for (int c = 0; c < 2; ++c)
        {
            fdl[c].assign ((size_t) (maxP * bins), {});
            inBuf[c].assign ((size_t) B, 0.0f); prevIn[c].assign ((size_t) B, 0.0f); outBuf[c].assign ((size_t) B, 0.0f);
        }
        work.assign ((size_t) (4 * B), 0.0f); acc.assign ((size_t) bins, {}); acc2.assign ((size_t) bins, {});
        fdlPos = 0; idx = 0;
        // FFT round-trip gain (JUCE's inverse scaling differs between back-ends)
        std::fill (work.begin(), work.end(), 0.0f); work[0] = 1.0f;
        fft->performRealOnlyForwardTransform (work.data());
        fft->performRealOnlyInverseTransform (work.data());
        invScale = 1.0f / work[0];
    }
    int blockSize() const { return B; }
    int fftSize() const { return 2 * B; }
    float roundTripScale() const { return invScale; }

    // message / worker thread
    bool canAccept() const { return pending.load() == nullptr && garbage.load() == nullptr; }
    void submit (IR* ir) { delete pending.exchange (ir); }
    void collectGarbage() { delete garbage.exchange (nullptr); }
    void setImmediately (IR* ir) { delete cur; cur = ir; }      // offline use only
    bool hasIR() const { return cur != nullptr; }

    inline void process (float* a, float* b, int n) noexcept
    {
        float* ch[2] { a, b };
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c) { const float x = ch[c][i]; ch[c][i] = outBuf[c][(size_t) idx]; inBuf[c][(size_t) idx] = x; }
            if (++idx == B) { runBlock(); idx = 0; }
        }
    }

private:
    void runBlock() noexcept
    {
        IR* nx = pending.exchange (nullptr);
        const int P = std::max (1, std::min (maxP, std::max (cur ? cur->parts : 1, nx ? nx->parts : 1)));
        for (int c = 0; c < 2; ++c)
        {
            // new input spectrum into the frequency-domain delay line
            std::copy (prevIn[c].begin(), prevIn[c].end(), work.begin());
            std::copy (inBuf[c].begin(), inBuf[c].end(), work.begin() + B);
            std::fill (work.begin() + 2 * B, work.end(), 0.0f);
            fft->performRealOnlyForwardTransform (work.data(), true);
            auto* X = fdl[c].data() + fdlPos * bins;
            for (int k = 0; k < bins; ++k) X[k] = { work[(size_t) (2 * k)], work[(size_t) (2 * k + 1)] };
            std::copy (inBuf[c].begin(), inBuf[c].end(), prevIn[c].begin());

            auto convolve = [&] (const IR* ir, std::vector<std::complex<float>>& y)
            {
                std::fill (y.begin(), y.end(), std::complex<float> {});
                if (ir == nullptr) { const auto* x0 = fdl[c].data() + fdlPos * bins; for (int k = 0; k < bins; ++k) y[(size_t) k] = x0[k]; return; }
                const int parts = std::min (ir->parts, maxP);
                for (int p = 0; p < parts; ++p)
                {
                    const auto* x = fdl[c].data() + ((fdlPos - p + maxP) % maxP) * bins;
                    const auto* h = ir->h[c].data() + p * bins;
                    for (int k = 0; k < bins; ++k) y[(size_t) k] += x[k] * h[k];
                }
            };
            auto toTime = [&] (const std::vector<std::complex<float>>& y, float* dst)
            {
                for (int k = 0; k < bins; ++k) { work[(size_t) (2 * k)] = y[(size_t) k].real(); work[(size_t) (2 * k + 1)] = y[(size_t) k].imag(); }
                for (int k = 1; k < B; ++k) { work[(size_t) (2 * (2 * B - k))] = y[(size_t) k].real(); work[(size_t) (2 * (2 * B - k) + 1)] = -y[(size_t) k].imag(); }
                fft->performRealOnlyInverseTransform (work.data());
                for (int i = 0; i < B; ++i) dst[i] = work[(size_t) (B + i)] * invScale;
            };
            convolve (cur, acc);
            toTime (acc, outBuf[c].data());
            if (nx != nullptr)
            {
                convolve (nx, acc2);
                std::vector<float>& tmp = tmpOut; tmp.resize ((size_t) B);
                toTime (acc2, tmp.data());
                for (int i = 0; i < B; ++i) { const float t = (i + 0.5f) / B; outBuf[c][(size_t) i] += (tmp[(size_t) i] - outBuf[c][(size_t) i]) * t; }
            }
        }
        (void) P;
        fdlPos = (fdlPos + 1) % maxP;
        if (nx != nullptr) { IR* old = cur; cur = nx; if (old) delete garbage.exchange (old); }
    }

    int B = 512, maxP = 1, bins = 513, fdlPos = 0, idx = 0;
    float invScale = 1.0f;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<std::complex<float>> fdl[2], acc, acc2;
    std::vector<float> inBuf[2], prevIn[2], outBuf[2], work, tmpOut;
    IR* cur = nullptr;
    std::atomic<IR*> pending { nullptr }, garbage { nullptr };
};

// ------------------------------------------------------------------------------------
class MasterEq
{
public:
    static constexpr int kBlock = 512;

    void prepare (double sampleRate, bool lowLatencyCapable = true)
    {
        fs = sampleRate;
        taps = 1; while (taps < (int) (fs * 0.30)) taps <<= 1;            // 16384 @ 44.1/48k, 32768 @ 96k
        taps = std::clamp (taps, 4096, 65536);
        conv.prepare (kBlock, taps / kBlock);
        for (auto& d : delay) { d.prepare (linearLatency()); d.setDelay (linearLatency()); }
        for (auto& s : sections) for (auto& q : s) q.reset();
        (void) lowLatencyCapable;
        built = {}; builtValid = false; wantSet = {}; wantVersion = 1; builtVersion = 0;
        smoothed = {};
    }
    int linearLatency() const { return taps / 2 + kBlock; }

    // called by the audio thread every block with the current settings
    void setBands (const EqSet& s)
    {
        if (s != natural) { natural = s; updateNatural(); }
        if (s != wantSet) { const juce::SpinLock::ScopedLockType l (lock); wantSet = s; ++wantVersion; }
    }

    // worker thread: builds the linear-phase filter when the settings changed
    void service()
    {
        conv.collectGarbage();
        if (wantVersion.load() == builtVersion || ! conv.canAccept()) return;
        EqSet s; int v;
        { const juce::SpinLock::ScopedLockType l (lock); s = wantSet; v = wantVersion.load(); }
        conv.submit (buildIR (s));
        builtVersion = v;
    }
    // offline (assistant / tests): build synchronously
    void buildNow() { EqSet s; { const juce::SpinLock::ScopedLockType l (lock); s = wantSet; builtVersion = wantVersion.load(); } conv.setImmediately (buildIR (s)); }

    // linear phase (latency = linearLatency())
    void processLinear (float* L, float* R, int n) noexcept
    {
        for (int i = 0; i < n; ++i) { const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]); L[i] = m; R[i] = s; }
        conv.process (L, R, n);
        for (int i = 0; i < n; ++i) { const float m = L[i], s = R[i]; L[i] = m + s; R[i] = m - s; }
    }
    // natural phase; delayed = true adds the same latency as the linear path
    void processNatural (float* L, float* R, int n, bool applyBands, bool delayed) noexcept
    {
        if (applyBands) runSections (L, R, n);
        if (delayed) for (int i = 0; i < n; ++i) { L[i] = delay[0].tick (L[i]); R[i] = delay[1].tick (R[i]); }
    }

    // response of the natural-phase filters (for the display) or the analog target
    double responseDb (double f, int role, bool analog) const
    {
        if (analog) return gainToDb (eqMagnitude (natural, f, role));
        double m = 1.0;
        for (int b = 0; b < kEqBands; ++b)
            if (natural[(size_t) b].active() && (natural[(size_t) b].chan == ChStereo || natural[(size_t) b].chan == (role == 0 ? ChMid : ChSide)))
                for (int k = 0; k < nSec[(size_t) b]; ++k) m *= digitalMag (sections[(size_t) b][(size_t) k].c, f, fs);
        return gainToDb (m);
    }

private:
    void updateNatural()
    {
        for (int b = 0; b < kEqBands; ++b)
        {
            const auto& e = natural[(size_t) b];
            int k = 0;
            if (e.active())
            {
                if (e.type == LowCut || e.type == HighCut) for (double q : cutQs (e.slope, e.q)) sections[(size_t) b][(size_t) k++].c = designRbj (e.type, fs, e.freq, q, 0);
                else if (e.type == TiltShelf)
                {
                    sections[(size_t) b][(size_t) k++].c = designRbj (LowShelf, fs, e.freq, 0.5, -0.5 * e.gain);
                    sections[(size_t) b][(size_t) k++].c = designRbj (HighShelf, fs, e.freq, 0.5, 0.5 * e.gain);
                }
                else sections[(size_t) b][(size_t) k++].c = designRbj (e.type, fs, e.freq, e.q, e.gain);
            }
            if (k != nSec[(size_t) b]) for (auto& q : sections[(size_t) b]) q.reset();
            nSec[(size_t) b] = k;
        }
    }
    void runSections (float* L, float* R, int n) noexcept
    {
        bool any = false; for (int b = 0; b < kEqBands; ++b) any |= nSec[(size_t) b] > 0;
        if (! any) return;
        for (int i = 0; i < n; ++i)
        {
            double m = 0.5 * ((double) L[i] + R[i]), s = 0.5 * ((double) L[i] - R[i]);
            for (int b = 0; b < kEqBands; ++b)
            {
                const int ns = nSec[(size_t) b]; if (ns == 0) continue;
                const int ch = natural[(size_t) b].chan;
                for (int k = 0; k < ns; ++k)
                {
                    auto& q = sections[(size_t) b][(size_t) k];
                    if (ch != ChSide) m = q.tick (m, 0);
                    if (ch != ChMid)  s = q.tick (s, 1);
                }
            }
            L[i] = (float) (m + s); R[i] = (float) (m - s);
        }
    }

    PartitionedConvolver::IR* buildIR (const EqSet& s) const
    {
        const int N = taps, half = N / 2;
        int order = 0; while ((1 << order) < N) ++order;
        juce::dsp::FFT big (order);
        std::vector<float> w ((size_t) (2 * N), 0.0f);
        auto* ir = new PartitionedConvolver::IR();
        const int B = conv.blockSize(), bins = B + 1;
        ir->parts = N / B;
        int pOrder = 0; while ((1 << pOrder) < 2 * B) ++pOrder;
        juce::dsp::FFT small (pOrder);
        // round-trip gain of the big FFT
        std::vector<float> t ((size_t) (2 * N), 0.0f); t[0] = 1.0f; big.performRealOnlyForwardTransform (t.data()); big.performRealOnlyInverseTransform (t.data());
        const float bigScale = 1.0f / t[0];
        for (int role = 0; role < 2; ++role)
        {
            std::fill (w.begin(), w.end(), 0.0f);
            for (int k = 0; k <= half; ++k)
            {
                const double f = (double) k * fs / N;
                const float m = (float) eqMagnitude (s, std::max (f, 0.5), role);
                w[(size_t) (2 * k)] = m;
                if (k > 0 && k < half) w[(size_t) (2 * (N - k))] = m;
            }
            big.performRealOnlyInverseTransform (w.data());
            // zero-phase -> centred, Blackman window
            std::vector<float> h ((size_t) N);
            for (int i = 0; i < N; ++i)
            {
                const int src = (i - half + N) % N;
                const double win = 0.42 - 0.5 * std::cos (2 * kPi * (i + 0.5) / N) + 0.08 * std::cos (4 * kPi * (i + 0.5) / N);
                h[(size_t) i] = (float) (w[(size_t) src] * bigScale * win);
            }
            // partition + transform
            ir->h[role].assign ((size_t) (ir->parts * bins), {});
            std::vector<float> pw ((size_t) (4 * B));
            for (int p = 0; p < ir->parts; ++p)
            {
                std::fill (pw.begin(), pw.end(), 0.0f);
                for (int i = 0; i < B; ++i) pw[(size_t) i] = h[(size_t) (p * B + i)];
                small.performRealOnlyForwardTransform (pw.data(), true);
                for (int k = 0; k < bins; ++k) ir->h[role][(size_t) (p * bins + k)] = { pw[(size_t) (2 * k)], pw[(size_t) (2 * k + 1)] };
            }
        }
        return ir;
    }

    double fs = 48000; int taps = 16384;
    PartitionedConvolver conv;
    DelayLine delay[2];
    EqSet natural {}, wantSet {}, built {}; bool builtValid = false;
    std::array<std::array<Biquad, 4>, kEqBands> sections {};
    std::array<int, kEqBands> nSec {};
    juce::SpinLock lock;
    std::atomic<int> wantVersion { 1 }; int builtVersion = 0;
    EqSet smoothed {};
};

} // namespace lumen::master
