#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <atomic>

// =====================================================================================
// Lumen Riser engine
//   timeline (AUTO bars / MANUAL rise / MIDI hold / one-shot preview) -> progress p in [0,1] and drop events
//   generator: noise sweep, supersaw riser, Shepard tone, impact, downlifter
//   transition FX on the input: filter sweep, accelerating stutter, reverb wash, width
//   pre-drop gap, level match, club safe (mono lows), shared FDN reverb
// =====================================================================================
namespace lumen::riser
{
constexpr float kPi = 3.14159265358979f;

enum Mode { AutoBars, ManualRise, MidiHold };
enum NoiseFilter { HpSweep, BpSweep, LpRise };
enum FxFilter { FxHp, FxLp, FxBp };
enum Stack { StRoot, StOctave, StPower, StMinor, StMajor };

inline juce::StringArray modeNames()        { return { "Auto (every N bars)", "Manual (automate RISE)", "MIDI (hold = build)" }; }
inline juce::StringArray lengthNames()      { return { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }; }
inline juce::StringArray periodNames()      { return { "4 bars", "8 bars", "16 bars", "32 bars", "64 bars" }; }
inline juce::StringArray gapNames()         { return { "Off", "1/16", "1/8", "1 beat", "2 beats", "1 bar" }; }
inline juce::StringArray noiseFilterNames() { return { "HP Sweep", "BP Sweep", "LP Rise" }; }
inline juce::StringArray fxFilterNames()    { return { "High-pass", "Low-pass", "Band-pass" }; }
inline juce::StringArray stackNames()       { return { "Root", "Octaves", "Power (1-5-8)", "Minor", "Major" }; }
inline juce::StringArray rollNames()        { return { "to 1/16", "to 1/32", "to 1/64" }; }
inline juce::StringArray downNames()        { return { "1/2 bar", "1 bar", "2 bars", "4 bars" }; }
inline double lengthBars (int i) { static const double v[] { 1, 2, 4, 8, 16 }; return v[std::clamp (i, 0, 4)]; }
inline double periodBars (int i) { static const double v[] { 4, 8, 16, 32, 64 }; return v[std::clamp (i, 0, 4)]; }
inline double gapBeats (int i)   { static const double v[] { 0, 0.25, 0.5, 1.0, 2.0, 4.0 }; return v[std::clamp (i, 0, 5)]; }
inline double downBars (int i)   { static const double v[] { 0.5, 1, 2, 4 }; return v[std::clamp (i, 0, 3)]; }

struct Params
{
    int mode = AutoBars; float rise = 0; int length = 3, period = 2, offset = 0; float curve = 0.3f; int gap = 0;
    int key = 9;
    bool noiseOn = true; float noiseLvl = 0.5f; bool pink = false; int noiseFilter = HpSweep; float noiseRes = 0.3f;
    bool synthOn = true; float synthLvl = 0.4f; int stack = StOctave, synthOct = 3; float synthPitch = 12, detune = 0.5f;
    bool shepOn = false; float shepLvl = 0.4f, shepSpeed = 0.25f;
    bool impactOn = true; float impactLvl = 0.7f, impactDecay = 1.2f;
    bool downOn = false; float downLvl = 0.4f; int downLen = 1;
    float space = 0.4f, size = 0.6f; bool tailCut = false;
    bool fxOn = true; int fxFilter = FxHp; float fxAmt = 0.6f, fxRes = 0.3f, stutter = 0.0f; int roll = 1; float fxVerb = 0.3f, width = 0.3f, fxMix = 1.0f;
    bool levelMatch = true, clubSafe = true; float genLvl = 1.0f, outGain = 1.0f;
    // render helpers: mute layers
    bool genOnly = false;
};

// ------------------------------------------------------------------------------------
// TPT state-variable filter (Zavalishin)
struct Svf
{
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    void set (float fs, float fc, float q)
    {
        fc = std::clamp (fc, 10.0f, 0.49f * fs);
        g = std::tan (kPi * fc / fs); k = 1.0f / std::max (0.3f, q);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    // returns lp, bp, hp
    inline void tick (float x, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

// 2nd-order Butterworth high-pass (biquad), cascaded twice = LR4
struct Hp2
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set (double fs, double fc)
    {
        const double K = std::tan (juce::MathConstants<double>::pi * fc / fs), q = 0.7071067811865476, n = 1.0 / (1.0 + K / q + K * K);
        b0 = n; b1 = -2 * n; b2 = n; a1 = 2 * (K * K - 1) * n; a2 = (1 - K / q + K * K) * n;
    }
    void reset() { z1 = z2 = 0; }
    inline float tick (float x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
};

// 8-line FDN reverb, stereo out
class Fdn
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        static const float baseMs[8] { 31.7f, 37.1f, 41.3f, 47.9f, 53.3f, 59.9f, 67.1f, 73.7f };
        for (int i = 0; i < 8; ++i) { line[(size_t) i].assign ((size_t) (fs * baseMs[i] * 0.0016f) + 4, 0.0f); pos[i] = 0; lp[i] = 0; }
        // input diffusers
        static const float apMs[4] { 4.7f, 3.6f, 12.7f, 9.3f };
        for (int i = 0; i < 4; ++i) { ap[(size_t) i].assign ((size_t) (fs * apMs[i] * 0.001f) + 2, 0.0f); apPos[i] = 0; }
        setSize (0.6f);
    }
    void setSize (float s)
    {
        static const float baseMs[8] { 31.7f, 37.1f, 41.3f, 47.9f, 53.3f, 59.9f, 67.1f, 73.7f };
        const float scale = 0.6f + 1.0f * s;                       // line lengths
        const float rt = 1.2f + 7.0f * s * s;                       // seconds
        for (int i = 0; i < 8; ++i)
        {
            len[i] = std::min ((int) line[(size_t) i].size() - 1, std::max (8, (int) (fs * baseMs[i] * 0.001f * scale)));
            fb[i] = std::pow (10.0f, -3.0f * (float) len[i] / (float) fs / rt);
        }
        damp = 0.25f + 0.35f * (1.0f - s);
    }
    void reset() { for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f); for (auto& a : ap) std::fill (a.begin(), a.end(), 0.0f); std::fill (std::begin (lp), std::end (lp), 0.0f); }
    inline void tick (float in, float duck, float& outL, float& outR) noexcept
    {
        // diffusion
        for (int i = 0; i < 4; ++i)
        {
            auto& b = ap[(size_t) i]; const int n = (int) b.size();
            const float d = b[(size_t) apPos[i]], v = in + 0.6f * d;
            b[(size_t) apPos[i]] = v; in = d - 0.6f * v;
            if (++apPos[i] >= n) apPos[i] = 0;
        }
        float o[8];
        for (int i = 0; i < 8; ++i)
        {
            const int n = len[i]; int rp = pos[i] - n; if (rp < 0) rp += (int) line[(size_t) i].size();
            float v = line[(size_t) i][(size_t) rp];
            lp[i] += (v - lp[i]) * (1.0f - damp); o[i] = lp[i];
        }
        // Hadamard 8
        float h[8];
        for (int i = 0; i < 8; ++i) h[i] = o[i];
        for (int s = 1; s < 8; s <<= 1)
            for (int i = 0; i < 8; i += 2 * s)
                for (int j = i; j < i + s; ++j) { const float a = h[j], b = h[j + s]; h[j] = a + b; h[j + s] = a - b; }
        const float norm = 0.35355339f;
        for (int i = 0; i < 8; ++i)
        {
            auto& l = line[(size_t) i];
            l[(size_t) pos[i]] = h[i] * norm * fb[i] * duck + in * (i & 1 ? -0.5f : 0.5f);
            if (++pos[i] >= (int) l.size()) pos[i] = 0;
        }
        outL = (o[0] - o[2] + o[4] - o[6] + o[1] * 0.5f) * 0.5f;
        outR = (o[1] - o[3] + o[5] - o[7] + o[2] * 0.5f) * 0.5f;
    }
private:
    double fs = 48000;
    std::array<std::vector<float>, 8> line; int pos[8] {}, len[8] {}; float fb[8] {}, lp[8] {}; float damp = 0.4f;
    std::array<std::vector<float>, 4> ap; int apPos[4] {};
};

// ------------------------------------------------------------------------------------
class RiserEngine
{
public:
    enum State { Idle, Building, Post };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        verb.prepare (fs);
        for (auto& h : csHp) h.set (fs, 150.0);
        reset();
    }
    void reset()
    {
        verb.reset();
        for (auto& f : nf) f.reset();
        for (auto& f : sf) f.reset();
        for (auto& f : fx) f.reset();
        for (auto& f : burst) f.reset();
        for (auto& f : dnf) f.reset();
        for (auto& h : csHp) h.reset();
        for (int i = 0; i < kSaws; ++i) sawPh[i] = rnd01();
        for (auto& a : pk) for (auto& v : a) v = 0;
        state = Idle; p = 0; pe = 0; prevPhase = -1; armed = false; riseSm = 0;
        postT = 1e9; postBeats = 0; impT = 1e9; downT = 1e9;
        midiHeld = 0; holdBuild = false; buildBeats = 0; previewing = false;
        gGen = 0; gFx = 0; gGap = 1; gStut = 1; lmGain = 1; msDry = msWet = 1e-6f; duck = 1;
        shepX = 0; boomPh = 0; downPh = 0;
        anchorOk = false; now = 0; freeBeat = 0;
        stutDiv = 0;
    }
    void setParams (const Params& np) { prm = np; verb.setSize (prm.size); }
    void preview() { previewReq.store (true); }
    void triggerDrop() { dropReq.store (true); }

