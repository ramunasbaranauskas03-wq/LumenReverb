#pragma once
// Lumen Bass 2 — three-band bass processor (runs at the oversampled rate).
//   SUB  : mono-maker, sub generator (unison / octave down), glue compressor, phone harmonics
//   BODY : drive (6 types, auto-levelled), tone tilt, squash (up+down compression), width
//   AIR  : same as BODY, for the fizz / top of the bass
//   global: kick duck (side-chain), dry/wet, delta, equal-loudness compare, soft clipper
#include <cmath>
#include <algorithm>
#include <array>
#include <vector>

namespace lumen::bass2
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kCtl = 8;          // control-rate divider for gain computers

inline float dbToGain (float db) noexcept { return std::exp (db * 0.11512925465f); }
inline float gainToDb (float g) noexcept  { return 8.68588963807f * std::log (std::max (g, 1.0e-9f)); }

// ---------------------------------------------------------------------------- TPT state-variable filter
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
    inline double ap (double x) noexcept { double l, b, h; tick (x, l, b, h); return x - 2.0 * k * b; }
};

// Linkwitz-Riley 24 dB/oct split: lo + hi = 2nd-order allpass (phase-coherent)
struct LR4
{
    Svf s1, sl, sh;
    void set (double fc, double fs) { s1.set (fc, fs); sl.set (fc, fs); sh.set (fc, fs); }
    void reset() { s1.reset(); sl.reset(); sh.reset(); }
    inline void split (double x, double& lo, double& hi) noexcept
    {
        double l, b, h; s1.tick (x, l, b, h);
        lo = sl.lp (l); hi = sh.hp (h);
    }
};

struct Smooth
{
    float v = 0, c = 0.001f;
    void init (float x, float timeS, double fs) { v = x; c = 1.0f - std::exp (-1.0f / (float) (timeS * fs)); }
    inline float next (float target) noexcept { v += (target - v) * c; return v; }
};

// ---------------------------------------------------------------------------- distortion
enum DriveType { Tube, Tape, Fuzz, Fold, Rectify, Crush, kNumDriveTypes };

struct Shaper
{
    float hold = 0; int holdCnt = 0;
    inline float process (int type, float x, float drive01, int osFactor) noexcept
    {
        switch (type)
        {
            case Tube:    { constexpr float b = 0.35f, tb = 0.33637554f; return std::tanh (x + b) - tb; }          // asymmetric: even harmonics
            case Tape:    return 0.63661977f * std::atan (x * 1.5707963f);
            case Fuzz:    { const float x2 = x * x; const float y = x / std::sqrt (std::sqrt (1.0f + x2 * x2));     // near-hard clip
                            return y > 0 ? y * 0.85f : y; }
            case Fold:    return std::sin (x * 1.5707963f);
            case Rectify: { const float t = std::tanh (x); return 0.45f * t + 0.75f * (std::fabs (t) - 0.5f); }   // adds the octave above
            case Crush:
            {
                const int step = std::max (1, (int) std::lround ((float) osFactor * (1.0f + drive01 * drive01 * 15.0f)));
                if (++holdCnt >= step) { holdCnt = 0; hold = x; }
                const float levels = std::exp2 (12.0f - drive01 * 9.0f);
                return std::tanh (std::round (hold * levels) / levels);
            }
            default: return x;
        }
    }
};

// ---------------------------------------------------------------------------- BODY / AIR band
struct BandParams
{
    bool on = true; int type = Tube;
    float drive = 0.25f, tone = 0.0f, mix = 1.0f, squash = 0.0f, width = 1.0f, levelDb = 0.0f;
};

