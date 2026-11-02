#include "Engine.h"
#include "OscMath.h"

namespace lxl
{
// ================================================================== envelopes
static void earLevel (float timeSec, float ratio, float fs, float& coef)
{
    const float rate = timeSec * fs;
    coef = rate <= 1.0f ? 0.0f : std::exp (-std::log ((1.0f + ratio) / ratio) / rate);
}

void EnvCoefs::compute (float a, float d, float s, float r, float curve, float fs)
{
    const float ratioA = 0.3f;
    const float ratioDR = 0.0001f * std::pow (10000.0f, curve);
    earLevel (a, ratioA, fs, aCoef);
    earLevel (d, ratioDR, fs, dCoef);
    earLevel (r, ratioDR, fs, rCoef);
    sustain = s;
    aBase = (1.0f + ratioA) * (1.0f - aCoef);
    dBase = (s - ratioDR) * (1.0f - dCoef);
    rBase = -ratioDR * (1.0f - rCoef);
    aCoefN = std::pow (aCoef, (float) kSubBlock);
    dCoefN = std::pow (dCoef, (float) kSubBlock);
    rCoefN = std::pow (rCoef, (float) kSubBlock);
}

inline float EnvState::tick (const EnvCoefs& c) noexcept
{
    switch (stage)
    {
        case Attack:
            level = c.aBase + level * c.aCoef;
            if (level >= 1.0f || c.aCoef == 0.0f) { level = 1.0f; stage = Decay; }
            break;
        case Decay:
            level = c.dBase + level * c.dCoef;
            if (level <= c.sustain || c.dCoef == 0.0f) { level = c.sustain; stage = Sustain; }
            break;
        case Sustain: level = c.sustain; break;
        case Release:
            level = c.rBase + level * c.rCoef;
            if (level <= 1.0e-5f || c.rCoef == 0.0f) { level = 0.0f; stage = Idle; }
            break;
        case Idle: default: level = 0.0f; break;
    }
    return level;
}

inline void EnvState::advance (const EnvCoefs& c, int n) noexcept
{
    auto powN = [n] (float coef, float coefN) { return n == kSubBlock ? coefN : std::pow (coef, (float) n); };
    switch (stage)
    {
        case Attack:
        {
            const float tgt = c.aCoef < 1.0f ? c.aBase / (1.0f - c.aCoef) : 1.0f;
            level = tgt + (level - tgt) * powN (c.aCoef, c.aCoefN);
            if (level >= 1.0f || c.aCoef == 0.0f) { level = 1.0f; stage = Decay; }
            break;
        }
        case Decay:
        {
            const float tgt = c.dCoef < 1.0f ? c.dBase / (1.0f - c.dCoef) : c.sustain;
            level = tgt + (level - tgt) * powN (c.dCoef, c.dCoefN);
            if (level <= c.sustain || c.dCoef == 0.0f) { level = c.sustain; stage = Sustain; }
            break;
        }
        case Sustain: level = c.sustain; break;
        case Release:
        {
            const float tgt = c.rCoef < 1.0f ? c.rBase / (1.0f - c.rCoef) : 0.0f;
            level = tgt + (level - tgt) * powN (c.rCoef, c.rCoefN);
            if (level <= 1.0e-5f || c.rCoef == 0.0f) { level = 0.0f; stage = Idle; }
            break;
        }
        case Idle: default: level = 0.0f; break;
    }
}

// ================================================================== small helpers
static inline float hashRand (int64_t i, uint32_t seed) noexcept
{
    uint32_t x = (uint32_t) (i * 2654435761u) ^ (seed * 0x9E3779B9u);
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return (float) (x >> 8) * (1.0f / 16777216.0f);
}

static inline float lfoShape (int shape, double abs, uint32_t seed) noexcept
{
    const float ph = (float) (abs - std::floor (abs));
    switch (shape)
    {
        case LfoSine:    return fastSin (ph);
        case LfoTri:     return 1.0f - 4.0f * std::abs (ph - 0.5f);
        case LfoSawUp:   return 2.0f * ph - 1.0f;
        case LfoSawDown: return 1.0f - 2.0f * ph;
        case LfoSquare:  return ph < 0.5f ? 1.0f : -1.0f;
        case LfoPump:    { const float u = (1.0f - std::exp (-7.0f * ph)) / (1.0f - std::exp (-7.0f)); return 2.0f * u - 1.0f; }
        case LfoStairs:  return std::floor (ph * 4.0f) / 3.0f * 2.0f - 1.0f;
        case LfoSmoothRandom:
        default:
        {
            const auto idx = (int64_t) std::floor (abs);
            const float a = hashRand (idx, seed), b = hashRand (idx + 1, seed);
            const float t = ph * ph * (3.0f - 2.0f * ph);
            return 2.0f * (a + t * (b - a)) - 1.0f;
        }
    }
}

static inline float insertShape (int type, float x, float amt, float& hold, float& count) noexcept
{
    switch (type)
    {
        case InsDrive:    { const float g = 1.0f + amt * amt * 24.0f; return fastTanh (x * g) / (1.0f + amt * 0.6f); }
        case InsTube:     { const float g = 1.0f + amt * 10.0f; return fastTanh (x * g + 0.35f * amt * x * x * g) * 0.85f; }
        case InsFold:     return foldSample (x * (1.0f + amt * 6.0f)) * 0.8f;
        case InsRectify:  return x < 0.0f ? x * (1.0f - 2.0f * amt) : x;
        case InsBitcrush: { const float lv = std::exp2 (16.0f - amt * 14.5f); return std::round (x * lv) / lv; }
        case InsDownsample:
        {
            const float factor = 1.0f + amt * amt * 48.0f;
            count += 1.0f;
            if (count >= factor) { count -= factor; hold = x; }
            return hold;
        }
        case InsHardClip: { const float g = 1.0f + amt * 14.0f; return juce::jlimit (-1.0f, 1.0f, x * g) * (1.0f / (1.0f + amt * 0.5f)); }
        default: return x;
    }
}

static inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

// ================================================================== Voice
void Voice::prepare (double maxSr)
{
    fs = (float) maxSr;
    for (auto& f : filt) for (auto& c : f) c.prepare (maxSr);
    active = false;
}

void Voice::setSampleRate (double sr)
{
    fs = (float) sr;
    for (auto& f : filt) for (auto& c : f) c.setSampleRate (sr);
    active = false;
}

void Voice::start (int n, float vel, float glideFrom, bool glideOn, const Snapshot& sp, bool legatoOnly)
{
    note = n;
    pitchTarget = (float) n;
    if (legatoOnly && active)
    {
        if (! glideOn) pitchCur = pitchTarget;
        gate = true; sustained = false;
        return;
    }

    velocity = vel;
    pitchCur = glideOn ? glideFrom : (float) n;
    seed = seed * 1664525u + 1013904223u + (uint32_t) n;
    rng = Rng (seed);
    voiceRand = rng.bip();

    if (! active)
    {
        for (auto& f : filt) for (auto& c : f) c.reset();
        fbL = fbR = 0.0f;
        fresh = true;
        for (auto& o : osc) o.prevInc = 0.0f;
        for (int k = 0; k < 4; ++k) { dsHold[k][0] = dsHold[k][1] = 0.0f; dsCount[k] = 0.0f; }
        for (int s = 0; s < kNumModSlots; ++s) { env[s].level = 0.0f; slotPrevVal[s] = 0.0f; }
        amp.level = 0.0f;
    }

    // keep oscillators free-running when a sounding voice is retriggered (avoids clicks)
    if (! active || amp.level < 0.02f)
    {
        for (int o = 0; o < kNumOsc; ++o)
            for (int u = 0; u < kMaxUnison; ++u)
            {
                osc[o].phase[u] = frac (sp.osc[o].phase + sp.osc[o].rndphase * rng.uni());
                osc[o].mphase[u] = 0.0f;
            }
        subPhase = 0.0f;
    }
    // analog drift: fixed per-note tuning offsets for every unison voice
    for (int o = 0; o < kNumOsc; ++o)
        for (int u = 0; u < kMaxUnison; ++u) driftOff[o][u] = rng.bip();
    driftCut = rng.bip();

    for (int s = 0; s < kNumModSlots; ++s)
    {
        env[s].gateOn();
        slotAbs[s] = sp.slot[s].type == ModLfo ? (double) sp.slot[s].phase : 0.0;
    }
    amp.gateOn();
    active = true; gate = true; sustained = false;
}

void Voice::release()
{
    gate = false;
    amp.gateOff();
    for (auto& e : env) e.gateOff();
}

void Voice::render (Engine& e, const Snapshot& sp, int blockOffset, float* outL, float* outR, int n)
{
    int done = 0;
    while (done < n && active)
    {
        const int m = juce::jmin (kSubBlock, n - done);
        renderSub (e, sp, blockOffset + done, outL + done, outR + done, m);
        done += m;
    }
}

void Voice::renderSub (Engine& e, const Snapshot& sp, int blockOffset, float* outL, float* outR, int n)
{
    // ---------------------------------------------------------- glide
    if (sp.glide > 0.0005f)
        pitchCur += (pitchTarget - pitchCur) * (1.0f - std::exp (-(float) n / (sp.glide * fs * 0.25f)));
    else
        pitchCur = pitchTarget;

    // ---------------------------------------------------------- modulation sources
    float src[kNumSources];
    std::copy (std::begin (e.globalSrc), std::end (e.globalSrc), src);
    src[SrcNone] = 0.0f;
    src[SrcVelocity] = velocity;
    src[SrcKeyTrack] = ((float) note - 60.0f) / 60.0f;
    src[SrcAftertouch] = juce::jmax (polyAT, e.chanPressure);
    src[SrcVoiceRandom] = voiceRand;
    src[SrcAmpEnv] = amp.level;

    for (int s = 0; s < kNumModSlots; ++s)
    {
        const auto& sl = sp.slot[s];
        const auto& g = e.slotGlobal[s];
        float v = 0.0f;
        if (sl.type == ModEnv)
        {
            v = env[s].level;
            env[s].advance (e.slotCoefs[s], n);
        }
        else
        {
            double abs;
            if (sl.retrig) { abs = slotAbs[s]; slotAbs[s] += g.incPerSample * n; }
            else abs = g.absStart + g.incPerSample * (double) blockOffset + (sl.type == ModLfo ? (double) sl.phase : 0.0);

            if (sl.type == ModLfo)
            {
                v = lfoShape (sl.shape, abs, 777u + (uint32_t) s + (sl.retrig ? seed : 0u));
                if (! sl.bipolar) v = 0.5f * (v + 1.0f);
            }
            else if (sl.type == ModPerformer)
            {
                const int len = juce::jmax (1, sl.len);
                const auto idx = (int64_t) std::floor (abs);
                const int step = (int) (((idx % len) + len) % len);
                const int prev = (step + len - 1) % len;
                const float t = (float) (abs - (double) idx);
                const float sm = sl.smooth * sl.smooth;
                const float a = sl.steps[prev], b = sl.steps[step];
                v = sm < 0.001f ? b : a + (b - a) * juce::jmin (1.0f, t / sm);
                perfStep[s] = step;
                if (sl.bipolar) v = 2.0f * v - 1.0f;
            }
            else // random
            {
                const auto idx = (int64_t) std::floor (abs);
                const float t = (float) (abs - (double) idx);
                const uint32_t sd = 31337u + (uint32_t) s * 101u + (sl.retrig ? seed : 0u);
                const float a = hashRand (idx - 1, sd), b = hashRand (idx, sd);
                const float sm = sl.smooth;
                float tt = sm < 0.001f ? 1.0f : juce::jmin (1.0f, t / sm);
                tt = tt * tt * (3.0f - 2.0f * tt);
                v = a + (b - a) * tt;
                if (sl.bipolar) v = 2.0f * v - 1.0f;
            }
        }
        slotOut[s] = v;
        src[SrcMod1 + s] = v;
    }

    // ---------------------------------------------------------- destinations
    float off[D::Count] {};
    for (const auto& r : e.voiceRows) off[r.dst] += r.amt * src[r.src];
    std::copy (std::begin (off), std::end (off), lastOffsets);

    float tgt[D::Count];
    for (int d = 1; d < D::GlobalStart; ++d)
        tgt[d] = e.destRange[d].convertFrom0to1 (clamp01 (e.baseNorm[d] + off[d]));

    const float invN = 1.0f / (float) n;
    if (fresh) { std::copy (tgt, tgt + D::GlobalStart, curDest); }
    for (int d = 1; d < D::GlobalStart; ++d) stepD[d] = (tgt[d] - curDest[d]) * invN;

    // ---------------------------------------------------------- oscillator setup
    const float bend = e.pitchBend * (float) sp.pbRange;
    // slow analog wander
    driftTimer -= n;
    if (driftTimer <= 0) { driftTarget = rng.bip(); driftTimer = (int) (fs * (0.15f + 0.35f * rng.uni())); }
    driftWalk += (driftTarget - driftWalk) * (1.0f - std::exp (-(float) n / (0.4f * fs)));
    const float driftSt = sp.drift * (0.06f * driftWalk);       // up to +-6 cents wander
    const float basePitch = pitchCur + bend + tgt[D::VoicePitch] + driftSt;
    const Wavetable* wt[kNumOsc] = { e.tables[0].load (std::memory_order_acquire), e.tables[1].load (std::memory_order_acquire) };
    int mip[kNumOsc] = { 0, 0 };
    float incStart[kNumOsc] {}, incStep[kNumOsc] {};
    int nu[kNumOsc] = { 1, 1 };

    for (int o = 0; o < kNumOsc; ++o)
    {
        const auto& os = sp.osc[o];
        if (! os.on || wt[o] == nullptr) continue;
        const float pitch = basePitch + (float) os.oct * 12.0f + tgt[D::osc (o, D::Semi)] + tgt[D::osc (o, D::Fine)] * 0.01f;
        const float freq = juce::jlimit (1.0f, fs * 0.45f, midiToHz (pitch));
        const float inc = freq / fs;
        incStart[o] = (fresh || osc[o].prevInc <= 0.0f) ? inc : osc[o].prevInc;
        incStep[o] = (inc - incStart[o]) * invN;
        osc[o].prevInc = inc;

        nu[o] = juce::jlimit (1, kMaxUnison, os.voices);
        const float detune = tgt[D::osc (o, D::Detune)];
        const float pan = tgt[D::osc (o, D::Pan)];
        float maxRatio = 1.0f, wsum = 0.0f;
        float w[kMaxUnison];
        for (int u = 0; u < nu[o]; ++u)
        {
            const float pos = nu[o] == 1 ? 0.0f : (2.0f * (float) u / (float) (nu[o] - 1) - 1.0f);
            // slightly irregular spacing (like classic supersaw) avoids a static, phasey beat
            static const float irregular[kMaxUnison] = { 0.0f, 0.07f, -0.05f, 0.04f, -0.08f, 0.06f, -0.03f, 0.05f };
            const float p2 = pos * (1.0f + irregular[u]);
            const float cents = p2 * detune * detune * 60.0f + p2 * detune * 15.0f + sp.drift * driftOff[o][u] * 7.0f;
            osc[o].ratio[u] = std::exp2 (cents / 1200.0f);
            maxRatio = juce::jmax (maxRatio, osc[o].ratio[u]);
            const bool inner = nu[o] <= 2 || std::abs (pos) <= 0.34f;
            w[u] = inner ? 1.0f : os.blend * 1.6f;
            wsum += w[u] * w[u];
            const float pu = juce::jlimit (-1.0f, 1.0f, pan + os.spread * pos * ((u & 1) ? -1.0f : 1.0f));
            const float ang = (pu + 1.0f) * 0.25f * kPi;
            osc[o].gL[u] = std::cos (ang) * 1.41421356f;
            osc[o].gR[u] = std::sin (ang) * 1.41421356f;
        }
        const float norm = (nu[o] == 1 ? 1.0f : 1.25f) / std::sqrt (juce::jmax (1.0e-6f, wsum));
        for (int u = 0; u < nu[o]; ++u) { osc[o].gL[u] *= w[u] * norm; osc[o].gR[u] *= w[u] * norm; }

        const float feff = freq * maxRatio * warpFactor (os.mode, tgt[D::osc (o, D::Warp)])
                           * (1.0f + tgt[D::osc (o, D::Fm)] * 3.0f * os.fmratio * 0.5f);
        const float lv = std::log2 (1024.0f * feff / (0.45f * fs));
        mip[o] = juce::jlimit (0, Wavetable::kLevels - 1, (int) std::ceil (lv));
    }

    // sub / noise
    const float subFreq = midiToHz (basePitch - 12.0f * (float) (1 + sp.subOct));
    const float subInc = subFreq / fs;
    const float keyRel = (basePitch - 60.0f) / 12.0f;
    float cutKey[kNumFilters];
    for (int f = 0; f < kNumFilters; ++f) cutKey[f] = std::exp2 (keyRel * sp.fil[f].key) * (1.0f + sp.drift * 0.04f * (driftCut + driftWalk * 0.5f));

    const float noiseColor = tgt[D::NoiseColor];
    const float nLpA = noiseColor < 0.5f ? 1.0f - std::exp (-kTwoPi * juce::jmin (0.45f * fs, 200.0f * std::exp2 (noiseColor * 2.0f * 6.6f)) / fs) : 1.0f;
    const float nHpA = noiseColor > 0.5f ? 1.0f - std::exp (-kTwoPi * juce::jmin (0.45f * fs, 20.0f * std::exp2 ((noiseColor - 0.5f) * 2.0f * 9.0f)) / fs) : 0.0f;
    static const float metalRatios[6] = { 1.0f, 1.4471f, 1.617f, 1.9265f, 2.5028f, 2.6637f };
    const float metalBase = midiToHz (basePitch) / fs;

    const bool parallel = sp.filterStruct == 1;
    const float velGain = 1.0f - sp.ampVel + sp.ampVel * velocity * velocity;
    const EnvCoefs& ac = e.ampCoefs;
    fresh = false;

    // ---------------------------------------------------------- sample loop
    for (int i = 0; i < n; ++i)
    {
        float aL = 0, aR = 0, bL = 0, bR = 0, dL = 0, dR = 0;
        auto route = [&] (int r, float l, float rr)
        {
            switch (r)
            {
                case RouteA: aL += l; aR += rr; break;
                case RouteB: bL += l; bR += rr; break;
                case RouteAB:
                    if (parallel) { aL += l * 0.7071f; aR += rr * 0.7071f; bL += l * 0.7071f; bR += rr * 0.7071f; }
                    else { aL += l; aR += rr; }
                    break;
                default: dL += l; dR += rr; break;
            }
        };

        for (int o = 0; o < kNumOsc; ++o)
        {
            const auto& os = sp.osc[o];
            if (! os.on || wt[o] == nullptr) continue;
            const Wavetable& t = *wt[o];
            const float inc = incStart[o] + incStep[o] * (float) i;
            const float pos = clamp01 (curDest[D::osc (o, D::Pos)]);
            const float fpos = pos * (float) (t.numFrames - 1);
            const int f0 = juce::jmin (t.numFrames - 1, (int) fpos);
            const int f1 = juce::jmin (t.numFrames - 1, f0 + 1);
            const float ft = fpos - (float) f0;
            const float* t0 = t.frame (f0, mip[o]);
            const float* t1 = t.frame (f1, mip[o]);
            const float warp = clamp01 (curDest[D::osc (o, D::Warp)]);
            const float fm = curDest[D::osc (o, D::Fm)];
            const float ring = clamp01 (curDest[D::osc (o, D::Ring)]);
            const int mode = os.mode;
            auto& rt = osc[o];
            float sL = 0, sR = 0;
            for (int u = 0; u < nu[o]; ++u)
            {
                float p = rt.phase[u];
                const float ms = (fm > 0.0005f || ring > 0.0005f) ? fastSin (rt.mphase[u]) : 0.0f;
                if (fm > 0.0005f) p = frac (p + fm * 1.6f * ms);
                float gain = 1.0f;
                p = warpPhase (mode, p, warp, gain);
                const float x = p * (float) Wavetable::kSize;
                int xi = (int) x;
                const float xf = x - (float) xi;
                xi &= Wavetable::kSize - 1;
                const float a = t0[xi] + xf * (t0[xi + 1] - t0[xi]);
                const float b = t1[xi] + xf * (t1[xi + 1] - t1[xi]);
                float s = (a + ft * (b - a)) * gain;
                if (mode == ModeFold) s = foldSample (s * (1.0f + warp * 5.0f));
                if (ring > 0.0005f) s *= (1.0f - ring) + ring * ms;
                sL += s * rt.gL[u];
                sR += s * rt.gR[u];
                const float pi = inc * rt.ratio[u];
                float np = rt.phase[u] + pi;
                rt.phase[u] = np >= 1.0f ? np - 1.0f : np;
                float nm = rt.mphase[u] + pi * os.fmratio;
                rt.mphase[u] = nm - (float) (int) nm;
            }
            const float lvl = curDest[D::osc (o, D::Level)];
            route (os.route, sL * lvl, sR * lvl);
        }

        if (sp.subOn)
        {
            float s;
            const float ph = subPhase;
            switch (sp.subShape)
            {
                case 0: s = fastSin (ph); break;
                case 1: s = 1.0f - 4.0f * std::abs (ph - 0.5f); break;
                default:
                {
                    s = ph < 0.5f ? 1.0f : -1.0f;
                    s += polyBlep (ph, subInc);
                    s -= polyBlep (frac (ph + 0.5f), subInc);
                    s *= 0.8f;
                    break;
                }
            }
            subPhase += subInc; if (subPhase >= 1.0f) subPhase -= 1.0f;
            const float lv = curDest[D::SubLevel];
            route (sp.subRoute, s * lv, s * lv);
        }

        if (sp.noiseOn)
        {
            float nz[2];
            for (int c = 0; c < 2; ++c)
            {
                float w = rng.bip();
                float v;
                switch (sp.noiseType)
                {
                    case 1:
                    {
                        auto* b = pink[c];
                        b[0] = 0.99886f * b[0] + w * 0.0555179f; b[1] = 0.99332f * b[1] + w * 0.0750759f;
                        b[2] = 0.96900f * b[2] + w * 0.1538520f; b[3] = 0.86650f * b[3] + w * 0.3104856f;
                        b[4] = 0.55000f * b[4] + w * 0.5329522f; b[5] = -0.7616f * b[5] - w * 0.0168980f;
                        v = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f) * 0.2f;
                        b[6] = w * 0.115926f;
                        break;
                    }
                    case 2:
                        noiseCrackle[c] *= 0.55f;
                        if (rng.uni() < 0.0012f) noiseCrackle[c] = rng.bip() > 0.0f ? 1.0f : -1.0f;
                        v = noiseCrackle[c];
                        break;
                    case 3:
                    {
                        v = 0.0f;
                        if (c == 0)
                            for (int k = 0; k < 6; ++k)
                            {
                                metalPhase[k] += metalBase * metalRatios[k] * 4.0f;
                                metalPhase[k] -= std::floor (metalPhase[k]);
                                v += metalPhase[k] < 0.5f ? 0.18f : -0.18f;
                            }
                        else v = nz[0];
                        break;
                    }
                    default:
                        if (sp.noiseType == 4)
                        {
                            noiseCrackle[c] *= 0.5f;
                            if (rng.uni() < 0.0004f) noiseCrackle[c] = rng.bip();
                            v = w * 0.15f + noiseCrackle[c];
                        }
                        else v = w * 0.6f;
                        break;
                }
                // tilt colour
                noiseTilt[c] += nLpA * (v - noiseTilt[c]);
                v = noiseTilt[c];
                if (nHpA > 0.0f) { noiseHp[c] += nHpA * (v - noiseHp[c]); v -= noiseHp[c]; }
                nz[c] = v;
            }
            const float lv = curDest[D::NoiseLevel];
            route (sp.noiseRoute, nz[0] * lv, nz[1] * lv);
        }

        // feedback into filter A input
        const float fbAmt = curDest[D::Feedback];
        if (fbAmt > 0.0005f) { aL += fbL * fbAmt * 1.4f; aR += fbR * fbAmt * 1.4f; }

        // pre-filter inserts (on both buses)
        for (int k = 0; k < kNumInserts; ++k)
        {
            const auto& is = sp.ins[k];
            if (is.type == InsOff || is.pos != 0) continue;
            const float amt = clamp01 (curDest[D::Ins1 + k]);
            float* bus[4] = { &aL, &aR, &bL, &bR };
            for (int c = 0; c < 4; ++c)
            {
                const float x = *bus[c];
                const float y = insertShape (is.type, x, amt, dsHold[k][c & 1], dsCount[k]);
                *bus[c] = x + is.mix * (y - x);
            }
        }

        // filters
        auto runFilter = [&] (int f, float& l, float& r)
        {
            const auto& fsn = sp.fil[f];
            if (! fsn.on) return;
            const float cut = curDest[D::fil (f, D::Cutoff)] * cutKey[f];
            const float res = clamp01 (curDest[D::fil (f, D::Reso)]);
            const float drv = clamp01 (curDest[D::fil (f, D::Drive)]);
            const float mx = clamp01 (curDest[D::fil (f, D::Mix)]);
            const float yl = filt[f][0].process (l, fsn.type, cut, res, drv);
            const float yr = filt[f][1].process (r, fsn.type, cut, res, drv);
            l += mx * (yl - l);
            r += mx * (yr - r);
        };

        float oL, oR;
        if (parallel)
        {
            runFilter (0, aL, aR);
            runFilter (1, bL, bR);
            oL = aL + bL; oR = aR + bR;
        }
        else
        {
            runFilter (0, aL, aR);
            oL = aL + bL; oR = aR + bR;
            runFilter (1, oL, oR);
        }
        fbL = fastTanh (oL); fbR = fastTanh (oR);
        oL += dL; oR += dR;

        // post-filter inserts
        for (int k = 0; k < kNumInserts; ++k)
        {
            const auto& is = sp.ins[k];
            if (is.type == InsOff || is.pos != 1) continue;
            const float amt = clamp01 (curDest[D::Ins1 + k]);
            const float yl = insertShape (is.type, oL, amt, dsHold[k + 2][0], dsCount[k + 2]);
            const float yr = insertShape (is.type, oR, amt, dsHold[k + 2][1], dsCount[k + 2]);
            oL += is.mix * (yl - oL);
            oR += is.mix * (yr - oR);
        }

        const float env = amp.tick (ac);
        const float g = env * velGain * curDest[D::AmpLevel] * 1.4f;
        const float pan = juce::jlimit (-1.0f, 1.0f, curDest[D::AmpPan]);
        const float pl = pan > 0.0f ? 1.0f - pan : 1.0f, pr = pan < 0.0f ? 1.0f + pan : 1.0f;
        outL[i] += oL * g * pl;
        outR[i] += oR * g * pr;

        for (int d = 1; d < D::GlobalStart; ++d) curDest[d] += stepD[d];

        if (amp.stage == EnvState::Idle) { active = false; break; }
    }
}

