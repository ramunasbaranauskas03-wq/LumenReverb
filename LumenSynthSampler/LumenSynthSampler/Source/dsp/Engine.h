#pragma once
#include "Resynth.h"
#include "../synth/SynthEngine.h"     // lumen::synth::VFilter, Env, EnvCoef, lfoShape, fastTanh

// =====================================================================================
// Lumen Synth Sampler - voice engine
//   3 sample layers (SYNTH resynthesis / CLASSIC repitch / STRETCH grains), sub, noise,
//   filter, 3 envelopes, 2 LFOs, 8-slot mod matrix, 4 macros, unison, glide, mono/legato.
//   Neutral defaults: a layer at its root note plays back bit-identical.
// =====================================================================================
namespace lumen::ss
{
using lumen::sampler::SampleSource; using lumen::sampler::Resynth; using lumen::sampler::ResynthReader; using lumen::sampler::SincReader;
using lumen::sampler::kPi;

static constexpr int kLayers = 3, kMaxUni = 8, kVoices = 32, kSlots = 8, kTick = 16;
enum LayerMode { MSynth, MClassic, MStretch };
enum Src { sOff, sLfo1, sLfo2, sEnv2, sEnv3, sVel, sKey, sMW, sAT, sMac1, sMac2, sMac3, sMac4, sRnd, kNumSrc };
enum Dst { dOff, dPitch, dLvlA, dLvlB, dLvlC, dPosA, dPosB, dPosC, dSpeed, dCutoff, dRes, dDrive, dSub, dNoise, dPan, dAmp, dUniDet, dL1Rate, dL2Rate, kNumDst };

struct LayerP
{
    bool on = false; int mode = MSynth; int root = 60, oct = 0, semi = 0; float fine = 0, level = 1, pan = 0, start = 0, speed = 1;
    bool loop = false; float loopStart = 0.3f, loopEnd = 0.8f, xfadeMs = 40; bool exactRoot = true, reverse = false; float transientMs = 0;
};
struct ModSlot { int src = 0, dst = 0; float amt = 0; };
struct Params
{
    LayerP layer[kLayers];
    bool subOn = false; int subShape = 0, subOct = 1; float subLevel = 0.5f;
    bool noiseOn = false; float noiseLevel = 0.2f, noiseColor = 0.5f;
    bool fOn = false; int fType = 2; float cutoff = 20000, res = 0.1f, drive = 0, fEnv = 0, keyTrack = 0;
    float env[3][4] { { 0, 0.4f, 1, 0.02f }, { 0.001f, 0.4f, 0, 0.2f }, { 0.001f, 0.4f, 0, 0.2f } };
    int lfoShape[2] {}; float lfoHz[2] { 2, 2 }; bool lfoRetrig[2] { true, true };
    ModSlot mod[kSlots];
    float macro[4] {};
    int poly = 16; bool legato = false; float glide = 0; int bend = 2; float velSens = 0;
    int uni = 1; float uniDetune = 15, uniSpread = 0.7f;
};

struct LayerData { const SampleSource* src = nullptr; const Resynth* rs = nullptr; };

// ------------------------------------------------------------------------------------ one layer, one unison copy
class LayerReader
{
public:
    bool active = false;
    void start (const LayerData& d, const LayerP& p, double semisFromRoot, double outRate, double phase, bool exactAllowed)
    {
        data = d; fs = outRate; active = d.src != nullptr && d.src->length > 0;
        if (! active) return;
        const auto& s = *d.src;
        mode = p.mode;
        if (mode == MSynth && (d.rs == nullptr || ! d.rs->valid)) mode = MStretch;       // unpitched: fall back to grains
        useExact = mode == MSynth && exactAllowed && p.exactRoot && semisFromRoot == 0.0 && p.speed == 1.0f && ! p.reverse;
        dir = p.reverse && mode != MSynth ? -1 : 1;
        const double st = std::clamp ((double) p.start, 0.0, 0.999) * s.length;
        pos = dir > 0 ? std::floor (st) : s.length - 1 - std::floor (st);
        rd.start (st, phase);
        loopA = std::clamp ((double) p.loopStart, 0.0, 1.0) * s.length; loopB = std::clamp ((double) p.loopEnd, 0.0, 1.0) * s.length;
        if (loopB < loopA + 64) loopB = std::min ((double) s.length, loopA + 64);
        loopOn = p.loop && loopB - loopA > 64;
        xfade = std::min (p.xfadeMs * 0.001 * s.rate, 0.5 * (loopB - loopA));
        transient = mode == MSynth && ! useExact ? p.transientMs * 0.001 * s.rate : 0.0;
        tpos = st;
        grains = {}; nextGrain = 0; pa = pos; lastGrainStart = -1;
    }

