#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <atomic>
#include <cstdint>

// =====================================================================================
// Lumen Glitch — tempo-synced multi-lane glitch sequencer, zero latency
//   TIME lanes (one at a time, priority order): Repeat (with roll/ramp), Reverse, Tape Stop, Half Speed,
//   Scratch, Shuffle — all read a recording ring buffer, never the future -> 0 latency.
//   FX lanes (stack): Gate, Crush, Filter sweep, Echo (tails ring out).
//   Every switch is crossfaded, every repeat slice is crossfaded into the next -> no clicks.
// =====================================================================================
namespace lumen::glitch
{

enum Lane { Repeat, Reverse, TapeStop, HalfSpeed, Scratch, Shuffle, Gate, Crush, Filter, Echo, kLanes };
inline const char* laneName (int l)
{
    static const char* n[] { "REPEAT", "REVERSE", "TAPE STOP", "HALF SPEED", "SCRATCH", "SHUFFLE", "GATE", "CRUSH", "FILTER", "ECHO" };
    return n[std::clamp (l, 0, kLanes - 1)];
}
inline double repeatBeats (int i) { static const double b[] { 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625, 1.0 / 3.0, 1.0 / 6.0 }; return b[std::clamp (i, 0, 7)]; }   // note values in beats
inline double stepBeats (int i)   { static const double b[] { 1.0, 0.5, 0.25, 0.125 }; return b[std::clamp (i, 0, 3)]; }
inline double echoBeats (int i)   { static const double b[] { 0.25, 0.5, 0.75, 1.0, 1.5 }; return b[std::clamp (i, 0, 4)]; }

struct Params
{
    int steps = 16, rate = 2; float swing = 0;
    int play = 0;                  // 0 always, 1 every 2nd bar, 2 last bar of 4, 3 last bar of 8, 4 MIDI only
    float xfadeMs = 3;
    int repRate = 3; float repRamp = 0;
    float stopTime = 0.8f, stopCurve = 1.5f;
    float scratch = 0.7f; int shufRange = 2;
    float gateDuty = 0.5f, gateSmooth = 2;
    float bits = 8, down = 6;
    int fltType = 0; float fltAmount = 0.7f, fltRes = 0.4f;
    int echoTime = 2; float echoFb = 0.45f, echoLevel = 0.6f;
    float prob[kLanes] { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    float mix = 1, outDb = 0;
    int barBeatsNum = 4, barBeatsDen = 4;
};

class GlitchEngine
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        int n = 1; while (n < fs * 10.0) n <<= 1;
        for (auto& b : buf) b.assign ((size_t) n, 0.0f);
        mask = n - 1; now = 0;
        int e = 1; while (e < fs * 3.0) e <<= 1;
        for (auto& b : echoBuf) b.assign ((size_t) e, 0.0f);
        eMask = e - 1; ePos = 0;
        cur = prev = Voice(); xf = 1; anchorOk = false;
        curStep = -1; held = 0; heldPrev = 0;
        for (auto& s : svf) s = {};
        gateEnv = 1; crushW = 0; fltW = 0; echoW = 0; holdL = holdR = 0; holdCnt = 0;
        for (auto& a : act) a = false;
        lpE[0] = lpE[1] = 0;
        outSm = std::pow (10.0f, p.outDb / 20.0f);
    }
    void setParams (const Params& np) noexcept { p = np; }
    void setPattern (int lane, uint32_t bits) noexcept { pattern[lane] = bits; }
    void setHeld (uint32_t lanes) noexcept { held = lanes; }      // MIDI-held lanes (momentary)