// ================================================================== Engine
Engine::Engine (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    jassert (numModDests() == D::Count);
    for (auto& t : tables) t.store (nullptr);
    voiceRows.reserve (kNumMatrix);
    for (auto* prm : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            raw[rp->getParameterID().toStdString()] = apvts.getRawParameterValue (rp->getParameterID());

    const auto& dests = modDests();
    for (int d = 1; d < D::Count; ++d)
    {
        destParam[d] = apvts.getParameter (dests[(size_t) d].paramId);
        jassert (destParam[d] != nullptr);
        destRange[d] = destParam[d]->getNormalisableRange();
        destRange[d].interval = 0.0f;
    }
    for (auto& a : modDisplay) a.store (0.0f);
    for (auto& a : slotDisplay) a.store (0.0f);
    for (auto& a : stepDisplay) a.store (0);
}

std::atomic<float>* Engine::p (const juce::String& id)
{
    auto it = raw.find (id.toStdString());
    jassert (it != raw.end());
    return it != raw.end() ? it->second : nullptr;
}

void Engine::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    for (auto& v : voices) { v.prepare (sr * 2.0); v.setSampleRate (sr * osFactor); }
    mix.setSize (2, juce::jmax (64, maxBlock));
    osBuf.setSize (2, juce::jmax (64, maxBlock) * 2);
    decimator.init();
    dist.prepare (sr); chorus.prepare (sr); phaser.prepare (sr); delay.prepare (sr); reverb.prepare (sr); tape.prepare (sr);
    hyper.prepare (sr); ott.prepare (sr); eq.prepare (sr); utility.prepare (sr);
    smoothedVol = -1.0f;
}