    // semis: total pitch offset from the layer root; speed: time speed factor; posMod: position offset (fraction)
    inline bool read (double pitchFactor, double speed, double posMod, bool gate, float& l, float& r) noexcept
    {
        const auto& s = *data.src;
        const double rr = s.rate / fs;
        if (useExact || mode == MClassic) return readClassic (pitchFactor * rr, gate, l, r);
        if (mode == MStretch) return readGrains (pitchFactor * rr, speed * rr, gate, l, r);
        // ---- SYNTH: harmonic resynthesis
        const auto& R = *data.rs;
        double save = rd.tau;
        if (posMod != 0.0) rd.tau = std::clamp (rd.tau + posMod * s.length, 0.0, R.frameTime.back() - 1.0);
        float rl, rrr, ratio;
        const bool ok = rd.read (R, R.f0 * pitchFactor, fs, speed * rr, rl, rrr, ratio);
        if (posMod != 0.0) rd.tau = save + speed * rr;
        if (! ok) return false;
        // sustain loop in time with a cross-fade (frames are phase aligned, so this is seamless)
        if (loopOn && gate)
        {
            if (rd.tau >= loopB) rd.tau -= (loopB - loopA);
            if (xfade > 1 && rd.tau > loopB - xfade)
            {
                ResynthReader tmp = rd; tmp.tau = rd.tau - (loopB - loopA); float a, b, q;
                tmp.read (R, R.f0 * pitchFactor, fs, 0.0, a, b, q);
                const float t = (float) ((rd.tau - (loopB - xfade)) / xfade);
                rl += (a - rl) * t; rrr += (b - rrr) * t;
            }
        }
        // keep the original attack (unpitched) for the first transient ms
        if (transient > 0 && tpos < transient)
        {
            const int i = (int) tpos;
            float ol = 0, orr = 0;
            if (i >= 0 && i < s.length) { ol = s.data (0, 0)[i]; orr = s.data (0, 1)[i]; }
            const double fade = std::min (transient * 0.25, 0.004 * s.rate);
            const float w = (float) std::clamp ((transient - tpos) / std::max (1.0, fade), 0.0, 1.0);
            rl = rl + (ol - rl) * w; rrr = rrr + (orr - rrr) * w;
            tpos += speed * rr;
        }
        l = rl; r = rrr;
        return true;
    }

private:
    bool readClassic (double step, bool gate, float& l, float& r) noexcept
    {
        const auto& s = *data.src;
        if (dir > 0 ? pos >= s.length : pos < 0) return false;
        if (step == 1.0 && pos == std::floor (pos)) { const int i = (int) pos; l = s.data (0, 0)[i]; r = s.data (0, 1)[i]; }
        else SincReader::read (s, pos, step, l, r);
        if (loopOn && dir > 0 && xfade >= 1 && pos >= loopB - xfade && pos < loopB)
        {
            const double t = (pos - (loopB - xfade)) / xfade; float a, b;
            SincReader::read (s, pos - (loopB - loopA), step, a, b);
            const double ga = std::cos (t * kPi * 0.5), gb = std::sin (t * kPi * 0.5);
            l = (float) (l * ga + a * gb); r = (float) (r * ga + b * gb);
        }
        pos += step * dir;
        if (loopOn && dir > 0 && pos >= loopB) pos -= (loopB - loopA);
        (void) gate;
        return true;
    }

