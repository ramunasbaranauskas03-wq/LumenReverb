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


} // namespace lumen