    // in-place stereo processing. beat = host ppq of sample 0, bps = beats per sample, bpb = beats per bar
    void process (float* L, float* R, int n, double beat, double bps, bool playing, double bpb, const juce::MidiBuffer& midi)
    {
        if (bps <= 0) bps = 2.0 / fs;
        if (playing)
        {
            const double predicted = anchorBeat + (double) (now - anchorS) * anchorBps;
            if (! anchorOk || std::abs (bps - anchorBps) > 1e-12 || std::abs (beat - predicted) > 1e-4) { anchorBeat = beat; anchorS = now; anchorBps = bps; anchorOk = true; }
        }
        else anchorOk = false;
        if (previewReq.exchange (false)) startBuild (true);
        if (dropReq.exchange (false)) drop();
        auto it = midi.begin();
        const float rootHz = 440.0f * std::pow (2.0f, (float) (prm.key - 9) / 12.0f);   // A4-relative pitch class
        updateRateCoeffs();
        for (int i = 0; i < n; ++i)
        {
            while (it != midi.end() && (*it).samplePosition <= i) { handleMidi ((*it).getMessage()); ++it; }
            const double b = playing ? anchorBeat + (double) (now - anchorS) * anchorBps : freeBeat;
            timeline (b, bps, playing, bpb);
            float gl = 0, gr = 0;
            renderGen (gl, gr, rootHz, bps);
            // ---- transition FX on the input
            const float inL = L[i], inR = R[i];
            float wl = inL, wr = inR, send = 0;
            gFx += ((prm.fxOn && pe > 0 ? 1.0f : 0.0f) - gFx) * aFx;
            if (gFx > 1e-5f)
            {
                if ((now & 7) == 0) setFxFilter();
                float lp, bp, hp;
                fx[0].tick (wl, lp, bp, hp); wl = prm.fxFilter == FxHp ? hp : prm.fxFilter == FxLp ? lp : bp * fxBpGain;
                fx[1].tick (wr, lp, bp, hp); wr = prm.fxFilter == FxHp ? hp : prm.fxFilter == FxLp ? lp : bp * fxBpGain;
                // stutter: rolls faster as the build rises
                const float depth = prm.stutter * smoothstep (0.0f, 0.12f, pe);
                float gt = 1.0f;
                if (depth > 0.001f)
                {
                    const int stages = 3 + prm.roll;
                    stutDiv = std::min (stages - 1, (int) (pe * (float) stages));
                    const double stepB = 1.0 / std::pow (2.0, stutDiv);
                    const double ph = gridBeat / stepB - std::floor (gridBeat / stepB);
                    gt = ph < 0.5 ? 1.0f : 1.0f - depth;
                }
                else stutDiv = -1;
                gStut += (gt - gStut) * aGate;
                wl *= gStut; wr *= gStut;
                // width
                const float m = 0.5f * (wl + wr), s = 0.5f * (wl - wr) * (1.0f + 1.5f * prm.width * pe);
                wl = m + s; wr = m - s;
                send = 0.5f * (wl + wr) * prm.fxVerb * std::pow (pe, 1.3f) * 1.4f;
                // level match (slow RMS of dry vs processed)
                msDry += (0.5f * (inL * inL + inR * inR) - msDry) * aRms;
                msWet += (0.5f * (wl * wl + wr * wr) - msWet) * aRms;
                const float target = prm.levelMatch && msDry > 1e-9f ? std::clamp (std::sqrt (msDry / std::max (msWet, 1e-12f)), 0.5f, 2.5f) : 1.0f;
                lmGain += (target - lmGain) * aLm;
                wl *= lmGain; wr *= lmGain;
                const float mx = prm.fxMix * gFx;
                wl = inL + (wl - inL) * mx; wr = inR + (wr - inR) * mx;
                send *= mx;
            }
            else { gStut = 1.0f; lmGain = 1.0f; msDry = msWet = 1e-6f; stutDiv = -1; if (gFx < 1e-5f) { fx[0].reset(); fx[1].reset(); } }
            // ---- reverb (generator + FX sends)
            const float tailTarget = prm.tailCut && state == Post && postT > 0.0 ? 0.0f : 1.0f;
            duck += (tailTarget - duck) * aDuck;
            float rl, rr; verb.tick (genSend + send, 0.15f + 0.85f * duck, rl, rr);
            rl *= duck; rr *= duck;
            float ol = wl + (gl + rl) * prm.genLvl, orr = wr + (gr + rr) * prm.genLvl;
            if (prm.genOnly) { ol = (gl + rl) * prm.genLvl; orr = (gr + rr) * prm.genLvl; }
            // pre-drop gap
            gGap += (gapTarget - gGap) * aGate;
            ol *= gGap; orr *= gGap;
            // club safe: mono below 150 Hz
            if (prm.clubSafe)
            {
                const float m = 0.5f * (ol + orr); float s = 0.5f * (ol - orr);
                s = csHp[1].tick (csHp[0].tick (s));
                ol = m + s; orr = m - s;
            }
            L[i] = safety (ol * prm.outGain); R[i] = safety (orr * prm.outGain);
            ++now; freeBeat += bps;
        }
        while (it != midi.end()) { handleMidi ((*it).getMessage()); ++it; }
        progressOut.store (pe); stateOut.store ((int) state); divOut.store (stutDiv);
    }