class BandFx
{
public:
    void prepare (double fsIn, float pivotHz)
    {
        fs = fsIn;
        for (auto& s : sm) s.init (0, 0.03f, fs);
        sm[5].v = 1; sm[6].v = 1;
        pIn = pOut = 1.0e-6f; lev = 1;
        levC = 1.0f - std::exp (-1.0f / (float) (0.25 * fs));
        nC = 1.0f - std::exp (-1.0f / (float) (0.3 * fs)); nIn = 0.0625f; normG = normS = 1;
        envA = std::exp (-1.0f / (float) (0.002 * fs)); envR = std::exp (-1.0f / (float) (0.08 * fs));
        gC = 1.0f - std::exp (-1.0f / (float) (0.0015 * fs));
        dcC = 1.0f - std::exp (-2.0f * (float) kPi * 8.0f / (float) fs);
        for (auto& t : tilt) { t.set (pivotHz, fs, 0.5); t.reset(); }
        env = 0; sqDb = 0; sqG = 1; sqGt = 1; dc[0] = dc[1] = 0; ctr = 0; driveG = 1; gl = gh = gm = 1;
    }
    void setParams (const BandParams& p, int osF) { prm = p; osFactor = osF; levelT = p.on ? dbToGain (p.levelDb) : 1.0f; }

    inline void process (float& l, float& r) noexcept
    {
        const float drive  = sm[1].next (prm.drive);
        const float wetAmt = sm[2].next (prm.on ? prm.mix * std::min (1.0f, prm.drive * 25.0f) : 0.0f);
        const float tone   = sm[3].next (prm.on ? prm.tone : 0.0f);
        const float squash = sm[4].next (prm.on ? prm.squash : 0.0f);
        const float width  = sm[5].next (prm.on ? prm.width : 1.0f);
        const float level  = sm[6].next (levelT);
        const bool ctl = (ctr++ % kCtl) == 0;
        if (ctl)
        {
            driveG = dbToGain (drive * 36.0f);
            gl = dbToGain (-tone * 6.0f); gh = 1.0f / gl; gm = 1.0f;     // sqrt (gl * gh) = 1
        }

        float x[2] = { l, r };
        if (wetAmt > 1.0e-4f)
        {
            // level-independent drive: the band is normalised to about -12 dB RMS first (slow, 300 ms),
            // so DRIVE sounds the same on a quiet or a loud bass
            nIn += (x[0] * x[0] + x[1] * x[1] - nIn) * nC;
            if (ctl) normG = std::clamp (0.25f / std::sqrt (std::max (nIn * 0.5f, 1.0e-12f)), 0.25f, 30.0f);
            normS += (normG - normS) * nC;
            float s[2];
            const float pre = driveG * normS;
            for (int c = 0; c < 2; ++c) s[c] = shp[c].process (prm.type, x[c] * pre, drive, osFactor);
            // auto level: wet RMS follows the clean band RMS (stereo linked, 250 ms)
            pIn  += (x[0] * x[0] + x[1] * x[1] - pIn) * levC;
            pOut += (s[0] * s[0] + s[1] * s[1] - pOut) * levC;
            if (ctl && pIn > 1.0e-10f) levT = std::clamp (std::sqrt (pIn / std::max (pOut, 1.0e-12f)), 1.0e-4f, 4.0f);
            lev += (levT - lev) * levC;
            for (int c = 0; c < 2; ++c)
            {
                float y = s[c] * lev;
                dc[c] += (y - dc[c]) * dcC; y -= dc[c];                 // remove the DC an asymmetric shaper makes
                x[c] += (y - x[c]) * wetAmt;
            }
        }
        if (std::fabs (tone) > 1.0e-4f)
            for (int c = 0; c < 2; ++c) { double lo, bp, hi; tilt[c].tick (x[c], lo, bp, hi); x[c] = (float) (lo * gl + hi * gh + bp * gm * tilt[c].k); }
        if (squash > 1.0e-4f)
        {
            const float a = std::max (std::fabs (x[0]), std::fabs (x[1]));
            env = a > env ? envA * env + (1 - envA) * a : envR * env + (1 - envR) * a;
            if (ctl)
            {
                const float lv = gainToDb (env);
                float g = 0;
                if (lv > -26.0f) g -= (lv + 26.0f) * (1.0f - 1.0f / 5.0f);                       // downward 5:1 above -26 dB
                if (lv < -34.0f && lv > -72.0f) g += std::min (18.0f, (-34.0f - lv) * 0.6f);    // upward below -34 dB
                sqDb = g * squash + squash * 4.0f;                                                // a little makeup
                sqGt = dbToGain (sqDb);
            }
        }
        else { sqGt = 1; sqDb = 0; }
        sqG += (sqGt - sqG) * gC;
        x[0] *= sqG; x[1] *= sqG;

        const float m = 0.5f * (x[0] + x[1]), sd = 0.5f * (x[0] - x[1]) * width;
        l = (m + sd) * level; r = (m - sd) * level;
    }
    float squashDb() const { return gainToDb (sqG); }

private:
    double fs = 48000; int osFactor = 1;
    BandParams prm;
    Smooth sm[7];
    Shaper shp[2];
    Svf tilt[2];
    unsigned ctr = 0;
    float pIn = 0, pOut = 0, lev = 1, levT = 1, levC = 0, env = 0, envA = 0, envR = 0, gC = 0, sqDb = 0, sqG = 1, sqGt = 1, dcC = 0, dc[2] {};
    float driveG = 1, gl = 1, gh = 1, gm = 1, levelT = 1, nIn = 0, nC = 0, normG = 1, normS = 1;
};

