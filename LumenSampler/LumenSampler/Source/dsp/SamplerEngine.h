#pragma once
#include "Resampler.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <array>
#include <cmath>

// =====================================================================================
// Lumen Sampler - voice engine
//   Classic (key-tracked), One-shot, Slice. Warp: Off (repitch, sinc), Key-lock, Tempo sync (WSOLA grains).
//   Default settings are transparent: unity gain, no filter, attack 0, sustain 1, centre pan.
// =====================================================================================
namespace lumen::sampler
{
enum Mode { Classic, OneShot, Slice };
enum Warp { WarpOff, KeyLock, TempoSync };
enum LoopMode { LoopFwd, LoopPingPong };
enum FType { LP12, LP24, HP12, HP24, BP, Notch };
enum LShape { LSine, LTri, LSaw, LSquare, LRand };

struct LfoP { int shape = LSine; float hz = 5, pitch = 0, filter = 0, vol = 0; bool retrig = true; };
struct Params
{
    int mode = Classic, root = 60, warp = WarpOff; float tune = 0, fine = 0, sampleBpm = 120, hostBpm = 120;
    float start = 0, end = 1; bool reverse = false;
    bool loop = false; int loopMode = LoopFwd; float loopStart = 0, loopEnd = 1, xfadeMs = 10;
    float A = 0, D = 0.3f, S = 1, R = 0.01f, velSens = 0;
    bool fOn = false; int fType = LP24; float cutoff = 20000, res = 0.1f, fEnv = 0, fA = 0, fD = 0.3f, fS = 1, fR = 0.1f, keyTrack = 0, velCut = 0;
    LfoP lfo[2];
    int poly = 16; bool legato = false; float glide = 0, bendRange = 2;
    float volume = 1, pan = 0, width = 1;
    const std::vector<int>* slices = nullptr;
    // extras (neutral defaults)
    float pEnvAmt = 0, pEnvDecay = 0.15f;
    int uni = 1; float uniDetune = 15, uniSpread = 0.6f;
    std::array<int, 6> chord { 0, 0, 0, 0, 0, 0 }; int chordN = 1;
    float humStart = 0, humPitch = 0, humVol = 0, fadeIn = 0, fadeOut = 0;
    bool choke = false;
};

// per-voice offsets set by the engine (chord interval, unison detune / pan, humanize)
struct VoiceExtra { int key = 60, interval = 0; double detune = 0; float gl = 1, gr = 1, volMul = 1; double startOff = 0; };

// ------------------------------------------------------------------------------------
struct Svf
{
    double g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, s1[2] {}, s2[2] {};
    void set (double fs, double fc, double q) { fc = std::clamp (fc, 15.0, 0.47 * fs); g = std::tan (M_PI * fc / fs); k = 1.0 / q; a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    void reset() { s1[0] = s1[1] = s2[0] = s2[1] = 0; }
    inline double tick (double x, int c, int type) noexcept
    {
        const double v3 = x - s2[c], v1 = a1 * s1[c] + a2 * v3, v2 = s2[c] + a2 * s1[c] + a3 * v3;
        s1[c] = 2 * v1 - s1[c]; s2[c] = 2 * v2 - s2[c];
        switch (type) { case 0: return v2; case 1: return x - k * v1 - v2; case 2: return v1 * k; default: return x - k * v1; }
    }
};

struct Env
{
    enum { Idle, Att, Dec, Sus, Rel };
    int st = Idle; double v = 0, ai = 1, dc = 0, s = 1, rc = 0;
    void set (double fs, float A, float D, float S, float R)
    {
        ai = A <= 0.0005f ? 2.0 : 1.0 / (A * fs);
        dc = std::exp (-6.9 / (std::max (0.001f, D) * fs)); s = std::clamp ((double) S, 0.0, 1.0);
        rc = std::exp (-6.9 / (std::max (0.0005f, R) * fs));
    }
    void on() { st = Att; if (ai >= 1.0) { v = 1.0; st = s >= 1.0 ? Sus : Dec; } }
    void off() { if (st != Idle) st = Rel; }
    inline double next() noexcept
    {
        switch (st)
        {
            case Att: v += ai; if (v >= 1.0) { v = 1.0; st = s >= 1.0 ? Sus : Dec; } break;
            case Dec: v = s + (v - s) * dc; if (std::abs (v - s) < 1e-6) { v = s; st = Sus; } break;
            case Rel: v *= rc; if (v < 1e-5) { v = 0; st = Idle; } break;
            default: break;
        }
        return v;
    }
};

// ------------------------------------------------------------------------------------
class Voice
{
public:
    bool active = false, gate = false; int note = 60; uint64_t age = 0;

