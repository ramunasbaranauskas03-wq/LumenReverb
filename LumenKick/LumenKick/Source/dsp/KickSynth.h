#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>
#include <memory>
#include <atomic>

// =====================================================================================
// Lumen Kick Designer — synth engine
//   SUB   : sine/shape oscillator, exponential pitch sweep (start interval, time, curve), hold + decay amp env,
//           key tracking (note 60 = TUNE), start phase
//   CLICK : Noise / Tick / Snap / Thump transient layer
//   SAMPLE: user sample (pitch, key track, start, decay, high-pass "top only", level)
//   MASTER: drive (ADAA tanh), punch, low shelf, high cut, soft clip, velocity
//   Retrigger = 3 ms crossfade into a fresh voice (no clicks, no stacking)
// =====================================================================================
namespace lumen::kick
{

struct Params
{
    float tuneHz = 50.0f; bool keyTrack = true;
    float pitchSt = 36.0f, pitchMs = 40.0f, pitchCurve = 1.0f;
    float attackMs = 0.0f, holdMs = 60.0f, decayMs = 350.0f, decayCurve = 1.0f;
    float shape = 0.0f, phaseDeg = 90.0f, subDb = 0.0f;
    bool clickOn = true; int clickType = 0; float clickDb = -12.0f, clickMs = 6.0f, clickTone = 0.5f;
    bool sampleOn = true; float sampleDb = 0.0f, sampleSt = 0.0f, sampleStartMs = 0.0f, sampleDecayMs = 0.0f, sampleHp = 20.0f; bool sampleTrack = false;
    float driveDb = 0.0f, punch = 0.0f, lowDb = 0.0f, highCut = 20000.0f; bool clip = true;
    float velSens = 0.5f, outDb = 0.0f;
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process (float x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
    void lp (double fs, double f, double q) { const double w = 2 * juce::MathConstants<double>::pi * std::min (f, fs * 0.49) / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al; b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void hp (double fs, double f, double q) { const double w = 2 * juce::MathConstants<double>::pi * std::min (f, fs * 0.49) / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al; b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void bp (double fs, double f, double q) { const double w = 2 * juce::MathConstants<double>::pi * std::min (f, fs * 0.49) / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al; b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    void peak (double fs, double f, double q, double db) { const double A = std::pow (10.0, db / 40.0), w = 2 * juce::MathConstants<double>::pi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al / A; b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0; a1 = -2 * c / a0; a2 = (1 - al / A) / a0; }
    void lowShelf (double fs, double f, double db) { const double A = std::pow (10.0, db / 40.0), w = 2 * juce::MathConstants<double>::pi * f / fs, c = std::cos (w), s = std::sin (w), al = s / 2 * std::sqrt (2.0), sa = 2 * std::sqrt (A) * al;
        const double a0 = (A + 1) + (A - 1) * c + sa; b0 = A * ((A + 1) - (A - 1) * c + sa) / a0; b1 = 2 * A * ((A - 1) - (A + 1) * c) / a0; b2 = A * ((A + 1) - (A - 1) * c - sa) / a0; a1 = -2 * ((A - 1) + (A + 1) * c) / a0; a2 = ((A + 1) + (A - 1) * c - sa) / a0; }
    void unity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
};

// immutable sample data shared with the audio thread
struct SampleData
{
    juce::AudioBuffer<float> buf;   // 1 or 2 channels
    double rate = 48000;
    juce::String name, path;
};

inline double noteHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

class KickSynth
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& v : voices) v = Voice();
        for (auto& b : postL) b.reset();
        for (auto& b : postR) b.reset();
        satPrev[0] = satPrev[1] = 0;
        lastHash = -1;
    }
    void setParams (const Params& np) noexcept { p = np; }
    void setSample (std::shared_ptr<const SampleData> s) noexcept { std::atomic_store (&sample, std::move (s)); }
    std::shared_ptr<const SampleData> getSample() const noexcept { return std::atomic_load (&sample); }

    void noteOn (int note, float velocity) noexcept
    {
        // fade out whatever is playing, start a fresh voice
        for (auto& v : voices) if (v.active && ! v.fading) { v.fading = true; v.fade = 1.0f; }
        Voice* slot = &voices[0];
        for (auto& v : voices) if (! v.active) { slot = &v; break; }
        if (slot->active) slot = voices[0].fade < voices[1].fade ? &voices[0] : &voices[1];
        *slot = Voice();
        slot->active = true; slot->note = note;
        slot->vel = 1.0f - p.velSens + p.velSens * std::clamp (velocity, 0.0f, 1.0f);
        slot->phase = p.phaseDeg / 360.0;
        slot->seed = seedCounter = seedCounter * 1664525u + 1013904223u;
        const auto s = getSample();
        if (s != nullptr) slot->sPos = std::max (0.0, p.sampleStartMs * 0.001 * s->rate);
        slot->clickBp.bp (fs, 1000.0 + 5000.0 * p.clickTone, 0.9);
        slot->clickHp.hp (fs, 2000.0 + 8000.0 * p.clickTone, 0.7071);
        slot->sHp[0].hp (fs, std::max (20.0f, p.sampleHp), 0.7071); slot->sHp[1] = slot->sHp[0];
        slot->sHp2[0].hp (fs, std::max (20.0f, p.sampleHp), 0.7071); slot->sHp2[1] = slot->sHp2[0];
    }

    void render (float* L, float* R, int n) noexcept
    {
        updatePost();
        const auto s = getSample();
        const double g = std::pow (10.0, p.driveDb / 20.0);
        const double norm = p.driveDb > 0.05f ? 0.9 / std::tanh (g * 0.9) : 1.0;
        const float outG = std::pow (10.0f, p.outDb / 20.0f);
        const float fadeStep = 1.0f / (0.003f * (float) fs);
        for (int i = 0; i < n; ++i)
        {
            float yl = 0, yr = 0;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                float l, r;
                renderVoice (v, s.get(), l, r);
                if (v.fading) { v.fade -= fadeStep; if (v.fade <= 0.0f) { v.active = false; continue; } l *= v.fade; r *= v.fade; }
                yl += l; yr += r;
            }
            // drive (ADAA tanh)
            if (p.driveDb > 0.05f)
            {
                float* ch[2] { &yl, &yr };
                for (int c = 0; c < 2; ++c)
                {
                    const double u = g * *ch[c], d = u - satPrev[c];
                    const double y = std::abs (d) > 1e-6 ? (logcosh (u) - logcosh (satPrev[c])) / d : std::tanh (0.5 * (u + satPrev[c]));
                    satPrev[c] = u; *ch[c] = (float) (y * norm);
                }
            }
            yl = postL[2].process (postL[1].process (postL[0].process (yl)));
            yr = postR[2].process (postR[1].process (postR[0].process (yr)));
            if (p.clip) { yl = softClip (yl); yr = softClip (yr); }
            L[i] = yl * outG; R[i] = yr * outG;
        }
    }
    bool isActive() const noexcept { return voices[0].active || voices[1].active; }

