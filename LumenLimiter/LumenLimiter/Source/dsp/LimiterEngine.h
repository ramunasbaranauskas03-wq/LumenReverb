#pragma once
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdint>

namespace lumen
{

constexpr double kPiL = 3.14159265358979323846;

//==============================================================================
// 4x true-peak estimator (windowed-sinc interpolation, 16 taps per phase).
// push(x) returns max |value| over sample (n-8) and the 3 interpolated points after it.
class TruePeak
{
public:
    TruePeak()
    {
        auto bessel0 = [] (double x)
        {
            double s = 1.0, t = 1.0;
            for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; }
            return s;
        };
        const double beta = 7.0, half = 8.0;
        for (int k = 1; k <= 3; ++k)
        {
            const double frac = k / 4.0;
            double sum = 0.0;
            for (int j = 0; j < 16; ++j)
            {
                // tap j uses sample (n-15+j); target time is (n-8+frac)
                const double t = (double) (j - 7) - frac;      // distance from target
                const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (kPiL * t) / (kPiL * t);
                const double r = t / half;
                const double win = std::abs (r) < 1.0 ? bessel0 (beta * std::sqrt (1.0 - r * r)) / bessel0 (beta) : 0.0;
                w[k - 1][j] = (float) (sinc * win);
                sum += sinc * win;
            }
            for (int j = 0; j < 16; ++j) w[k - 1][j] = (float) (w[k - 1][j] / sum);
        }
        reset();
    }

    void reset() { std::fill (std::begin (h), std::end (h), 0.0f); pos = 0; }

    inline float push (float x) noexcept
    {
        h[pos] = x;
        pos = (pos + 1) & 15;               // h[pos] is now the oldest (n-15)
        float m = std::fabs (h[(pos + 7) & 15]);   // sample n-8
        for (int k = 0; k < 3; ++k)
        {
            float acc = 0.0f;
            for (int j = 0; j < 16; ++j) acc += w[k][j] * h[(pos + j) & 15];
            m = std::max (m, std::fabs (acc));
        }
        return m;
    }

    static constexpr int kDelay = 8;

private:
    float w[3][16] {};
    float h[16] {};
    int pos = 0;
};

//==============================================================================
// Lookahead limiter core for one channel's gain path:
//   required gain -> sliding min over W -> release follower -> box average over W.
// Guarantees the smoothed gain at a peak is <= the gain that peak needs.
class GainPath
{
public:
    void prepare (int maxW)
    {
        cap = 1; while (cap < maxW + 4) cap <<= 1;
        dqVal.assign ((size_t) cap, 1.0f); dqIdx.assign ((size_t) cap, 0);
        box.assign ((size_t) cap, 1.0f);
        setWindow (std::max (1, maxW / 4));
    }

    void setWindow (int w)
    {
        W = std::max (1, std::min (w, cap - 2));
        head = tail = 0; counter = 0;
        std::fill (box.begin(), box.end(), 1.0f);
        boxSum = (double) W; boxPos = 0;
        env = slow = 1.0f;
    }

    void setRelease (float fastCoef, float slowCoef, float slowAttack, bool dual) noexcept
    { rFast = fastCoef; rSlow = slowCoef; aSlow = slowAttack; dualStage = dual; }

    inline float process (float req) noexcept
    {
        // sliding minimum (monotonic deque)
        const int64_t i = counter++;
        while (tail != head && dqVal[(size_t) ((tail - 1) & (cap - 1))] >= req) --tail;
        dqVal[(size_t) (tail & (cap - 1))] = req; dqIdx[(size_t) (tail & (cap - 1))] = i; ++tail;
        while (dqIdx[(size_t) (head & (cap - 1))] <= i - W) ++head;
        const float held = dqVal[(size_t) (head & (cap - 1))];

        // release (instant attack)
        if (held < env) env = held;
        else            env = held + (env - held) * rFast;
        float g = env;   // env <= held always (instant attack, release from below)
        if (dualStage)
        {
            // Slow stage: sinks slowly during sustained limiting and recovers slowly,
            // so long passages release gently while single transients recover fast.
            if (held < slow) slow = held + (slow - held) * aSlow;
            else             slow = held + (slow - held) * rSlow;
            g = std::min (env, slow);
        }

        // box average over W
        boxSum += (double) g - (double) box[(size_t) boxPos];
        box[(size_t) boxPos] = g;
        boxPos = (boxPos + 1) % W;
        if ((counter & 0xFFF) == 0) { boxSum = 0.0; for (int k = 0; k < W; ++k) boxSum += box[(size_t) k]; } // drift guard
        return (float) (boxSum / (double) W);
    }

    int getWindow() const noexcept { return W; }

private:
    std::vector<float> dqVal, box;
    std::vector<int64_t> dqIdx;
    int cap = 0, W = 1, boxPos = 0;
    int64_t head = 0, tail = 0, counter = 0;
    double boxSum = 1.0;
    float env = 1.0f, slow = 1.0f, rFast = 0.99f, rSlow = 0.999f, aSlow = 0.999f;
    bool dualStage = false;
};

