#pragma once
// Lumen Kick 2 - layered kick engine.
//   Three layers: TOP (click), MID (body / punch), LOW (sub). Each layer = synth + sample (both optional),
//   with its own amp envelope, nudge and phase. A loaded kick is split into the three bands with
//   zero-phase, perfectly reconstructing crossovers (low + mid + top == original, sample for sample).
//   Master: EQ -> drive (2x oversampled) -> clipper -> output.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>
#include <array>
#include <vector>
#include <memory>
#include <random>

namespace lumen::kick2
{
constexpr double kPi = 3.14159265358979323846;
enum Layer { Top, Mid, Low, kLayers };
enum TopType { Click, Noise, Snap, Tick };
enum DriveType { DriveOff, Tube, HardClip, Fold, Crush };
enum KeyTrack { TrackOff, TrackSub, TrackAll };

inline float dbToGain (float db) { return db <= -59.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }
inline double midiHz (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); }

// ------------------------------------------------------------------------------------ sample data
struct SampleData
{
    juce::AudioBuffer<float> full;                 // original (trimmed, normalised), at `rate`
    std::array<juce::AudioBuffer<float>, 3> band;  // zero-phase band splits (Top / Mid / Low), same length + pad
    double rate = 48000;
    int pad = 0;                                   // band buffers start `pad` samples early (zero-phase pre-ringing room)
    juce::String name;
    juce::File file;
    float lowHz = 0;                               // detected pitch of the low band
    float x1 = 150, x2 = 2500;                     // crossovers used for the split
};

// zero-phase 3-band split: complementary raised-cosine masks (1 octave wide) in the frequency domain
inline void splitBands (SampleData& s, float x1, float x2)
{
    const int len = s.full.getNumSamples(), nch = s.full.getNumChannels();
    s.pad = (int) (0.006 * s.rate);
    const int need = len + 2 * s.pad + (int) (0.05 * s.rate);
    int order = 10; while ((1 << order) < need) ++order;
    const int N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2);
    auto lowMask = [] (double f, double xc) { if (f <= 0) return 1.0; const double t = std::clamp (std::log2 (f / xc) / 0.5, -1.0, 1.0); return 0.5 * (1.0 - std::sin (kPi * 0.5 * t)); };
    for (auto& b : s.band) b.setSize (nch, len + s.pad, false, true, false);
    for (int c = 0; c < nch; ++c)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < len; ++i) buf[(size_t) (i + s.pad)] = s.full.getSample (c, i);
        fft.performRealOnlyForwardTransform (buf.data());
        std::vector<float> spec (buf);
        for (int k = 0; k < 3; ++k)
        {
            std::vector<float> b2 (spec);
            for (int bin = 0; bin <= N / 2; ++bin)
            {
                const double f = (double) bin * s.rate / N;
                const double l1 = lowMask (f, x1), l2 = lowMask (f, x2);
                const double m = k == Low ? l1 : k == Mid ? (l2 - l1) : (1.0 - l2);
                b2[(size_t) bin * 2] *= (float) m; b2[(size_t) bin * 2 + 1] *= (float) m;
            }
            fft.performRealOnlyInverseTransform (b2.data());
            for (int i = 0; i < len + s.pad; ++i) s.band[(size_t) k].setSample (c, i, b2[(size_t) i]);
        }
    }
    s.x1 = x1; s.x2 = x2;
}

// ------------------------------------------------------------------------------------ parameters
struct LayerParams
{
    bool on = true, invert = false, filter = true, solo = false;
    float synLvl = 1, smpLvl = 0, smpPitch = 0, smpStartMs = 0, nudgeMs = 0, attackMs = 0, decayMs = 300, curve = 0.4f;
    // synth
    float startSt = 36, pDecayMs = 40, drive = 0;      // LOW / MID
    float freq = 180, wave = 0;                        // MID
    int type = Click; float tone = 4000, tDecayMs = 8; // TOP
};
struct Params
{
    std::array<LayerParams, 3> L;
    int keyTrack = TrackSub, root = 29;                // F1
    float velSens = 0.5f, pitch = 0;
    int drvType = DriveOff; float drvAmt = 0;
    float eqLow = 0, eqMidF = 800, eqMidG = 0, eqHigh = 0;
    bool clip = true; float outDb = 0;
};