// ---------------------------------------------------------------------------- SUB band
struct SubParams
{
    bool on = true, mono = true; int genMode = 0;   // 0 = unison (reinforce), 1 = octave down
    float tight = 0.3f, gen = 0.0f, phone = 0.0f, character = 0.4f, levelDb = 0.0f;
};

class SubFx
{
public:
    void prepare (double fsIn)
    {
        fs = fsIn;
        for (auto& s : sm) s.init (0, 0.03f, fs);
        sm[5].v = 1;
        eA = std::exp (-1.0f / (float) (0.003 * fs)); eR = std::exp (-1.0f / (float) (0.12 * fs));
        gA = 1.0f - std::exp (-(float) kCtl / (float) (0.010 * fs)); gR = 1.0f - std::exp (-(float) kCtl / (float) (0.150 * fs));
        gC = 1.0f - std::exp (-1.0f / (float) (0.002 * fs));
        hA = std::exp (-1.0f / (float) (0.001 * fs)); hR = std::exp (-1.0f / (float) (0.06 * fs));
        genEA = std::exp (-1.0f / (float) (0.004 * fs)); genER = std::exp (-1.0f / (float) (0.08 * fs));
        ampC = 1.0f - std::exp (-1.0f / (float) (0.005 * fs));
        reset();
    }
    void reset()
    {
        env = hEnv = genEnv = 0; grDb = 0; g = gT = 1; schmitt = false; half = false; lastCross = 0; count = 0; period = 0; phase = 0; amp = 0; ctr = 0;
        for (auto& f : hHp) f.reset();
        hLp.reset(); genLp.reset();
    }
    void setParams (const SubParams& p, float xoverHz) { prm = p; xover = xoverHz; levelT = p.on ? dbToGain (p.levelDb) : 1.0f; }
    void updateFilters()   // call every few samples (the crossover can move)
    {
        for (auto& f : hHp) f.set (std::max (60.0, (double) xover * 1.15), fs);
        hLp.set (3200.0, fs);
        const double fc = std::max (30.0, (double) xover * 0.9);
        genLp.set (fc, fs, 0.6);
        // phase lag of that low-pass at the tracked pitch (in cycles) -> the oscillator is advanced by it
        if (period > 0) { const double rr = fs / period / fc; lagCyc = std::atan2 (rr / 0.6, 1.0 - rr * rr) / (2.0 * kPi); }
    }