void Engine::reset()
{
    for (auto& v : voices) v.kill();
    heldNotes.clear();
    delay.clear(); reverb.clear();
}

void Engine::readSnapshot()
{
    auto f = [this] (const juce::String& id) { return p (id)->load (std::memory_order_relaxed); };
    auto i = [&] (const juce::String& id) { return (int) std::lround (f (id)); };
    auto b = [&] (const juce::String& id) { return f (id) > 0.5f; };

    for (int o = 0; o < kNumOsc; ++o)
    {
        auto& s = snap.osc[o];
        s.on = b (oscId (o, "on")); s.mode = i (oscId (o, "mode")); s.oct = i (oscId (o, "oct"));
        s.voices = i (oscId (o, "voices")); s.route = i (oscId (o, "route"));
        s.spread = f (oscId (o, "spread")); s.blend = f (oscId (o, "blend")); s.fmratio = f (oscId (o, "fmratio"));
        s.phase = f (oscId (o, "phase")); s.rndphase = f (oscId (o, "rndphase"));
    }
    snap.subOn = b ("sub_on"); snap.subOct = i ("sub_oct"); snap.subShape = i ("sub_shape"); snap.subRoute = i ("sub_route");
    snap.noiseOn = b ("noise_on"); snap.noiseType = i ("noise_type"); snap.noiseRoute = i ("noise_route");
    for (int k = 0; k < kNumFilters; ++k)
    {
        snap.fil[k].on = b (filId (k, "on")); snap.fil[k].type = i (filId (k, "type")); snap.fil[k].key = f (filId (k, "key"));
    }
    snap.filterStruct = i ("filter_struct");
    for (int k = 0; k < kNumInserts; ++k)
    {
        snap.ins[k].type = i (insId (k, "type")); snap.ins[k].pos = i (insId (k, "pos")); snap.ins[k].mix = f (insId (k, "mix"));
    }
    snap.ampA = f ("amp_a"); snap.ampD = f ("amp_d"); snap.ampS = f ("amp_s"); snap.ampR = f ("amp_r");
    snap.ampCurve = f ("amp_curve"); snap.ampVel = f ("amp_vel");
    snap.poly = i ("voice_poly"); snap.voiceMode = i ("voice_mode"); snap.pbRange = i ("voice_pbrange");
    snap.glide = f ("voice_glide"); snap.masterVol = f ("master_vol"); snap.drift = f ("voice_drift");

    for (int s = 0; s < kNumModSlots; ++s)
    {
        auto& sl = snap.slot[s];
        sl.type = i (modId (s, "type")); sl.div = i (modId (s, "div")); sl.shape = i (modId (s, "shape")); sl.len = i (modId (s, "len"));
        sl.rate = f (modId (s, "rate")); sl.phase = f (modId (s, "phase")); sl.smooth = f (modId (s, "smooth"));
        sl.a = f (modId (s, "a")); sl.d = f (modId (s, "d")); sl.s = f (modId (s, "s")); sl.r = f (modId (s, "r"));
        sl.curve = f (modId (s, "curve"));
        sl.sync = b (modId (s, "sync")); sl.retrig = b (modId (s, "retrig")); sl.bipolar = b (modId (s, "bipolar"));
        for (int k = 0; k < kNumSteps; ++k) sl.steps[k] = f (stepId (s, k));
    }
    for (int m = 0; m < kNumMacros; ++m) snap.macro[m] = f (macroId (m));

    snap.distOn = b ("fx_dist_on"); snap.distType = i ("fx_dist_type"); snap.distTone = f ("fx_dist_tone"); snap.distMix = f ("fx_dist_mix");
    snap.chorusOn = b ("fx_chorus_on"); snap.chorusRate = f ("fx_chorus_rate"); snap.chorusDepth = f ("fx_chorus_depth");
    snap.phaserOn = b ("fx_phaser_on"); snap.phaserRate = f ("fx_phaser_rate"); snap.phaserDepth = f ("fx_phaser_depth"); snap.phaserFb = f ("fx_phaser_fb");
    snap.delayOn = b ("fx_delay_on"); snap.delaySync = b ("fx_delay_sync"); snap.delayDiv = i ("fx_delay_div"); snap.delayTime = f ("fx_delay_time");
    snap.delayPP = b ("fx_delay_pp"); snap.delayTone = f ("fx_delay_tone");
    snap.revOn = b ("fx_rev_on"); snap.revDecay = f ("fx_rev_decay"); snap.revDamp = f ("fx_rev_damp"); snap.revPre = f ("fx_rev_pre"); snap.revWidth = f ("fx_rev_width");
    snap.tapeOn = b ("fx_tape_on"); snap.tapeWow = f ("fx_tape_wow"); snap.tapeFlutter = f ("fx_tape_flutter"); snap.tapeSat = f ("fx_tape_sat");
    snap.clipOn = b ("fx_clip_on");
    snap.hyperOn = b ("fx_hyper_on"); snap.hyperRate = f ("fx_hyper_rate"); snap.hyperDetune = f ("fx_hyper_detune"); snap.hyperDim = f ("fx_hyper_dim");
    snap.ottOn = b ("fx_ott_on"); snap.ottTime = f ("fx_ott_time"); snap.ottUp = f ("fx_ott_up"); snap.ottDown = f ("fx_ott_down"); snap.ottGain = f ("fx_ott_gain");
    snap.eqOn = b ("fx_eq_on"); snap.eqHp = f ("fx_eq_hp"); snap.eqLow = f ("fx_eq_low"); snap.eqMidF = f ("fx_eq_midf"); snap.eqMid = f ("fx_eq_mid");
    snap.bassMono = f ("fx_bassmono");
}