// ------------------------------------------------------------------------------------ small DSP
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    void setShelf (bool high, double f, double g, double fs)
    {
        const double A = std::pow (10.0, g / 40.0), w = 2 * kPi * f / fs, cw = std::cos (w), sw = std::sin (w), al = sw / 2 * std::sqrt (2.0);
        const double sa = 2 * std::sqrt (A) * al;
        double a0;
        if (! high)
        {
            a0 = (A + 1) + (A - 1) * cw + sa;
            b0 = A * ((A + 1) - (A - 1) * cw + sa) / a0; b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0; b2 = A * ((A + 1) - (A - 1) * cw - sa) / a0;
            a1 = -2 * ((A - 1) + (A + 1) * cw) / a0; a2 = ((A + 1) + (A - 1) * cw - sa) / a0;
        }
        else
        {
            a0 = (A + 1) - (A - 1) * cw + sa;
            b0 = A * ((A + 1) + (A - 1) * cw + sa) / a0; b1 = -2 * A * ((A - 1) + (A + 1) * cw) / a0; b2 = A * ((A + 1) + (A - 1) * cw - sa) / a0;
            a1 = 2 * ((A - 1) - (A + 1) * cw) / a0; a2 = ((A + 1) - (A - 1) * cw - sa) / a0;
        }
    }
    void setBell (double f, double g, double q, double fs)
    {
        const double A = std::pow (10.0, g / 40.0), w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al / A;
        b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0; a1 = -2 * c / a0; a2 = (1 - al / A) / a0;
    }
    inline float process (float x, int c) noexcept
    {
        const double y = b0 * x + z1[c]; z1[c] = b1 * x - a1 * y + z2[c]; z2[c] = b2 * x - a2 * y; return (float) y;
    }
};
struct Svf
{
    double a1 = 0, a2 = 0, a3 = 0, k = 1, ic1 = 0, ic2 = 0;
    void set (double fc, double fs, double q) { const double g = std::tan (kPi * std::clamp (fc, 10.0, 0.45 * fs) / fs); k = 1.0 / q; a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    void reset() { ic1 = ic2 = 0; }
    inline void tick (double v0, double& lp, double& bp, double& hp) noexcept
    {
        const double v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
};

// ------------------------------------------------------------------------------------ one kick hit
class Voice
{
public:
    // fsBase = host rate; the synth runs osf times faster (no aliasing), samples play at the host rate (bit-exact)
    void start (const Params& p, double fsBase, int note, float vel, int osf = 1, double synthDelay = 0)
    {
        osr = std::max (1, osf); fs = fsBase; fsY = fs * osr;
        line[0] = line[1] = Line {};
        line[1].t = -synthDelay;
        fadeOut = false;
        impLeft = osr; noiseGain = std::sqrt ((float) osr);
        velGain = 1.0f - p.velSens * (1.0f - std::clamp (vel, 0.0f, 1.0f));
        const double gp = std::pow (2.0, p.pitch / 12.0);
        const double noteRatio = std::pow (2.0, (note - p.root) / 12.0);
        lowEnd = midiHz (p.keyTrack != TrackOff ? note : p.root) * gp;
        midRatio = (p.keyTrack == TrackAll ? noteRatio : 1.0) * gp;
        smpRatio = p.keyTrack == TrackAll ? noteRatio * gp : gp;
        for (int k = 0; k < 3; ++k) { ph[k] = 0; pos[k] = -1; }
        rng.seed (12345);                                   // the same click every hit (like a sample)
        for (auto& f : topF) f.reset();
        const auto& tp = p.L[Top];
        topF[0].set (tp.tone, fsY, tp.type == Tick ? 4.0 : 0.9);
        topF[1].set (tp.tone, fsY, 0.7);
    }
    // gentle at low settings, real saturation only towards 100 % (35 % = warm, not a square wave)
    static inline float driveGain (float drive) noexcept { return 1.0f + 6.0f * drive * drive; }
    void release() { fadeOut = true; }
    bool isActive() const { return ! (line[0].done && line[1].done); }

    // amp envelope: linear attack, then (1-x)^p decay that ends exactly at attack + decay;
    // curve 0 = flat (a sample plays untouched), higher = faster fade; 3 ms fade at the very end (no click)
    static inline float envAt (const LayerParams& lp, double ms) noexcept
    {
        if (ms < lp.attackMs) return (float) (ms / std::max (0.01, (double) lp.attackMs));
        const double left = lp.attackMs + lp.decayMs - ms;
        return (float) (std::pow (1.0 - (ms - lp.attackMs) / std::max (1.0, (double) lp.decayMs), lp.curve * 5.4) * std::min (1.0, left / 3.0));
    }

    // SAMPLE part: one host-rate sample (called once per host sample)
    inline void tickSamples (const Params& p, const std::array<const SampleData*, 3>& smp, float& l, float& r, float* layerOut) noexcept
    {
        tickLine (0, p, 1.0, l, r, layerOut, [&] (int k, const LayerParams& lp, double, float* out)
        {
            if (lp.smpLvl > 0.0f && smp[(size_t) k] != nullptr) addSample (k, lp, *smp[(size_t) k], out);
        });
    }
    // SYNTH part: one oversampled sample (called osf times per host sample)
    inline void tickSynth (const Params& p, float& l, float& r, float* layerOut) noexcept
    {
        tickLine (1, p, 1.0 / osr, l, r, layerOut, [&] (int k, const LayerParams& lp, double ms, float* out)
        {
            if (lp.synLvl > 0.0f) out[0] = out[1] = synth (k, lp, ms) * lp.synLvl;
        });
    }

    double lowFreqAt (const Params& p, double ms) const
    {
        const auto& lp = p.L[Low];
        return lowEnd * std::pow (2.0, lp.startSt / 12.0 * std::exp (-ms / std::max (0.5, (double) lp.pDecayMs)));
    }

private:
    struct Line { double t = 0; float fade = 1; bool done = false; };
    // shared timeline logic of the two parts: nudge, envelope, solo / mute, choke fade, end of the hit
    template <typename Fn>
    inline void tickLine (int which, const Params& p, double step, float& l, float& r, float* layerOut, Fn&& gen) noexcept
    {
        auto& ln = line[which];
        if (ln.done) return;
        bool any = false;
        float sum[2] {};
        const bool anySolo = p.L[0].solo || p.L[1].solo || p.L[2].solo;
        const float lw = (float) step;                                   // the synth adds osf sub-samples per display sample
        for (int k = 0; k < 3; ++k)
        {
            const auto& lp = p.L[(size_t) k];
            const double tk = ln.t - lp.nudgeMs * 0.001 * fs;           // host samples since this layer started
            const bool audible = lp.on && (! anySolo || lp.solo);
            if (! audible) continue;
            if (tk < 0) { any = true; continue; }                       // still waiting for its nudge
            const double ms = tk * 1000.0 / fs;
            if (ms >= (double) lp.attackMs + lp.decayMs) continue;
            any = true;
            float out[2] {};
            gen (k, lp, ms, out);
            const float g = envAt (lp, ms) * (lp.invert ? -1.0f : 1.0f);
            out[0] *= g; out[1] *= g;
            sum[0] += out[0]; sum[1] += out[1];
            if (layerOut != nullptr) layerOut[k] += 0.5f * (out[0] + out[1]) * velGain * ln.fade * lw;
        }
        if (fadeOut) { ln.fade -= (float) (step / (0.003 * fs)); if (ln.fade <= 0) { ln.done = true; return; } }
        l += sum[0] * velGain * ln.fade; r += sum[1] * velGain * ln.fade;
        ln.t += step;
        if (! any) ln.done = true;
    }

    inline float synth (int k, const LayerParams& lp, double ms) noexcept
    {
        if (k == Low)
        {
            const double f = lowEnd * std::pow (2.0, lp.startSt / 12.0 * std::exp (-ms / std::max (0.5, (double) lp.pDecayMs)));
            ph[Low] += f / fsY; ph[Low] -= std::floor (ph[Low]);
            float y = (float) std::sin (2.0 * kPi * ph[Low]);
            if (lp.drive > 0.001f) { const float d = driveGain (lp.drive); y = std::tanh (y * d) / std::tanh (d); }
            return y;
        }
        if (k == Mid)
        {
            const double f = lp.freq * midRatio * std::pow (2.0, lp.startSt / 12.0 * std::exp (-ms / std::max (0.5, (double) lp.pDecayMs)));
            ph[Mid] += f / fsY; ph[Mid] -= std::floor (ph[Mid]);
            const double x = ph[Mid];
            const float sine = (float) std::sin (2.0 * kPi * x);
            const float tri = (float) (x < 0.25 ? 4 * x : x < 0.75 ? 2 - 4 * x : 4 * x - 4);
            const float sq = std::tanh (sine * 4.0f);
            float y = lp.wave < 0.5f ? sine + (tri - sine) * lp.wave * 2.0f : tri + (sq - tri) * (lp.wave - 0.5f) * 2.0f;
            if (lp.drive > 0.001f) { const float d = driveGain (lp.drive); y = std::tanh (y * d) / std::tanh (d); }
            return y;
        }
        // Top: click synthesis
        const double sec = ms * 0.001;
        const float impulse = impLeft > 0 ? 1.0f : 0.0f;     // one base-rate sample long, whatever the oversampling
        const float dec = (float) std::exp (-ms / std::max (0.2, (double) lp.tDecayMs));
        float y = 0;
        double lo, bp, hp;
        switch (lp.type)
        {
            case Click:
                y = (float) std::sin (2.0 * kPi * lp.tone * sec) * (float) std::exp (-ms / std::max (0.2, lp.tDecayMs * 0.35)) + impulse;
                break;
            case Noise:
                topF[0].tick (uni (rng) * noiseGain, lo, bp, hp); y = (float) bp * 1.6f * dec;
                break;
            case Snap:
            {
                topF[1].tick (uni (rng) * noiseGain, lo, bp, hp);
                ph[Top] += lp.tone * 0.35 * std::pow (2.0, 2.0 * std::exp (-ms / 1.5)) / fsY; ph[Top] -= std::floor (ph[Top]);
                y = (float) (0.7 * hp + 0.6 * std::sin (2 * kPi * ph[Top])) * dec;
                break;
            }
            case Tick: default:
                topF[0].tick (impulse * 6.0f, lo, bp, hp); y = (float) bp * 2.0f;
                break;
        }
        if (impLeft > 0) --impLeft;
        return y;
    }

    inline void addSample (int k, const LayerParams& lp, const SampleData& sd, float* out) noexcept
    {
        const auto& buf = lp.filter ? sd.band[(size_t) k] : sd.full;
        const int off = lp.filter ? sd.pad : 0;          // band buffers carry `pad` samples of pre-ringing before the onset
        const int n = buf.getNumSamples();
        if (pos[k] < 0) pos[k] = lp.smpStartMs * 0.001 * sd.rate;
        const double step = sd.rate / fs * smpRatio * std::pow (2.0, lp.smpPitch / 12.0);
        const double p = pos[k];
        pos[k] += step;
        // cubic Hermite interpolation
        const double fr = p - std::floor (p);
        const int base = (int) std::floor (p) + off;
        if (base + 2 >= n) return;
        const int nch = buf.getNumChannels();
        for (int c = 0; c < 2; ++c)
        {
            const float* d = buf.getReadPointer (std::min (c, nch - 1));
            const float y0 = base - 1 >= 0 ? d[base - 1] : 0.0f, y1 = base >= 0 ? d[base] : 0.0f, y2 = d[base + 1], y3 = d[base + 2];
            const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
            out[c] += (float) (((c3 * fr + c2) * fr + c1) * fr + y1) * lp.smpLvl;
        }
    }

    double fs = 48000, fsY = 48000, lowEnd = 43.65, midRatio = 1, smpRatio = 1;
    double ph[3] {}, pos[3] { -1, -1, -1 };
    Line line[2] { { 0, 1, true }, { 0, 1, true } };
    int osr = 1;
    bool fadeOut = false;
    float velGain = 1, noiseGain = 1; int impLeft = 0;
    std::minstd_rand rng; std::uniform_real_distribution<float> uni { -1.0f, 1.0f };
    Svf topF[2];
};

// ------------------------------------------------------------------------------------ the engine
class Engine
{
public:
    static constexpr int kOsLog2 = 1, kOs = 1 << kOsLog2;   // everything (voices, EQ, drive, clipper) runs at 4x: no aliasing, no grit
    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate; fsOs = fs * kOs;
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) kOsLog2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        maxBlock = std::max (512, maxBlock);
        os->initProcessing ((size_t) maxBlock);
        work.setSize (2, maxBlock);
        // samples go through the up + down filters, the synth (generated at 4x) only through the down filter:
        // up and down are the same linear-phase filters, so the synth starts half the total latency later -> perfectly aligned
        synthDelay = os->getLatencyInSamples() * 0.5;
        for (auto& v : voices) v = Voice();
        for (auto& b : eq) b.reset();
        lastEq = {}; drvSm = 0; outSm = 1;
        crushHold[0] = crushHold[1] = 0; crushCnt = 0;
        dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0;
        dcR = (float) std::exp (-2.0 * kPi * 8.0 / fsOs);
    }
    int latency() const { return os ? (int) std::lround (os->getLatencyInSamples()) : 0; }
    void setParams (const Params& np) { p = np; }
    void setSamples (const std::array<const SampleData*, 3>& s) { smp = s; }