    // l, r: the low band (in place). harm: phone harmonics generated from it (to be added to the output)
    inline void process (float& l, float& r, float& harm) noexcept
    {
        const float mono  = sm[0].next (prm.on && prm.mono ? 1.0f : 0.0f);
        const float gen   = sm[1].next (prm.on ? prm.gen : 0.0f);
        const float phone = sm[2].next (prm.on ? prm.phone : 0.0f);
        const float tight = sm[3].next (prm.on ? prm.tight : 0.0f);
        const float chr   = sm[4].next (prm.character);
        const float level = sm[5].next (levelT);
        const bool ctl = (ctr++ % kCtl) == 0;

        float m = 0.5f * (l + r);
        const float s = 0.5f * (l - r) * (1.0f - mono);

        // ---- sub generator: a sine oscillator phase-locked to the bass (zero crossings of the low-passed sub)
        {
            const float mf = (float) genLp.lp (m);
            const float am = std::fabs (mf);
            genEnv = am > genEnv ? genEA * genEnv + (1 - genEA) * am : genER * genEnv + (1 - genER) * am;
            ++count;
            bool cross = false;
            if (! schmitt && mf > 0.12f * genEnv && genEnv > 1.0e-5f)
            {
                schmitt = true; cross = true;
                const int p = count - lastCross; lastCross = count;
                if (p > (int) (fs / 300.0) && p < (int) (fs / 18.0))
                    period = (period <= 0 || std::abs (p / period - 1.0) > 0.2) ? p : period + (p - period) * 0.35;
                half = ! half;
            }
            else if (schmitt && mf < -0.12f * genEnv) schmitt = false;
            if (count - lastCross > (int) (fs / 15.0)) period = 0;                 // lost the note
            if (count > (1 << 30)) { count -= lastCross; lastCross = 0; }

            const bool oct = prm.genMode == 1;
            if (period > 0)
            {
                phase += (oct ? 0.5 : 1.0) / period;
                if (cross)
                {
                    // the input crossed upward a little after its true zero (hysteresis) -> expected osc phase
                    const double want = oct ? (half ? 0.0 : 0.5) : 0.0;
                    double err = phase - want - (oct ? 0.5 : 1.0) * (0.02 + lagCyc); err -= std::floor (err + 0.5);
                    phase -= err * 0.5;
                }
                phase -= std::floor (phase);
            }
            const float ampT = period > 0 ? genEnv : 0.0f;
            amp += (ampT - amp) * ampC;
            if (gen > 1.0e-4f && amp > 1.0e-6f)
                m += (float) std::sin (2.0 * kPi * phase) * amp * gen * 1.2f;
        }

        // ---- phone harmonics (Chebyshev on the envelope-normalised sub -> exact 2nd/3rd/4th)
        harm = 0;
        {
            const float a = std::fabs (m);
            hEnv = a > hEnv ? hA * hEnv + (1 - hA) * a : hR * hEnv + (1 - hR) * a;
            if (phone > 1.0e-4f && hEnv > 1.0e-5f)
            {
                const float x = std::clamp (m / hEnv, -1.0f, 1.0f), x2 = x * x;
                const float T2 = 2 * x2 - 1, T3 = x * (4 * x2 - 3), T4 = 8 * x2 * x2 - 8 * x2 + 1;
                double h = hEnv * ((1 - chr) * (0.75f * T2 + 0.3f * T4) + chr * (0.8f * T3 + 0.25f * T2));
                for (auto& f : hHp) h = f.hp (h);
                h = hLp.lp (h);
                harm = (float) h * phone * 3.0f;
            }
        }

        // ---- glue compressor (stereo linked, slow and clean so it does not distort the sub)
        l = m + s; r = m - s;
        if (tight > 1.0e-4f)
        {
            const float a = std::max (std::fabs (l), std::fabs (r));
            env = a > env ? eA * env + (1 - eA) * a : eR * env + (1 - eR) * a;
            if (ctl)
            {
                const float thr = -6.0f - tight * 24.0f, ratio = 1.0f + tight * 7.0f, knee = 6.0f;
                const float over = gainToDb (env) - thr;
                float red = 0;
                if (over > knee * 0.5f) red = over * (1 - 1 / ratio);
                else if (over > -knee * 0.5f) { const float t = over + knee * 0.5f; red = (1 - 1 / ratio) * t * t / (2 * knee); }
                grDb += (-red - grDb) * (-red < grDb ? gA : gR);
                const float makeup = -thr * (1 - 1 / ratio) * 0.45f * tight;
                gT = dbToGain (grDb + makeup);
            }
        }
        else { grDb = 0; gT = 1; }
        g += (gT - g) * gC;
        l *= g * level; r *= g * level;
    }
    float grDbNow() const { return grDb; }

private:
    double fs = 48000; float xover = 110, levelT = 1;
    SubParams prm;
    Smooth sm[6];
    unsigned ctr = 0;
    float env = 0, eA = 0, eR = 0, gA = 0, gR = 0, gC = 0, grDb = 0, g = 1, gT = 1;
    float hEnv = 0, hA = 0, hR = 0, genEnv = 0, genEA = 0, genER = 0, amp = 0, ampC = 0;
    int lastCross = 0, count = 0; bool schmitt = false, half = false; double period = 0, phase = 0, lagCyc = 0;
    Svf hHp[2], hLp, genLp;
};