    // current sub frequency curve (for the UI): frequency at time t for note 60
    static double subFreq (const Params& p, double tSec, int note = 60)
    {
        const double fEnd = p.tuneHz * (p.keyTrack ? std::pow (2.0, (note - 60) / 12.0) : 1.0);
        const double tau = std::max (0.5, (double) p.pitchMs) * 0.001;
        const double e = std::exp (-std::pow (tSec / tau, (double) std::clamp (p.pitchCurve, 0.3f, 3.0f)));
        return fEnd * std::pow (2.0, p.pitchSt / 12.0 * e);
    }
    static double ampEnv (const Params& p, double tSec)
    {
        const double at = p.attackMs * 0.001, ho = p.holdMs * 0.001, de = std::max (1.0, (double) p.decayMs) * 0.001;
        if (tSec < at) return tSec / std::max (1e-6, at);
        if (tSec < at + ho) return 1.0;
        const double x = (tSec - at - ho) / de;                 // reaches -60 dB at x = 1
        if (x >= 1.0) return 0.0;
        return std::pow (10.0, -3.0 * std::pow (x, (double) std::clamp (p.decayCurve, 0.3f, 3.0f))) * (1.0 - std::pow (x, 8.0));
    }

private:
    struct Voice
    {
        bool active = false, fading = false; float fade = 1.0f;
        int note = 60; float vel = 1;
        double t = 0, phase = 0, sPos = 0;
        uint32_t seed = 1;
        float clickNoiseEnv = 1;
        Biquad clickBp, clickHp, sHp[2], sHp2[2];
    };

    static double logcosh (double u) { const double a = std::abs (u); return a + std::log1p (std::exp (-2.0 * a)) - 0.6931471805599453; }
    static float softClip (float x) noexcept { const float a = std::abs (x); if (a <= 0.8f) return x; const float s = x < 0 ? -1.0f : 1.0f; return s * (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f)); }