    void noteOn (int note, float vel)
    {
        for (auto& v : voices) if (v.isActive()) v.release();                    // choke the previous hit (3 ms fade)
        Voice* free = nullptr;
        for (auto& v : voices) if (! v.isActive()) { free = &v; break; }
        if (free == nullptr) free = &voices[(size_t) (next++ % voices.size())];
        free->start (p, fs, note, vel, kOs, synthDelay);
    }
    bool anyActive() const { for (auto& v : voices) if (v.isActive()) return true; return false; }

    // renders n samples (adds nothing, overwrites L / R). layerOut: optional [3][n] per-layer (pre-master) buffers
    void render (float* L, float* R, int n, float** layerOut = nullptr)
    {
        updateEq();
        juce::dsp::AudioBlock<float> blk (work);
        auto sub = blk.getSubBlock (0, (size_t) n);
        // 1) samples at the host rate (exactly as recorded) ...
        float* wl = work.getWritePointer (0); float* wr = work.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            float l = 0, r = 0, lo[3] {};
            for (auto& v : voices) v.tickSamples (p, smp, l, r, layerOut != nullptr ? lo : nullptr);
            wl[i] = l; wr[i] = r;
            if (layerOut != nullptr) for (int k = 0; k < 3; ++k) layerOut[k][i] = lo[k];
        }
        // 2) ... upsampled 4x with a linear-phase filter, 3) the synth added at 4x, EQ, drive and clipper at 4x
        auto up = os->processSamplesUp (sub);
        float* ul = up.getChannelPointer (0); float* ur = up.getChannelPointer (1);
        const float drvT = p.drvType == DriveOff ? 0.0f : p.drvAmt;
        const float smC = 1.0f - std::exp (-1.0f / (0.02f * (float) fsOs));
        for (int i = 0; i < n; ++i)
        {
            float lo[3] {};
            for (int k = 0; k < kOs; ++k)
            {
                const int j = i * kOs + k;
                float l = ul[j], r = ur[j];
                for (auto& v : voices) v.tickSynth (p, l, r, layerOut != nullptr ? lo : nullptr);
                l = eq[0].process (l, 0); r = eq[0].process (r, 1);
                l = eq[1].process (l, 0); r = eq[1].process (r, 1);
                l = eq[2].process (l, 0); r = eq[2].process (r, 1);
                drvSm += (drvT - drvSm) * smC;
                ul[j] = shape (l, 0); ur[j] = shape (r, 1);
            }
            if (layerOut != nullptr) for (int k = 0; k < 3; ++k) layerOut[k][i] += lo[k];
        }
        os->processSamplesDown (sub);
        wl = work.getWritePointer (0); wr = work.getWritePointer (1);
        const float outT = std::pow (10.0f, p.outDb / 20.0f);
        for (int i = 0; i < n; ++i)
        {
            outSm += (outT - outSm) * 0.002f;
            L[i] = safety (wl[i] * outSm); R[i] = safety (wr[i] * outSm);
        }
    }

    // offline render of one hit (message thread) - for the display and WAV export
    static void renderHit (const Params& prm, const std::array<const SampleData*, 3>& s, double sr, int note, juce::AudioBuffer<float>& out, std::array<std::vector<float>, 3>* layers)
    {
        Engine e; e.prepare (sr, 512); e.setParams (prm); e.setSamples (s);
        e.noteOn (note, 1.0f);
        const int lat = e.latency();
        const int maxLen = (int) (3.0 * sr) + lat;
        juce::AudioBuffer<float> tmp (2, maxLen); tmp.clear();
        std::array<std::vector<float>, 3> lay; for (auto& v : lay) v.assign ((size_t) maxLen, 0.0f);
        int done = 0, silentRun = 0;
        while (done < maxLen)
        {
            const int n = std::min (512, maxLen - done);
            float* lo[3] { lay[0].data() + done, lay[1].data() + done, lay[2].data() + done };
            e.render (tmp.getWritePointer (0) + done, tmp.getWritePointer (1) + done, n, lo);
            done += n;
            if (! e.anyActive()) { silentRun += n; if (silentRun > lat + 2048) break; }
        }
        // trim the latency and trailing silence
        int end = done; while (end > lat + 1 && std::abs (tmp.getSample (0, end - 1)) < 1.0e-5f && std::abs (tmp.getSample (1, end - 1)) < 1.0e-5f) --end;
        const int len = std::max (1, end - lat);
        out.setSize (2, len);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, 0, tmp, c, lat, len);
        if (layers != nullptr) for (int k = 0; k < 3; ++k) (*layers)[(size_t) k].assign (lay[(size_t) k].begin(), lay[(size_t) k].begin() + std::min ((int) lay[(size_t) k].size(), len));
    }
    double lowFreqAt (double ms) const { Voice v; v.start (p, fs, p.root, 1.0f); return v.lowFreqAt (p, ms); }