// ---------------------------------------------------------------------------- whole engine
struct Params
{
    float inDb = 0, outDb = 0, mix = 1, x1 = 110, x2 = 2500;
    bool clip = true, compare = false, delta = false;
    bool duckMid = false;
    bool soloSub = false, soloMid = false, soloTop = false;
    SubParams sub; BandParams mid, top;
};

class Engine
{
public:
    void prepare (double osRate, int osFactor)
    {
        fs = osRate; osF = osFactor;
        sub.prepare (fs); mid.prepare (fs, 700.0f); top.prepare (fs, 6000.0f);
        smIn.init (1, 0.03f, fs); smOut.init (1, 0.03f, fs); smMix.init (1, 0.03f, fs);
        x1s = 110; x2s = 2500; glideC = 1.0f - std::exp (-32.0f / (float) (0.05 * fs));
        for (int c = 0; c < 2; ++c) { a[c].reset(); b[c].reset(); ap[c].reset(); }
        setFilters();
        cmpC = 1.0f - std::exp (-1.0f / (float) (0.4 * fs)); pw = pc = 1.0e-6f; cmpG = 1;
        sub.updateFilters(); ctr = 0; first = true;
    }
    void setParams (const Params& p)
    {
        prm = p; inT = dbToGain (p.inDb); outT = dbToGain (p.outDb);
        sub.setParams (p.sub, x1s); mid.setParams (p.mid, osF); top.setParams (p.top, osF);
        if (first) { x1s = p.x1; x2s = p.x2; setFilters(); sub.setParams (p.sub, x1s); sub.updateFilters(); first = false; smMix.v = p.mix; smIn.v = inT; smOut.v = outT; }
    }