    bool readGrains (double pitchSpeed, double timeSpeed, bool gate, float& l, float& r) noexcept
    {
        const auto& s = *data.src;
        const int G = std::max (256, (int) (fs * 0.046)), H = G / 4;
        if (nextGrain <= 0)
        {
            double target = pa;
            if (lastGrainStart >= 0)
            {
                const double cont = lastGrainStart + H * pitchSpeed * dir;
                const int D = (int) (0.012 * s.rate);
                double best = -1e30, bestPos = pa; const float* a = s.data (0, 0);
                for (int d = -D; d <= D; d += 3)
                {
                    const double cand = pa + d; double c = 0;
                    for (int k = 0; k < 192; k += 3)
                    {
                        const int i1 = (int) (cont + k * pitchSpeed * dir), i2 = (int) (cand + k * pitchSpeed * dir);
                        if (i1 < 0 || i2 < 0 || i1 >= s.length || i2 >= s.length) continue;
                        c += (double) a[i1] * a[i2];
                    }
                    if (c > best) { best = c; bestPos = cand; }
                }
                target = bestPos;
            }
            if (loopOn && gate && dir > 0 && target >= loopB) { pa -= (loopB - loopA); target -= (loopB - loopA); }
            const bool past = dir > 0 ? target >= s.length : target < 0;
            if (! past) { for (auto& gr : grains) if (! gr.on) { gr.on = true; gr.pos = target; gr.t = 0; break; } lastGrainStart = target; }
            pa += H * timeSpeed * dir; nextGrain = H;
            bool any = false; for (auto& gr : grains) any |= gr.on;
            if (past && ! any) return false;
        }
        --nextGrain;
        double sl = 0, sr = 0;
        for (auto& gr : grains)
        {
            if (! gr.on) continue;
            const double w = 0.5 - 0.5 * std::cos (2 * kPi * (gr.t + 0.5) / G);
            const double at = gr.pos + gr.t * pitchSpeed * dir;
            if (at >= 0 && at < s.length - 2)
            {
                int level = 0; double sp = std::abs (pitchSpeed);
                while (sp > 1.0 && level < SampleSource::kLevels - 1) { sp *= 0.5; ++level; }
                const double p = at / (double) (1 << level); const int i = (int) p; const float f = (float) (p - i);
                sl += w * ResynthReader::herm (s.data (level, 0), i, f); sr += w * ResynthReader::herm (s.data (level, 1), i, f);
            }
            if (++gr.t >= G) gr.on = false;
        }
        l = (float) (sl * 0.5); r = (float) (sr * 0.5);
        return true;
    }

    struct Grain { bool on = false; double pos = 0; int t = 0; };
    LayerData data; double fs = 48000; int mode = MSynth, dir = 1; bool useExact = false, loopOn = false;
    double pos = 0, loopA = 0, loopB = 0, xfade = 0, transient = 0, tpos = 0;
    ResynthReader rd;
    std::array<Grain, 6> grains {}; int nextGrain = 0; double pa = 0, lastGrainStart = -1;
};

// ------------------------------------------------------------------------------------ voice
struct Shared
{
    const Params* p = nullptr; const LayerData* layers = nullptr;
    double fs = 48000; float bendSemis = 0, mw = 0, at = 0;
    lumen::synth::EnvCoef ec[3];
    float lfoHz[2] { 2, 2 };
};

class Voice
{
public:
    bool active = false, gate = false; int note = 60, key = 60; uint64_t age = 0;

    void start (int n, float velocity, const Shared& sh, uint64_t a, double fromNote, uint32_t seed)
    {
        const auto& p = *sh.p;
        note = key = n; vel = velocity; age = a; gate = true; killing = false; killG = 1;
        rng = seed ? seed : 0x1234567u;
        rnd = uni() * 2 - 1;
        curNote = fromNote >= 0 && p.glide > 0.0005f ? fromNote : (double) n;
        for (int e = 0; e < 3; ++e) { env[e].c = &sh.ec[e]; env[e].v = 0; env[e].on(); }
        for (int l = 0; l < 2; ++l) if (p.lfoRetrig[l]) lfoPh[l] = 0;
        filt.reset(); filtSet = false;
        subPh = 0; noiseLp = 0;
        nUni = std::clamp (p.uni, 1, kMaxUni);
        bool any = false;
        for (int L = 0; L < kLayers; ++L)
        {
            const auto& lp = p.layer[L];
            for (int u = 0; u < kMaxUni; ++u) rd[L][u].active = false;
            if (! lp.on || sh.layers[L].src == nullptr) continue;
            const double semis = layerSemis (lp, curNote, 0.0, p);
            for (int u = 0; u < nUni; ++u)
            {
                const double ph = nUni > 1 ? uni() : 0.0;
                rd[L][u].start (sh.layers[L], lp, semis, sh.fs, ph, nUni == 1 && p.glide <= 0.0005f);
                any |= rd[L][u].active;
            }
        }
        active = any || p.subOn || p.noiseOn;
        tick = 0;
    }
    void release() { gate = false; for (auto& e : env) e.off(); }
    void kill() { killing = true; }
    bool isKilling() const { return killing; }
    void legatoTo (int n) { note = key = n; }