private:
    void updateEq()
    {
        const std::array<float, 4> now { p.eqLow, p.eqMidF, p.eqMidG, p.eqHigh };
        if (now == lastEq) return;
        lastEq = now;
        eq[0].setShelf (false, 70.0, p.eqLow, fsOs);
        eq[1].setBell (p.eqMidF, p.eqMidG, 1.0, fsOs);
        eq[2].setShelf (true, 7000.0, p.eqHigh, fsOs);
    }
    // after downsampling: the filter can ring a hair above the clipper's 0.98 - catch it smoothly below 0 dBFS
    static inline float safety (float y) noexcept
    {
        constexpr float kn = 0.95f, ce = 0.999f;
        const float a = std::fabs (y);
        return a > kn ? std::copysign (kn + (ce - kn) * std::tanh ((a - kn) / (ce - kn)), y) : y;
    }
    inline float shape (float x, int c) noexcept
    {
        float y = x;
        if (drvSm > 1.0e-4f)
        {
            const float g = 1.0f + 10.0f * drvSm * std::sqrt (drvSm);      // gentle start, full grit near 100 %
            float s;
            switch (p.drvType)
            {
                case Tube:     s = (std::tanh (x * g + 0.2f) - 0.19737532f) / std::sqrt (g); break;
                case HardClip: s = std::clamp (x * g, -1.0f, 1.0f) / std::sqrt (g); break;
                case Fold:     s = std::sin (x * g * 1.5707963f) / std::sqrt (g); break;
                case Crush:
                {
                    if (c == 0 && ++crushCnt >= kOs * (1 + (int) (drvSm * 10.0f))) crushCnt = 0;
                    if (crushCnt == 0) crushHold[c] = x;
                    const float q = std::pow (2.0f, 12.0f - drvSm * 8.0f);
                    s = std::round (crushHold[c] * q) / q;
                    break;
                }
                default: s = x; break;
            }
            // makeup: keep roughly the same loudness while driving
            const float added = s * (1.0f + drvSm * 1.5f) - x;
            // DC-block only what the drive adds (asymmetric tube saturation can't push the kick off centre;
            // with the drive off the signal stays untouched)
            dcY[c] = added - dcX[c] + dcR * dcY[c]; dcX[c] = added;
            y = x + dcY[c];
        }
        if (p.clip)
        {
            constexpr float ce = 0.98f, kn = 0.7f;
            const float a = std::fabs (y);
            if (a > kn) y = std::copysign (kn + (ce - kn) * std::tanh ((a - kn) / (ce - kn)), y);
        }
        return y;
    }

    double fs = 48000, fsOs = 192000;
    double synthDelay = 0;
    float dcX[2] {}, dcY[2] {}, dcR = 0.9993f;
    Params p;
    std::array<const SampleData*, 3> smp {};
    std::array<Voice, 4> voices;
    unsigned next = 0;
    Biquad eq[3];
    std::array<float, 4> lastEq {};
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> work;
    float drvSm = 0, outSm = 1, crushHold[2] {}; int crushCnt = 0;
};