void Engine::noteOn (int note, float vel)
{
    const bool glideOn = snap.glide > 0.0005f;
    if (snap.voiceMode != 0) // mono / legato
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        heldNotes.push_back (note);
        auto& v = voices[0];
        for (int k = 1; k < kMaxVoices; ++k) if (voices[(size_t) k].active) voices[(size_t) k].release();
        const bool legato = snap.voiceMode == 2 && v.active && v.gate;
        const float from = v.active ? v.pitchCur : lastPitch;
        v.start (note, vel, from, glideOn && (v.active || snap.voiceMode == 1), snap, legato);
        v.age = ++noteCounter;
        lastPitch = (float) note;
        return;
    }

    // poly: retrigger same note if still held
    Voice* target = nullptr;
    for (auto& v : voices)
        if (v.active && v.note == note && (v.gate || v.sustained)) { target = &v; break; }

    if (target == nullptr)
    {
        int activeCount = 0;
        for (auto& v : voices) if (v.active) ++activeCount;
        if (activeCount < snap.poly)
            for (auto& v : voices) if (! v.active) { target = &v; break; }
    }
    if (target == nullptr)
    {
        // steal: quietest released voice, else oldest
        float best = 1.0e9f;
        for (auto& v : voices)
            if (v.active && ! v.gate && ! v.sustained && v.amp.level < best) { best = v.amp.level; target = &v; }
        if (target == nullptr)
        {
            uint64_t oldest = UINT64_MAX;
            for (auto& v : voices) if (v.active && v.age < oldest) { oldest = v.age; target = &v; }
        }
    }
    if (target == nullptr) target = &voices[0];
    target->start (note, vel, lastPitch, glideOn, snap, false);
    target->age = ++noteCounter;
    lastPitch = (float) note;
}

