#pragma once
#include "VocalDsp.h"
#include "ReverbEngine.h"
#include <atomic>

// =====================================================================================
// Lumen Vocal engine
//   in -> low cut -> DE-ESS -> COMP -> EQ (body / presence / air) -> WARMTH
//      -> TUNE (PSOLA, key/scale, retune speed, amount, transpose, formant)
//      -> + DOUBLE (ADT pair) + 2 HARMONY voices (diatonic, own PSOLAs)
//      -> FX bus: ping-pong DELAY + REVERB, both ducked while the singer sings
//   Pitch is detected on the cleaned input; every voice shares one constant latency.
// =====================================================================================
namespace lumen::vox
{

struct Params
{
    float inDb = 0, lowCut = 90;
    bool tune = true; int key = 0, scale = Scale::Chromatic; float retuneMs = 40, amount = 1, transpose = 0, formant = 0;
    float deess = 0.3f, deessFreq = 6500, comp = 0.4f;
    float body = 0, presence = 0, air = 0, warmth = 0;
    float dbl = 0, width = 0.7f;
    int h1 = 0, h2 = 0; float h1Lvl = 0.5f, h2Lvl = 0.5f;
    float dly = 0; int dlyDiv = 3; float dlyFb = 0.35f;
    float verb = 0, verbSize = 0.5f, duck = 0.5f;
    float outDb = 0;
    double bpm = 120.0;
};

class VocalEngine
{
public:
    static constexpr int kMaxBlock = 512;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        L = Psola::minLatency (fs);
        for (auto* ps : { &psLead, &psH1, &psH2 }) ps->prepare (fs, L);
        yin.prepare (fs);
        int n = 1; while (n < L + 8) n <<= 1;
        dryLine.assign ((size_t) n, 0.0f); dryMask = n - 1; dryPos = 0;
        hp1.reset(); hp2.reset(); eqB.reset(); eqP.reset(); eqA.reset();
        deess.prepare (fs); comp.prepare (fs); dbl.prepare (fs);
        const int dn = (int) (fs * 3.2);
        dlyL.assign ((size_t) dn, 0.0f); dlyR.assign ((size_t) dn, 0.0f); dlyPos = 0;
        verb.prepare (fs);
        shift = hShift[0] = hShift[1] = 0.0f; desired = hDesired[0] = hDesired[1] = 0.0f;
        T = 0.0f; curTarget = -1; tuneMix = 1.0f;
        envF = envS = 0.0f;
        fbLpL = fbLpR = fbHpL = fbHpR = 0.0f; fbHpzL = fbHpzR = 0.0f;
        lastLow = lastBody = lastPres = lastAir = -999.0f;
        inG = outG = 1.0f; first = true;
        frame = {};
    }

    int latency() const noexcept { return L; }