    void prepare (double sr) { fs = sr; }

    int key = 60;
    void start (int n, float velocity, const SampleSource* s, const Params& p, uint64_t a, double fromNote = -1, const VoiceExtra& x = {})
    {
        src = s; ex = x; key = x.key; note = n; vel = velocity; age = a; active = src != nullptr; gate = true;
        if (! active) return;
        killing = false; killG = 1;
        const int len = src->length;
        // region
        if (p.mode == Slice && p.slices != nullptr && ! p.slices->empty())
        {
            const int idx = n - 36;
            if (idx < 0 || idx >= (int) p.slices->size()) { active = false; return; }
            regionA = (*p.slices)[(size_t) idx];
            regionB = idx + 1 < (int) p.slices->size() ? (*p.slices)[(size_t) idx + 1] : len;
        }
        else
        {
            regionA = (int) std::lround (std::clamp (p.start, 0.0f, 1.0f) * len);
            regionB = (int) std::lround (std::clamp (p.end, 0.0f, 1.0f) * len);
            if (regionB <= regionA + 1) regionB = std::min (len, regionA + 2);
        }
        loopA = (int) std::lround (std::clamp (p.loopStart, 0.0f, 1.0f) * len);
        loopB = (int) std::lround (std::clamp (p.loopEnd, 0.0f, 1.0f) * len);
        loopA = std::clamp (loopA, regionA, regionB - 1); loopB = std::clamp (loopB, loopA + 2, regionB);
        loopOn = p.loop && p.mode != Slice && loopB - loopA > 16;
        dir = p.reverse ? -1 : 1;
        pos = dir > 0 ? (double) regionA : (double) (regionB - 1);
        if (x.startOff > 0) pos = dir > 0 ? std::min ((double) regionB - 2, pos + std::floor (x.startOff)) : std::max ((double) regionA, pos - std::floor (x.startOff));
        pe = 1.0; pdc = std::exp (-6.9 / (std::max (0.005f, p.pEnvDecay) * fs));
        fadeInS = p.fadeIn * 0.001 * src->rate; fadeOutS = p.fadeOut * 0.001 * src->rate;
        curNote = fromNote >= 0 && p.glide > 0.0005f ? fromNote : (double) n;
        env.set (fs, p.A, p.D, p.S, p.R); env.on();
        fenv.set (fs, p.fA, p.fD, p.fS, p.fR); fenv.on();
        filt.reset(); filt2.reset();
        for (int l = 0; l < 2; ++l) if (p.lfo[l].retrig) lfoPh[l] = 0;
        // grains (warp)
        grains = {}; nextGrain = 0; pa = pos; lastGrainStart = -1;
        tick = 0;
    }
    void release() { gate = false; env.off(); fenv.off(); }
    void kill() { killing = true; killStep = 1.0 / (0.003 * fs); }
    bool isKilling() const { return killing; }
    double position() const { return warpOn ? pa : pos; }
    void legatoTo (int k) { key = k; note = k + ex.interval; }

