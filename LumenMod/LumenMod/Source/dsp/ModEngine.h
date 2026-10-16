#pragma once
#include "DelayLine.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

// =====================================================================================
// Lumen Mod engine — 7 modulation machines in one
//   CHORUS    : modern 1-8 voice chorus (variants Modern / Tri / Lush / Detune)
//   JUNO      : BBD chorus modelled on the classic synth chorus (I / II / I+II / Deep)
//   DIMENSION : subtle 2-line widener in the style of the 4-button rack unit (1..4)
//   ENSEMBLE  : 3-phase string ensemble (slow + fast LFO per line)
//   FLANGER   : feedback flanger (Classic / Negative / Jet / Wide)
//   PHASER    : 4..16-stage allpass phaser with feedback
//   VIBRATO   : pure pitch vibrato / rotary (doppler + AM)
// Shared: tempo sync, LFO shapes, analog drift, tone tilt, BBD character, BASS MONO (wet high-passed so the
// low end stays dry and mono), DUCK (wet steps back while the input plays), ENV (input level drives depth).
// =====================================================================================
namespace lumen::mod
{

constexpr float kPi = 3.14159265358979f;
constexpr int kMaxVoices = 8;
enum Mode { Chorus = 0, Juno, Dimension, Ensemble, Flanger, Phaser, Vibrato, kNumModes };

struct Params
{
    int mode = Chorus, variant = 0, shape = 0;     // shape: 0 sine, 1 triangle, 2 smooth random
    float rateHz = 0.8f, depth = 0.5f, delay = 0.5f, feedback = 0.0f, width = 0.8f, mix = 0.5f;
    int voices = 3;
    float tone = 0.0f, character = 0.0f, drift = 0.0f, bassMono = 120.0f, duck = 0.0f, env = 0.0f, outDb = 0.0f;
};

struct OnePole
{
    float a = 0, z = 0;
    void setLP (float fc, float fs) { a = std::exp (-2.0f * kPi * std::min (fc, 0.45f * fs) / fs); }
    inline float lp (float x) noexcept { z = x + a * (z - x); return z; }
    void reset() { z = 0; }
};
struct Bq
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void hp (float fc, float fs)
    {
        const float w = 2 * kPi * std::min (fc, 0.45f * fs) / fs, c = std::cos (w), al = std::sin (w) / (2 * 0.7071f), a0 = 1 + al;
        b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    inline float process (float x) noexcept { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
};

class ModEngine
{
public:
    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        for (auto& d : dl) d.prepare ((int) (0.06f * fs) + 16);
        for (auto& v : ph) v = 0.0;
        for (auto& v : phFast) v = 0.0;
        for (auto& c : apState) for (auto& s : c) s = 0.0f;
        for (int c = 0; c < 2; ++c) { fb[c] = 0; toneLp[c].reset(); bbd[c].reset(); hpA[c].reset(); hpB[c].reset(); }
        for (auto& r : rnd) r = {};
        seed = 0x12345u;
        envF = envS = 0.0f;
        lastBass = -1.0f; first = true;
        for (auto& v : voicePos) v = 0.0f;
    }

    void setParams (const Params& np, double syncedRateHz /* <=0: free */)
    {
        p = np;
        rate = syncedRateHz > 0.0 ? (float) syncedRateHz : p.rateHz;
        if (std::abs (p.bassMono - lastBass) > 0.5f)
        {
            for (int c = 0; c < 2; ++c) { hpA[c].hp (std::max (10.0f, p.bassMono), fs); hpB[c].hp (std::max (10.0f, p.bassMono), fs); }
            lastBass = p.bassMono;
        }
        // tone tilt: <0 darker (lowpass), >0 brighter (wet high shelf via lp subtraction)
        for (auto& t : toneLp) t.setLP (p.tone < 0 ? 18000.0f * std::pow (0.12f, -p.tone) : 1800.0f, fs);
        for (auto& b : bbd) b.setLP (9500.0f - 5500.0f * p.character, fs);
        outT = std::pow (10.0f, p.outDb / 20.0f);
        if (first) { mixSm = p.mix; depthSm = p.depth; outSm = outT; first = false; }
    }

    // tempo sync: put every voice clock on the host's bar position
    void syncPhase (double phase) noexcept { for (auto& v : ph) v = phase - std::floor (phase); }

    void process (float* L, float* R, int n) noexcept
    {
        const float sm = 1.0f - std::exp (-1.0f / (0.03f * fs));
        const float eA = 1.0f - std::exp (-1.0f / (0.004f * fs)), eR = 1.0f - std::exp (-1.0f / (0.18f * fs)), eS = 1.0f - std::exp (-1.0f / (1.5f * fs));
        for (int i = 0; i < n; ++i)
        {
            mixSm += (p.mix - mixSm) * sm; depthSm += (p.depth - depthSm) * sm; outSm += (outT - outSm) * sm;
            const float xl = L[i], xr = R[i];
            // input envelope (for DUCK and ENV)
            const float a = 0.5f * (std::abs (xl) + std::abs (xr));
            envF += (a - envF) * (a > envF ? eA : eR);
            envS = std::max (envF, envS + (envF - envS) * eS);
            const float envN = std::clamp (envF / (envS + 1e-6f), 0.0f, 1.0f);
            const float depth = std::clamp (depthSm * (1.0f + p.env * (envN * 2.0f - 1.0f)), 0.0f, 1.0f);

            float wl = 0, wr = 0;
            if (p.mode == Phaser) phaser (xl, xr, depth, wl, wr);
            else delays (xl, xr, depth, wl, wr);

            // wet colour: BBD character, tone tilt, bass mono
            if (p.character > 0.001f && (p.mode == Juno || p.mode == Dimension || p.mode == Ensemble || p.mode == Chorus))
            {
                const float d = 1.0f + 2.0f * p.character;
                wl = bbd[0].lp (std::tanh (wl * d) / d); wr = bbd[1].lp (std::tanh (wr * d) / d);
            }
            if (p.tone < -0.01f) { wl = toneLp[0].lp (wl); wr = toneLp[1].lp (wr); }
            else if (p.tone > 0.01f) { wl += p.tone * 1.2f * (wl - toneLp[0].lp (wl)); wr += p.tone * 1.2f * (wr - toneLp[1].lp (wr)); }
            if (p.bassMono >= 20.0f) { wl = hpB[0].process (hpA[0].process (wl)); wr = hpB[1].process (hpA[1].process (wr)); }

            const float duckG = 1.0f - p.duck * 0.85f * envN;
            // 0% = dry, 50% = dry + wet at full level (the classic chorus / flanger / phaser balance), 100% = wet only
            const float wetG = std::min (1.0f, 2.0f * mixSm) * duckG, dryG = std::min (1.0f, 2.0f * (1.0f - mixSm));
            L[i] = (xl * dryG + wl * wetG) * outSm;
            R[i] = (xr * dryG + wr * wetG) * outSm;
        }
    }

    // UI: normalised positions of the modulated voices (0..1), and how many
    float voicePos[kMaxVoices] {};
    int voiceCount = 0;
    float phaserNotch[2] {};

private:
    // ---------- LFO ----------
    inline float lfo (double phase) noexcept
    {
        const float x = (float) (phase - std::floor (phase));
        switch (p.shape)
        {
            case 1: return 4.0f * std::abs (x - 0.5f) - 1.0f;           // triangle
            default: return std::sin (2.0f * kPi * x);
        }
    }
    struct Rnd { float cur = 0, target = 0, step = 0; int cnt = 0; };
    inline float randomWalk (Rnd& r, float speedHz) noexcept
    {
        if (--r.cnt <= 0)
        {
            seed = seed * 1664525u + 1013904223u;
            r.target = ((seed >> 9) * (1.0f / 8388608.0f)) * 2.0f - 1.0f;
            r.cnt = std::max (8, (int) (fs / std::max (0.05f, speedHz)));
            r.step = (r.target - r.cur) / (float) r.cnt;
        }
        r.cur += r.step;
        return r.cur;
    }
    inline void advance (double& phase, float hz) noexcept { phase += hz / fs; if (phase >= 1.0) phase -= 1.0; }
    inline float msToS (float ms) const noexcept { return ms * 0.001f * fs; }

    // ---------- delay-line machines ----------
    void delays (float xl, float xr, float depth, float& wl, float& wr) noexcept
    {
        const float mid = 0.5f * (xl + xr);
        const float drift = p.drift;
        int nv = 1;
        float base[kMaxVoices], dep[kMaxVoices], pan[kMaxVoices], gain[kMaxVoices], phOff[kMaxVoices], rmul[kMaxVoices];
        int src[kMaxVoices];   // 0 mid, 1 left, 2 right
        bool fbOn = false;
        for (int v = 0; v < kMaxVoices; ++v) { base[v] = 10; dep[v] = 2; pan[v] = 0; gain[v] = 1; phOff[v] = 0; rmul[v] = 1; src[v] = 0; }
        switch (p.mode)
        {
            case Chorus:
            {
                const int var = p.variant;
                nv = var == 1 ? 3 : var == 2 ? 8 : var == 3 ? 4 : std::clamp (p.voices, 1, kMaxVoices);
                const float d0 = var == 3 ? 4.0f + 6.0f * p.delay : 6.0f + 24.0f * p.delay;
                for (int v = 0; v < nv; ++v)
                {
                    const float t = nv == 1 ? 0.5f : (float) v / (float) (nv - 1);
                    base[v] = d0 * (0.75f + 0.5f * t);
                    dep[v] = (var == 3 ? 0.6f : 1.0f) * (0.4f + 5.6f * depth) * (var == 2 ? 1.2f : 1.0f);
                    pan[v] = nv == 1 ? 0.0f : (t * 2.0f - 1.0f) * (v % 2 ? -1.0f : 1.0f) * p.width;
                    phOff[v] = (float) v / (float) nv;
                    rmul[v] = (var == 3 ? 4.0f : 1.0f) * (1.0f + 0.11f * (float) v);
                    gain[v] = 1.4f / std::sqrt ((float) nv);
                }
                break;
            }
            case Juno:
            {
                // two BBD lines in anti-phase, triangle LFO; I = 0.5 Hz, II = 0.83 Hz, I+II = fast & shallow
                nv = 2;
                static const float rateMul[4] { 1.0f, 1.65f, 15.0f, 0.8f }, depMs[4] { 1.85f, 1.85f, 0.25f, 2.6f };
                const int var = std::clamp (p.variant, 0, 3);
                for (int v = 0; v < 2; ++v)
                {
                    base[v] = 3.5f + 2.0f * p.delay; dep[v] = depMs[var] * (0.3f + 1.4f * depth);
                    phOff[v] = v * 0.5f; rmul[v] = rateMul[var]; pan[v] = v == 0 ? -p.width : p.width;
                }
                break;
            }
            case Dimension:
            {
                // two lines, opposite triangle LFO, wet cross-fed in anti-phase: wide but very gentle
                nv = 2;
                const int var = std::clamp (p.variant, 0, 3);
                const float dms = 0.25f + 0.35f * (float) var;
                for (int v = 0; v < 2; ++v)
                {
                    base[v] = 2.0f + 2.5f * p.delay; dep[v] = dms * (0.5f + depth); phOff[v] = v * 0.5f;
                    rmul[v] = 0.5f + 0.25f * (float) var; pan[v] = v == 0 ? -1.0f : 1.0f; gain[v] = 0.9f;
                }
                break;
            }
            case Ensemble:
            {
                nv = 3;
                for (int v = 0; v < 3; ++v)
                {
                    base[v] = 5.0f + 6.0f * p.delay; dep[v] = 0.5f + 2.5f * depth; phOff[v] = v / 3.0f;
                    rmul[v] = 1.0f; pan[v] = (v - 1) * p.width; gain[v] = 0.85f;
                }
                break;
            }
            case Flanger:
            {
                nv = 2; fbOn = true;
                const int var = std::clamp (p.variant, 0, 3);
                for (int v = 0; v < 2; ++v)
                {
                    base[v] = 0.25f + 6.0f * p.delay * p.delay + (var == 2 ? 0.5f : 0.0f);
                    dep[v] = base[v] * (0.2f + 0.78f * depth);
                    phOff[v] = v == 0 ? 0.0f : 0.5f * p.width * (var == 3 ? 1.0f : 0.5f);
                    src[v] = v == 0 ? 1 : 2; pan[v] = v == 0 ? -1.0f : 1.0f;
                    gain[v] = var == 1 ? -1.0f : 1.0f;
                }
                break;
            }
            case Vibrato:
            {
                nv = 2;
                for (int v = 0; v < 2; ++v)
                {
                    base[v] = 1.0f + 6.0f * depth; dep[v] = 0.1f + 5.5f * depth; src[v] = v == 0 ? 1 : 2; pan[v] = v == 0 ? -1.0f : 1.0f;
                    phOff[v] = p.variant == 1 ? v * 0.25f : v * 0.5f * p.width * 0.2f;
                }
                break;
            }
            default: break;
        }
        // input into the lines (flanger has its own feedback per side)
        const float fbAmt = fbOn ? std::clamp (p.feedback * (p.variant == 2 ? 1.0f : 0.95f), -0.97f, 0.97f) : (p.mode == Chorus ? 0.5f * p.feedback : 0.0f);
        dl[0].push (xl + fb[0] * fbAmt);
        dl[1].push (xr + fb[1] * fbAmt);
        dl[2].push (mid + 0.5f * (fb[0] + fb[1]) * fbAmt);

        // shared clocks: one per voice so their rates can differ slightly
        float outL = 0, outR = 0, fbL = 0, fbR = 0;
        voiceCount = nv;
        for (int v = 0; v < nv; ++v)
        {
            float r = rate * rmul[v];
            if (p.mode == Vibrato) r *= p.variant == 2 ? 0.25f : p.variant == 3 ? 1.6f : 1.0f;
            if (drift > 0.001f) r *= 1.0f + 0.25f * drift * randomWalk (rnd[v], 0.7f);
            advance (ph[v], r);
            const double phs = ph[v] + phOff[v];
            float m = lfo (phs);
            if (p.shape == 2) m = randomWalk (rnd[kMaxVoices + v], r * 2.0f);
            if (p.mode == Ensemble)
            {
                static const float fastAmt[4] { 0.25f, 0.5f, 0.1f, 0.3f };
                const float fa = fastAmt[std::clamp (p.variant, 0, 3)];
                advance (phFast[v], r * 9.3f);   // own clock: no jump when the slow LFO wraps
                m = (1.0f - fa) * m + fa * std::sin (2.0f * kPi * (float) (phFast[v] + phOff[v]));
            }
            // flanger sweeps downwards from `base` (its notches move through the whole range), the rest swing around it
            float dMs = p.mode == Flanger ? base[v] - dep[v] * 0.5f * (1.0f - m) : base[v] + dep[v] * 0.5f * m;
            if (drift > 0.001f) dMs += 0.3f * drift * randomWalk (rnd[2 * kMaxVoices + v], 0.3f);
            const float ds = std::max (2.0f, msToS (std::max (0.02f, dMs)));
            voicePos[v] = std::clamp (dMs / 30.0f, 0.0f, 1.0f);
            const auto& line = dl[src[v] == 1 ? 0 : src[v] == 2 ? 1 : 2];
            float s = line.readCubic (ds) * gain[v];
            if (p.mode == Vibrato && p.variant == 1) s *= 1.0f - 0.25f * depth * (0.5f + 0.5f * m);   // rotary AM
            const float pl = std::sqrt (0.5f * (1.0f - pan[v])), pr = std::sqrt (0.5f * (1.0f + pan[v]));
            outL += s * pl; outR += s * pr;
            if (src[v] == 1) fbL = s; else if (src[v] == 2) fbR = s; else { fbL += s * pl; fbR += s * pr; }
        }
        if (p.mode == Dimension) { const float a = outL, b = outR; outL = a - 0.35f * b; outR = b - 0.35f * a; }
        fb[0] = std::clamp (fbL, -4.0f, 4.0f); fb[1] = std::clamp (fbR, -4.0f, 4.0f);
        const float norm = p.mode == Chorus || p.mode == Ensemble ? 1.0f : 1.41421356f;
        wl = outL * norm; wr = outR * norm;
    }

    // ---------- phaser ----------
    void phaser (float xl, float xr, float depth, float& wl, float& wr) noexcept
    {
        static const int stagesFor[4] { 4, 8, 12, 16 };
        const int st = stagesFor[std::clamp (p.variant, 0, 3)];
        float r = rate;
        if (p.drift > 0.001f) r *= 1.0f + 0.25f * p.drift * randomWalk (rnd[0], 0.7f);
        advance (ph[0], r);
        voiceCount = 0;
        const float centre = 150.0f * std::pow (40.0f, p.delay);          // 150 Hz .. 6 kHz
        for (int c = 0; c < 2; ++c)
        {
            double phs = ph[0] + (c == 1 ? 0.5 * p.width : 0.0);
            float m = p.shape == 2 ? randomWalk (rnd[1 + c], r * 2.0f) : lfo (phs);
            const float f = std::clamp (centre * std::pow (2.0f, 2.8f * depth * m), 30.0f, 0.45f * fs);
            phaserNotch[c] = f;
            const float t = std::tan (kPi * f / fs), a = (t - 1.0f) / (t + 1.0f);
            float x = (c == 0 ? xl : xr) + fb[c] * std::clamp (p.feedback, -0.95f, 0.95f);
            for (int s = 0; s < st; ++s)
            {
                const float y = a * x + apState[c][s];
                apState[c][s] = x - a * y;
                x = y;
            }
            fb[c] = x;
            (c == 0 ? wl : wr) = x;
        }
    }

    Params p;
    float fs = 48000, rate = 0.8f;
    lumen::DelayLine dl[3];
    double ph[kMaxVoices] {}, phFast[kMaxVoices] {};
    float fb[2] {}, apState[2][16] {};
    Rnd rnd[3 * kMaxVoices];
    uint32_t seed = 0x12345u;
    OnePole toneLp[2], bbd[2];
    Bq hpA[2], hpB[2];
    float envF = 0, envS = 0, mixSm = 0.5f, depthSm = 0.5f, outT = 1, outSm = 1, lastBass = -1;
    bool first = true;
};

} // namespace lumen::mod