    void renderVoice (Voice& v, const SampleData* s, float& l, float& r) noexcept
    {
        const double tSec = v.t / fs;
        // SUB
        const double f = subFreq (p, tSec, v.note);
        v.phase += f / fs; v.phase -= std::floor (v.phase);
        const double ph = 2.0 * juce::MathConstants<double>::pi * v.phase;
        double osc = std::sin (ph);
        if (p.shape > 0.0f)
        {
            // shape: towards a rounded square (more harmonics, more "knock")
            const double sq = std::tanh (4.0 * std::sin (ph)) / std::tanh (4.0);
            osc += (sq - osc) * p.shape;
        }
        const double env = ampEnv (p, tSec);
        float y = (float) (osc * env) * std::pow (10.0f, p.subDb / 20.0f);
        // CLICK
        if (p.clickOn && tSec < 0.2)
        {
            const float cg = std::pow (10.0f, p.clickDb / 20.0f);
            const double ce = std::exp (-tSec / std::max (0.0003, p.clickMs * 0.001));
            float c = 0;
            switch (p.clickType)
            {
                case 0: c = v.clickHp.process (rnd (v)) * 1.5f; break;                                          // noise
                case 1: c = (float) std::sin (2 * juce::MathConstants<double>::pi * (2000.0 + 4000.0 * p.clickTone) * tSec); break; // tick
                case 2: c = v.clickBp.process (rnd (v)) * 3.0f; break;                                           // snap
                default: c = (float) std::sin (2 * juce::MathConstants<double>::pi * (120.0 + 200.0 * p.clickTone) * tSec); break;  // thump
            }
            y += c * (float) ce * cg;
        }
        l = r = y;
        // SAMPLE
        if (p.sampleOn && s != nullptr && s->buf.getNumSamples() > 1)
        {
            const double ratio = std::pow (2.0, (p.sampleSt + (p.sampleTrack ? (v.note - 60) : 0)) / 12.0) * s->rate / fs;
            const int ns = s->buf.getNumSamples();
            if (v.sPos < ns - 1)
            {
                const int i0 = (int) v.sPos; const float fr = (float) (v.sPos - i0);
                const float sg = std::pow (10.0f, p.sampleDb / 20.0f);
                float de = 1.0f;
                if (p.sampleDecayMs > 0.5f) de = (float) std::pow (10.0, -3.0 * std::min (1.0, tSec / (p.sampleDecayMs * 0.001)));
                if (tSec >= p.sampleDecayMs * 0.001 && p.sampleDecayMs > 0.5f) de = 0.0f;
                for (int c = 0; c < 2; ++c)
                {
                    const float* d = s->buf.getReadPointer (std::min (c, s->buf.getNumChannels() - 1));
                    float sv = d[i0] + (d[std::min (i0 + 1, ns - 1)] - d[i0]) * fr;
                    if (p.sampleHp > 21.0f) sv = v.sHp2[c].process (v.sHp[c].process (sv));
                    (c == 0 ? l : r) += sv * sg * de;
                }
                v.sPos += ratio;
            }
        }
        l *= v.vel; r *= v.vel;
        v.t += 1.0;
        const bool subDone = env <= 0.0 && tSec > 0.01;
        const bool smpDone = ! p.sampleOn || s == nullptr || v.sPos >= s->buf.getNumSamples() - 1 || (p.sampleDecayMs > 0.5f && tSec > p.sampleDecayMs * 0.001);
        if (subDone && smpDone && tSec > 0.21) v.active = false;
    }

    static float rnd (Voice& v) noexcept { v.seed = v.seed * 1664525u + 1013904223u; return (float) (v.seed >> 8) / 8388608.0f - 1.0f; }

    void updatePost()
    {
        const float h = p.punch * 7.1f + p.lowDb * 3.3f + p.highCut * 0.001f;
        if (std::abs (h - lastHash) < 1e-5f) return;
        lastHash = h;
        for (auto* bank : { postL, postR })
        {
            if (std::abs (p.lowDb) > 0.05f) bank[0].lowShelf (fs, 70.0, p.lowDb); else bank[0].unity();
            if (p.punch > 0.01f) bank[1].peak (fs, 160.0, 1.2, 8.0 * p.punch); else bank[1].unity();
            if (p.highCut < 19500.0f) bank[2].lp (fs, p.highCut, 0.7071); else bank[2].unity();
        }
    }

    Params p;
    double fs = 48000;
    Voice voices[2];
    Biquad postL[3], postR[3];
    double satPrev[2] {};
    float lastHash = -1;
    uint32_t seedCounter = 12345u;
    std::shared_ptr<const SampleData> sample;
};

} // namespace lumen::kick
