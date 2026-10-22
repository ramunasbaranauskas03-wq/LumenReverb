#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <atomic>

// =====================================================================================
// ITU-R BS.1770-4 / EBU R128 loudness meter
//   K-weighting (coefficients derived for any sample rate), 100 ms sub-blocks,
//   Momentary (400 ms), Short-term (3 s), Integrated (abs -70 / rel -10 gating, histogram -> any length),
//   LRA (EBU Tech 3342: short-term, abs -70 / rel -20, 10th..95th percentile), True Peak (4x polyphase FIR).
// =====================================================================================
namespace lumen::meter
{

struct KFilter
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double c0 = 1, c1 = -2, c2 = 1, d1 = 0, d2 = 0;
    double z[4] {};
    void design (double fs)
    {
        // stage 1: high shelf
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            b0 = (Vh + Vb * K / Q + K * K) / a0; b1 = 2.0 * (K * K - Vh) / a0; b2 = (Vh - Vb * K / Q + K * K) / a0;
            a1 = 2.0 * (K * K - 1.0) / a0; a2 = (1.0 - K / Q + K * K) / a0;
        }
        // stage 2: RLB high-pass
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs), a0 = 1.0 + K / Q + K * K;
            d1 = 2.0 * (K * K - 1.0) / a0; d2 = (1.0 - K / Q + K * K) / a0;
            c0 = 1.0; c1 = -2.0; c2 = 1.0;
        }
        reset();
    }
    void reset() { for (auto& v : z) v = 0; }
    inline double process (double x) noexcept
    {
        const double y = b0 * x + z[0]; z[0] = b1 * x - a1 * y + z[1]; z[1] = b2 * x - a2 * y;
        const double w = c0 * y + z[2]; z[2] = c1 * y - d1 * w + z[3]; z[3] = c2 * y - d2 * w;
        return w;
    }
};

class TruePeak
{
public:
    void prepare (double fs)
    {
        os = fs >= 176400.0 ? 1 : fs >= 88200.0 ? 2 : 4;
        const int taps = 12;                      // per phase
        len = taps;
        h.assign ((size_t) (os * taps), 0.0f);
        const double centre = (os * taps - 1) * 0.5;
        for (int i = 0; i < os * taps; ++i)
        {
            const double x = (i - centre) / os;
            const double sinc = std::abs (x) < 1e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x * 0.96) / (juce::MathConstants<double>::pi * x);
            const double w = 0.42 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * (i + 0.5) / (os * taps)) + 0.08 * std::cos (4 * juce::MathConstants<double>::pi * (i + 0.5) / (os * taps));
            h[(size_t) i] = (float) (sinc * w * (os > 1 ? 0.96 : 1.0));
        }
        // normalise each phase to unity DC gain
        for (int p = 0; p < os; ++p)
        {
            double s = 0; for (int j = 0; j < taps; ++j) s += h[(size_t) (p + os * j)];
            for (int j = 0; j < taps; ++j) h[(size_t) (p + os * j)] = (float) (h[(size_t) (p + os * j)] / s);
        }
        for (auto& b : hist) b.assign ((size_t) taps, 0.0f);
        pos = 0;
    }
    // returns the largest absolute inter-sample value of this sample (both channels)
    inline float process (float l, float r) noexcept
    {
        if (os == 1) return std::max (std::abs (l), std::abs (r));
        hist[0][(size_t) pos] = l; hist[1][(size_t) pos] = r;
        float m = 0;
        for (int c = 0; c < 2; ++c)
            for (int p = 0; p < os; ++p)
            {
                float acc = 0;
                for (int j = 0; j < len; ++j) acc += h[(size_t) (p + os * j)] * hist[c][(size_t) ((pos - j + len) % len)];
                m = std::max (m, std::abs (acc));
            }
        pos = (pos + 1) % len;
        return m;
    }
    int latencyDummy = 0;
private:
    int os = 4, len = 12, pos = 0;
    std::vector<float> h, hist[2];
};

