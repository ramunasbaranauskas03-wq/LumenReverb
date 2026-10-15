#pragma once
#include <cmath>
#include <algorithm>
#include <vector>
#include <atomic>

namespace lumen
{

constexpr float kPiP = 3.14159265358979323846f;

// TPT state-variable filter (Simper / Zavalishin), Butterworth Q by default.
struct SVFp
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1.41421356f, ic1 = 0, ic2 = 0;

    void set (float fc, float fs) noexcept
    {
        fc = std::clamp (fc, 10.0f, 0.45f * fs);
        const float g = std::tan (kPiP * fc / fs);
        k  = 1.41421356f;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline void process (float v0, float& low, float& high) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low  = v2;
        high = v0 - k * v1 - v2;
    }
    void reset() noexcept { ic1 = ic2 = 0.0f; }
};

// Linkwitz-Riley 4th order split (two cascaded Butterworth stages). low + high = allpass.
struct LR4
{
    SVFp lp1, lp2, hp1, hp2;
    void set (float fc, float fs) noexcept { lp1.set (fc, fs); lp2.set (fc, fs); hp1.set (fc, fs); hp2.set (fc, fs); }
    void reset() noexcept { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    inline void process (float x, float& low, float& high) noexcept
    {
        float l, h, t;
        lp1.process (x, l, t);  lp2.process (l, low, t);
        hp1.process (x, t, h);  hp2.process (h, t, high);
    }
};

//==============================================================================
// Three-band phase-coherent split:  [ below low ] [ PUMP BAND ] [ above high ]
// The pump band is ducked by `depth`, the rest by `outside`. Curve gives level 0..1 over one cycle.
class PumpEngine
{
public:
    enum Trigger { Sync = 0, Sidechain = 1, Midi = 2 };
    static constexpr int kLutSize = 512;

    struct Params
    {
        int    trigger    = Sync;
        double cycleBeats = 1.0;     // length of one pump cycle in quarter notes
        double bpm        = 120.0;
        bool   playing    = false;
        double ppq        = 0.0;     // host position at block start (if playing)
        float  lowHz      = 20.0f;
        float  highHz     = 400.0f;
        float  depth      = 1.0f;    // 0..1 inside the band
        float  outside    = 0.0f;    // 0..1 outside the band
        float  offset     = 0.0f;    // -0.5..0.5 of a cycle
        float  scThreshDb = -30.0f;
        bool   solo       = false;
        float  outputDb   = 0.0f;
    };

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        for (auto& c : ch) c = {};
        fastA = std::exp (-1.0f / (0.0005f * fs));
        fastR = std::exp (-1.0f / (0.010f * fs));
        slowR = std::exp (-1.0f / (0.150f * fs));
        gainSmooth = 1.0f - std::exp (-1.0f / (0.0008f * fs));   // anti-click
        envFast = envSlow = 0.0f;
        lockout = 0;
        freePhase = 0.0;
        trigPhase = 1.0;
        gIn = gOut = 1.0f;
        first = true;
        lut.assign (kLutSize + 1, 1.0f);
    }

    void setCurve (const float* table) noexcept   // kLutSize + 1 values
    {
        for (int i = 0; i <= kLutSize; ++i) lut[(size_t) i] = table[i];
    }

    void beginBlock (const Params& p, int numSamples) noexcept
    {
        params = p;
        const float lo = std::clamp (p.lowHz, 10.0f, 0.4f * fs);
        const float hi = std::clamp (std::max (p.highHz, lo * 1.06f), 12.0f, 0.42f * fs);
        if (first) { curLo = lo; curHi = hi; }
        // glide crossover frequencies (log domain) to avoid zipper noise
        const float k = first ? 1.0f : 1.0f - std::exp (-(float) numSamples / (0.03f * fs));
        curLo = std::exp (std::log (curLo) + (std::log (lo) - std::log (curLo)) * k);
        curHi = std::exp (std::log (curHi) + (std::log (hi) - std::log (curHi)) * k);
        for (auto& c : ch) { c.x1.set (curLo, fs); c.x2.set (curHi, fs); c.ap.set (curHi, fs); }

        const double samplesPerBeat = 60.0 / std::max (20.0, p.bpm) * fs;
        cycleSamples = std::max (32.0, p.cycleBeats * samplesPerBeat);
        phaseInc = 1.0 / cycleSamples;
        scThresh = std::pow (10.0f, p.scThreshDb / 20.0f);
        outGain  = std::pow (10.0f, p.outputDb / 20.0f);

        if (p.trigger == Sync)
        {
            if (p.playing)
            {
                double ph = p.ppq / p.cycleBeats - (double) p.offset;
                ph -= std::floor (ph);
                freePhase = ph;
            }
        }
        first = false;
    }