    static double layerSemis (const LayerP& lp, double noteNow, double pitchMod, const Params& p)
    {
        (void) p;
        return (noteNow - lp.root) + lp.oct * 12 + lp.semi + lp.fine * 0.01 + pitchMod;
    }

    void render (float* L, float* R, int n, const Shared& sh) noexcept
    {
        if (! active) return;
        const auto& p = *sh.p;
        const double fs = sh.fs;
        const double glideK = p.glide > 0.0005f ? 1.0 - std::exp (-1.0 / (p.glide * fs * 0.25)) : 1.0;
        const float velG = 1.0f - p.velSens * (1.0f - vel * vel);
        for (int i = 0; i < n; ++i)
        {
            if (tick == 0) control (sh);
            tick = (tick + 1) % kTick;
            curNote += ((double) note - curNote) * glideK;
            // ---- layers
            float sl = 0, sr = 0;
            bool alive = false;
            for (int Ly = 0; Ly < kLayers; ++Ly)
            {
                const auto& lp = p.layer[Ly];
                if (! lp.on) continue;
                const float lv = lvl[Ly];
                for (int u = 0; u < nUni; ++u)
                {
                    auto& r = rd[Ly][u];
                    if (! r.active) continue;
                    float a, b;
                    const double pf = glideK < 1.0 || pitchModSemis != 0.0 || nUni > 1 ? pitchFactor[Ly] * uniF[u] * std::exp2 ((curNote - note) / 12.0) : pitchFactor[Ly];
                    if (! r.read (pf, speedNow[Ly], posMod[Ly], gate, a, b)) { r.active = false; continue; }
                    alive = true;
                    const float gl = lv * uniGl[u] * panGl[Ly], gr = lv * uniGr[u] * panGr[Ly];
                    sl += a * gl; sr += b * gr;
                }
            }
            // ---- sub + noise
            if (p.subOn && subG > 0)
            {
                const double f = baseHz / std::exp2 ((double) p.subOct);
                subPh += f / fs; subPh -= std::floor (subPh);
                float v;
                switch (p.subShape) { case 1: v = (float) (1.0 - 4.0 * std::abs (subPh - 0.5)); break;
                                      case 2: v = (float) (std::sin (2 * kPi * subPh) >= 0 ? 0.7 : -0.7); break;
                                      default: v = (float) std::sin (2 * kPi * subPh); }
                sl += v * subG; sr += v * subG; alive = true;
            }
            if (p.noiseOn && noiseG > 0)
            {
                const float w = uni() * 2 - 1;
                noiseLp += (w - noiseLp) * (0.05f + 0.95f * p.noiseColor);
                sl += noiseLp * noiseG; sr += noiseLp * noiseG; alive = true;
            }
            // ---- filter
            if (p.fOn) { sl = filt.process (sl, 0); sr = filt.process (sr, 1); }
            // ---- amp
            float g = env[0].next(); env[1].next(); env[2].next();
            if (env[0].st == lumen::synth::Env::Idle || (! alive && ! gate)) { active = false; break; }
            g *= velG * ampMod;
            if (killing) { killG -= (float) (1.0 / (0.003 * fs)); if (killG <= 0) { active = false; break; } g *= killG; }
            if (g == 1.0f && panAll == 0.0f) { L[i] += sl; R[i] += sr; }
            else { L[i] += sl * g * std::min (1.0f, 1.0f - panAll); R[i] += sr * g * std::min (1.0f, 1.0f + panAll); }
            if (! alive && gate) { active = false; break; }
        }
    }

private:
    float uni() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng >> 8) * (1.0f / 16777216.0f); }

    void control (const Shared& sh) noexcept
    {
        const auto& p = *sh.p;
        const double fs = sh.fs;
        // sources
        float src[kNumSrc] {};
        for (int l = 0; l < 2; ++l)
        {
            const float hz = sh.lfoHz[l] * lfoRateMul[l];
            lfoPh[l] += hz * kTick / (float) fs; if (lfoPh[l] >= 1) { lfoPh[l] -= 1; shA[l] = shB[l]; shB[l] = uni() * 2 - 1; }
            static const int map[] { lumen::synth::lSine, lumen::synth::lTri, lumen::synth::lSawUp, lumen::synth::lSawDown, lumen::synth::lSquare, lumen::synth::lSH, lumen::synth::lSmooth };
            src[sLfo1 + l] = lumen::synth::lfoShape (map[std::clamp (p.lfoShape[l], 0, 6)], lfoPh[l], shA[l], shB[l]);
        }
        src[sEnv2] = env[1].v; src[sEnv3] = env[2].v; src[sVel] = vel; src[sKey] = (float) ((curNote - 60) / 60.0);
        src[sMW] = sh.mw; src[sAT] = sh.at; for (int m = 0; m < 4; ++m) src[sMac1 + m] = p.macro[m]; src[sRnd] = rnd;
        float d[kNumDst] {};
        for (auto& s : p.mod) if (s.src > 0 && s.dst > 0 && s.amt != 0.0f) d[s.dst] += src[s.src] * s.amt;
        lfoRateMul[0] = std::exp2 (2.0f * d[dL1Rate]); lfoRateMul[1] = std::exp2 (2.0f * d[dL2Rate]);
        pitchModSemis = 24.0 * d[dPitch] + sh.bendSemis;
        for (int Ly = 0; Ly < kLayers; ++Ly)
        {
            const auto& lp = p.layer[Ly];
            const double semis = layerSemis (lp, (double) note, pitchModSemis, p);
            pitchFactor[Ly] = semis == 0.0 ? 1.0 : std::exp2 (semis / 12.0);
            const float lm = d[dLvlA + Ly];
            lvl[Ly] = lm == 0.0f ? lp.level : std::clamp (lp.level + lm, 0.0f, 2.0f);
            posMod[Ly] = d[dPosA + Ly];
            panGl[Ly] = std::min (1.0f, 1.0f - lp.pan); panGr[Ly] = std::min (1.0f, 1.0f + lp.pan);
        }
        for (int Ly = 0; Ly < kLayers; ++Ly) speedNow[Ly] = d[dSpeed] == 0.0f ? (double) p.layer[Ly].speed : std::max (0.0, (double) p.layer[Ly].speed + d[dSpeed]);
        baseHz = 440.0 * std::exp2 ((curNote - 69 + pitchModSemis) / 12.0);
        // unison
        const float det = std::max (0.0f, p.uniDetune + 50.0f * d[dUniDet]);
        for (int u = 0; u < nUni; ++u)
        {
            const float t = nUni > 1 ? 2.0f * u / (nUni - 1) - 1.0f : 0.0f;
            uniF[u] = nUni > 1 ? std::exp2 (t * det / 1200.0f) : 1.0;
            const float pn = t * p.uniSpread;
            const float ug = nUni > 1 ? 1.0f / std::sqrt ((float) nUni) : 1.0f;
            uniGl[u] = std::min (1.0f, 1.0f - pn) * ug; uniGr[u] = std::min (1.0f, 1.0f + pn) * ug;
        }
        subG = p.subOn ? std::clamp (p.subLevel + d[dSub], 0.0f, 1.5f) : 0.0f;
        noiseG = p.noiseOn ? std::clamp (p.noiseLevel + d[dNoise], 0.0f, 1.5f) : 0.0f;
        panAll = std::clamp (d[dPan], -1.0f, 1.0f);
        ampMod = d[dAmp] == 0.0f ? 1.0f : std::clamp (1.0f + d[dAmp], 0.0f, 2.0f);
        if (p.fOn)
        {
            const double oct = p.fEnv * 8.0 * env[1].v + p.keyTrack * (curNote - 60.0) / 12.0 + 6.0 * d[dCutoff];
            filt.set (p.fType, (float) std::clamp (p.cutoff * std::exp2 (oct), 15.0, 0.46 * fs), std::clamp (p.res + d[dRes], 0.0f, 1.0f), std::clamp (p.drive + d[dDrive], 0.0f, 1.0f), (float) fs);
        }
    }

    float vel = 1, rnd = 0; uint32_t rng = 1;
    double curNote = 60, baseHz = 261.6, speedNow[kLayers] { 1, 1, 1 }, pitchModSemis = 0;
    lumen::synth::Env env[3];
    lumen::synth::VFilter filt; bool filtSet = false;
    float lfoPh[2] {}, shA[2] {}, shB[2] {}, lfoRateMul[2] { 1, 1 };
    LayerReader rd[kLayers][kMaxUni];
    int nUni = 1, tick = 0;
    double pitchFactor[kLayers] { 1, 1, 1 }, uniF[kMaxUni] { 1, 1, 1, 1, 1, 1, 1, 1 }, posMod[kLayers] {};
    float lvl[kLayers] { 1, 1, 1 }, panGl[kLayers] { 1, 1, 1 }, panGr[kLayers] { 1, 1, 1 }, uniGl[kMaxUni] {}, uniGr[kMaxUni] {};
    float subG = 0, noiseG = 0, panAll = 0, ampMod = 1, noiseLp = 0;
    double subPh = 0;
    bool killing = false; float killG = 1;
};