// ------------------------------------------------------------------------------------ pitch detection (YIN) for the display
inline float detectPitch (const float* x, int n, double fs)
{
    const int dec = std::max (1, (int) std::lround (fs / 6000.0));
    const double fd = fs / dec;
    Svf a, b; a.set (400, fs, 0.707); b.set (400, fs, 0.707);
    std::vector<float> y;
    for (int i = 0; i < n; ++i) { double lo, bp, hp; a.tick (x[i], lo, bp, hp); double lo2; b.tick (lo, lo2, bp, hp); if (i % dec == 0) y.push_back ((float) lo2); }
    const int tauMin = (int) (fd / 400.0), tauMax = (int) (fd / 25.0);
    const int W = (int) y.size() - tauMax - 1;
    if (W < tauMax) return 0;
    double e = 0; for (int j = 0; j < W; ++j) e += (double) y[(size_t) j] * y[(size_t) j];
    if (e / W < 1.0e-9) return 0;
    std::vector<double> d ((size_t) tauMax + 2, 0.0); double run = 0;
    for (int t = 1; t <= tauMax + 1; ++t)
    {
        double s = 0; for (int j = 0; j < W; ++j) { const double df = y[(size_t) j] - y[(size_t) (j + t)]; s += df * df; }
        run += s; d[(size_t) t] = run > 0 ? s * t / run : 1.0;
    }
    int best = -1;
    for (int t = std::max (2, tauMin); t <= tauMax; ++t) if (d[(size_t) t] < 0.2 && d[(size_t) t] <= d[(size_t) t - 1] && d[(size_t) t] <= d[(size_t) t + 1]) { best = t; break; }
    if (best < 0) return 0;
    const double d0 = d[(size_t) best - 1], d1 = d[(size_t) best], d2 = d[(size_t) best + 1], den = d0 - 2 * d1 + d2;
    return (float) (fd / (best + (std::abs (den) > 1e-12 ? 0.5 * (d0 - d2) / den : 0.0)));
}

} // namespace lumen::kick2
