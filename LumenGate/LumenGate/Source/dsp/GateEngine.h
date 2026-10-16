#pragma once
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdint>

// =====================================================================================
// Lumen Gate engine
//   DYNAMICS : gate / expander / smooth (program-dependent) / upward expander / ducker
//              hysteresis (separate open & close thresholds: no chatter), hold, soft knee,
//              look-ahead (sample-exact), key filter, external sidechain, MIDI trigger, stereo link
//   PATTERN  : tempo-synced step gate (8/16/32 steps, per-step level), swing, gate length,
//              attack / release shaping, stereo offset, depth
//   BOTH     : pattern applied after the dynamic gate
// =====================================================================================
namespace lumen::gate
{

constexpr int kMaxSteps = 32;
enum Style { Gate = 0, Expander, Smooth, Upward, Ducker };
enum EngineMode { Dynamics = 0, Pattern, Both };

struct Params
{
    int mode = Dynamics, style = Gate;
    float thrDb = -40, rangeDb = 60, ratio = 4, attMs = 0.5f, holdMs = 20, relMs = 120, kneeDb = 3, hystDb = 3;
    int lookahead = 0;                 // samples
    bool extSc = false, listen = false, midiTrig = false;
    float scHp = 20, scLp = 20000, link = 1;
    // pattern
    int steps = 16; double stepBeats = 0.25; float swing = 0, gateLen = 0.5f, pAttMs = 2, pRelMs = 30, depth = 1, stereoOffset = 0;
    float level[kMaxSteps] {};
    float mix = 1, outDb = 0;
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process (float x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
    void hp (double fs, double f) { const double w = 2 * 3.14159265358979 * f / fs, al = std::sin (w) / (2 * 0.7071), c = std::cos (w), a0 = 1 + al; b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void lp (double fs, double f) { const double w = 2 * 3.14159265358979 * f / fs, al = std::sin (w) / (2 * 0.7071), c = std::cos (w), a0 = 1 + al; b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void bypass() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
};

class GateEngine
{
public:
    void prepare (double sampleRate, int maxLookahead)
    {
        fs = sampleRate;
        for (auto& b : la) b.assign ((size_t) maxLookahead + 4, 0.0f);
        laPos = 0;
        for (int c = 0; c < 2; ++c) { hpF[c].reset(); lpF[c].reset(); gainDb[c] = 0.0f; det[c] = 0.0f; open[c] = false; holdCnt[c] = 0; openTime[c] = 0; }
        lastHp = lastLp = -1.0f;
        patG[0] = patG[1] = 1.0f;
        mixSm = 1.0f; outSm = 1.0f; first = true;
        resetMeters();
    }

    void setParams (const Params& np) noexcept
    {
        p = np;
        if (std::abs (p.scHp - lastHp) > 0.1f) { if (p.scHp > 20.5f) { hpF[0].hp (fs, p.scHp); hpF[1].hp (fs, p.scHp); } else { hpF[0].bypass(); hpF[1].bypass(); } lastHp = p.scHp; }
        if (std::abs (p.scLp - lastLp) > 0.1f) { if (p.scLp < 19500.0f) { lpF[0].lp (fs, p.scLp); lpF[1].lp (fs, p.scLp); } else { lpF[0].bypass(); lpF[1].bypass(); } lastLp = p.scLp; }
        outT = std::pow (10.0f, p.outDb / 20.0f);
        if (first) { mixSm = p.mix; outSm = outT; first = false; }
    }

    // ppq / beatsPerSecond describe the host transport at the first sample (or a free-running clock)
    // sc: external key (nullptr = internal), midiOpen: a MIDI note is held
    void process (float* L, float* R, const float* scL, const float* scR, int n, double ppq, double beatsPerSample, bool midiOpen) noexcept
    {
        const float sm = 1.0f - (float) std::exp (-1.0 / (0.02 * fs));
        // dynamics: slope-limited gain in dB -> ATTACK = time to open fully, RELEASE = time to close fully (exact, musical)
        const float span = std::max (6.0f, p.style == Upward ? std::min (24.0f, p.rangeDb) : p.rangeDb);
        const float upStep = span / std::max (1.0f, (float) (p.attMs * 0.001 * fs));
        const float downStep = span / std::max (1.0f, (float) (p.relMs * 0.001 * fs));
        // pattern: one-pole in amplitude, ATTACK = time to ~95 %, RELEASE = time to -60 dB
        const float pa = coef (std::max (0.05f, p.pAttMs) / 3.0f), pr = coef (std::max (0.05f, p.pRelMs) / 6.9f);
        const float detR = coef (8.0f);       // key detector: instant attack, 8 ms peak release (no audio-rate chatter)
        const int holdN = (int) (p.holdMs * 0.001 * fs);
        const bool useDyn = p.mode != Pattern, usePat = p.mode != Dynamics;
        const int laD = std::clamp (p.lookahead, 0, (int) la[0].size() - 2);
        const int laN = (int) la[0].size();
        for (int i = 0; i < n; ++i)
        {
            mixSm += (p.mix - mixSm) * sm; outSm += (outT - outSm) * sm;
            const float inL = L[i], inR = R[i];
            // ---- key signal ----
            float kl = scL ? scL[i] : inL, kr = scR ? scR[i] : inR;
            kl = lpF[0].process (hpF[0].process (kl)); kr = lpF[1].process (hpF[1].process (kr));
            // ---- look-ahead: the audio is delayed, the key is not ----
            la[0][(size_t) laPos] = inL; la[1][(size_t) laPos] = inR;
            int rp = laPos - laD; if (rp < 0) rp += laN;
            const float dl = la[0][(size_t) rp], dr = la[1][(size_t) rp];
            laPos = (laPos + 1) % laN;

            float gl = 1.0f, gr = 1.0f;
            if (useDyn)
            {
                det[0] = std::max (std::abs (kl), det[0] * detR); det[1] = std::max (std::abs (kr), det[1] * detR);
                const float al = det[0], ar = det[1], mx = std::max (al, ar);
                const float lvl[2] { toDb (p.link * mx + (1.0f - p.link) * al), toDb (p.link * mx + (1.0f - p.link) * ar) };
                for (int c = 0; c < 2; ++c)
                {
                    const float target = midiOpen && p.midiTrig ? openGain() : computeGain (c, lvl[c], holdN);
                    // up = opening (ducker: recovering), down = closing (ducker: ducking)
                    float up = upStep, down = downStep;
                    if (p.style == Ducker) std::swap (up, down);
                    if (p.style == Smooth) down = downStep / smoothRelMul (c);
                    const float d = target - gainDb[c];
                    gainDb[c] += std::clamp (d, -down, up);
                    meterGr = std::min (meterGr, gainDb[c]); meterUp = std::max (meterUp, gainDb[c]);
                }
                meterKey = std::max (meterKey, std::max (lvl[0], lvl[1]));
                gl = std::pow (10.0f, gainDb[0] / 20.0f); gr = std::pow (10.0f, gainDb[1] / 20.0f);
                if (p.link > 0.999f) gr = gl;
            }
            if (usePat)
            {
                const double pos = ppq + beatsPerSample * i;
                for (int c = 0; c < 2; ++c)
                {
                    const float env = patternEnv (pos + (c == 1 ? p.stereoOffset * p.stepBeats : 0.0), beatsPerSample);
                    const float target = 1.0f - p.depth * (1.0f - env);
                    patG[c] = target + (patG[c] - target) * (target > patG[c] ? pa : pr);
                }
                gl *= patG[0]; gr *= patG[1];
                patMeter = patG[0];
            }
            float ol, orr;
            if (p.listen) { ol = kl; orr = kr; }
            else { ol = dl * (1.0f + (gl - 1.0f) * mixSm); orr = dr * (1.0f + (gr - 1.0f) * mixSm); }
            L[i] = ol * outSm; R[i] = orr * outSm;
            meterIn = std::max (meterIn, std::max (std::abs (inL), std::abs (inR)));
            meterOut = std::max (meterOut, std::max (std::abs (L[i]), std::abs (R[i])));
        }
        lastPos = ppq + beatsPerSample * n;
    }

    // meters for the UI
    float meterIn = 0, meterOut = 0, meterKey = -120, meterGr = 0, meterUp = -120, patMeter = 1;
    double lastPos = 0;
    bool isOpen() const noexcept { return open[0]; }
    void resetMeters() noexcept { meterIn = meterOut = 0; meterKey = -120; meterGr = 0; meterUp = -120; }
    int currentStep (double ppq) const noexcept
    {
        if (p.stepBeats <= 0) return 0;
        return (int) std::fmod (std::floor (ppq / p.stepBeats), (double) std::max (1, p.steps));
    }

    // static curve, for the UI and tests
    float staticGain (float lv) const noexcept
    {
        const float k = std::max (0.01f, p.kneeDb), t = p.thrDb;
        auto softBelow = [&] (float x) { // how far below threshold, with knee
            const float d = t - x;
            if (d <= -k * 0.5f) return 0.0f;
            if (d >= k * 0.5f) return d;
            const float u = d + k * 0.5f; return u * u / (2.0f * k);
        };
        switch (p.style)
        {
            case Gate:     return lv >= t ? 0.0f : -p.rangeDb;
            case Expander:
            case Smooth:   return std::max (-p.rangeDb, -softBelow (lv) * (p.ratio - 1.0f));
            case Upward:   { const float above = lv - t; return above > 0 ? std::min (std::min (24.0f, p.rangeDb), above * (p.ratio - 1.0f) / p.ratio) : 0.0f; }
            case Ducker:   { const float above = lv - t + k * 0.5f; if (above <= 0) return 0.0f; const float a = above < k ? above * above / (2.0f * k) : above - k * 0.5f; return std::max (-p.rangeDb, -a * (p.ratio - 1.0f)); }
            default:       return 0.0f;
        }
    }

private:
    float openGain() const noexcept { return p.style == Ducker ? -p.rangeDb : 0.0f; }
    float computeGain (int c, float lv, int holdN) noexcept
    {
        if (p.style == Gate || p.style == Smooth)
        {
            // hysteresis: opens at threshold, closes only HYST dB below it, after HOLD
            if (lv >= p.thrDb) { open[c] = true; holdCnt[c] = holdN; }
            else if (lv < p.thrDb - p.hystDb) { if (holdCnt[c] > 0) --holdCnt[c]; else open[c] = false; }
            else if (holdCnt[c] > 0) --holdCnt[c];
            openTime[c] = open[c] ? std::min (openTime[c] + 1, (int) fs * 4) : 0;
            if (p.style == Gate) return open[c] ? 0.0f : -p.rangeDb;
            return open[c] ? 0.0f : std::max (-p.rangeDb, staticGain (lv));    // smooth: expander below, fully open while held
        }
        if (p.style == Expander)
        {
            if (lv >= p.thrDb) holdCnt[c] = holdN;
            if (holdCnt[c] > 0) { --holdCnt[c]; open[c] = true; return 0.0f; }
            open[c] = lv >= p.thrDb - p.hystDb;
            return staticGain (lv);
        }
        open[c] = lv >= p.thrDb;
        return staticGain (lv);
    }
    float smoothRelMul (int c) const noexcept
    {
        // program dependent: short notes release fast, long ones slowly (like a good engineer riding a fader)
        const float held = std::min (1.0f, (float) openTime[c] / (float) (0.5 * fs));
        return 0.4f + 1.6f * held;
    }
    float patternEnv (double pos, double bps) const noexcept
    {
        const double sb = std::max (1e-6, p.stepBeats);
        const int steps = std::clamp (p.steps, 1, kMaxSteps);
        const double stepF = pos / sb;
        long long si = (long long) std::floor (stepF);
        double ph = stepF - (double) si;
        // swing: odd steps start later
        if (p.swing > 0.0f)
        {
            const double sh = p.swing * 0.5;
            if ((si & 1) == 1) { if (ph < sh) { si -= 1; ph = 1.0 + ph; } else ph = (ph - sh) / (1.0 - sh); }
            else ph = ph / (1.0 + sh);
            ph = std::min (ph, 1.0);
        }
        const int idx = (int) (((si % steps) + steps) % steps);
        const float lvl = p.level[idx];
        (void) bps;
        if (lvl <= 0.0f) return 0.0f;
        return ph < p.gateLen ? lvl : 0.0f;
    }
    float coef (float ms) const noexcept { return (float) std::exp (-1.0 / (std::max (0.01, (double) ms) * 0.001 * fs)); }
    static inline float toDb (float x) noexcept { return 20.0f * std::log10 (std::max (x, 1.0e-7f)); }

    Params p;
    double fs = 48000;
    std::vector<float> la[2]; int laPos = 0;
    Biquad hpF[2], lpF[2];
    float lastHp = -1, lastLp = -1;
    float gainDb[2] {}, patG[2] { 1, 1 }, det[2] {};
    bool open[2] {};
    int holdCnt[2] {}, openTime[2] {};
    float mixSm = 1, outT = 1, outSm = 1;
    bool first = true;
};

} // namespace lumen::gate