// ------------------------------------------------------------------------------------ engine
class Engine
{
public:
    void prepare (double sr) { sh.fs = sr; for (auto& v : voices) v.active = false; held.clear(); }
    void setLayers (const LayerData* l) { sh.layers = l; }
    void setParams (const Params& np, double bpmLfo1Hz, double bpmLfo2Hz)
    {
        p = np; sh.p = &p;
        sh.lfoHz[0] = (float) bpmLfo1Hz; sh.lfoHz[1] = (float) bpmLfo2Hz;
        for (int e = 0; e < 3; ++e)
        {
            auto& c = sh.ec[e]; const float a = p.env[e][0], d = std::max (0.001f, p.env[e][1]), s = p.env[e][2], r = std::max (0.0005f, p.env[e][3]);
            c.ac = a <= 0.0f ? 1.0f : 1.0f - std::exp (-1.466f / (a * (float) sh.fs));
            c.dc = std::exp (-4.6f / (d * (float) sh.fs)); c.s = std::clamp (s, 0.0f, 1.0f);
            c.rc = std::exp (-6.9f / (r * (float) sh.fs));
        }
    }
    void setBend (float semis) { sh.bendSemis = semis; }
    void setModWheel (float v) { sh.mw = v; }
    void setAftertouch (float v) { sh.at = v; }