    std::atomic<float> progressOut { 0 };
    std::atomic<int> stateOut { 0 }, divOut { -1 };
    std::atomic<int> dropCount { 0 };
    // last drop position (sample index) for tests
    int64_t lastDropSample = -1;

private:
    // transparent below -0.9 dBFS, soft knee to a 0 dBFS ceiling: the build can never clip the track
    static float safety (float x) { const float a = std::abs (x); if (a <= 0.9f) return x; return std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) * 10.0f), x); }
    static float smoothstep (float a, float b, float x) { const float t = std::clamp ((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
    float rnd01() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (float) (rs & 0xffffff) / 16777216.0f; }
    float noise() { return rnd01() * 2.0f - 1.0f; }

    void updateRateCoeffs()
    {
        auto a = [this] (double ms) { return (float) (1.0 - std::exp (-1.0 / (fs * ms * 0.001))); };
        aGen = a (3.0); aFx = a (15.0); aGate = a (1.2); aRms = a (300.0); aLm = a (120.0); aDuck = a (40.0); aRise = a (20.0);
    }

    float shape (float x) const
    {
        const float c = std::clamp (prm.curve, -1.0f, 1.0f);
        if (c >= 0) return std::pow (x, 1.0f + 2.5f * c);
        return 1.0f - std::pow (1.0f - x, 1.0f - 2.5f * c);
    }

    void handleMidi (const juce::MidiMessage& m)
    {
        if (prm.mode != MidiHold) return;
        if (m.isNoteOn()) { if (midiHeld++ == 0) startBuild (false); }
        else if (m.isNoteOff()) { if (midiHeld > 0 && --midiHeld == 0 && holdBuild) { if (p > 0.2f) drop(); else { holdBuild = false; state = Idle; } } }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { midiHeld = 0; if (holdBuild) { holdBuild = false; state = Idle; } }
    }

    void startBuild (bool oneShot)
    {
        holdBuild = true; previewing = oneShot; buildBeats = 0; state = Building;
    }

    void drop()
    {
        state = Post; postT = 0; postBeats = 0; impT = -1.0 / fs; downT = -1.0 / fs; holdBuild = false; previewing = false; armed = false;
        boomPh = 0; downPh = 0; burst[0].reset(); burst[1].reset();
        lastDropSample = now; ++dropCount;
    }

    void timeline (double b, double bps, bool playing, double bpb)
    {
        float raw = 0; gapTarget = 1.0f;
        const double gapB = gapBeats (prm.gap);
        if (holdBuild)
        {
            const double lenB = lengthBars (prm.length) * bpb;
            buildBeats += bps;
            raw = (float) std::min (1.0, buildBeats / lenB);
            if (previewing && gapB > 0 && lenB - buildBeats <= gapB) gapTarget = 0.0f;
            if (previewing && buildBeats >= lenB) { drop(); raw = 0; }
        }
        else if (prm.mode == AutoBars)
        {
            if (playing)
            {
                const double P = periodBars (prm.period), Lb = std::min (lengthBars (prm.length), P);
                double phase = std::fmod (b / bpb - (double) prm.offset, P); if (phase < 0) phase += P;
                // drop: phase wrapped from the end of a build
                if (prevPhase >= 0 && phase < prevPhase && prevPhase - phase > P * 0.5 && prevPhase >= P - Lb + 0.5 * Lb) drop();
                prevPhase = phase;
                if (phase >= P - Lb)
                {
                    raw = (float) ((phase - (P - Lb)) / Lb);
                    if (gapB > 0 && (P - phase) * bpb <= gapB + 1e-9) gapTarget = 0.0f;
                    if (state != Post || postT > 0.05) state = Building;
                }
                else if (state == Building) state = Idle;
            }
            else { prevPhase = -1; if (state == Building) state = Idle; }
        }
        else if (prm.mode == ManualRise)
        {
            riseSm += (prm.rise - riseSm) * aRise;
            raw = riseSm < 1e-4f ? 0.0f : riseSm;
            if (riseSm > 0.9f) armed = true;
            if (armed && riseSm < 0.3f) { drop(); raw = 0; }
            if (raw > 0 && state != Post) state = Building;
            else if (raw <= 0 && state == Building) state = Idle;
        }
        p = std::clamp (raw, 0.0f, 1.0f);
        pe = p > 0 ? shape (p) : 0.0f;
        gridBeat = holdBuild ? buildBeats : b;
        impT += 1.0 / fs; downT += 1.0 / fs;
        const double downLenS = downBars (prm.downLen) * bpb / (bps * fs);
        if (downT <= 0.0) downDur = (float) downLenS;      // length fixed at the drop
        if (state == Post)
        {
            postT += 1.0 / fs; postBeats += bps;
            const double tailS = std::max ({ prm.impactOn ? (double) prm.impactDecay * 3.0 : 0.0, prm.downOn ? (double) downDur : 0.0, 0.25 });
            if (postT > tailS && pe <= 0) state = Idle;
            if (pe > 0 && postT > 0.05) state = Building;
        }
    }

    void renderGen (float& gl, float& gr, float rootHz, double bps)
    {
        gGen += ((pe > 0 ? 1.0f : 0.0f) - gGen) * aGen;
        genSend = 0;
        const float q = pe;
        if (gGen > 1e-5f)
        {
            // noise sweep
            if (prm.noiseOn)
            {
                float nl = noise(), nr = noise();
                if (prm.pink) { nl = pinkFilter (nl, true); nr = pinkFilter (nr, false); }
                if ((now & 7) == 0)
                {
                    float fc, qq = 0.6f + 6.0f * prm.noiseRes;
                    if (prm.noiseFilter == HpSweep) fc = 150.0f * std::pow (80.0f, q);
                    else if (prm.noiseFilter == BpSweep) fc = 200.0f * std::pow (60.0f, q);
                    else fc = 300.0f * std::pow (60.0f, q);
                    nf[0].set ((float) fs, fc, qq); nf[1].set ((float) fs, fc, qq);
                }
                float lp, bp, hp, a, c;
                nf[0].tick (nl, lp, bp, hp); a = prm.noiseFilter == HpSweep ? hp : prm.noiseFilter == BpSweep ? bp * 1.6f : lp;
                nf[1].tick (nr, lp, bp, hp); c = prm.noiseFilter == HpSweep ? hp : prm.noiseFilter == BpSweep ? bp * 1.6f : lp;
                const float amp = prm.noiseLvl * 0.35f * std::pow (q, 0.8f) * gGen;
                gl += a * amp; gr += c * amp; genSend += (a + c) * amp * 0.5f * prm.space * 0.8f;
            }
            // supersaw riser
            if (prm.synthOn)
            {
                static const int stacks[5][4] { { 0, 99, 99, 99 }, { 0, 12, 99, 99 }, { 0, 7, 12, 99 }, { 0, 3, 7, 12 }, { 0, 4, 7, 12 } };
                const float base = rootHz * std::pow (2.0f, (float) (prm.synthOct - 4));
                const float bend = std::pow (2.0f, prm.synthPitch * q / 12.0f);
                const float det = prm.detune * (0.25f + 0.75f * q) * 0.6f;   // semitones at the outer voices
                if ((now & 7) == 0) { const float fc = 350.0f * std::pow (45.0f, q); sf[0].set ((float) fs, fc, 0.8f); sf[1].set ((float) fs, fc, 0.8f); }
                float sl = 0, sr = 0; int nv = 0;
                for (int k = 0; k < 4; ++k)
                {
                    const int iv = stacks[std::clamp (prm.stack, 0, 4)][k]; if (iv > 24) break;
                    const float f0 = base * bend * std::pow (2.0f, iv / 12.0f);
                    for (int v = 0; v < 7; ++v)
                    {
                        const int idx = k * 7 + v;
                        const float off = (v - 3) / 3.0f;
                        const float f = f0 * std::pow (2.0f, off * det / 12.0f);
                        const float dt = std::min (0.45f, f / (float) fs);
                        float ph = sawPh[idx] + dt; if (ph >= 1) ph -= 1; sawPh[idx] = ph;
                        float s = 2 * ph - 1;
                        // polyBLEP
                        if (ph < dt) { const float t = ph / dt; s -= t + t - t * t - 1; }
                        else if (ph > 1 - dt) { const float t = (ph - 1) / dt; s -= t * t + t + t + 1; }
                        const float pan = v == 3 ? 0.5f : (v & 1 ? 0.15f : 0.85f);
                        sl += s * (1 - pan); sr += s * pan; ++nv;
                    }
                }
                float lp, bp, hp;
                sf[0].tick (sl, lp, bp, hp); sl = lp; sf[1].tick (sr, lp, bp, hp); sr = lp;
                const float amp = prm.synthLvl * 0.55f / std::sqrt ((float) std::max (1, nv)) * std::pow (q, 1.1f) * gGen;
                gl += sl * amp; gr += sr * amp; genSend += (sl + sr) * amp * 0.5f * prm.space;
            }
            // Shepard tone: endless rise, in key
            if (prm.shepOn)
            {
                shepX += (float) (prm.shepSpeed * bps * (0.3 + 1.7 * q));
                if (shepX >= 1) shepX -= 1;
                const float fmin = rootHz / 16.0f;               // 4 octaves below A4-class root
                float s = 0;
                for (int k = 0; k < 9; ++k)
                {
                    const float o = (float) k + shepX;          // octaves above fmin (0..9)
                    const float f = fmin * std::pow (2.0f, o);
                    const float w = std::exp (-0.5f * (o - 4.5f) * (o - 4.5f) / (1.5f * 1.5f));
                    shepPh[k] += f / (float) fs; if (shepPh[k] >= 1) shepPh[k] -= 1;
                    s += w * std::sin (2 * kPi * shepPh[k]);
                }
                const float amp = prm.shepLvl * 0.22f * std::pow (q, 0.7f) * gGen;
                gl += s * amp; gr += s * amp; genSend += s * amp * prm.space;
            }
        }
        // impact (after the drop)
        if (prm.impactOn && impT < 12.0)
        {
            const float t = (float) impT;
            const float fEnd = rootHz / 8.0f * (rootHz / 8.0f > 62.0f ? 0.5f : 1.0f);    // A1-ish
            const float f = fEnd * (1.0f + 3.0f * std::exp (-t / 0.03f));
            boomPh += f / (float) fs; if (boomPh >= 1) boomPh -= 1;
            const float env = std::exp (-t / std::max (0.05f, prm.impactDecay * 0.35f)) * std::min (1.0f, t * 2000.0f);
            float boom = std::tanh (1.8f * std::sin (2 * kPi * boomPh)) * env;
            // noise crack
            float lp, bp, hp; burst[0].set ((float) fs, 2500.0f, 0.7f);
            burst[0].tick (noise(), lp, bp, hp);
            const float crack = lp * std::exp (-t / 0.09f) * 1.2f;
            float nlp, nbp, nhp; burst[1].set ((float) fs, 6000.0f, 0.7f);
            burst[1].tick (noise(), nlp, nbp, nhp);
            const float air = nhp * std::exp (-t / std::max (0.1f, prm.impactDecay * 0.5f)) * 0.25f;
            const float amp = prm.impactLvl * 0.8f;
            const float x = (boom * 0.9f + crack) * amp;
            gl += x + air * amp; gr += x - air * amp;
            genSend += (crack * 0.8f + boom * 0.3f + air) * amp * (0.3f + prm.space);
        }
        // downlifter
        if (prm.downOn && downT < (double) downDur + 0.01)
        {
            const float u = std::clamp ((float) (downT / std::max (0.05f, downDur)), 0.0f, 1.0f);
            const float env = std::pow (1.0f - u, 1.6f) * std::min (1.0f, (float) downT * 400.0f);
            if ((now & 7) == 0) { const float fc = 9000.0f * std::pow (1.0f / 50.0f, u); dnf[0].set ((float) fs, fc, 1.5f); dnf[1].set ((float) fs, fc, 1.5f); }
            float lp, bp, hp; float a, c;
            dnf[0].tick (noise(), lp, bp, hp); a = bp;
            dnf[1].tick (noise(), lp, bp, hp); c = bp;
            const float f = rootHz * 2.0f * std::pow (2.0f, -3.0f * u);
            downPh += f / (float) fs; if (downPh >= 1) downPh -= 1;
            const float tone = std::sin (2 * kPi * downPh) * 0.35f;
            const float amp = prm.downLvl * 0.6f * env;
            gl += (a + tone) * amp; gr += (c + tone) * amp;
            genSend += (a + c) * 0.5f * amp * prm.space;
        }
    }

    float pinkFilter (float w, bool left)
    {
        // Paul Kellet's economy pink (per channel state packed)
        float* s = left ? pk[0] : pk[1];
        s[0] = 0.99765f * s[0] + w * 0.0990460f; s[1] = 0.96300f * s[1] + w * 0.2965164f; s[2] = 0.57000f * s[2] + w * 1.0526913f;
        return (s[0] + s[1] + s[2] + w * 0.1848f) * 0.25f;
    }

    void setFxFilter()
    {
        const float amt = prm.fxAmt, q = 0.6f + 5.0f * prm.fxRes * prm.fxRes;
        float fc;
        if (prm.fxFilter == FxHp) fc = 20.0f * std::pow (std::pow (250.0f, amt), pe);
        else if (prm.fxFilter == FxLp) fc = 20000.0f / std::pow (std::pow (100.0f, amt), pe);
        else fc = 400.0f * std::pow (std::pow (20.0f, amt), pe);
        fx[0].set ((float) fs, fc, q); fx[1].set ((float) fs, fc, q);
        fxBpGain = std::sqrt (q) * 1.4f;
    }

    double fs = 48000;
    Params prm;
    Fdn verb;
    Svf nf[2], sf[2], fx[2], burst[2], dnf[2];
    Hp2 csHp[2];
    static constexpr int kSaws = 28;
    float sawPh[kSaws] {}, shepPh[9] {};
    float pk[2][3] {};
    uint32_t rs = 0x9e3779b9u;
    State state = Idle;
    float p = 0, pe = 0, riseSm = 0; double prevPhase = -1; bool armed = false;
    double postT = 1e9, postBeats = 0, impT = 1e9, downT = 1e9; float downDur = 2.0f;
    int midiHeld = 0; bool holdBuild = false, previewing = false; double buildBeats = 0;
    std::atomic<bool> previewReq { false }, dropReq { false };
    float gGen = 0, gFx = 0, gGap = 1, gapTarget = 1, gStut = 1, lmGain = 1, msDry = 1e-6f, msWet = 1e-6f, duck = 1, genSend = 0, fxBpGain = 1;
    float aGen = 0.01f, aFx = 0.01f, aGate = 0.02f, aRms = 0.0001f, aLm = 0.001f, aDuck = 0.001f, aRise = 0.002f;
    float shepX = 0, boomPh = 0, downPh = 0;
    int stutDiv = -1; double gridBeat = 0;
    int64_t now = 0; double freeBeat = 0;
    double anchorBeat = 0, anchorBps = 0; int64_t anchorS = 0; bool anchorOk = false;
};

} // namespace lumen::riser
