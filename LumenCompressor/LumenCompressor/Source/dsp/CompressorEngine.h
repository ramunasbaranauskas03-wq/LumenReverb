#pragma once
#include "DelayLine.h"
#include <cmath>
#include <algorithm>

namespace lumen
{

constexpr float kPiC = 3.14159265358979323846f;

// TPT state-variable filter (Simper / Zavalishin)
struct SVFc
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1.41421356f, ic1 = 0, ic2 = 0;

    void set (float fc, float fs, float q = 0.70710678f) noexcept
    {
        fc = std::clamp (fc, 10.0f, 0.45f * fs);
        const float g = std::tan (kPiC * fc / fs);
        k  = 1.0f / q;
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

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

//==============================================================================
// Stereo compressor.
//   Styles: Clean (peak, feed-forward), Opto (RMS, feedback, program-dependent release),
//           FET (peak, feedback, fast, harmonic colour), Vari-Mu (RMS, wide knee, ratio grows with level)
//   Detector: internal or external sidechain, SC high/low-pass, stereo link, Stereo or Mid/Side.
//   Gain: threshold/ratio/knee/range, attack/release (+auto release), lookahead,
//         manual + loudness-matched auto make-up, drive, parallel mix, sidechain listen.
class CompressorEngine
{
public:
    enum Style   { Clean = 0, Opto = 1, FET = 2, VariMu = 3 };
    enum Channel { StereoMode = 0, MidSide = 1 };
    static constexpr float kMaxLookaheadMs = 10.0f;

    struct Params
    {
        float thresholdDb = -18.0f;
        float ratio       = 4.0f;     // >= 20 means infinity (limiter)
        float kneeDb      = 6.0f;
        float attackMs    = 10.0f;
        float releaseMs   = 120.0f;
        bool  autoRelease = false;
        float rangeDb     = 40.0f;
        float lookaheadMs = 0.0f;
        float link        = 1.0f;     // 0..1
        float scHpHz      = 20.0f;    // <= 21 Hz -> off
        float scLpHz      = 20000.0f; // >= 19.9 kHz -> off
        bool  external    = false;
        bool  listen      = false;
        int   style       = Clean;
        int   channel     = StereoMode;
        float drive       = 0.0f;     // 0..1
        float makeupDb    = 0.0f;
        bool  autoGain    = false;
        float mix         = 1.0f;     // 0..1
        float outputDb    = 0.0f;
    };

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        const int maxLa = (int) std::ceil (kMaxLookaheadMs * 0.001f * fs) + 4;
        laA.prepare (maxLa);
        laB.prepare (maxLa);
        rmsCoef     = 1.0f - std::exp (-1.0f / (0.015f * fs));
        pkRel       = std::exp (-1.0f / (0.02f * fs));
        loudCoef    = 1.0f - std::exp (-1.0f / (1.5f * fs));
        smoothCoef  = 1.0f - std::exp (-1.0f / (0.02f * fs));
        slowAtk     = std::exp (-1.0f / (0.4f * fs));
        reset();
        first = true;
    }

    void reset()
    {
        laA.clear(); laB.clear();
        for (auto* f : { &hpA, &hpB, &lpA, &lpB }) f->reset();
        rmsA = rmsB = pkA = pkB = 0.0f;
        envA = envB = slowA = slowB = 0.0f;
        lastGA = lastGB = 1.0f;
        inPow = wetPow = 1.0e-9f;
    }

    int getLatencySamples (float lookaheadMs) const noexcept
    {
        return (int) std::round (std::clamp (lookaheadMs, 0.0f, kMaxLookaheadMs) * 0.001f * fs);
    }

    void setParams (const Params& p) noexcept { params = p; }

    void beginBlock() noexcept
    {
        const auto& p = params;
        style = p.style;

        thr   = p.thresholdDb;
        slope = p.ratio >= 19.99f ? -1.0f : (1.0f / std::max (1.0f, p.ratio) - 1.0f);
        knee  = std::max (0.0f, p.kneeDb);
        if (style == VariMu) knee = std::max (knee, 12.0f);
        range = std::max (0.0f, p.rangeDb);

        float atk = std::max (0.01f, p.attackMs);
        float rel = std::max (1.0f,  p.releaseMs);
        if (style == FET)    atk *= 0.5f;                  // FET: snappier
        if (style == Opto)   atk = std::max (atk, 3.0f);   // Opto: never instant
        aCoef  = std::exp (-1.0f / (atk * 0.001f * fs));
        rCoef  = std::exp (-1.0f / (rel * 0.001f * fs));
        rFast  = std::exp (-1.0f / (rel * 0.35f * 0.001f * fs));
        rSlow  = std::exp (-1.0f / (rel * 2.5f * 0.001f * fs));
        autoRel = p.autoRelease || style == Opto;

        useRms   = (style == Opto || style == VariMu);
        feedback = (style == Opto || style == FET) && ! p.external;
        ffSlope = slope;
        if (feedback)
        {
            // A feedback detector sees the already-compressed signal; rescale so the
            // ratio knob gives the same static curve as feed-forward.
            const float sAbs = -slope;
            slope = -std::min (50.0f, sAbs / std::max (1.0e-3f, 1.0f - sAbs));
        }
        link     = std::clamp (p.link, 0.0f, 1.0f);
        ms       = p.channel == MidSide;
        external = p.external;
        listen   = p.listen;

        hpOn = p.scHpHz > 21.0f;
        lpOn = p.scLpHz < 19900.0f;
        if (hpOn) { hpA.set (p.scHpHz, fs); hpB.set (p.scHpHz, fs); }
        if (lpOn) { lpA.set (p.scLpHz, fs); lpB.set (p.scLpHz, fs); }

        laSamples = getLatencySamples (p.lookaheadMs);

        float drv = std::clamp (p.drive, 0.0f, 1.0f);
        if (style == FET) drv = std::max (drv, 0.12f);     // FET always has a bit of colour
        driveOn  = drv > 0.001f;
        driveK   = 0.5f + 3.5f * drv;        // tanh(K*x)/K : unity small-signal gain, soft ceiling
        driveInv = 1.0f / driveK;

        // Loudness-matched auto make-up (computed from running powers)
        float autoDb = 0.0f;
        if (p.autoGain && inPow > 1.0e-7f)
            autoDb = std::clamp (10.0f * std::log10 (inPow / std::max (wetPow, 1.0e-12f)), 0.0f, 24.0f);
        autoMakeupDb = autoDb;

        makeupTarget = dbToGain (p.makeupDb + autoMakeupDb);
        mixTarget    = std::clamp (p.mix, 0.0f, 1.0f);
        outTarget    = dbToGain (p.outputDb);

        if (first)
        {
            makeupSm = makeupTarget; mixSm = mixTarget; outSm = outTarget;
            first = false;
        }
    }

    // Returns the current gain reduction in dB (positive number) via grOut.
    inline void processSample (float inL, float inR, float scL, float scR,
                               float& outL, float& outR, float& grOut) noexcept
    {
        makeupSm += (makeupTarget - makeupSm) * smoothCoef;
        mixSm    += (mixTarget - mixSm) * smoothCoef;
        outSm    += (outTarget - outSm) * smoothCoef;

        // Encode program
        float a = inL, b = inR;
        if (ms) { a = 0.5f * (inL + inR); b = 0.5f * (inL - inR); }

        // Detector source
        float da, db;
        if (external)
        {
            da = scL; db = scR;
            if (ms) { const float m = 0.5f * (scL + scR), s = 0.5f * (scL - scR); da = m; db = s; }
        }
        else
        {
            da = a; db = b;
        }

        float lo, hi;
        if (hpOn) { hpA.process (da, lo, hi); da = hi; hpB.process (db, lo, hi); db = hi; }
        if (lpOn) { lpA.process (da, lo, hi); da = lo; lpB.process (db, lo, hi); db = lo; }
        const float listenA = da, listenB = db;

        if (feedback) { da *= lastGA; db *= lastGB; }

        // Level detection
        float lvA, lvB;
        if (useRms)
        {
            rmsA += (da * da - rmsA) * rmsCoef;
            rmsB += (db * db - rmsB) * rmsCoef;
            lvA = std::sqrt (2.0f * rmsA);   // sine-calibrated: RMS*sqrt2 == peak for a sine
            lvB = std::sqrt (2.0f * rmsB);
        }
        else
        {
            // Peak detector: instant attack, short release so the level holds across a waveform cycle
            pkA = std::max (std::fabs (da), pkA * pkRel);
            pkB = std::max (std::fabs (db), pkB * pkRel);
            lvA = pkA;
            lvB = pkB;
        }
        const float lmax = std::max (lvA, lvB);
        lvA += (lmax - lvA) * link;
        lvB += (lmax - lvB) * link;

        const float tA = computeGr (gainToDb (lvA));
        const float tB = computeGr (gainToDb (lvB));

        const float grA = ballistics (tA, envA, slowA);
        const float grB = ballistics (tB, envB, slowB);

        const float gA = dbToGain (-grA);
        const float gB = dbToGain (-grB);
        lastGA = gA; lastGB = gB;
        grOut = std::max (grA, grB);

        // Lookahead delay on the program path
        laA.push (a); laB.push (b);
        const float xa = laA.tap (laSamples + 1);
        const float xb = laB.tap (laSamples + 1);

        float ya = xa * gA, yb = xb * gB;

        // Loudness tracking for auto make-up (pre make-up, pre drive)
        inPow  += (xa * xa + xb * xb - inPow)  * loudCoef;
        wetPow += (ya * ya + yb * yb - wetPow) * loudCoef;

        ya *= makeupSm; yb *= makeupSm;

        if (driveOn)
        {
            ya = std::tanh (driveK * ya) * driveInv;
            yb = std::tanh (driveK * yb) * driveInv;
        }

        // Decode
        float wl = ya, wr = yb, dl = xa, dr = xb;
        if (ms) { wl = ya + yb; wr = ya - yb; dl = xa + xb; dr = xa - xb; }

        if (listen)
        {
            float la = listenA, lb = listenB;
            if (ms) { la = listenA + listenB; lb = listenA - listenB; }
            outL = la; outR = lb;
            return;
        }

        outL = (dl + (wl - dl) * mixSm) * outSm;
        outR = (dr + (wr - dr) * mixSm) * outSm;
    }

    float getAutoMakeupDb() const noexcept { return autoMakeupDb; }

    // Static curve for the UI (input dB -> output dB, no make-up)
    float staticCurve (float xDb) const noexcept { return xDb - computeGr (xDb, ffSlope); }

private:
    inline float computeGr (float x) const noexcept { return computeGr (x, slope); }

    inline float computeGr (float x, float slp) const noexcept
    {
        const float over = x - thr;
        float gr;
        if (2.0f * over < -knee)
            gr = 0.0f;
        else if (knee > 0.0f && 2.0f * std::fabs (over) <= knee)
        {
            const float t = over + 0.5f * knee;
            gr = -slp * t * t / (2.0f * knee);
        }
        else
            gr = -slp * over;

        if (style == VariMu) // ratio rises gently with level
            gr *= std::clamp (0.55f + over / 30.0f, 0.55f, 1.0f);

        return std::min (gr, range);
    }

    inline float ballistics (float target, float& env, float& slow) const noexcept
    {
        if (! autoRel)
        {
            env = target > env ? aCoef * env + (1.0f - aCoef) * target
                               : rCoef * env + (1.0f - rCoef) * target;
            return env;
        }
        // Program-dependent: fast stage for transients, slow stage for sustained reduction
        env  = target > env  ? aCoef * env + (1.0f - aCoef) * target
                             : rFast * env + (1.0f - rFast) * target;
        slow = target > slow ? slowAtk * slow + (1.0f - slowAtk) * target
                             : rSlow * slow + (1.0f - rSlow) * target;
        return std::max (env, slow);
    }

    Params params;
    float fs = 48000.0f;
    bool first = true;

    DelayLine laA, laB;
    SVFc hpA, hpB, lpA, lpB;

    int style = Clean;
    float thr = -18.0f, slope = -0.75f, ffSlope = -0.75f, knee = 6.0f, range = 40.0f;
    float aCoef = 0, rCoef = 0, rFast = 0, rSlow = 0, slowAtk = 0;
    bool autoRel = false, useRms = false, feedback = false, ms = false, external = false, listen = false;
    bool hpOn = false, lpOn = false, driveOn = false;
    float link = 1.0f, rmsCoef = 0, loudCoef = 0, smoothCoef = 0;
    int laSamples = 0;
    float driveK = 1.0f, driveInv = 1.0f;

    float pkA = 0, pkB = 0, pkRel = 0.999f;
    float rmsA = 0, rmsB = 0, envA = 0, envB = 0, slowA = 0, slowB = 0, lastGA = 1, lastGB = 1;
    float inPow = 1e-9f, wetPow = 1e-9f, autoMakeupDb = 0.0f;
    float makeupTarget = 1, makeupSm = 1, mixTarget = 1, mixSm = 1, outTarget = 1, outSm = 1;
};

} // namespace lumen