    void render (float* L, float* R, int n, const Params& p, double bendSemis) noexcept
    {
        if (! active) return;
        const double rateRatio = src->rate / fs;
        const double glideK = p.glide > 0.0005f ? 1.0 - std::exp (-1.0 / (p.glide * fs * 0.25)) : 1.0;
        const double velG = 1.0 - p.velSens * (1.0 - (double) vel * vel);
        const bool oneShot = p.mode == OneShot;
        for (int i = 0; i < n; ++i)
        {
            // ---- modulation (per sample, cheap)
            curNote += ((double) note - curNote) * glideK;
            double lfoV[2];
            for (int l = 0; l < 2; ++l)
            {
                lfoPh[l] += p.lfo[l].hz / fs; if (lfoPh[l] >= 1) { lfoPh[l] -= 1; shV[l] = rnd(); }
                const double ph = lfoPh[l];
                switch (p.lfo[l].shape)
                {
                    case LTri: lfoV[l] = 1.0 - 4.0 * std::abs (ph - 0.5); break;
                    case LSaw: lfoV[l] = 2.0 * ph - 1.0; break;
                    case LSquare: lfoV[l] = ph < 0.5 ? 1.0 : -1.0; break;
                    case LRand: lfoV[l] = shV[l]; break;
                    default: lfoV[l] = std::sin (2 * M_PI * ph); break;
                }
            }
            const double pitchMod = p.lfo[0].pitch * lfoV[0] + p.lfo[1].pitch * lfoV[1];
            const double keySemis = p.mode == Slice ? 0.0 : curNote - p.root;
            double semis = keySemis + p.tune + p.fine * 0.01 + bendSemis + pitchMod + ex.detune;
            if (p.pEnvAmt != 0.0f) { semis += p.pEnvAmt * pe; pe *= pdc; }
            const double pitchSpeed = rateRatio * (semis == 0.0 ? 1.0 : std::pow (2.0, semis / 12.0));
            // ---- source
            float l = 0, r = 0;
            bool ended = false;
            warpOn = p.warp != WarpOff;
            double fade = 1.0;
            if (fadeInS > 0 || fadeOutS > 0)
            {
                const double cp = position();
                const double dIn = dir > 0 ? cp - regionA : regionB - 1 - cp, dOut = dir > 0 ? regionB - cp : cp - regionA;
                if (fadeInS > 0 && dIn < fadeInS) fade *= std::max (0.0, dIn / fadeInS);
                if (fadeOutS > 0 && ! loopOn && dOut < fadeOutS) fade *= std::max (0.0, dOut / fadeOutS);
            }
            if (p.warp == WarpOff) ended = ! readRepitch (pitchSpeed, l, r);
            else
            {
                const double timeSpeed = rateRatio * (p.warp == TempoSync && p.sampleBpm > 1 ? p.hostBpm / p.sampleBpm : 1.0);
                ended = ! readGrains (pitchSpeed, timeSpeed, l, r);
            }
            if (ended) { active = false; break; }
            // ---- filter
            if (p.fOn)
            {
                if ((tick++ & 15) == 0)
                {
                    const double fe = fenv.v;
                    const double oct = p.fEnv * 8.0 * fe + p.keyTrack * (curNote - 60.0) / 12.0 + p.velCut * 4.0 * ((double) vel - 0.5)
                                     + p.lfo[0].filter * lfoV[0] + p.lfo[1].filter * lfoV[1];
                    const double q = 0.5 + 12.0 * p.res * p.res;
                    filt.set (fs, p.cutoff * std::pow (2.0, oct), q); filt2.set (fs, p.cutoff * std::pow (2.0, oct), 0.707);
                }
                const int t = p.fType == LP12 || p.fType == LP24 ? 0 : p.fType == HP12 || p.fType == HP24 ? 1 : p.fType == BP ? 2 : 3;
                double a = filt.tick (l, 0, t), b = filt.tick (r, 1, t);
                if (p.fType == LP24 || p.fType == HP24) { a = filt2.tick (a, 0, t); b = filt2.tick (b, 1, t); }
                l = (float) a; r = (float) b;
            }
            fenv.next();
            // ---- amp
            double g = env.next();
            if (oneShot && ! killing && env.st == Env::Rel) { env.st = Env::Sus; env.v = env.s; g = env.v; }   // one-shot ignores note-off
            if (env.st == Env::Idle) { active = false; break; }
            g *= velG * ex.volMul * fade;
            const double vm = p.lfo[0].vol * 0.5 * (1.0 - lfoV[0]) + p.lfo[1].vol * 0.5 * (1.0 - lfoV[1]);
            if (vm > 0) g *= std::max (0.0, 1.0 - vm);
            if (killing) { killG -= killStep; if (killG <= 0) { active = false; break; } g *= killG; }
            if (g == 1.0 && ex.gl == 1.0f && ex.gr == 1.0f) { L[i] += l; R[i] += r; }
            else { L[i] += (float) (l * g * ex.gl); R[i] += (float) (r * g * ex.gr); }
        }
    }

private:
    double rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (double) (rs & 0xffffff) / 8388608.0 - 1.0; }

