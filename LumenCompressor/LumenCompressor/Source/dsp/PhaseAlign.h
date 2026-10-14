#pragma once
#include "DelayLine.h"
#include "CompressorEngine.h"   // SVFc
#include <atomic>
#include <vector>
#include <cmath>
#include <algorithm>

namespace lumen
{

//==============================================================================
// IIR Hilbert pair (two allpass chains with ~90 deg difference, +-1 deg from 20 Hz at 44.1/48 kHz).
// Lets us rotate phase by any angle at all frequencies: y = I*cos(t) + Q*sin(t).
class HilbertPair
{
public:
    HilbertPair()
    {
        static constexpr double c1[4] = { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
        static constexpr double c2[4] = { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };
        for (int i = 0; i < 4; ++i) { a[i].a2 = (float) (c1[i] * c1[i]); b[i].a2 = (float) (c2[i] * c2[i]); }
    }

    void reset() noexcept { for (auto& s : a) s.reset(); for (auto& s : b) s.reset(); z = 0.0f; }

    inline void process (float x, float& i, float& q) noexcept
    {
        float ya = x; for (auto& s : a) ya = s.process (ya);
        float yb = x; for (auto& s : b) yb = s.process (yb);
        i = z; z = ya;   // one-sample delay on the first path
        q = yb;
    }

private:
    struct Sec
    {
        float a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        inline float process (float x) noexcept
        {
            const float y = a2 * (x + y2) - x2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
        void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }
    };
    Sec a[4], b[4];
    float z = 0.0f;
};

//==============================================================================
// Kick/bass phase alignment. The bass (main input) is phase-rotated and delayed so its
// low end lines up with the kick (sidechain). Analysis runs on a short capture.
class PhaseAligner
{
public:
    static constexpr float kMaxDelayMs   = 10.0f;
    static constexpr int   kDecim        = 4;
    static constexpr float kCaptureSecs  = 4.0f;

    struct Result
    {
        bool  valid = false;
        float rotationDeg = 0.0f, delayMs = 0.0f;
        float corrBefore = 0.0f, corrAfter = 0.0f;
    };

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        const int maxD = (int) std::ceil (kMaxDelayMs * 0.001f * fs) + 8;
        dL.prepare (maxD); dR.prepare (maxD);
        hL.reset(); hR.reset();
        corrCoef   = 1.0f - std::exp (-1.0f / (0.4f * fs));
        smoothCoef = 1.0f - std::exp (-1.0f / (0.05f * fs));
        capLen = (int) (kCaptureSecs * fs / (float) kDecim);
        capK.assign ((size_t) capLen, 0.0f); capX.assign ((size_t) capLen, 0.0f);
        capI.assign ((size_t) capLen, 0.0f); capQ.assign ((size_t) capLen, 0.0f);
        capPos = 0; decimCount = 0;
        eKK = eXX = eXK = eYY = eYK = 0.0f;
        first = true;
        setFocus (focusHz);
    }

    void setFocus (float hz)
    {
        focusHz = std::clamp (hz, 40.0f, 400.0f);
        for (auto* f : { &lpK, &lpX, &lpI, &lpQ, &lpY }) f->set (focusHz, fs, 0.6f);
    }

    void setParams (bool enabled, float rotationDeg, float delayMs) noexcept
    {
        onTarget = enabled ? 1.0f : 0.0f;
        const float t = rotationDeg * 3.14159265f / 180.0f;
        cosT = std::cos (t); sinT = std::sin (t);
        delayTarget = std::clamp (delayMs, 0.0f, kMaxDelayMs) * 0.001f * fs;
        if (first) { onSm = onTarget; cosSm = cosT; sinSm = sinT; delaySm = delayTarget; first = false; }
    }

    // l/r: bass (in/out). kick: sidechain mono, kickPresent: sidechain is connected.
    inline void processSample (float& l, float& r, float kick, bool kickPresent) noexcept
    {
        onSm    += (onTarget - onSm) * smoothCoef;
        cosSm   += (cosT - cosSm) * smoothCoef;
        sinSm   += (sinT - sinSm) * smoothCoef;
        delaySm += (delayTarget - delaySm) * smoothCoef;

        float iL, qL, iR, qR;
        hL.process (l, iL, qL);
        hR.process (r, iR, qR);

        const float yl0 = iL * cosSm + qL * sinSm;
        const float yr0 = iR * cosSm + qR * sinSm;
        dL.push (yl0); dR.push (yr0);
        const float yl = dL.readLinear (1.0f + delaySm);
        const float yr = dR.readLinear (1.0f + delaySm);

        const float xm = 0.5f * (l + r);
        const float ym = 0.5f * (yl + yr);

        // Live low-band correlation meters
        if (kickPresent)
        {
            float lo, hi;
            lpK.process (kick, lo, hi); const float k = lo;
            lpX.process (xm, lo, hi);   const float x = lo;
            lpY.process (onSm > 0.5f ? ym : xm, lo, hi); const float y = lo;

            eKK += (k * k - eKK) * corrCoef;
            eXX += (x * x - eXX) * corrCoef;
            eXK += (x * k - eXK) * corrCoef;
            eYY += (y * y - eYY) * corrCoef;
            eYK += (y * k - eYK) * corrCoef;

            // Capture for analysis (decimated low band)
            if (capturing.load (std::memory_order_relaxed))
            {
                lpI.process (0.5f * (iL + iR), lo, hi); const float ci = lo;
                lpQ.process (0.5f * (qL + qR), lo, hi); const float cq = lo;
                if (++decimCount >= kDecim)
                {
                    decimCount = 0;
                    if (std::fabs (k) + std::fabs (x) > 1.0e-5f || capPos > 0)
                    {
                        capK[(size_t) capPos] = k; capX[(size_t) capPos] = x;
                        capI[(size_t) capPos] = ci; capQ[(size_t) capPos] = cq;
                        ++capPos;
                        progress.store ((float) capPos / (float) capLen, std::memory_order_relaxed);
                        if (capPos >= capLen)
                        {
                            capturing.store (false);
                            captureReady.store (true, std::memory_order_release);
                        }
                    }
                }
            }
        }

        l = l + (yl - l) * onSm;
        r = r + (yr - r) * onSm;
    }

