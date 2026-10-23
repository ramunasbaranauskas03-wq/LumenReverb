#pragma once
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <vector>
#include <atomic>
#include <memory>

// uniformly partitioned FFT convolver (2 channels, block B, latency B); new IRs crossfade in over one block
namespace lumen::eq2
{
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
    bool canAccept() const { return garbage.load() == nullptr; }   // a newer IR may replace a pending one
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

}