    // Retrigger the curve now (sidechain onset or MIDI note)
    void retrigger() noexcept { trigPhase = 0.0; }

    // Returns current curve value (0..1) via levelOut for metering.
    inline void processSample (float& l, float& r, float scMono, float& levelOut) noexcept
    {
        // ---- Clock -----------------------------------------------------------
        double phase;
        if (params.trigger == Sync)
        {
            phase = freePhase;
            freePhase += phaseInc;
            if (freePhase >= 1.0) freePhase -= 1.0;
        }
        else
        {
            if (params.trigger == Sidechain)
                detectOnset (scMono);
            phase = trigPhase;
            if (trigPhase < 1.0) trigPhase = std::min (1.0, trigPhase + phaseInc);
        }

        const float y = curveAt ((float) phase);
        levelOut = y;
        lastPhase = (float) phase;

        const float tIn  = 1.0f - params.depth   * (1.0f - y);
        const float tOut = 1.0f - params.outside * (1.0f - y);
        gIn  += (tIn  - gIn)  * gainSmooth;
        gOut += (tOut - gOut) * gainSmooth;

        // ---- Split + apply ---------------------------------------------------
        float* io[2] = { &l, &r };
        for (int c = 0; c < 2; ++c)
        {
            auto& s = ch[c];
            float low, rest, mid, high, a1, a2;
            s.x1.process (*io[c], low, rest);
            s.x2.process (rest, mid, high);
            s.ap.process (low, a1, a2);           // keep the low band phase-aligned with mid+high
            low = a1 + a2;

            float out;
            if (params.solo) out = mid * gIn;
            else             out = (low + high) * gOut + mid * gIn;
            *io[c] = out * outGain;
        }
    }

    bool  scActive() const noexcept { return envFast > scThresh; }
    float scEnvelope() const noexcept { return envFast; }
    double getCycleSamples() const noexcept { return cycleSamples; }
    float  getPhase() const noexcept { return lastPhase; }

private:
    inline float curveAt (float ph) const noexcept
    {
        const float x = std::clamp (ph, 0.0f, 1.0f) * (float) kLutSize;
        const int i = std::min ((int) x, kLutSize - 1);
        const float f = x - (float) i;
        return lut[(size_t) i] + f * (lut[(size_t) i + 1] - lut[(size_t) i]);
    }

    inline void detectOnset (float sc) noexcept
    {
        const float a = std::fabs (sc);
        envFast = a > envFast ? fastA * envFast + (1.0f - fastA) * a : fastR * envFast + (1.0f - fastR) * a;
        envSlow = a > envSlow ? a : slowR * envSlow + (1.0f - slowR) * a;
        if (lockout > 0) { --lockout; return; }
        if (envFast > scThresh && envFast > 1.6f * prevSlow)
        {
            trigPhase = 0.0;
            lockout = (int) std::max (0.03 * fs, 0.35 * cycleSamples);
        }
        prevSlow = 0.995f * prevSlow + 0.005f * envSlow;
    }

    struct Channel { LR4 x1, x2, ap; };

    Params params;
    float fs = 48000.0f;
    bool first = true;
    Channel ch[2];
    std::vector<float> lut;

    float curLo = 20.0f, curHi = 400.0f;
    double cycleSamples = 24000.0, phaseInc = 1.0 / 24000.0, freePhase = 0.0, trigPhase = 1.0;
    float gIn = 1.0f, gOut = 1.0f, gainSmooth = 0.01f, outGain = 1.0f;
    float fastA = 0, fastR = 0, slowR = 0, envFast = 0, envSlow = 0, prevSlow = 0, scThresh = 0.03f;
    int lockout = 0;
    float lastPhase = 0.0f;
};