class LoudnessMeter
{
public:
    static constexpr float kFloor = -70.0f;
    static constexpr int kBins = 1000;            // -70 .. +30 LUFS in 0.1 LU

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& k : kf) k.design (fs);
        tp.prepare (fs);
        blockLen = (int) std::round (fs * 0.1);
        reset();
    }
    void reset()
    {
        for (auto& k : kf) k.reset();
        acc = 0; accN = 0; nBlocks = 0;
        std::fill (std::begin (ring), std::end (ring), 0.0);
        for (auto& b : intBins) b = {};
        for (auto& b : stBins) b = {};
        intCount = 0; stCount = 0;
        mom = sht = intg = lra = kFloor - 10.0f; momMax = shtMax = kFloor - 10.0f;
        tpMax = 0; tpDecay = 0; samplePeak = 0; clipCount = 0;
    }

    void process (const float* L, const float* R, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double l = kf[0].process (L[i]), r = kf[1].process (R[i]);
            acc += l * l + r * r; ++accN;
            const float t = tp.process (L[i], R[i]);
            tpMax = std::max (tpMax, t);
            tpDecay = std::max (t, tpDecay * tpRel);
            samplePeak = std::max (samplePeak, std::max (std::abs (L[i]), std::abs (R[i])));
            if (std::abs (L[i]) >= 0.99999f || std::abs (R[i]) >= 0.99999f) ++clipCount;
            if (accN >= blockLen) { pushBlock (acc / accN); acc = 0; accN = 0; }
        }
    }

    // results (LUFS / LU / linear peaks)
    float momentary() const noexcept { return mom; }
    float shortTerm() const noexcept { return sht; }
    float integrated() const noexcept { return intg; }
    float range() const noexcept { return lra; }
    float momentaryMax() const noexcept { return momMax; }
    float shortTermMax() const noexcept { return shtMax; }
    float truePeakMax() const noexcept { return tpMax; }
    float truePeakNow() const noexcept { return tpDecay; }
    float samplePeakMax() const noexcept { return samplePeak; }
    int clips() const noexcept { return clipCount; }
    int blocks() const noexcept { return nBlocks; }
    bool hasIntegrated() const noexcept { return intg > kFloor; }

private:
    static float toLufs (double ms) { return (float) (-0.691 + 10.0 * std::log10 (std::max (1e-20, ms))); }
    static int bin (float lufs) { return std::clamp ((int) std::floor ((lufs - kFloor) * 10.0f), 0, kBins - 1); }

    void pushBlock (double ms) noexcept
    {
        ring[(size_t) (nBlocks % 30)] = ms;
        ++nBlocks;
        // momentary: last 4 sub-blocks
        if (nBlocks >= 4)
        {
            double s = 0; for (int k = 0; k < 4; ++k) s += ring[(size_t) ((nBlocks - 1 - k) % 30)];
            const double m = s / 4.0;
            mom = toLufs (m); momMax = std::max (momMax, mom);
            // gating block (400 ms, 75 % overlap) for the integrated loudness
            if (mom > kFloor) { auto& b = intBins[(size_t) bin (mom)]; b.power += m; ++b.count; ++intCount; }
            updateIntegrated();
        }
        else mom = toLufs (ring[(size_t) ((nBlocks - 1) % 30)]);
        // short-term: last 30 sub-blocks
        if (nBlocks >= 30)
        {
            double s = 0; for (int k = 0; k < 30; ++k) s += ring[(size_t) k];
            const double st = s / 30.0;
            sht = toLufs (st); shtMax = std::max (shtMax, sht);
            if (sht > kFloor) { auto& b = stBins[(size_t) bin (sht)]; b.power += st; ++b.count; ++stCount; }
            updateRange();
        }
        else
        {
            double s = 0; const int k = (int) nBlocks; for (int j = 0; j < k; ++j) s += ring[(size_t) j];
            sht = toLufs (s / k);
        }
    }

    void updateIntegrated() noexcept
    {
        double p = 0; long c = 0;
        for (auto& b : intBins) { p += b.power; c += b.count; }
        if (c == 0) { intg = kFloor - 10.0f; return; }
        const float rel = toLufs (p / c) - 10.0f;
        double p2 = 0; long c2 = 0;
        for (int i = std::max (0, bin (rel)); i < kBins; ++i)
        {
            // bins straddling the gate: count those above it (bin resolution 0.1 LU)
            if (kFloor + (float) i * 0.1f + 0.05f < rel) continue;
            p2 += intBins[(size_t) i].power; c2 += intBins[(size_t) i].count;
        }
        intg = c2 > 0 ? toLufs (p2 / c2) : kFloor - 10.0f;
    }

    void updateRange() noexcept
    {
        double p = 0; long c = 0;
        for (auto& b : stBins) { p += b.power; c += b.count; }
        if (c < 2) { lra = 0; return; }
        const float rel = toLufs (p / c) - 20.0f;
        long n = 0; const int first = bin (rel);
        for (int i = first; i < kBins; ++i) n += stBins[(size_t) i].count;
        if (n < 2) { lra = 0; return; }
        auto pct = [&] (double q)
        {
            const long target = (long) std::floor (q * (double) (n - 1));
            long seen = 0;
            for (int i = first; i < kBins; ++i) { seen += stBins[(size_t) i].count; if (seen > target) return kFloor + (float) i * 0.1f + 0.05f; }
            return kFloor;
        };
        lra = pct (0.95) - pct (0.10);
    }

    struct Bin { double power = 0; long count = 0; };
    double fs = 48000;
    KFilter kf[2];
    TruePeak tp;
    int blockLen = 4800;
    double acc = 0; int accN = 0;
    long nBlocks = 0;
    double ring[30] {};
    std::array<Bin, kBins> intBins {}, stBins {};
    long intCount = 0, stCount = 0;
    float mom = -80, sht = -80, intg = -80, lra = 0, momMax = -80, shtMax = -80;
    float tpMax = 0, tpDecay = 0, samplePeak = 0;
    float tpRel = 0.99995f;
    int clipCount = 0;
};

} // namespace lumen::meter