void Engine::noteOff (int note)
{
    if (snap.voiceMode != 0)
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        auto& v = voices[0];
        if (! v.active || v.note != note) return;
        if (! heldNotes.empty())
        {
            const int back = heldNotes.back();
            v.start (back, v.velocity, v.pitchCur, snap.glide > 0.0005f, snap, true);
            lastPitch = (float) back;
        }
        else if (sustainPedal) v.sustained = true;
        else v.release();
        return;
    }
    for (auto& v : voices)
        if (v.active && v.gate && v.note == note)
        {
            if (sustainPedal) { v.gate = false; v.sustained = true; }
            else v.release();
        }
}

void Engine::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff()) noteOff (m.getNoteNumber());
    else if (m.isPitchWheel()) pitchBend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
    else if (m.isController())
    {
        if (m.getControllerNumber() == 1) modWheel = (float) m.getControllerValue() / 127.0f;
        else if (m.getControllerNumber() == 64)
        {
            sustainPedal = m.getControllerValue() >= 64;
            if (! sustainPedal)
                for (auto& v : voices) if (v.active && v.sustained) { v.sustained = false; v.release(); }
        }
        else if (m.getControllerNumber() == 120 || m.getControllerNumber() == 123)
        {
            for (auto& v : voices) if (v.active) v.release();
            heldNotes.clear();
        }
    }
    else if (m.isChannelPressure()) chanPressure = (float) m.getChannelPressureValue() / 127.0f;
    else if (m.isAftertouch())
    {
        for (auto& v : voices)
            if (v.active && v.note == m.getNoteNumber()) v.polyAT = (float) m.getAfterTouchValue() / 127.0f;
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (auto& v : voices) if (v.active) v.release();
        heldNotes.clear();
    }
}