    // one oversampled sample; duck = side-chain gain for the sub (linear)
    inline void process (float& l, float& r, float duck) noexcept
    {
        if ((ctr++ & 31) == 0 && (std::abs (x1s - prm.x1) > 1.0e-3f || std::abs (x2s - std::max (prm.x2, x1s * 2.0f)) > 1.0e-3f))
        {
            x1s = std::exp (std::log (x1s) + (std::log (prm.x1) - std::log (x1s)) * glideC);
            x2s = std::exp (std::log (x2s) + (std::log (std::max (prm.x2, x1s * 2.0f)) - std::log (x2s)) * glideC);
            if (std::abs (x1s - prm.x1) < 0.01f) x1s = prm.x1;
            setFilters(); sub.setParams (prm.sub, x1s); sub.updateFilters();
        }
        const float gin = smIn.next (inT);
        float lo[2], md[2], hi[2];
        const float in[2] = { l * gin, r * gin };
        for (int c = 0; c < 2; ++c)
        {
            double l1, h1, m2, t2;
            a[c].split (in[c], l1, h1);
            b[c].split (h1, m2, t2);
            lo[c] = (float) ap[c].ap (l1); md[c] = (float) m2; hi[c] = (float) t2;
        }
        const float cleanL = lo[0] + md[0] + hi[0], cleanR = lo[1] + md[1] + hi[1];

        float harm;
        sub.process (lo[0], lo[1], harm);
        mid.process (md[0], md[1]);
        top.process (hi[0], hi[1]);
        lo[0] *= duck; lo[1] *= duck; harm *= duck;
        if (prm.duckMid) { md[0] *= duck; md[1] *= duck; }

        const bool anySolo = prm.soloSub || prm.soloMid || prm.soloTop;
        const float ks = ! anySolo || prm.soloSub ? 1.0f : 0.0f, km = ! anySolo || prm.soloMid ? 1.0f : 0.0f, kt = ! anySolo || prm.soloTop ? 1.0f : 0.0f;
        float wl = ks * (lo[0] + harm) + km * md[0] + kt * hi[0];
        float wr = ks * (lo[1] + harm) + km * md[1] + kt * hi[1];
        const float mix = smMix.next (prm.mix);
        if (! anySolo) { wl = cleanL + (wl - cleanL) * mix; wr = cleanR + (wr - cleanR) * mix; }

        // equal-loudness compare follower
        pw += (wl * wl + wr * wr - pw) * cmpC; pc += (cleanL * cleanL + cleanR * cleanR - pc) * cmpC;
        if (prm.compare)
        {
            if ((ctr & 7) == 0) cmpG = std::clamp (std::sqrt (pw / std::max (pc, 1.0e-12f)), 0.1f, 8.0f);
            wl = cleanL * cmpG; wr = cleanR * cmpG;
        }
        else if (prm.delta) { wl -= cleanL; wr -= cleanR; }

        const float g = smOut.next (outT);
        wl *= g; wr *= g;
        if (prm.clip) { wl = softClip (wl); wr = softClip (wr); }
        l = wl; r = wr;
    }
    static inline float softClip (float x) noexcept
    {
        constexpr float c = 0.98f, t = 0.75f;
        const float ax = std::fabs (x);
        if (ax <= t) return x;
        return std::copysign (t + (c - t) * std::tanh ((ax - t) / (c - t)), x);
    }
    float subGrDb() const    { return sub.grDbNow(); }
    float midSqDb() const    { return mid.squashDb(); }
    float topSqDb() const    { return top.squashDb(); }
    float x1Now() const { return x1s; }
    float x2Now() const { return x2s; }

private:
    void setFilters()
    {
        for (int c = 0; c < 2; ++c) { a[c].set (x1s, fs); b[c].set (x2s, fs); ap[c].set (x2s, fs); }
    }
    double fs = 48000; int osF = 1;
    Params prm;
    LR4 a[2], b[2]; Svf ap[2];
    SubFx sub; BandFx mid, top;
    Smooth smIn, smOut, smMix;
    float x1s = 110, x2s = 2500, glideC = 0.1f, inT = 1, outT = 1;
    float cmpC = 0, pw = 0, pc = 0, cmpG = 1;
    unsigned ctr = 0; bool first = true;
};

// ---------------------------------------------------------------------------- side-chain kick duck (base rate)
class KickDuck
{
public:
    void prepare (double fsIn) { fs = fsIn; lp1.set (150, fs); lp2.set (150, fs); lp1.reset(); lp2.reset(); env = 0; peak = 1.0e-4f; g = 1; setRelease (150); }
    void setRelease (float ms) { rel = std::exp (-1.0f / (float) (ms * 0.001 * fs)); }
    // returns linear gain for the bass
    inline float process (float sc, float depthDb) noexcept
    {
        const float x = std::fabs ((float) lp2.lp (lp1.lp (sc)));
        env = x > env ? 0.6f * env + 0.4f * x : rel * env + (1 - rel) * x;
        peak = std::max (env, peak * pkDecay);
        // how far below its recent peak the kick is: 0 dB -> full duck, -18 dB or lower -> no duck
        const float t = peak > 1.0e-4f ? std::clamp (1.0f + gainToDb (env / peak) / 18.0f, 0.0f, 1.0f) : 0.0f;
        const float target = dbToGain (-depthDb * t);
        g += (target - g) * (target < g ? 0.5f : 0.05f);
        return g;
    }
    float gainDb() const { return gainToDb (g); }
private:
    double fs = 48000; Svf lp1, lp2;
    float env = 0, peak = 1.0e-4f, rel = 0.999f, g = 1, pkDecay = 0.99997f;
};