    // beat = host beat position of the first sample, bps = beats per sample
    void process (float* L, float* R, int n, double beat, double bps) noexcept
    {
        const float xfStep = 1.0f / std::max (1.0f, p.xfadeMs * 0.001f * (float) fs);
        const float smFast = 1.0f - std::exp (-1.0f / (0.004f * (float) fs));
        const float gSm = 1.0f - std::exp (-1.0f / (std::max (0.3f, p.gateSmooth) * 0.001f * (float) fs));
        const float outT = std::pow (10.0f, p.outDb / 20.0f);
        const double sb = stepBeats (p.rate);
        const double spb = 1.0 / std::max (1e-12, bps);                       // samples per beat
        // the beat of every sample comes from one anchor -> identical results for any host block size
        const double predicted = anchorBeat + (double) (now - anchorS) * anchorBps;
        if (! anchorOk || std::abs (bps - anchorBps) > 1e-12 || std::abs (beat - predicted) > 1e-4)
        { anchorBeat = beat; anchorS = now; anchorBps = bps; anchorOk = true; }
        for (int i = 0; i < n; ++i)
        {
            const float x[2] { L[i], R[i] };
            buf[0][(size_t) (now & mask)] = x[0]; buf[1][(size_t) (now & mask)] = x[1];

            // ---- step clock (with swing) ----
            const double b = anchorBeat + (double) (now - anchorS) * anchorBps;
            const double sf = b / sb;
            const double pair = std::floor (sf * 0.5);
            const double u = sf - 2.0 * pair;
            const double sw = std::clamp ((double) p.swing, 0.0, 0.75);
            const bool second = u >= 1.0 + sw;
            const int64_t k = (int64_t) pair * 2 + (second ? 1 : 0);
            const double stepStartBeat = (pair * 2.0 + (second ? 1.0 + sw : 0.0)) * sb;
            const double stepLenBeats = (second ? 1.0 - sw : 1.0 + sw) * sb;
            const bool midiChange = held != heldPrev;
            if (k != curStep || midiChange)
            {
                const int64_t offs = midiChange && k == curStep ? 0 : (int64_t) std::llround ((b - stepStartBeat) * spb);
                newStep (k, stepStartBeat, stepLenBeats * spb, offs, spb);
                curStep = k; heldPrev = held;
            }

            // ---- time stage (crossfaded voices) ----
            float y[2], yp[2];
            render (cur, x, y);
            if (xf < 1.0f)
            {
                render (prev, x, yp);
                xf = std::min (1.0f, xf + xfStep);
                const float a = std::sin (xf * 1.5707963f), c = std::cos (xf * 1.5707963f);   // equal power
                for (int ch = 0; ch < 2; ++ch) y[ch] = y[ch] * a + yp[ch] * c;
            }
            cur.t += 1.0; if (xf < 1.0f) prev.t += 1.0;

            // ---- FX lanes ----
            const float ph = (float) std::clamp ((double) (now - stepS) / std::max (1.0, stepLenS), 0.0, 1.0);
            // gate
            const float gT = act[Gate] ? (ph < p.gateDuty ? 1.0f : 0.0f) : 1.0f;
            gateEnv += (gT - gateEnv) * gSm;
            // crush
            crushW += ((act[Crush] ? 1.0f : 0.0f) - crushW) * smFast;
            if (crushW > 0.999f) crushW = 1.0f;
            if (crushW > 1e-4f)
            {
                if (--holdCnt <= 0) { holdCnt = std::max (1, (int) std::round (p.down)); holdL = y[0]; holdR = y[1]; }
                const float q = std::pow (2.0f, std::clamp (p.bits, 1.0f, 24.0f) - 1.0f);
                const float cl = std::round (holdL * q) / q, cr = std::round (holdR * q) / q;
                y[0] += (cl - y[0]) * crushW; y[1] += (cr - y[1]) * crushW;
            }
            // filter sweep
            fltW += ((act[Filter] ? 1.0f : 0.0f) - fltW) * smFast;
            if (fltW > 0.999f) fltW = 1.0f;
            if (fltW > 1e-4f || act[Filter])
            {
                float f0, f1;
                if (p.fltType == 0) { f0 = 18000.0f; f1 = 18000.0f * std::pow (250.0f / 18000.0f, p.fltAmount); }
                else if (p.fltType == 1) { f0 = 20.0f; f1 = 20.0f * std::pow (6000.0f / 20.0f, p.fltAmount); }
                else { f0 = 300.0f; f1 = 300.0f * std::pow (6000.0f / 300.0f, p.fltAmount); }
                const float fc = act[Filter] ? f0 * std::pow (f1 / f0, ph) : fltLastFc;
                fltLastFc = fc;
                const float g = std::tan (juce::MathConstants<float>::pi * std::min (fc, 0.45f * (float) fs) / (float) fs);
                const float kq = 2.0f - 1.9f * std::clamp (p.fltRes, 0.0f, 1.0f);
                const float a1 = 1.0f / (1.0f + g * (g + kq)), a2 = g * a1, a3 = g * a2;
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto& s = svf[ch];
                    const float v3 = y[ch] - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
                    s.ic1 = 2.0f * v1 - s.ic1; s.ic2 = 2.0f * v2 - s.ic2;
                    const float out = p.fltType == 0 ? v2 : p.fltType == 1 ? y[ch] - kq * v1 - v2 : v1 * kq;
                    y[ch] += (out - y[ch]) * fltW;
                }
            }
            for (auto& v : y) v *= gateEnv;
            // echo (send while the lane is on, the tail rings out)
            echoW += ((act[Echo] ? 1.0f : 0.0f) - echoW) * smFast;
            {
                const double dS = std::clamp (echoBeats (p.echoTime) * spb, 16.0, (double) eMask - 4);
                const int64_t rp = ePos - (int64_t) dS;
                float eo[2];
                for (int ch = 0; ch < 2; ++ch)
                {
                    eo[ch] = echoBuf[ch][(size_t) (rp & eMask)];
                    lpE[ch] += (eo[ch] - lpE[ch]) * 0.35f;
                }
                // ping-pong: left feeds right
                echoBuf[0][(size_t) (ePos & eMask)] = (y[0] + y[1]) * 0.5f * echoW + lpE[1] * p.echoFb;
                echoBuf[1][(size_t) (ePos & eMask)] = lpE[0] * p.echoFb;
                ++ePos;
                y[0] += eo[0] * p.echoLevel; y[1] += eo[1] * p.echoLevel;
            }

            outSm += (outT - outSm) * smFast;
            L[i] = (x[0] + (y[0] - x[0]) * p.mix) * outSm;
            R[i] = (x[1] + (y[1] - x[1]) * p.mix) * outSm;
            ++now;
        }
    }

    // UI
    std::atomic<int> uiStep { 0 };
    std::atomic<uint32_t> uiActive { 0 };
    int timeMode() const noexcept { return cur.mode; }