void Engine::renderVoices (int start, int n)
{
    auto& target = osFactor == 2 ? osBuf : mix;
    start *= osFactor;
    n *= osFactor;
    float* L = target.getWritePointer (0) + start;
    float* R = target.getWritePointer (1) + start;
    for (auto& v : voices)
        if (v.active) v.render (*this, snap, start, L, R, n);
}

void Engine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const HostInfo& host)
{
    const int n = buffer.getNumSamples();
    if (mix.getNumSamples() < n) mix.setSize (2, n, false, false, true);
    if (osBuf.getNumSamples() < n * 2) osBuf.setSize (2, n * 2, false, false, true);
    mix.clear (0, n);
    hostInfo = host;
    readSnapshot();

    const int wantOs = p ("quality")->load() > 0.5f ? 2 : 1;
    if (wantOs != osFactor)
    {
        osFactor = wantOs;
        for (auto& v : voices) v.setSampleRate (sampleRate * osFactor);
        decimator.reset();
    }
    if (osFactor == 2) osBuf.clear (0, n * 2);

    // ---- base values & matrix
    for (int d = 1; d < D::Count; ++d) baseNorm[d] = destParam[d]->getValue();
    voiceRows.clear();
    std::array<ModRow, kNumMatrix> globalRows {};
    int numGlobalRows = 0;
    for (int r = 0; r < kNumMatrix; ++r)
    {
        const int s = (int) std::lround (p (mtxId (r, "src"))->load());
        const int d = (int) std::lround (p (mtxId (r, "dst"))->load());
        const float a = p (mtxId (r, "amt"))->load();
        if (s <= 0 || d <= 0 || s >= kNumSources || d >= D::Count || std::abs (a) < 1.0e-4f) continue;
        if (d >= D::GlobalStart) globalRows[(size_t) numGlobalRows++] = { s, d, a };
        else voiceRows.push_back ({ s, d, a });
    }

    // global sources
    std::fill (std::begin (globalSrc), std::end (globalSrc), 0.0f);
    globalSrc[SrcModWheel] = modWheel;
    globalSrc[SrcPitchBend] = pitchBend;
    globalSrc[SrcAftertouch] = chanPressure;
    for (int m = 0; m < kNumMacros; ++m) globalSrc[SrcMacro1 + m] = snap.macro[m];

    float gOff[D::Count] {};
    for (int k = 0; k < numGlobalRows; ++k) if (const auto& r = globalRows[(size_t) k]; isGlobalSource (r.src) || r.src == SrcAftertouch) gOff[r.dst] += r.amt * globalSrc[r.src];
    for (int d = D::GlobalStart; d < D::Count; ++d)
        globalVal[d] = destRange[d].convertFrom0to1 (clamp01 (baseNorm[d] + gOff[d]));

    // ---- envelopes (at the internal voice rate)
    const float fs = (float) sampleRate;
    const float fsVoice = fs * (float) osFactor;
    ampCoefs.compute (globalVal[D::AmpA], globalVal[D::AmpD], snap.ampS, globalVal[D::AmpR], snap.ampCurve, fsVoice);
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const auto& sl = snap.slot[s];
        slotCoefs[s].compute (sl.a, s == 0 ? globalVal[D::Mod1Decay] : (s == 1 ? globalVal[D::Mod2Decay] : sl.d), sl.s, sl.r, sl.curve, fsVoice);
    }

    // ---- tempo & free-running modulator phases
    const double bpm = juce::jlimit (20.0, 999.0, host.bpm > 0.0 ? host.bpm : 120.0);
    const double ppqStart = host.playing ? host.ppq : internalPpq;
    internalPpq = ppqStart + (double) n * bpm / 60.0 / sampleRate;
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const auto& sl = snap.slot[s];
        auto& g = slotGlobal[s];
        if (sl.type == ModEnv) { g = {}; continue; }
        const double beats = syncDivBeats (sl.div);
        if (sl.sync)
        {
            g.absStart = ppqStart / beats;
            g.incPerSample = bpm / 60.0 / beats / (sampleRate * osFactor);
        }
        else
        {
            g.incPerSample = (double) sl.rate / (sampleRate * osFactor);
            g.absStart = freePhase[s];
            freePhase[s] += g.incPerSample * n * osFactor;
            if (freePhase[s] > 1.0e6) freePhase[s] = std::fmod (freePhase[s], 1024.0);
        }
    }

    // ---- MIDI, sample accurate
    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, n, meta.samplePosition);
        if (t > pos) { renderVoices (pos, t - pos); pos = t; }
        handleMidi (meta.getMessage());
    }
    if (pos < n) renderVoices (pos, n - pos);

    if (osFactor == 2)
        decimator.process (osBuf.getReadPointer (0), osBuf.getReadPointer (1), mix.getWritePointer (0), mix.getWritePointer (1), n);

    applyFx (n);

    // ---- output
    const float vol = dbToGain (snap.masterVol);
    if (smoothedVol < 0.0f) smoothedVol = vol;
    float* L = mix.getWritePointer (0);
    float* R = mix.getWritePointer (1);
    float pkL = 0.0f, pkR = 0.0f;
    const float vs = 1.0f - std::exp (-1.0f / (0.02f * fs));
    const float dcA = 1.0f - std::exp (-kTwoPi * 8.0f / fs);
    for (int i = 0; i < n; ++i)
    {
        smoothedVol += vs * (vol - smoothedVol);
        float l = L[i] * smoothedVol, r = R[i] * smoothedVol;
        dcL += dcA * (l - dcL); l -= dcL;
        dcR += dcA * (r - dcR); r -= dcR;
        if (snap.clipOn)
        {
            auto clip = [] (float x) { const float a = std::abs (x); return a < 0.9f ? x : std::copysign (0.9f + 0.1f * fastTanh ((a - 0.9f) * 10.0f), x); };
            l = clip (l); r = clip (r);
        }
        if (! std::isfinite (l)) l = 0.0f;
        if (! std::isfinite (r)) r = 0.0f;
        L[i] = l; R[i] = r;
        pkL = juce::jmax (pkL, std::abs (l)); pkR = juce::jmax (pkR, std::abs (r));
    }
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        buffer.copyFrom (c, 0, mix, juce::jmin (c, 1), 0, n);
    peakL.store (pkL);
    peakR.store (pkR);

    // ---- UI feedback
    const Voice* show = nullptr;
    int count = 0;
    for (auto& v : voices)
        if (v.active) { ++count; if (show == nullptr || v.age > show->age) show = &v; }
    activeVoiceCount.store (count);
    for (int d = 1; d < D::Count; ++d)
        modDisplay[(size_t) d].store (d >= D::GlobalStart ? gOff[d] : (show != nullptr ? show->lastOffsets[d] : 0.0f));
    for (int s = 0; s < kNumModSlots; ++s)
    {
        if (show != nullptr)
        {
            slotDisplay[(size_t) s].store (show->slotOut[s]);
            stepDisplay[(size_t) s].store (show->perfStep[s]);
        }
        else if (snap.slot[s].type == ModPerformer)
        {
            const int len = juce::jmax (1, snap.slot[s].len);
            const auto idx = (int64_t) std::floor (slotGlobal[s].absStart);
            stepDisplay[(size_t) s].store ((int) (((idx % len) + len) % len));
            slotDisplay[(size_t) s].store (0.0f);
        }
        else slotDisplay[(size_t) s].store (0.0f);
    }
}