//==============================================================================
// Curve: points (x 0..1, y 0..1) with a global smooth amount -> lookup table.
struct CurvePoint { float x, y; };

inline void renderCurve (const std::vector<CurvePoint>& pts, float smooth, float* out, int size)
{
    for (int i = 0; i <= size; ++i)
    {
        const float x = (float) i / (float) size;
        size_t j = 0;
        while (j + 2 < pts.size() && pts[j + 1].x < x) ++j;
        const auto& a = pts[j];
        const auto& b = pts[std::min (j + 1, pts.size() - 1)];
        const float w = b.x - a.x;
        float t = w > 1.0e-6f ? std::clamp ((x - a.x) / w, 0.0f, 1.0f) : 1.0f;
        const float st = t * t * (3.0f - 2.0f * t);
        t = t + (st - t) * std::clamp (smooth, 0.0f, 1.0f);
        out[i] = std::clamp (a.y + (b.y - a.y) * t, 0.0f, 1.0f);
    }
}

//==============================================================================
// Kick analysis for Smart Assist: onset decay time and low fundamental.
struct KickInfo { bool valid = false; float decayMs = 0.0f, fundamentalHz = 0.0f; int hits = 0; };

inline KickInfo analyseKick (const std::vector<float>& x, float fs)
{
    KickInfo info;
    const int n = (int) x.size();
    if (n < (int) (fs * 0.5f)) return info;

    // envelope
    std::vector<float> env ((size_t) n);
    const float rel = std::exp (-1.0f / (0.005f * fs));
    float e = 0.0f;
    for (int i = 0; i < n; ++i) { const float a = std::fabs (x[(size_t) i]); e = a > e ? a : rel * e + (1.0f - rel) * a; env[(size_t) i] = e; }

    float peakAll = 0.0f;
    for (float v : env) peakAll = std::max (peakAll, v);
    if (peakAll < 1.0e-3f) return info;

    // onsets: rising through 50% of global peak, then find local peak
    double decaySum = 0.0, f0Sum = 0.0; int f0Count = 0;
    int i = 1;
    const int lock = (int) (0.1f * fs);
    while (i < n)
    {
        if (env[(size_t) i] >= 0.5f * peakAll && env[(size_t) i - 1] < 0.5f * peakAll)
        {
            int pk = i;
            for (int k = i; k < std::min (n, i + (int) (0.02f * fs)); ++k) if (env[(size_t) k] > env[(size_t) pk]) pk = k;
            const float target = env[(size_t) pk] * 0.125f; // -18 dB
            int end = pk;
            while (end < n - 1 && env[(size_t) end] > target && end - pk < (int) (1.5f * fs)) ++end;
            if (end < n - 1)
            {
                decaySum += (double) (end - pk) / fs * 1000.0;
                ++info.hits;

                // fundamental: zero crossings over the body (20..120 ms after peak) of a low-passed copy
                const int a = pk + (int) (0.02f * fs), b = std::min (n - 1, pk + (int) (0.12f * fs));
                float lp = 0.0f; const float c = 1.0f - std::exp (-2.0f * kPiP * 250.0f / fs);
                int zc = 0; float prev = 0.0f; int firstZ = -1, lastZ = -1;
                for (int k = pk; k < b; ++k)
                {
                    lp += (x[(size_t) k] - lp) * c;
                    if (k >= a && ((prev < 0.0f && lp >= 0.0f)))
                    {
                        if (firstZ < 0)
                            firstZ = k;
                        lastZ = k;
                        ++zc;
                    }
                    prev = lp;
                }
                if (zc >= 3 && lastZ > firstZ)
                {
                    f0Sum += (double) (zc - 1) * fs / (double) (lastZ - firstZ);
                    ++f0Count;
                }
            }
            i = pk + lock;
        }
        else ++i;
    }

    if (info.hits == 0) return info;
    info.valid = true;
    info.decayMs = (float) (decaySum / info.hits);
    info.fundamentalHz = f0Count > 0 ? (float) (f0Sum / f0Count) : 0.0f;
    return info;
}

} // namespace lumen