//==============================================================================
// Simple delay line (integer)
class IntDelay
{
public:
    void prepare (int maxD) { buf.assign ((size_t) maxD + 2, 0.0f); pos = 0; d = 0; }
    void setDelay (int dd) { d = std::clamp (dd, 0, (int) buf.size() - 2); std::fill (buf.begin(), buf.end(), 0.0f); }
    inline float process (float x) noexcept
    {
        const int n = (int) buf.size();
        buf[(size_t) pos] = x;
        int r = pos - d; if (r < 0) r += n;
        const float y = buf[(size_t) r];
        pos = (pos + 1) % n;
        return y;
    }
private:
    std::vector<float> buf;
    int pos = 0, d = 0;
};

//==============================================================================
// Soft-knee clipper: unity below the knee, smooth (or hard) approach to +-1.
inline float clipShape (float u, float hardness) noexcept
{
    const float k = 0.5f + 0.499f * std::clamp (hardness, 0.0f, 1.0f);   // knee start
    const float a = std::fabs (u);
    if (a <= k) return u;
    const float y = k + (1.0f - k) * std::tanh ((a - k) / (1.0f - k));
    return u < 0.0f ? -y : y;
}

//==============================================================================
// K-weighted loudness meter (ITU-R BS.1770-4 / EBU R128): momentary, short-term, integrated.
class LoudnessMeter
{
public:
    void prepare (double fs)
    {
        // Stage 1: high shelf
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPiL * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1 = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                   2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        // Stage 2: RLB high-pass
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (kPiL * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            s2 = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        blockLen = std::max (1, (int) std::round (fs * 0.1));
        reset();
    }

    void reset()
    {
        for (auto& c : st) c = {};
        acc = 0.0; accN = 0; ringPos = 0; ringCount = 0;
        std::fill (std::begin (ring), std::end (ring), 0.0);
        std::fill (std::begin (histCount), std::end (histCount), 0);
        std::fill (std::begin (histSum), std::end (histSum), 0.0);
        momentary = shortTerm = integrated = -100.0f;
    }

    inline void process (float l, float r) noexcept
    {
        const double kl = filt (0, l), kr = filt (1, r);
        acc += kl * kl + kr * kr;
        if (++accN >= blockLen) finishBlock();
    }

    float momentary = -100.0f, shortTerm = -100.0f, integrated = -100.0f;

private:
    struct Bq { double b0, b1, b2, a1, a2; };
    struct State { double x1 = 0, x2 = 0, y1 = 0, y2 = 0, z1 = 0, z2 = 0, w1 = 0, w2 = 0; };

    inline double filt (int c, double x) noexcept
    {
        auto& s = st[c];
        const double y = s1.b0 * x + s1.b1 * s.x1 + s1.b2 * s.x2 - s1.a1 * s.y1 - s1.a2 * s.y2;
        s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y;
        const double z = s2.b0 * y + s2.b1 * s.z1 + s2.b2 * s.z2 - s2.a1 * s.w1 - s2.a2 * s.w2;
        s.z2 = s.z1; s.z1 = y; s.w2 = s.w1; s.w1 = z;
        return z;
    }

    static float lufs (double ms) { return ms > 1e-12 ? (float) (-0.691 + 10.0 * std::log10 (ms)) : -100.0f; }

    void finishBlock() noexcept
    {
        ring[ringPos] = acc / (double) accN;   // 100 ms mean square (sum of channels)
        ringPos = (ringPos + 1) % 30;
        ringCount = std::min (ringCount + 1, 30);
        acc = 0.0; accN = 0;

        auto meanLast = [&] (int n)
        {
            n = std::min (n, ringCount);
            double s = 0.0;
            for (int k = 1; k <= n; ++k) s += ring[(ringPos - k + 30) % 30];
            return n > 0 ? s / n : 0.0;
        };
        const double m400 = meanLast (4);
        momentary = ringCount >= 4 ? lufs (m400) : -100.0f;
        shortTerm = ringCount >= 30 ? lufs (meanLast (30)) : (ringCount >= 4 ? lufs (meanLast (ringCount)) : -100.0f);

        // Integrated: 400 ms blocks every 100 ms, absolute gate -70, relative gate -10 LU
        if (ringCount >= 4)
        {
            const float L = lufs (m400);
            if (L > -70.0f)
            {
                const int bin = std::clamp ((int) ((L + 70.0f) * 10.0f), 0, kBins - 1);
                ++histCount[bin]; histSum[bin] += m400;
            }
            double s = 0.0; int64_t n = 0;
            for (int b = 0; b < kBins; ++b) { s += histSum[b]; n += histCount[b]; }
            if (n > 0)
            {
                const float rel = lufs (s / (double) n) - 10.0f;
                const int startBin = std::clamp ((int) std::ceil ((rel + 70.0f) * 10.0f), 0, kBins);
                double s2_ = 0.0; int64_t n2 = 0;
                for (int b = startBin; b < kBins; ++b) { s2_ += histSum[b]; n2 += histCount[b]; }
                integrated = n2 > 0 ? lufs (s2_ / (double) n2) : -100.0f;
            }
        }
    }

    static constexpr int kBins = 1000;   // 0.1 LU bins from -70 to +30 LUFS
    Bq s1 {}, s2 {};
    State st[2];
    double acc = 0.0; int accN = 0, blockLen = 4800;
    double ring[30] {}; int ringPos = 0, ringCount = 0;
    int64_t histCount[kBins] {}; double histSum[kBins] {};
};

} // namespace lumen