// ---------------------------------------------------------------------------- pitch (YIN on a decimated copy)
// x: most recent n samples at fs. Returns the fundamental in Hz (25..400) or 0 when there is no clear pitch.
inline float detectPitch (const float* x, int n, double fs)
{
    const int dec = std::max (1, (int) std::lround (fs / 6000.0));
    const double fd = fs / dec;
    Svf a, b; a.set (450, fs); b.set (450, fs);
    std::vector<float> y; y.reserve ((size_t) (n / dec + 1));
    for (int i = 0; i < n; ++i) { const double v = b.lp (a.lp (x[i])); if (i % dec == 0) y.push_back ((float) v); }
    const int tauMin = (int) (fd / 400.0), tauMax = (int) (fd / 25.0);
    const int W = (int) y.size() - tauMax - 1;
    if (W < tauMax) return 0;
    const int off = 200;   // skip the filter start-up
    const int w = W - off;
    if (w < tauMax) return 0;
    double e = 0; for (int j = off; j < off + w; ++j) e += (double) y[(size_t) j] * y[(size_t) j];
    if (e / w < 1.0e-8) return 0;
    std::vector<double> d ((size_t) tauMax + 2, 0.0);
    double run = 0;
    for (int t = 1; t <= tauMax + 1; ++t)
    {
        double s = 0;
        for (int j = off; j < off + w; ++j) { const double df = y[(size_t) j] - y[(size_t) (j + t)]; s += df * df; }
        run += s;
        d[(size_t) t] = run > 0 ? s * t / run : 1.0;
    }
    int best = -1;
    for (int t = std::max (2, tauMin); t <= tauMax; ++t)
        if (d[(size_t) t] < 0.2 && d[(size_t) t] <= d[(size_t) t - 1] && d[(size_t) t] <= d[(size_t) t + 1]) { best = t; break; }
    if (best < 0)
    {
        double mn = 1e9; for (int t = std::max (2, tauMin); t <= tauMax; ++t) if (d[(size_t) t] < mn) { mn = d[(size_t) t]; best = t; }
        if (mn > 0.35) return 0;
    }
    const double d0 = d[(size_t) best - 1], d1 = d[(size_t) best], d2 = d[(size_t) best + 1];
    const double den = d0 - 2 * d1 + d2;
    const double tau = best + (std::abs (den) > 1e-12 ? 0.5 * (d0 - d2) / den : 0.0);
    return (float) (fd / tau);
}

// ---------------------------------------------------------------------------- offline analysis for LEARN
struct BassInfo { bool valid = false; float f0 = 0, subRatio = 0, crest = 0; };
inline BassInfo analyse (const float* x, int n, double fs)
{
    BassInfo info;
    if (n < (int) fs) return info;
    Svf s1, s2; s1.set (100, fs); s2.set (100, fs);
    double eAll = 0, eSub = 0, pk = 0;
    for (int i = 0; i < n; ++i)
    {
        const double sb = s2.lp (s1.lp (x[i]));
        eAll += (double) x[i] * x[i]; eSub += sb * sb; pk = std::max (pk, (double) std::fabs (x[i]));
    }
    if (eAll < 1.0e-6) return info;
    info.valid = true;
    std::vector<float> f;
    const int win = (int) (0.2 * fs);
    for (int s = 0; s + win <= n; s += win / 2) { const float h = detectPitch (x + s, win, fs); if (h > 0) f.push_back (std::log (h)); }
    if (f.size() >= 3) { std::nth_element (f.begin(), f.begin() + (long) f.size() / 2, f.end()); info.f0 = std::exp (f[f.size() / 2]); }
    info.subRatio = (float) (eSub / eAll);
    info.crest = (float) (pk / std::sqrt (eAll / n));
    return info;
}

inline const char* noteName (float hz, int& octave, float& cents)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const float midi = 69.0f + 12.0f * std::log2 (std::max (hz, 1.0f) / 440.0f);
    const int nn = (int) std::lround (midi);
    cents = (midi - (float) nn) * 100.0f;
    octave = nn / 12 - 1;
    return names[((nn % 12) + 12) % 12];
}

} // namespace lumen::bass2