    // classic repitch: bit-exact when the speed is exactly 1 and the position is a whole sample
    bool readRepitch (double speed, float& l, float& r) noexcept
    {
        const double step = speed * dir;
        if (dir > 0 ? pos >= regionB : pos < regionA) return false;
        const int len = src->length;
        auto fetch = [&] (double at, float& a, float& b)
        {
            if (speed == 1.0 && at == std::floor (at))
            {
                const int i = (int) at;
                if (i < 0 || i >= len) { a = b = 0; return; }
                a = src->data (0, 0)[i]; b = src->data (0, 1)[i];
            }
            else SincReader::read (*src, at, speed, a, b);
        };
        fetch (pos, l, r);
        if (loopOn)
        {
            const double L = loopB - loopA;
            const double xf = std::min ({ (double) xfade, L * 0.5, (double) (loopA - regionA) + 0.0, 1e9 });
            if (loopMode == LoopFwd && dir > 0 && xf >= 1.0 && pos >= loopB - xf && pos < loopB)
            {
                // equal-power crossfade into the audio just before the loop start
                const double t = (pos - (loopB - xf)) / xf;
                float a2, b2; fetch (pos - L, a2, b2);
                const double ga = std::cos (t * M_PI * 0.5), gb = std::sin (t * M_PI * 0.5);
                l = (float) (l * ga + a2 * gb); r = (float) (r * ga + b2 * gb);
            }
        }
        pos += step;
        if (loopOn && gate)
        {
            if (loopMode == LoopFwd)
            {
                if (dir > 0 && pos >= loopB) pos -= (loopB - loopA);
                if (dir < 0 && pos < loopA) pos += (loopB - loopA);
            }
            else
            {
                if (pos >= loopB) { pos = 2.0 * loopB - pos - 1.0; dir = -1; }
                else if (pos < loopA) { pos = 2.0 * loopA - pos; dir = 1; }
            }
        }
        else if (loopOn && ! gate && loopMode == LoopFwd)
        {
            // keep looping through the release tail
            if (dir > 0 && pos >= loopB) pos -= (loopB - loopA);
        }
        return true;
    }

    // key-lock / tempo sync: overlapping Hann grains (4x) with WSOLA alignment
    bool readGrains (double pitchSpeed, double timeSpeed, float& l, float& r) noexcept
    {
        const int G = std::max (256, (int) (fs * 0.046)), H = G / 4;
        if (nextGrain <= 0)
        {
            // analysis position for the new grain
            double target = pa;
            if (lastGrainStart >= 0)
            {
                // natural continuation of the previous grain, searched around the analysis position
                const double cont = lastGrainStart + H * pitchSpeed * dir;
                const int D = (int) (0.012 * src->rate);
                double best = -1e30; double bestPos = pa;
                const float* a = src->data (0, 0);
                for (int d = -D; d <= D; d += 3)
                {
                    const double cand = pa + d;
                    double c = 0;
                    for (int k = 0; k < 192; k += 3)
                    {
                        const int i1 = (int) (cont + k * pitchSpeed * dir), i2 = (int) (cand + k * pitchSpeed * dir);
                        if (i1 < 0 || i2 < 0 || i1 >= src->length || i2 >= src->length) continue;
                        c += (double) a[i1] * a[i2];
                    }
                    if (c > best) { best = c; bestPos = cand; }
                }
                target = bestPos;
            }
            // end / loop handling of the analysis pointer
            if (loopOn && gate)
            {
                if (dir > 0 && target >= loopB) { pa -= (loopB - loopA); target -= (loopB - loopA); }
                if (dir < 0 && target < loopA) { pa += (loopB - loopA); target += (loopB - loopA); }
            }
            const bool past = dir > 0 ? target >= regionB : target < regionA;
            if (! past)
            {
                for (auto& gr : grains) if (! gr.on) { gr.on = true; gr.pos = target; gr.t = 0; break; }
                lastGrainStart = target;
            }
            pa += H * timeSpeed * dir;
            nextGrain = H;
            bool any = false; for (auto& gr : grains) any |= gr.on;
            if (past && ! any) return false;
        }
        --nextGrain;
        double sl = 0, sr = 0;
        for (auto& gr : grains)
        {
            if (! gr.on) continue;
            const double w = 0.5 - 0.5 * std::cos (2 * M_PI * (gr.t + 0.5) / G);
            const double at = gr.pos + gr.t * pitchSpeed * dir;
            if (at >= regionA && at < regionB)
            {
                float a, b; hermite (at, pitchSpeed, a, b);
                sl += w * a; sr += w * b;
            }
            if (++gr.t >= G) gr.on = false;
        }
        l = (float) (sl * 0.5); r = (float) (sr * 0.5);
        return true;
    }