    // Called on the audio thread at block start
    void handleRequests() noexcept
    {
        if (captureRequested.exchange (false))
        {
            capPos = 0; decimCount = 0;
            progress.store (0.0f);
            captureReady.store (false);
            capturing.store (true);
        }
    }

    // UI thread
    void requestCapture() noexcept { captureRequested.store (true); }
    void cancelCapture() noexcept  { capturing.store (false); }
    bool isCapturing() const noexcept { return capturing.load(); }
    float captureProgress() const noexcept { return progress.load (std::memory_order_relaxed); }

    float corrBefore() const noexcept { return eXK / std::sqrt (std::max (eXX * eKK, 1.0e-18f)); }
    float corrAfter()  const noexcept { return eYK / std::sqrt (std::max (eYY * eKK, 1.0e-18f)); }
    bool  hasLowEnd()  const noexcept { return eKK > 1.0e-7f && eXX > 1.0e-7f; }

    // UI thread: analyse a finished capture. Searches delay 0..10 ms and the optimal
    // rotation angle for each delay (closed form), picks the best overall.
    bool takeResult (Result& out)
    {
        if (! captureReady.exchange (false, std::memory_order_acquire))
            return false;

        const int n = capLen;
        const float fsD = fs / (float) kDecim;
        const int maxLag = (int) (kMaxDelayMs * 0.001f * fsD);

        double eK = 0, eX = 0, eI = 0, cXK = 0;
        for (int i = 0; i < n; ++i)
        {
            eK += (double) capK[(size_t) i] * capK[(size_t) i];
            eX += (double) capX[(size_t) i] * capX[(size_t) i];
            eI += (double) capI[(size_t) i] * capI[(size_t) i];
            cXK += (double) capX[(size_t) i] * capK[(size_t) i];
        }
        out = {};
        if (eK < 1.0e-6 || eX < 1.0e-6)
            return true; // not enough low end in one of the signals -> invalid result

        double best = -1.0; int bestLag = 0; double bestT = 0.0;
        for (int lag = 0; lag <= maxLag; ++lag)
        {
            double ca = 0, cb = 0;
            for (int i = lag; i < n; ++i)
            {
                ca += (double) capI[(size_t) (i - lag)] * capK[(size_t) i];
                cb += (double) capQ[(size_t) (i - lag)] * capK[(size_t) i];
            }
            const double mag = std::sqrt (ca * ca + cb * cb) * (double) n / (double) (n - lag);
            // Small preference for shorter delays so equal-quality results don't drift
            const double score = mag * (1.0 - 0.02 * lag / std::max (1, maxLag));
            if (score > best) { best = score; bestLag = lag; bestT = std::atan2 (cb, ca); }
        }

        double deg = bestT * 180.0 / 3.14159265358979;
        if (deg < 0.0) deg += 360.0;

        out.valid = true;
        out.rotationDeg = (float) deg;
        out.delayMs = (float) bestLag / fsD * 1000.0f;
        out.corrBefore = (float) (cXK / std::sqrt (eX * eK));
        out.corrAfter  = (float) std::min (1.0, best / std::sqrt (eI * eK));
        return true;
    }

private:
    float fs = 48000.0f, focusHz = 150.0f;
    bool first = true;
    HilbertPair hL, hR;
    DelayLine dL, dR;
    SVFc lpK, lpX, lpI, lpQ, lpY;

    float onTarget = 0, onSm = 0, cosT = 1, sinT = 0, cosSm = 1, sinSm = 0, delayTarget = 0, delaySm = 0;
    float smoothCoef = 0.001f, corrCoef = 0.0001f;
    float eKK = 0, eXX = 0, eXK = 0, eYY = 0, eYK = 0;

    std::vector<float> capK, capX, capI, capQ;
    int capLen = 0, capPos = 0, decimCount = 0;
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> captureRequested { false }, capturing { false }, captureReady { false };
};

} // namespace lumen