void Engine::Decimator::init()
{
    const int M = kTaps / 2;
    float sum = 0.0f;
    for (int k = 0; k < kTaps; ++k)
    {
        const float x = (float) (k - M);
        const float sinc = k == M ? 1.0f : std::sin (kPi * 0.5f * x) / (kPi * 0.5f * x);
        // Blackman-Harris window
        const float t = (float) k / (float) (kTaps - 1);
        const float w = 0.35875f - 0.48829f * std::cos (kTwoPi * t) + 0.14128f * std::cos (2.0f * kTwoPi * t) - 0.01168f * std::cos (3.0f * kTwoPi * t);
        h[k] = sinc * w;
        sum += h[k];
    }
    for (auto& v : h) v /= sum;
    reset();
}

void Engine::Decimator::process (const float* inL, const float* inR, float* outL, float* outR, int nOut)
{
    for (int i = 0; i < nOut; ++i)
    {
        for (int k = 0; k < 2; ++k)
        {
            const float l = inL[i * 2 + k], r = inR[i * 2 + k];
            hist[0][pos] = l; hist[0][pos + kTaps] = l;
            hist[1][pos] = r; hist[1][pos + kTaps] = r;
            pos = (pos + 1) % kTaps;
        }
        // hist[c][pos .. pos+kTaps-1] holds the last kTaps samples, oldest first
        const float* hl = hist[0] + pos;
        const float* hr = hist[1] + pos;
        float al = 0.0f, ar = 0.0f;
        for (int k = 0; k < kTaps; ++k) { al += h[k] * hl[k]; ar += h[k] * hr[k]; }
        outL[i] += al;
        outR[i] += ar;
    }
}