    void noteOn (int n, float vel)
    {
        held.push_back (n);
        if (p.poly <= 1)
        {
            for (auto& v : voices) if (v.active && v.gate && ! v.isKilling() && p.legato) { v.legatoTo (n); lastNote = n; return; }
            for (auto& v : voices) if (v.active) v.kill();
        }
        else
        {
            int count = 0; Voice* oldest = nullptr;
            for (auto& v : voices) if (v.active && v.gate && ! v.isKilling()) { ++count; if (! oldest || v.age < oldest->age) oldest = &v; }
            if (count >= std::max (1, p.poly) && oldest) oldest->kill();
        }
        Voice* f = nullptr;
        for (auto& v : voices) if (! v.active) { f = &v; break; }
        if (f == nullptr) { f = &voices[0]; for (auto& v : voices) if (v.age < f->age) f = &v; }
        f->start (n, vel, sh, ++counter, lastNote, (uint32_t) (counter * 2654435761u));
        lastNote = n;
    }
    void noteOff (int n)
    {
        held.erase (std::remove (held.begin(), held.end(), n), held.end());
        if (p.poly <= 1 && ! held.empty())
        {
            for (auto& v : voices) if (v.active && v.gate && ! v.isKilling())
            {
                if (p.legato) v.legatoTo (held.back());
                else { v.kill(); Voice* f = nullptr; for (auto& w : voices) if (! w.active) { f = &w; break; } if (f) f->start (held.back(), 0.8f, sh, ++counter, lastNote, (uint32_t) counter); }
                lastNote = held.back(); break;
            }
            return;
        }
        for (auto& v : voices) if (v.active && v.gate && v.key == n) { if (sustain) sustained.push_back (n); else v.release(); }
    }
    void setSustain (bool on)
    {
        sustain = on;
        if (! on) { for (int n : sustained) if (std::find (held.begin(), held.end(), n) == held.end()) for (auto& v : voices) if (v.active && v.gate && v.key == n) v.release(); sustained.clear(); }
    }
    void allNotesOff() { for (auto& v : voices) if (v.active) v.release(); held.clear(); }
    void kill() { for (auto& v : voices) v.active = false; held.clear(); }
    void render (float* L, float* R, int n) { for (auto& v : voices) if (v.active) v.render (L, R, n, sh); }
    int activeVoices() const { int c = 0; for (auto& v : voices) c += v.active; return c; }

private:
    Params p; Shared sh;
    std::array<Voice, kVoices> voices;
    std::vector<int> held, sustained; bool sustain = false;
    uint64_t counter = 0; int lastNote = -1;
};
} // namespace lumen::ss