    // 4-point Hermite on the octave level that keeps the read band-limited (grain mode)
    inline void hermite (double at, double speed, float& outL, float& outR) const noexcept
    {
        int level = 0; double sp = std::abs (speed);
        while (sp > 1.0 && level < SampleSource::kLevels - 1) { sp *= 0.5; ++level; }
        const double p = at / (double) (1 << level);
        const int i = (int) std::floor (p); const float f = (float) (p - i);
        for (int c = 0; c < 2; ++c)
        {
            const float* d = src->data (level, c);
            const float xm1 = d[i - 1], x0 = d[i], x1 = d[i + 1], x2 = d[i + 2];
            const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            const float y = ((c3 * f + c2) * f + c1) * f + x0;
            (c == 0 ? outL : outR) = y;
        }
    }

    struct Grain { bool on = false; double pos = 0; int t = 0; };
    double fs = 48000;
    const SampleSource* src = nullptr;
    float vel = 1;
    double pos = 0, curNote = 60; int dir = 1;
    int regionA = 0, regionB = 0, loopA = 0, loopB = 0; bool loopOn = false;
public:
    int loopMode = LoopFwd; double xfade = 480;
private:
    Env env, fenv; Svf filt, filt2; int tick = 0;
    VoiceExtra ex; double pe = 1, pdc = 1, fadeInS = 0, fadeOutS = 0; bool warpOn = false;
    double lfoPh[2] {}, shV[2] {}; uint32_t rs = 0x2545F491u;
    bool killing = false; double killG = 1, killStep = 0.01;
    std::array<Grain, 6> grains {}; int nextGrain = 0; double pa = 0, lastGrainStart = -1;
};

// ------------------------------------------------------------------------------------
class Engine
{
public:
    static constexpr int kMaxVoices = 64;
    void prepare (double sr) { fs = sr; for (auto& v : voices) { v.prepare (sr); v.active = false; } held.clear(); }
    void setSource (const SampleSource* s) { src = s; }
    void setParams (const Params& np) { p = np; }
    void noteOn (int n, float vel)
    {
        if (src == nullptr) return;
        held.push_back (n);
        const bool mono = p.poly <= 1;
        if (mono)
        {
            bool any = false;
            for (auto& c : voices) if (c.active && ! c.isKilling() && c.gate && p.legato) { c.legatoTo (n); any = true; }
            if (any) { lastNote = n; return; }
            for (auto& c : voices) if (c.active) c.kill();
        }
        else if (p.mode == Slice && p.choke)
        {
            for (auto& c : voices) if (c.active) c.kill();
        }
        else
        {
            // polyphony counts played keys; the oldest key is stolen
            std::vector<int> keys; Voice* oldest = nullptr;
            for (auto& c : voices) if (c.active && c.gate && ! c.isKilling()) { if (std::find (keys.begin(), keys.end(), c.key) == keys.end()) keys.push_back (c.key); if (! oldest || c.age < oldest->age) oldest = &c; }
            if ((int) keys.size() >= std::max (1, p.poly) && oldest) { const int k = oldest->key; for (auto& c : voices) if (c.active && c.key == k) c.kill(); }
        }
        spawn (n, vel, lastNote);
        lastNote = n;
    }
    void noteOff (int n)
    {
        held.erase (std::remove (held.begin(), held.end(), n), held.end());
        if (p.poly <= 1 && ! held.empty())
        {
            bool gated = false; for (auto& c : voices) gated |= c.active && c.gate && ! c.isKilling();
            if (gated)
            {
                if (p.legato) { for (auto& c : voices) if (c.active && c.gate) c.legatoTo (held.back()); }
                else { for (auto& c : voices) if (c.active) c.kill(); spawn (held.back(), 0.8f, lastNote); }
                lastNote = held.back();
            }
            return;
        }
        for (auto& c : voices) if (c.active && c.gate && c.key == n) { if (sustain) sustained.push_back (n); else c.release(); }
    }
    void setSustain (bool on)
    {
        sustain = on;
        if (! on) { for (int n : sustained) if (std::find (held.begin(), held.end(), n) == held.end()) for (auto& c : voices) if (c.active && c.gate && c.key == n) c.release(); sustained.clear(); }
    }
    void allNotesOff() { for (auto& c : voices) if (c.active) c.release(); held.clear(); }
    void kill() { for (auto& c : voices) c.active = false; held.clear(); }
    void render (float* L, float* R, int n, double bendSemis) { for (auto& v : voices) if (v.active) v.render (L, R, n, p, bendSemis); }
    int activeVoices() const { int c = 0; for (auto& v : voices) c += v.active; return c; }
    // play positions of the newest voices (for the waveform display)
    int positions (double* out, int maxN) const { int k = 0; for (auto& v : voices) if (v.active && k < maxN) out[k++] = v.position(); return k; }
private:
    double rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (double) (rs & 0xffffff) / 8388608.0 - 1.0; }   // -1..1
    Voice* freeVoice()
    {
        for (auto& c : voices) if (! c.active) return &c;
        Voice* v = &voices[0]; for (auto& c : voices) if (c.age < v->age) v = &c;
        return v;
    }
    // one key = chord notes x unison voices
    void spawn (int n, float vel, int from)
    {
        const int cn = p.mode == Slice ? 1 : std::clamp (p.chordN, 1, 6);
        const int un = std::clamp (p.uni, 1, 8);
        const float ug = un > 1 ? 1.0f / std::sqrt ((float) un) : 1.0f;
        const float cg = cn > 1 ? 1.0f / std::sqrt ((float) cn) : 1.0f;
        for (int ci = 0; ci < cn; ++ci)
            for (int u = 0; u < un; ++u)
            {
                VoiceExtra x; x.key = n; x.interval = p.chord[(size_t) ci];
                if (un > 1)
                {
                    const double t = 2.0 * u / (un - 1) - 1.0;      // -1..1
                    x.detune = t * p.uniDetune * 0.01;
                    const float pan = (float) t * p.uniSpread;
                    x.gl = std::min (1.0f, 1.0f - pan); x.gr = std::min (1.0f, 1.0f + pan);
                }
                x.volMul = ug * cg;
                if (p.humPitch > 0) x.detune += rnd() * p.humPitch * 0.01;
                if (p.humVol > 0) x.volMul *= (float) (1.0 - p.humVol * 0.5 * (rnd() + 1.0) * 0.5);
                if (p.humStart > 0 && src != nullptr) x.startOff = 0.5 * (rnd() + 1.0) * p.humStart * 0.001 * src->rate;
                const int note = std::clamp (n + x.interval, 0, 127);
                Voice* v = freeVoice();
                setVoiceLoop (*v);
                v->start (note, vel, src, p, ++counter, from >= 0 ? (double) (from + x.interval) : -1.0, x);
            }
    }
    uint32_t rs = 0x9E3779B9u;
    void setVoiceLoop (Voice& v) { v.loopMode = p.loopMode; v.xfade = p.xfadeMs * 0.001 * (src ? src->rate : fs); }
    double fs = 48000;
    const SampleSource* src = nullptr;
    Params p;
    std::array<Voice, kMaxVoices> voices;
    std::vector<int> held, sustained; bool sustain = false;
    uint64_t counter = 0; int lastNote = -1;
};

} // namespace lumen::sampler