void Engine::applyFx (int n)
{
    float* L = mix.getWritePointer (0);
    float* R = mix.getWritePointer (1);
    const double bpm = juce::jlimit (20.0, 999.0, hostInfo.bpm > 0.0 ? hostInfo.bpm : 120.0);

    // Serum-like order: Hyper > Distortion > OTT > Chorus > Phaser > Delay > Reverb > EQ > Tape > Stereo
    if (snap.hyperOn)  hyper.process (L, R, n, snap.hyperRate, snap.hyperDetune, snap.hyperDim, globalVal[D::HyperMix]);
    if (snap.distOn)   dist.process (L, R, n, snap.distType, globalVal[D::DistDrive], snap.distTone, snap.distMix);
    if (snap.ottOn)    ott.process (L, R, n, globalVal[D::OttDepth], snap.ottTime, snap.ottUp, snap.ottDown, snap.ottGain);
    if (snap.chorusOn) chorus.process (L, R, n, snap.chorusRate, snap.chorusDepth, globalVal[D::ChorusMix]);
    if (snap.phaserOn) phaser.process (L, R, n, snap.phaserRate, snap.phaserDepth, snap.phaserFb, globalVal[D::PhaserMix]);
    if (snap.delayOn)
    {
        const float t = snap.delaySync ? (float) (syncDivBeats (snap.delayDiv) * 60.0 / bpm) : snap.delayTime * 0.001f;
        delay.process (L, R, n, t, globalVal[D::DelayFb], snap.delayPP, snap.delayTone, globalVal[D::DelayMix]);
    }
    if (snap.revOn) reverb.process (L, R, n, globalVal[D::RevSize], snap.revDecay, snap.revDamp, snap.revPre, snap.revWidth, globalVal[D::RevMix]);
    if (snap.eqOn)  eq.process (L, R, n, snap.eqHp, snap.eqLow, snap.eqMidF, snap.eqMid, globalVal[D::EqHigh]);
    if (snap.tapeOn) tape.process (L, R, n, snap.tapeWow, snap.tapeFlutter, snap.tapeSat, globalVal[D::TapeMix]);
    const float width = globalVal[D::Width];
    if (std::abs (width - 1.0f) > 0.001f || snap.bassMono > 21.0f) utility.process (L, R, n, width, snap.bassMono);
}
} // namespace lxl