private:
    struct Voice
    {
        int mode = -1;                 // -1 = live
        int64_t S = 0;                 // absolute start sample
        double t = 0, len = 1;
        double posAcc = 0;             // tape stop integral
        double sliceT = 0, sliceLen = 1, nextLen = 1;
        int shufK = 1;
    };
    struct Svf { float ic1 = 0, ic2 = 0; };

    void newStep (int64_t k, double startBeat, double lenS, int64_t offs, double spb) noexcept
    {
        const int idx = (int) (((k % p.steps) + p.steps) % p.steps);
        // fill / play modes
        const double barBeats = (double) p.barBeatsNum * 4.0 / (double) std::max (1, p.barBeatsDen);
        const int64_t bar = (int64_t) std::floor ((startBeat + 1e-9) / std::max (0.25, barBeats));
        bool patternOn = true;
        switch (p.play)
        {
            case 1: patternOn = (bar & 1) == 1; break;
            case 2: patternOn = (bar & 3) == 3; break;
            case 3: patternOn = (bar & 7) == 7; break;
            case 4: patternOn = false; break;
            default: break;
        }
        uint32_t mask32 = 0;
        for (int l = 0; l < kLanes; ++l)
        {
            bool on = patternOn && ((pattern[l] >> idx) & 1u) != 0;
            if (on && p.prob[l] < 0.999f) on = rand01() < p.prob[l];
            if ((held >> l) & 1u) on = true;
            act[l] = on;
            if (on) mask32 |= 1u << l;
        }
        uiStep.store (idx); uiActive.store (mask32);
        int mode = -1;
        for (int l = 0; l <= Shuffle; ++l) if (act[l]) { mode = l; break; }

        stepS = now - std::max<int64_t> (0, offs); stepLenS = std::max (1.0, lenS);
        const bool newlyHeld = mode >= 0 && ((held >> mode) & 1u) != 0 && ((heldPrev >> mode) & 1u) == 0;
        // the same time lane on consecutive steps = ONE gesture: a roll keeps accelerating, a stop keeps slowing
        if (mode >= 0 && mode == cur.mode && mode != Shuffle && ! newlyHeld) return;
        // run length of this lane in the pattern from here (tape stop / scratch span the whole run)
        int run = 1;
        if (mode >= 0 && ((pattern[mode] >> idx) & 1u) != 0)
            while (run < p.steps && ((pattern[mode] >> ((idx + run) % p.steps)) & 1u) != 0) ++run;

        Voice v;
        v.mode = mode; v.S = now - std::max<int64_t> (0, offs); v.t = (double) std::max<int64_t> (0, offs); v.len = std::max (1.0, lenS) * run;
        v.sliceT = 0; v.sliceLen = std::max (32.0, repeatBeats (p.repRate) * spb);
        v.shufK = 1 + (int) (rand01() * (float) std::max (1, p.shufRange));
        if (v.shufK > p.shufRange) v.shufK = p.shufRange;
        v.len = mode == Shuffle ? std::max (1.0, lenS) : v.len;
        // already inside the step (transport jump / start): advance the repeat slices
        while (v.mode == Repeat && v.t >= v.sliceT + v.sliceLen) { v.sliceT += v.sliceLen; v.sliceLen = std::max (fs * 0.006, v.sliceLen * (1.0 - 0.4 * p.repRamp)); }
        if (v.mode == TapeStop) { for (double t = 0; t < v.t; t += 1.0) v.posAcc += tapeSpeed (t, v.len); }

        if (mode == -1 && cur.mode == -1) { cur = v; return; }        // live -> live: nothing to fade
        prev = cur; cur = v; xf = 0.0f;
    }

    float rand01() noexcept { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; }

    double tapeSpeed (double t, double len) const noexcept
    {
        const double T = std::max (1.0, p.stopTime * len);
        if (t >= T) return 0.0;
        return std::pow (1.0 - t / T, (double) p.stopCurve);
    }

    inline float rd (int ch, double pos) const noexcept
    {
        pos = std::clamp (pos, (double) (now - mask + 8), (double) now);
        const double fl = std::floor (pos);
        const int64_t i = (int64_t) fl; const float f = (float) (pos - fl);
        auto at = [&] (int64_t j) { return buf[ch][(size_t) (std::min (j, now) & mask)]; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        if (f < 1e-6f) return x0;
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    void render (Voice& v, const float* x, float* y) noexcept
    {
        const double t = v.t;
        const double S = (double) v.S;
        switch (v.mode)
        {
            case -1: y[0] = x[0]; y[1] = x[1]; return;
            case Repeat:
            {
                if (t >= v.sliceT + v.sliceLen) { v.sliceT += v.sliceLen; v.sliceLen = std::max (fs * 0.006, v.sliceLen * (1.0 - 0.4 * p.repRamp)); }
                const double local = t - v.sliceT;
                const double F = std::min (fs * 0.0025, v.sliceLen * 0.25);
                for (int ch = 0; ch < 2; ++ch)
                {
                    float a = (local < 0.5 && v.sliceT < 0.5) ? x[ch] : rd (ch, S + local);
                    if (local > v.sliceLen - F)
                    {
                        // crossfade into the start of the next slice (which begins at S again)
                        const float w = (float) ((local - (v.sliceLen - F)) / F);
                        const float bnext = rd (ch, S + (local - v.sliceLen));
                        a = a * std::cos (w * 1.5707963f) + bnext * std::sin (w * 1.5707963f);
                    }
                    y[ch] = a;
                }
                return;
            }
            case Reverse:
                for (int ch = 0; ch < 2; ++ch) y[ch] = rd (ch, S - t);
                return;
            case TapeStop:
            {
                const double sp = tapeSpeed (t, v.len);
                const float g = (float) std::min (1.0, sp * 6.0);
                for (int ch = 0; ch < 2; ++ch) y[ch] = rd (ch, S + v.posAcc) * g;
                v.posAcc += sp;
                return;
            }
            case HalfSpeed:
                for (int ch = 0; ch < 2; ++ch) y[ch] = rd (ch, S + 0.5 * t);
                return;
            case Scratch:
            {
                const double P = std::max (64.0, v.len * 0.5), D = p.scratch * 1.5 * P / (2.0 * juce::MathConstants<double>::pi);
                const double pos = t - D * (1.0 - std::cos (2.0 * juce::MathConstants<double>::pi * t / P));
                for (int ch = 0; ch < 2; ++ch) y[ch] = rd (ch, S + pos);
                return;
            }
            case Shuffle:
                for (int ch = 0; ch < 2; ++ch) y[ch] = rd (ch, S + t - v.shufK * v.len);
                return;
            default: y[0] = x[0]; y[1] = x[1]; return;
        }
    }

    Params p;
    double fs = 48000;
    std::vector<float> buf[2], echoBuf[2];
    int64_t mask = 0, now = 0, eMask = 0, ePos = 0;
    Voice cur, prev;
    float xf = 1;
    int64_t curStep = -1;
    uint32_t pattern[kLanes] {};
    std::atomic<uint32_t> held { 0 };
    uint32_t heldPrev = 0;
    bool act[kLanes] {};
    uint32_t seed = 0x1234567u;
    Svf svf[2];
    float gateEnv = 1, crushW = 0, fltW = 0, echoW = 0, holdL = 0, holdR = 0, fltLastFc = 18000, lpE[2] {}, outSm = 1;
    int holdCnt = 0;
    int64_t stepS = 0; double stepLenS = 1;
    double anchorBeat = 0, anchorBps = 0; int64_t anchorS = 0; bool anchorOk = false;
};

} // namespace lumen::glitch