    void setParams (const Params& np) noexcept
    {
        p = np;
        if (std::abs (p.lowCut - lastLow) > 0.1f) { hp1.highPass (fs, p.lowCut, 0.5412); hp2.highPass (fs, p.lowCut, 1.3066); lastLow = p.lowCut; }
        if (std::abs (p.body - lastBody) > 0.01f) { eqB.peak (fs, 260.0, 0.9, p.body); lastBody = p.body; }
        if (std::abs (p.presence - lastPres) > 0.01f) { eqP.peak (fs, 3200.0, 0.8, p.presence); lastPres = p.presence; }
        if (std::abs (p.air - lastAir) > 0.01f) { eqA.highShelf (fs, 11000.0, p.air); lastAir = p.air; }
        deess.set (p.deess, p.deessFreq);
        comp.set (p.comp);
        ReverbEngine::Params rp;
        rp.size = 0.35f + 0.6f * p.verbSize; rp.decay = 0.7f + 4.3f * p.verbSize; rp.preDelayMs = 18.0f + 20.0f * p.verbSize;
        rp.dampHz = 7500.0f; rp.lowCutHz = 260.0f; rp.highCutHz = 10000.0f; rp.early = 0.35f; rp.diffusion = 0.8f; rp.width = 1.1f; rp.modDepth = 0.35f; rp.modRate = 0.5f;
        verb.setParams (rp);
        static const double beats[] { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
        dlyTime = std::clamp ((float) (fs * 60.0 / std::max (30.0, p.bpm) * beats[std::clamp (p.dlyDiv, 0, 5)]), 16.0f, (float) dlyL.size() - 4.0f);
        if (first) { inG = std::pow (10.0f, p.inDb / 20.0f); outG = std::pow (10.0f, p.outDb / 20.0f); tuneMix = p.tune ? 1.0f : 0.0f; first = false; }
    }

    void process (float* outL, float* outR, const float* inL, const float* inR, int n) noexcept
    {
        const float sm = 1.0f - (float) std::exp (-1.0 / (0.02 * fs));
        const float rt = 1.0f - (float) std::exp (-1.0 / (std::max (0.3, (double) p.retuneMs) * 0.001 * fs));
        const float hr = 1.0f - (float) std::exp (-1.0 / (0.012 * fs));
        const float fr = std::pow (2.0f, p.formant / 12.0f);
        const float inT = std::pow (10.0f, p.inDb / 20.0f), outT = std::pow (10.0f, p.outDb / 20.0f);
        const float eA = 1.0f - (float) std::exp (-1.0 / (0.005 * fs)), eR = 1.0f - (float) std::exp (-1.0 / (0.25 * fs)), eS = 1.0f - (float) std::exp (-1.0 / (2.0 * fs));
        const float fbHpC = (float) std::exp (-2.0 * kPi * 280.0 / fs), fbLpC = 1.0f - (float) std::exp (-2.0 * kPi * 6500.0 / fs);
        const float wl = 0.5f + 0.5f * p.width, wr = 0.5f - 0.5f * p.width;
        verb.beginBlock (n);
        for (int i = 0; i < n; ++i)
        {
            inG += (inT - inG) * sm; outG += (outT - outG) * sm;
            tuneMix += ((p.tune ? 1.0f : 0.0f) - tuneMix) * sm;
            float x = 0.5f * (inL[i] + inR[i]) * inG;
            inPk = std::max (inPk, std::abs (x));
            x = hp2.process (hp1.process (x));
            if (yin.push (x)) onPitch();

            // tone chain (zero latency)
            float y = deess.process (x);
            y = comp.process (y);
            y = eqA.process (eqP.process (eqB.process (y)));
            if (p.warmth > 0.001f)
            {
                const float d = 1.0f + 5.0f * p.warmth;
                const float s = std::tanh (d * y + 0.08f * p.warmth) - std::tanh (0.08f * p.warmth);
                y = y + p.warmth * (s / d * (1.0f + 0.6f * p.warmth) - y);
            }

            // tuning
            shift += (desired - shift) * rt;
            const float ratio = std::pow (2.0f, shift / 12.0f);
            float lead = psLead.process (y, T, ratio, fr);
            dryLine[(size_t) dryPos] = y;
            const float plain = dryLine[(size_t) ((dryPos - L) & dryMask)];
            dryPos = (dryPos + 1) & dryMask;
            lead = plain + (lead - plain) * tuneMix;

            float bl = lead, br = lead;
            // harmonies (always from the tuned target, panned opposite each other)
            for (int h = 0; h < 2; ++h)
            {
                const int iv = h == 0 ? p.h1 : p.h2;
                if (iv == 0) continue;
                hShift[h] += (hDesired[h] - hShift[h]) * hr;
                const float v = (h == 0 ? psH1 : psH2).process (y, T, std::pow (2.0f, hShift[h] / 12.0f), fr) * (h == 0 ? p.h1Lvl : p.h2Lvl);
                const float pl = h == 0 ? wl : wr, pr = h == 0 ? wr : wl;
                bl += v * pl * 1.2f; br += v * pr * 1.2f;
            }
            if (p.dbl > 0.001f)
            {
                float dlv, drv; dbl.process (lead, dlv, drv);
                bl += p.dbl * 0.8f * (dlv * wl + drv * wr); br += p.dbl * 0.8f * (drv * wl + dlv * wr);
            }

            // ducking envelope from the lead
            const float al = std::abs (lead);
            envF += (al - envF) * (al > envF ? eA : eR);
            envS = std::max (envF, envS + (envF - envS) * eS);
            const float duckG = 1.0f - p.duck * 0.85f * std::clamp (envF / (envS + 1e-6f), 0.0f, 1.0f);

            // FX bus
            const float send = 0.5f * (bl + br);
            float fxL = 0.0f, fxR = 0.0f;
            if (p.dly > 0.001f || dlyActive)
            {
                const int dn = (int) dlyL.size();
                float rp = (float) dlyPos - dlyTime; if (rp < 0) rp += (float) dn;
                const int ri = (int) rp; const float rf = rp - (float) ri;
                const float oL = dlyL[(size_t) ri] + (dlyL[(size_t) ((ri + 1) % dn)] - dlyL[(size_t) ri]) * rf;
                const float oR = dlyR[(size_t) ri] + (dlyR[(size_t) ((ri + 1) % dn)] - dlyR[(size_t) ri]) * rf;
                // feedback with band-limiting so repeats sit behind the vocal
                fbLpL += (oL - fbLpL) * fbLpC; fbLpR += (oR - fbLpR) * fbLpC;
                const float fL = fbHpC * (fbHpL + fbLpL - fbHpzL); fbHpzL = fbLpL; fbHpL = fL;
                const float fR = fbHpC * (fbHpR + fbLpR - fbHpzR); fbHpzR = fbLpR; fbHpR = fR;
                dlyL[(size_t) dlyPos] = send * p.dly + p.dlyFb * fR;   // ping-pong
                dlyR[(size_t) dlyPos] = p.dlyFb * fL;
                dlyPos = (dlyPos + 1) % dn;
                fxL += oL; fxR += oR;
                dlyActive = std::abs (oL) + std::abs (oR) > 1e-7f || p.dly > 0.001f;
            }
            if (p.verb > 0.001f || verbActive)
            {
                float vl, vr; verb.processSample (bl * p.verb, br * p.verb, vl, vr);
                fxL += vl * 0.9f; fxR += vr * 0.9f;
                verbActive = std::abs (vl) + std::abs (vr) > 1e-7f || p.verb > 0.001f;
            }
            float l = (bl + fxL * duckG) * outG, r = (br + fxR * duckG) * outG;
            outL[i] = l; outR[i] = r;
            outPk = std::max ({ outPk, std::abs (l), std::abs (r) });
        }
        frame.deessGr = std::min (frame.deessGr, deess.grDb); frame.compGr = std::max (frame.compGr, comp.gr);
    }

    // ---- pitch / meter info for the UI ----
    struct Frame { float detMidi = -1, outMidi = -1, target = -1, level = 0, deessGr = 0, compGr = 0; };
    Frame frame;
    float inPk = 0, outPk = 0;
    float currentMidi() const noexcept { return lastMidi; }
    float currentShift() const noexcept { return shift; }
    int currentTarget() const noexcept { return curTarget; }
    void takeFrame (Frame& f) noexcept { f = frame; frame = {}; frame.detMidi = lastMidi; frame.outMidi = lastMidi > 0 ? lastMidi + shift : -1; frame.target = (float) curTarget; }
    float yinF0() const noexcept { return yin.f0; }

private:
    void onPitch() noexcept
    {
        const float f0 = yin.f0;
        frame.level = std::max (frame.level, yin.level);
        if (f0 <= 0.0f) { T = 0.0f; lastMidi = -1.0f; frame.detMidi = -1; frame.outMidi = -1; return; }
        T = (float) (fs / f0);
        const float m = midiFromHz (f0);
        lastMidi = m;
        if (curTarget < 0 || std::abs (m - (float) curTarget) > 0.62f || ! Scale::inScale (curTarget, p.key, p.scale))
            curTarget = Scale::snap (m, p.key, p.scale);
        desired = ((float) curTarget - m) * p.amount + p.transpose;
        for (int h = 0; h < 2; ++h)
        {
            const int iv = h == 0 ? p.h1 : p.h2;
            const int hn = Scale::harmony (curTarget, iv, p.key, p.scale);
            hDesired[h] = (float) hn - m + p.transpose;
        }
        frame.detMidi = m; frame.outMidi = m + shift; frame.target = (float) curTarget;
    }

    Params p;
    double fs = 48000;
    int L = 3000;
    Yin yin;
    Psola psLead, psH1, psH2;
    std::vector<float> dryLine; int dryMask = 0, dryPos = 0;
    Biquad hp1, hp2, eqB, eqP, eqA;
    DeEsser deess; VoxComp comp; Doubler dbl;
    std::vector<float> dlyL, dlyR; int dlyPos = 0; float dlyTime = 24000;
    float fbLpL = 0, fbLpR = 0, fbHpL = 0, fbHpR = 0, fbHpzL = 0, fbHpzR = 0;
    bool dlyActive = false, verbActive = false;
    lumen::ReverbEngine verb;
    float T = 0, shift = 0, desired = 0, hShift[2] {}, hDesired[2] {}, lastMidi = -1, tuneMix = 1;
    int curTarget = -1;
    float envF = 0, envS = 0, inG = 1, outG = 1;
    float lastLow = -999, lastBody = -999, lastPres = -999, lastAir = -999;
    bool first = true;
};

} // namespace lumen::vox
