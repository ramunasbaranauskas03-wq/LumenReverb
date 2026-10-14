#pragma once
#include <vector>
#include <algorithm>

namespace lumen
{

// Power-of-two circular delay line with linear and cubic (Hermite) fractional reads.
// tap(1) is the most recently pushed sample.
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 16;
        while (size < maxDelaySamples + 8)
            size <<= 1;

        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void clear() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    inline void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    inline float tap (int k) const noexcept
    {
        return buffer[(size_t) ((writePos - k) & mask)];
    }

    inline float readLinear (float d) const noexcept
    {
        d = std::clamp (d, 1.0f, (float) (mask - 4));
        const int i = (int) d;
        const float f = d - (float) i;
        const float a = tap (i);
        const float b = tap (i + 1);
        return a + f * (b - a);
    }

    inline float readCubic (float d) const noexcept
    {
        d = std::clamp (d, 2.0f, (float) (mask - 4));
        const int i = (int) d;
        const float f = d - (float) i;

        const float xm1 = tap (i - 1);
        const float x0  = tap (i);
        const float x1  = tap (i + 1);
        const float x2  = tap (i + 2);

        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

private:
    std::vector<float> buffer;
    int mask = 0;
    int writePos = 0;
};

} // namespace lumen
