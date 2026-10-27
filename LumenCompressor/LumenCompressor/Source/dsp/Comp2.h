#pragma once
// Lumen Compressor 2 — engine.
//
//   detect()      base rate : lookahead delay, side-chain EQ / tempo trigger, level detector,
//                             gain computer (10 styles), attack / hold / release smoothing, linking.
//                             Smash (3-band up+down) is rendered here directly.
//   shapeSample() OS rate   : applies the (interpolated) gain in the channel domain (L/R or M/S)
//                             and the character saturation, alias-free.
//   finish()      base rate : dry/wet, delta, auto gain, makeup, output, meters, history.
#include <cmath>
#include <algorithm>
#include <array>
#include <vector>
#include <atomic>

namespace lumen::comp2
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToGain (float db) noexcept { return std::exp (db * 0.11512925465f); }
inline float gainToDb (float g) noexcept  { return 8.68588963807f * std::log (std::max (g, 1.0e-9f)); }
inline float msCoef (float ms, double fs) noexcept { return 1.0f / (1.0f + std::max (ms, 0.001f) * 0.001f * (float) fs); }   // one-pole step

// ---------------------------------------------------------------------------- TPT SVF
struct Svf
{
    double a1 = 0, a2 = 0, a3 = 0, k = 1.41421356237, ic1 = 0, ic2 = 0;
    void set (double fc, double fs, double q = 0.70710678118)
    {
        const double g = std::tan (kPi * std::clamp (fc, 5.0, 0.48 * fs) / fs);
        k = 1.0 / q; a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    inline void tick (double v0, double& lp, double& bp, double& hp) noexcept
    {
        const double v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
    inline double lp (double x) noexcept { double l, b, h; tick (x, l, b, h); return l; }
    inline double hp (double x) noexcept { double l, b, h; tick (x, l, b, h); return h; }
};

// RBJ peaking bell
struct Bell
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set (double f, double gainDb, double q, double fs)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * std::clamp (f, 10.0, 0.45 * fs) / fs, al = std::sin (w) / (2 * q), c = std::cos (w);
        const double a0 = 1 + al / A;
        b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0; a1 = -2 * c / a0; a2 = (1 - al / A) / a0;
    }
    void reset() { z1 = z2 = 0; }
    inline double process (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// ---------------------------------------------------------------------------- enums & styles
enum Style    { Clean, Glue, Punch, Opto, VariMu, Vocal, Master, Pump, Upward, Smash, kNumStyles };
enum ChanMode { Stereo, MidSide, MidOnly, SideOnly, LeftOnly, RightOnly };
enum ScSource { Internal, External, Tempo };
enum CharType { CharOff, Tube, Tape, Transformer, Diode, Bright, kNumChar };
enum RelMode  { RelFixed, RelAuto, RelOpto, RelPump };

struct StyleDef { float rmsBlend, rmsMs, feedback, kneeMin; int rel; bool varimu; };
inline const StyleDef& styleDef (int s)
{
    static const StyleDef d[kNumStyles] {
        { 0.00f,  1.0f, 0.00f, 0.0f, RelFixed, false },   // Clean   : peak, feed-forward, exact
        { 0.50f, 10.0f, 0.00f, 2.0f, RelAuto,  false },   // Glue    : SSL-style bus VCA, program-dependent release
        { 0.00f,  1.0f, 0.55f, 0.0f, RelFixed, false },   // Punch   : FET, feedback, fast & aggressive
        { 0.70f, 20.0f, 0.65f, 6.0f, RelOpto,  false },   // Opto    : LA-2A style two-stage release with memory
        { 0.65f, 25.0f, 0.00f, 8.0f, RelAuto,  true  },   // Vari-Mu : ratio rises with level
        { 0.40f,  5.0f, 0.20f, 3.0f, RelOpto,  false },   // Vocal   : smooth, fast, even
        { 0.75f, 30.0f, 0.00f, 4.0f, RelAuto,  false },   // Master  : transparent RMS, gentle
        { 0.00f,  1.0f, 0.00f, 0.0f, RelPump,  false },   // Pump    : side-chain pumping, linear-gain release
        { 0.60f, 10.0f, 0.00f, 6.0f, RelFixed, false },   // Upward  : lifts the quiet parts
        { 0.50f,  5.0f, 0.00f, 6.0f, RelFixed, false },   // Smash   : 3-band upward + downward (OTT-like)
    };
    return d[std::clamp (s, 0, kNumStyles - 1)];
}

struct Params
{
    int style = Clean;
    float threshold = -18, ratio = 4, knee = 6, attack = 10, release = 120, hold = 0, range = 40, lookahead = 0;
    bool autoRelease = false, autoThreshold = false;
    int chanMode = Stereo; float link = 1;
    int scSource = Internal; float scHp = 10, scLp = 20000, scBellF = 1000, scBellG = 0; bool scListen = false;
    float divisionQ = 1, offset = 0;      // tempo trigger: division in quarter notes, offset in fractions of it
    int character = CharOff; float drive = 0; bool charPost = false;
    float makeupDb = 0, mix = 1, outDb = 0; bool autoGain = false, delta = false;
};

struct Transport { bool valid = false, playing = false; double ppq = 0, bpm = 120; };

// ---------------------------------------------------------------------------- side-chain EQ
struct ScFilter
{
    Svf hp[2], lp[2]; Bell bell; bool hpOn = false, lpOn = false, bellOn = false;
    void set (const Params& p, double fs)
    {
        hpOn = p.scHp > 12.0f; lpOn = p.scLp < 19000.0f && p.scLp < 0.45 * fs; bellOn = std::abs (p.scBellG) > 0.05f;
        for (auto& f : hp) f.set (p.scHp, fs);
        for (auto& f : lp) f.set (p.scLp, fs);
        bell.set (p.scBellF, p.scBellG, 1.0, fs);
    }
    void reset() { for (auto& f : hp) f.reset(); for (auto& f : lp) f.reset(); bell.reset(); }
    inline float process (float x) noexcept
    {
        double y = x;
        if (hpOn) { y = hp[0].hp (y); y = hp[1].hp (y); }
        if (lpOn) { y = lp[0].lp (y); y = lp[1].lp (y); }
        if (bellOn) y = bell.process (y);
        return (float) y;
    }
};

// ---------------------------------------------------------------------------- gain computer
// level L and threshold T in dB; s = 1 - 1/ratio; W = knee width (dB). Returns gain change in dB.
inline float downwardGain (float L, float T, float s, float W, bool varimu) noexcept
{
    const float over = L - T;
    if (varimu)   // ratio grows with how far over the threshold the signal is
    {
        const float k = std::clamp ((over + W * 0.5f) / (W * 0.5f + 14.0f), 0.12f, 1.0f);
        s *= k;
    }
    if (2.0f * over <= -W) return 0.0f;
    if (W > 0.01f && 2.0f * std::abs (over) < W) { const float t = over + W * 0.5f; return -s * t * t / (2.0f * W); }
    return -s * over;
}
inline float upwardGain (float L, float T, float s, float W) noexcept
{
    if (L < -72.0f) return 0.0f;                            // never lift the noise floor
    const float under = T - L;
    float g;
    if (2.0f * under <= -W) g = 0.0f;
    else if (W > 0.01f && 2.0f * std::abs (under) < W) { const float t = under + W * 0.5f; g = s * t * t / (2.0f * W); }
    else g = s * under;
    return g * std::clamp ((L + 72.0f) / 12.0f, 0.0f, 1.0f);   // fade the lift out near the floor
}

// ---------------------------------------------------------------------------- character (runs oversampled)
class Character
{
public:
    void prepare (double fsIn)
    {
        fs = fsIn;
        lpC = 1.0f - std::exp (-2.0f * (float) kPi * 140.0f / (float) fs);
        hpC = 1.0f - std::exp (-2.0f * (float) kPi * 3200.0f / (float) fs);
        tapeC = 1.0f - std::exp (-2.0f * (float) kPi * 15000.0f / (float) fs);
        dcC = 1.0f - std::exp (-2.0f * (float) kPi * 7.0f / (float) fs);
        levC = 1.0f - std::exp (-1.0f / (float) (0.3 * fs));
        smC = 1.0f - std::exp (-1.0f / (float) (0.03 * fs));
        reset();
    }
    void reset() { for (int c = 0; c < 2; ++c) { lo[c] = hiLp[c] = tape[c] = dc[c] = 0; } pIn = pOut = 1.0e-6f; lev = 1; amt = 0; driveSm = 0; }
    void set (int t, float drive01) { type = t; drive = drive01; }

    inline void process (float& l, float& r) noexcept
    {
        const float target = type == CharOff ? 0.0f : std::min (1.0f, drive * 25.0f);
        amt += (target - amt) * smC;
        driveSm += (drive - driveSm) * smC;
        if (amt < 1.0e-4f) return;
        const float g = dbToGain (driveSm * 24.0f);
        float x[2] = { l, r }, y[2];
        for (int c = 0; c < 2; ++c) y[c] = shape (c, x[c] * g);
        pIn  += (x[0] * x[0] + x[1] * x[1] - pIn) * levC;
        pOut += (y[0] * y[0] + y[1] * y[1] - pOut) * levC;
        if (pIn > 1.0e-10f) lev += (std::clamp (std::sqrt (pIn / std::max (pOut, 1.0e-12f)), 0.01f, 4.0f) - lev) * levC;
        for (int c = 0; c < 2; ++c)
        {
            float v = y[c] * lev;
            dc[c] += (v - dc[c]) * dcC; v -= dc[c];
            x[c] += (v - x[c]) * amt;
        }
        l = x[0]; r = x[1];
    }

private:
    inline float shape (int c, float x) noexcept
    {
        switch (type)
        {
            case Tube:  { constexpr float b = 0.22f, tb = 0.21651806f; return std::tanh (x + b) - tb; }
            case Tape:  { tape[c] += (x - tape[c]) * tapeC; return 0.63661977f * std::atan (tape[c] * 1.5707963f); }
            case Transformer:
            {
                lo[c] += (x - lo[c]) * lpC;                     // the low end saturates harder (iron core)
                const float l2 = lo[c] * 1.8f, lsat = (std::tanh (l2 + 0.15f) - 0.14888503f) / 1.8f;
                return std::tanh ((x - lo[c]) + lsat);
            }
            case Diode: return x > 0 ? (1.0f - std::exp (-x)) : -(1.0f - std::exp (x * 1.6f)) / 1.6f;
            case Bright:
            {
                hiLp[c] += (x - hiLp[c]) * hpC;
                const float hi = x - hiLp[c];
                return std::tanh (x) + 0.35f * std::tanh (2.5f * hi);
            }
            default: return x;
        }
    }
    double fs = 48000; int type = CharOff; float drive = 0;
    float lpC = 0, hpC = 0, tapeC = 0, dcC = 0, levC = 0, smC = 0, pIn = 0, pOut = 0, lev = 1, amt = 0, driveSm = 0;
    float lo[2] {}, hiLp[2] {}, tape[2] {}, dc[2] {};
};

// ---------------------------------------------------------------------------- Smash: 3-band upward + downward
class SmashBands
{
public:
    void prepare (double fsIn)
    {
        fs = fsIn;
        for (int c = 0; c < 2; ++c) { a[c].set (120, fs); b[c].set (2500, fs); ap[c].set (2500, fs); }
        for (int c = 0; c < 2; ++c) { a[c].reset(); b[c].reset(); ap[c].reset(); }
        for (auto& e : env) e = 0;
        for (auto& g : gDb) g = 0;
    }
    struct LR4
    {
        Svf s1, sl, sh;
        void set (double f, double rate) { s1.set (f, rate); sl.set (f, rate); sh.set (f, rate); }
        void reset() { s1.reset(); sl.reset(); sh.reset(); }
        inline void split (double x, double& lo, double& hi) noexcept { double l, bb, h; s1.tick (x, l, bb, h); lo = sl.lp (l); hi = sh.hp (h); }
    };
    // returns the most negative / positive band gain for metering
    inline void process (float& l, float& r, const Params& p, float aC, float rC, float& grOut) noexcept
    {
        float band[3][2];
        const float in[2] = { l, r };
        for (int c = 0; c < 2; ++c)
        {
            double l1, h1, m2, t2;
            a[c].split (in[c], l1, h1); b[c].split (h1, m2, t2);
            band[0][c] = (float) ap[c].ap (l1); band[1][c] = (float) m2; band[2][c] = (float) t2;
        }
        const float s = 1.0f - 1.0f / std::max (1.0f, p.ratio);
        static constexpr float bandTrim[3] { -2.0f, 0.0f, -4.0f };   // per-band threshold trim (like OTT's tilt)
        l = r = 0; grOut = 0;
        for (int k = 0; k < 3; ++k)
        {
            const float pk = std::max (std::fabs (band[k][0]), std::fabs (band[k][1]));
            env[k] += (pk * pk - env[k]) * envC;
            const float L = gainToDb (std::sqrt (2.0f * env[k]) * 0.5f + pk * 0.5f);
            const float T = p.threshold + bandTrim[k];
            float t = downwardGain (L, T, s, 6.0f, false) + upwardGain (L, T - 8.0f, s, 6.0f);
            t = std::clamp (t, -p.range, p.range);
            gDb[k] += (t - gDb[k]) * (t < gDb[k] ? aC : rC);
            const float g = dbToGain (gDb[k]);
            l += band[k][0] * g; r += band[k][1] * g;
            if (std::abs (gDb[k]) > std::abs (grOut)) grOut = gDb[k];
        }
    }
    void setEnvTime (float ms) { envC = msCoef (ms, fs); }
private:
    double fs = 48000;
    LR4 a[2], b[2];
    struct AP { Svf s; void set (double f, double rate) { s.set (f, rate); } void reset() { s.reset(); } inline double ap (double x) noexcept { double l, bb, h; s.tick (x, l, bb, h); return x - 2.0 * s.k * bb; } } ap[2];
    float env[3] {}, gDb[3] {}, envC = 0.01f;
};

// ---------------------------------------------------------------------------- the engine
class Engine
{
public:
    static constexpr int kHist = 2048, kHistChunk = 128;
    struct HistPoint { float in = -120, out = -120, gr = 0; };
    HistPoint hist[kHist];
    std::atomic<int> histPos { 0 };
    std::atomic<float> grNow { 0 }, grMax { 0 }, inPk { 0 }, outPk { 0 }, autoGainDb { 0 }, detNow { -120 }, thrEff { -18 };

    void prepare (double sampleRate, int maxBlockIn)
    {
        fs = sampleRate; maxBlock = maxBlockIn;
        for (auto* v : { &gArr[0], &gArr[1], &dry[0], &dry[1], &inAbs, &grArr }) v->assign ((size_t) maxBlock, 0.0f);
        laLen = (int) (0.021 * fs) + 4; for (auto& d : laBuf) d.assign ((size_t) laLen, 0.0f); laPos = 0;
        for (auto& f : sc) f.reset();
        smash.prepare (fs);
        for (int c = 0; c < 2; ++c) { y[c] = 0; rms[c] = 0; pkEnv[c] = 0; sus[c] = 0; mem[c] = 0; yPk[c] = 0; holdCnt[c] = 0; gLin[c] = 1; }
        loud = 0.0f; autoOff = 0; agDb = 0; pDry = pWet = 1.0e-8f; gPrevApplied[0] = gPrevApplied[1] = 1;
        smMix = 1; smMake = 1; smOut = 1; smAg = 1;
        histAcc = HistPoint {}; histCount = 0; firstParams = true;
        tempoFree = 0;
    }
    void prepareOs (double osRate) { character.prepare (osRate); }
    const float* dryData (int c) const { return dry[(size_t) c].data(); }
    int lookaheadSamples() const { return laSamples; }

    void setParams (const Params& np)
    {
        auto diff = [] (float u, float w) { return std::abs (u - w) > 1.0e-6f; };
        const bool scChanged = firstParams || diff (np.scHp, p.scHp) || diff (np.scLp, p.scLp) || diff (np.scBellF, p.scBellF) || diff (np.scBellG, p.scBellG);
        p = np;
        if (scChanged) for (auto& f : sc) f.set (p, fs);
        laSamples = std::clamp ((int) std::lround (p.lookahead * 0.001 * fs), 0, laLen - 2);
        const StyleDef& sd = styleDef (p.style);
        aC = msCoef (std::max (0.01f, p.attack), fs);
        rC = msCoef (std::max (1.0f, p.release), fs);
        rmsC = msCoef (sd.rmsMs, fs);
        pkDecay = std::exp (-1.0f / (std::clamp (p.release * 0.2f, 1.0f, 20.0f) * 0.001f * (float) fs));
        pkRelax = msCoef (1500.0f, fs);
        smash.setEnvTime (5.0f);
        character.set (p.character, p.drive);
        if (firstParams) { smMix = p.mix; smMake = dbToGain (p.makeupDb); smOut = dbToGain (p.outDb); firstParams = false; }
    }

    //------------------------------------------------------------------------------------------ stage 1
    // L/R: main input (in place -> delayed or smashed signal, in the L/R domain). sc: external side-chain or nullptr.
    void detect (float* L, float* R, const float* scL, const float* scR, int n, const Transport& tp) noexcept
    {
        const StyleDef& sd = styleDef (p.style);
        const bool ms = p.chanMode == MidSide || p.chanMode == MidOnly || p.chanMode == SideOnly;
        const float W = std::max (p.knee, sd.kneeMin);
        float s = 1.0f - 1.0f / std::max (1.0f, p.ratio);
        const float fb = sd.feedback;
        if (fb > 0 && p.style != Upward) s = std::min (20.0f, s / std::max (0.05f, 1.0f - s * fb));   // keeps the static curve exact with feedback
        const int holdN = (int) (p.hold * 0.001f * (float) fs);
        const float susC = msCoef (std::max (20.0f, p.release * 2.0f), fs), memC = msCoef (1500.0f, fs), loudC = msCoef (2000.0f, fs);
        const bool useAutoRel = p.autoRelease || sd.rel == RelAuto;
        // tempo trigger
        const double qPerSample = tp.bpm / 60.0 / fs;
        double pos = tp.valid && tp.playing ? tp.ppq : tempoFree;
        const double pulseQ = 0.008 * tp.bpm / 60.0;   // 8 ms pulse

        for (int i = 0; i < n; ++i)
        {
            const float xl = L[i], xr = R[i];
            inAbs[(size_t) i] = std::max (std::fabs (xl), std::fabs (xr));

            // ---- detection signal in the channel domain
            float d[2];
            if (p.scSource == Tempo)
            {
                double ph = pos / std::max (0.05, (double) p.divisionQ) - p.offset;
                ph -= std::floor (ph);
                const float v = ph * p.divisionQ < pulseQ ? 1.0f : 0.0f;
                d[0] = d[1] = v;
                pos += qPerSample;
            }
            else
            {
                float a = xl, b = xr;
                if (p.scSource == External && scL != nullptr) { a = scL[i]; b = scR != nullptr ? scR[i] : scL[i]; }
                if (ms) { const float m = 0.5f * (a + b), sd2 = 0.5f * (a - b); a = m; b = sd2; }
                d[0] = sc[0].process (a); d[1] = sc[1].process (b);
            }

            // ---- lookahead delay of the main signal
            laBuf[0][(size_t) laPos] = xl; laBuf[1][(size_t) laPos] = xr;
            int rp = laPos - laSamples; if (rp < 0) rp += laLen;
            float ol = laBuf[0][(size_t) rp], orr = laBuf[1][(size_t) rp];
            if (++laPos >= laLen) laPos = 0;

            if (p.scListen)
            {
                float a = d[0], b = d[1];
                if (ms) { const float l2 = a + b, r2 = a - b; a = l2; b = r2; }
                L[i] = a; R[i] = b; dry[0][(size_t) i] = ol; dry[1][(size_t) i] = orr;
                gArr[0][(size_t) i] = gArr[1][(size_t) i] = 1.0f; grArr[(size_t) i] = 0;
                continue;
            }

            // ---- auto threshold: follow the programme loudness (2 s), referenced to -18 dB
            {
                const float m2 = 0.5f * (d[0] * d[0] + d[1] * d[1]);
                if (m2 > 1.0e-7f) loud += (m2 - loud) * loudC;
                autoOff = p.autoThreshold && p.scSource != Tempo ? std::clamp (gainToDb (std::sqrt (2.0f * loud)) + 18.0f, -30.0f, 24.0f) : 0.0f;
            }
            const float T = p.threshold + autoOff;

            dry[0][(size_t) i] = ol; dry[1][(size_t) i] = orr;

            if (p.style == Smash)
            {
                float gr;
                smash.process (ol, orr, p, aC, rC, gr);
                L[i] = ol; R[i] = orr;
                gArr[0][(size_t) i] = gArr[1][(size_t) i] = 1.0f; grArr[(size_t) i] = gr;
                y[0] = y[1] = gr;
                continue;
            }

            // ---- level + gain computer per channel
            float t[2];
            for (int c = 0; c < 2; ++c)
            {
                rms[c] += (d[c] * d[c] - rms[c]) * rmsC;
                // peak part: instant rise, short decay that bridges the gaps between waveform peaks
                const float ad = std::fabs (d[c]);
                pkEnv[c] = ad > pkEnv[c] ? ad : pkEnv[c] * pkDecay;
                const float lin = (1.0f - sd.rmsBlend) * pkEnv[c] + sd.rmsBlend * std::sqrt (2.0f * rms[c]);
                const float Lv = gainToDb (lin) + fb * y[c];
                if (p.style == Upward) t[c] = std::min (upwardGain (gainToDb (lin), T, s, W), p.range);
                else t[c] = std::max (downwardGain (Lv, T, s, W, sd.varimu), -p.range);
            }
            if (p.chanMode == MidOnly || p.chanMode == LeftOnly)  t[1] = 0;
            if (p.chanMode == SideOnly || p.chanMode == RightOnly) t[0] = 0;
            if (p.link > 0 && (p.chanMode == Stereo || p.chanMode == MidSide))
            {
                const float ext = std::abs (t[0]) > std::abs (t[1]) ? t[0] : t[1];
                for (auto& v : t) v += (ext - v) * p.link;
            }
            detNow.store (gainToDb (std::max (std::max (pkEnv[0], pkEnv[1]), std::sqrt (2.0f * std::max (rms[0], rms[1])))), std::memory_order_relaxed);

            // ---- smoothing: attack / hold / release (dB domain; Pump releases in the linear domain)
            float grMin = 0;
            for (int c = 0; c < 2; ++c)
            {
                float& yy = y[c];
                if (t[c] < yy)
                {
                    yy += (t[c] - yy) * aC; holdCnt[c] = holdN;
                    yPk[c] = std::min (yPk[c], yy);
                }
                else if (holdCnt[c] > 0) --holdCnt[c];
                else
                {
                    float rc = rC;
                    if (sd.rel == RelOpto)
                    {
                        // fast first half (~60 ms), then a slow tail that gets longer the longer it was compressing
                        const float frac = yPk[c] < -0.1f ? std::clamp (yy / yPk[c], 0.0f, 1.0f) : 0.0f;
                        const float tMs = frac > 0.5f ? 60.0f : p.release * (0.5f + 4.0f * mem[c]);
                        rc = msCoef (tMs, fs);
                    }
                    else if (useAutoRel)
                    {
                        // transient reduction recovers fast, sustained reduction slowly
                        const float q = yy < -0.05f ? std::clamp (sus[c] / yy, 0.0f, 1.0f) : 1.0f;
                        const float fast = std::max (12.0f, p.release * 0.15f), slow = p.release * 1.6f;
                        rc = msCoef (fast + (slow - fast) * q, fs);
                    }
                    if (sd.rel == RelPump)
                    {
                        float g = dbToGain (yy); const float gt = dbToGain (t[c]);
                        g += (gt - g) * rc; yy = gainToDb (g);
                    }
                    else yy += (t[c] - yy) * rc;
                    yPk[c] += (yy - yPk[c]) * pkRelax;   // peak memory relaxes slowly (1.5 s)
                }
                sus[c] += (yy - sus[c]) * susC;
                mem[c] += ((yy < -1.0f ? 1.0f : 0.0f) - mem[c]) * memC;
                gArr[(size_t) c][(size_t) i] = dbToGain (yy);
                grMin = std::abs (yy) > std::abs (grMin) ? yy : grMin;
            }
            grArr[(size_t) i] = grMin;
            L[i] = ol; R[i] = orr;
        }
        tempoFree = pos;
        thrEff.store (p.threshold + autoOff, std::memory_order_relaxed);
    }

    // per-sample gain for the OS stage: linear interpolation between the base-rate gains
    inline float gainAt (int c, int i, float frac) const noexcept
    {
        const float g1 = gArr[(size_t) c][(size_t) i];
        const float g0 = i > 0 ? gArr[(size_t) c][(size_t) i - 1] : gPrevApplied[c];
        return g0 + (g1 - g0) * frac;
    }
    void endOsBlock (int n) { if (n > 0) { gPrevApplied[0] = gArr[0][(size_t) n - 1]; gPrevApplied[1] = gArr[1][(size_t) n - 1]; } }

    //------------------------------------------------------------------------------------------ stage 2 (oversampled)
    inline void shapeSample (float& l, float& r, float g0, float g1) noexcept
    {
        if (p.scListen) return;
        if (! p.charPost) character.process (l, r);
        const bool ms = p.chanMode == MidSide || p.chanMode == MidOnly || p.chanMode == SideOnly;
        if (ms) { const float m = 0.5f * (l + r) * g0, s = 0.5f * (l - r) * g1; l = m + s; r = m - s; }
        else { l *= g0; r *= g1; }
        if (p.charPost) character.process (l, r);
    }

    //------------------------------------------------------------------------------------------ stage 3
    // DL / DR: the dry signal after the same oversampling round trip as the wet one (phase-identical)
    void finish (float* L, float* R, const float* DL, const float* DR, int n) noexcept
    {
        const float agC = msCoef (1500.0f, fs), smC = msCoef (30.0f, fs);
        const float mkT = dbToGain (p.makeupDb), outT = dbToGain (p.outDb);
        float ip = 0, op = 0, grm = 0;
        for (int i = 0; i < n; ++i)
        {
            const float dl = DL[i], dr = DR[i];

            float wl = L[i], wr = R[i];
            if (p.scListen) { /* hear the detector */ }
            else
            {
                // auto gain: long-term loudness of wet vs dry
                const float pd = dl * dl + dr * dr, pw = wl * wl + wr * wr;
                if (pd > 1.0e-8f) { pDry += (pd - pDry) * agC; pWet += (pw - pWet) * agC; }
                const float agT = p.autoGain ? std::clamp (std::sqrt (pDry / std::max (pWet, 1.0e-12f)), 0.06f, 16.0f) : 1.0f;
                smAg += (agT - smAg) * agC * 4.0f;
                smMake += (mkT - smMake) * smC; smMix += (p.mix - smMix) * smC;
                if (p.delta) { wl = dl - wl; wr = dr - wr; }
                else
                {
                    const float g = smMake * smAg;
                    wl = dl + (wl * g - dl) * smMix; wr = dr + (wr * g - dr) * smMix;
                }
            }
            smOut += (outT - smOut) * smC;
            wl *= smOut; wr *= smOut;
            L[i] = wl; R[i] = wr;

            ip = std::max (ip, inAbs[(size_t) i]); op = std::max (op, std::max (std::fabs (wl), std::fabs (wr)));
            if (std::abs (grArr[(size_t) i]) > std::abs (grm)) grm = grArr[(size_t) i];

            // history (one point per 128 samples)
            histAcc.in = std::max (histAcc.in, gainToDb (inAbs[(size_t) i]));
            histAcc.out = std::max (histAcc.out, gainToDb (std::max (std::fabs (wl), std::fabs (wr))));
            if (std::abs (grArr[(size_t) i]) > std::abs (histAcc.gr)) histAcc.gr = grArr[(size_t) i];
            if (++histCount >= kHistChunk)
            {
                const int hp = histPos.load (std::memory_order_relaxed);
                hist[hp] = histAcc;
                histPos.store ((hp + 1) & (kHist - 1), std::memory_order_release);
                histAcc = HistPoint {}; histCount = 0;
            }
        }
        inPk.store (ip); outPk.store (op); grNow.store (grm);
        grMax.store (std::abs (grm) > std::abs (grMax.load()) ? grm : grMax.load() * 0.995f);
        autoGainDb.store (gainToDb (smAg));
    }
    float currentGrDb (int c) const { return y[c]; }
    const Params& params() const { return p; }

private:
    double fs = 48000; int maxBlock = 512;
    Params p; bool firstParams = true;
    ScFilter sc[2];
    SmashBands smash;
    Character character;
    std::array<std::vector<float>, 2> gArr, dry;
    std::vector<float> inAbs, grArr;
    std::array<std::vector<float>, 2> laBuf;
    int laLen = 0, laPos = 0, laSamples = 0;
    float aC = 0.1f, rC = 0.01f, rmsC = 0.1f;
    float pkEnv[2] {}, pkDecay = 0.99f, pkRelax = 0.0001f;
    float y[2] {}, rms[2] {}, sus[2] {}, mem[2] {}, yPk[2] {}; int holdCnt[2] {};
    float gLin[2] { 1, 1 }, gPrevApplied[2] { 1, 1 };
    float loud = 0, autoOff = 0, agDb = 0, pDry = 0, pWet = 0;
    float smMix = 1, smMake = 1, smOut = 1, smAg = 1;
    double tempoFree = 0;
    HistPoint histAcc; int histCount = 0;
};

} // namespace lumen::comp2
