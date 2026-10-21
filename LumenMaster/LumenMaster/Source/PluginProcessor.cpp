#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <map>

using namespace lumen::master;

// ================================================================================================
// background worker: builds linear-phase EQ filters and runs the Master Assistant
class LumenMasterProcessor::Worker : public juce::Thread
{
public:
    explicit Worker (LumenMasterProcessor& p) : juce::Thread ("Lumen Master worker"), proc (p) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            if (proc.prepared) proc.chain.serviceBackground();
            if (proc.aState.load() == Analysing) proc.runAssistant();
            wait (15);
        }
    }
    LumenMasterProcessor& proc;
};

// ================================================================================================
namespace
{
struct Preset { const char* name; std::vector<std::pair<juce::String, float>> v; };
const std::vector<Preset>& presets()
{
    using namespace IDs;
    static const std::vector<Preset> p {
        { "Default - clean limiter", {} },
        { "Club Banger (EDM)", { { eqOn, 1 }, { IDs::eq (0, "gain"), 1.0f }, { IDs::eq (5, "gain"), 1.0f }, { shOn, 1 }, { shAmount, 0.35f },
                                 { bassOn, 1 }, { bassPunch, 0.25f }, { glueOn, 1 }, { glueThr, -14 }, { glueAttack, 4 },
                                 { satOn, 1 }, { satDrive, 3 }, { satMix, 0.5f }, { imgOn, 1 }, { imgHigh, 1.15f },
                                 { clipOn, 1 }, { clipDrive, 2 }, { maxDrive, 6 }, { maxMode, 2 }, { maxCeiling, -0.5f } } },
        { "Transparent Loud", { { shOn, 1 }, { shAmount, 0.25f }, { glueOn, 1 }, { glueThr, -10 }, { glueRatio, 0 },
                                { clipOn, 1 }, { clipDrive, 1.5f }, { clipSoft, 0.7f }, { maxDrive, 5 }, { maxMode, 0 } } },
        { "Warm Analog", { { satOn, 1 }, { satMode, 1 }, { satDrive, 4 }, { satMix, 0.6f }, { satTone, -0.2f }, { glueOn, 1 }, { glueAttack, 5 }, { glueRelease, 2 },
                           { eqOn, 1 }, { IDs::eq (5, "gain"), -0.5f }, { maxDrive, 3 }, { maxMode, 3 } } },
        { "Streaming Safe (-14 LUFS)", { { platform, 1 }, { glueOn, 1 }, { glueThr, -16 }, { glueRatio, 0 }, { maxDrive, 0 }, { maxCeiling, -1 } } },
        { "Wide & Bright", { { imgOn, 1 }, { imgMid, 1.15f }, { imgHigh, 1.3f }, { eqOn, 1 }, { IDs::eq (5, "gain"), 1.5f }, { IDs::eq (5, "freq"), 10000 },
                             { satOn, 1 }, { satExciter, 0.3f }, { satMix, 0.5f }, { maxDrive, 4 } } },
        { "Tight Low End", { { bassOn, 1 }, { bassPunch, 0.35f }, { bassSustain, -0.2f }, { mbOn, 1 }, { IDs::mb (0, "thr"), -20 }, { IDs::mb (0, "ratio"), 3 },
                             { maxDrive, 4 } } },
        { "Gentle Polish", { { shOn, 1 }, { shAmount, 0.3f }, { shTame, 0.4f }, { glueOn, 1 }, { glueThr, -16 }, { glueRatio, 0 }, { maxDrive, 2 } } },
    };
    return p;
}
}

LumenMasterProcessor::LumenMasterProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenMaster", createLayout())
{
    for (int c = 0; c < 2; ++c) scope[c].assign ((size_t) kScope, 0.0f);
    worker = std::make_unique<Worker> (*this);
    worker->startThread();
}

LumenMasterProcessor::~LumenMasterProcessor()
{
    alive->store (false);
    worker->stopThread (4000);
}

bool LumenMasterProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

// ------------------------------------------------------------------------------------------------
Settings LumenMasterProcessor::readSettings (const Getter& P)
{
    Settings s;
    auto Z = [&] (const juce::String& id) { const float v = P (id); return std::abs (v) < 1e-5f ? 0.0f : v; };
    s.inGain = Z (IDs::inGain); s.outGain = Z (IDs::outGain); s.lowLat = P (IDs::lowLat) > 0.5f;
    for (int m = 0; m < kModules; ++m) s.on[m] = P (moduleOnId (m)) > 0.5f;
    s.eqMode = (int) P (IDs::eqMode);
    for (int b = 0; b < kEqBands; ++b)
    {
        auto& e = s.eq[(size_t) b];
        e.on = P (IDs::eq (b, "on")) > 0.5f; e.type = (int) P (IDs::eq (b, "type")); e.freq = P (IDs::eq (b, "freq"));
        e.gain = Z (IDs::eq (b, "gain")); e.q = P (IDs::eq (b, "q")); e.slope = (int) P (IDs::eq (b, "slope")); e.chan = (int) P (IDs::eq (b, "ch"));
    }
    s.sh.amount = P (IDs::shAmount); s.sh.tame = P (IDs::shTame); s.sh.boost = P (IDs::shBoost); s.sh.speed = P (IDs::shSpeed);
    s.sh.lowHz = P (IDs::shLow); s.sh.highHz = P (IDs::shHigh); s.sh.target = (int) P (IDs::genre);
    s.bass.freq = P (IDs::bassFreq); s.bass.punch = Z (IDs::bassPunch); s.bass.sustain = Z (IDs::bassSustain); s.bass.gain = Z (IDs::bassGain); s.bass.mono = P (IDs::bassMono) > 0.5f;
    s.mb.x1 = P (IDs::mbX1); s.mb.x2 = P (IDs::mbX2); s.mb.x3 = P (IDs::mbX3);
    for (int b = 0; b < 4; ++b) { s.mb.thr[b] = P (IDs::mb (b, "thr")); s.mb.ratio[b] = P (IDs::mb (b, "ratio")); s.mb.gain[b] = Z (IDs::mb (b, "gain")); }
    s.mb.attack = P (IDs::mbAttack); s.mb.release = P (IDs::mbRelease); s.mb.mix = P (IDs::mbMix);
    s.glue.thr = P (IDs::glueThr); s.glue.ratio = glueRatios()[juce::jlimit (0, 3, (int) P (IDs::glueRatio))];
    s.glue.attackMs = glueAttacks()[juce::jlimit (0, 5, (int) P (IDs::glueAttack))]; s.glue.releaseMs = glueReleases()[juce::jlimit (0, 4, (int) P (IDs::glueRelease))];
    s.glue.makeup = Z (IDs::glueMakeup); s.glue.mix = P (IDs::glueMix); s.glue.scHz = glueScs()[juce::jlimit (0, 3, (int) P (IDs::glueSc))];
    s.sat.mode = (int) P (IDs::satMode); s.sat.drive = P (IDs::satDrive); s.sat.tone = Z (IDs::satTone); s.sat.exciter = Z (IDs::satExciter); s.sat.mix = P (IDs::satMix);
    s.img.x1 = P (IDs::imgX1); s.img.x2 = P (IDs::imgX2); s.img.wLow = P (IDs::imgLow); s.img.wMid = P (IDs::imgMid); s.img.wHigh = P (IDs::imgHigh);
    s.clip.drive = Z (IDs::clipDrive); s.clip.soft = P (IDs::clipSoft);
    s.mx.drive = Z (IDs::maxDrive); s.mx.ceiling = P (IDs::maxCeiling); s.mx.release = P (IDs::maxRelease); s.mx.mode = (int) P (IDs::maxMode);
    s.mx.link = P (IDs::maxLink); s.mx.delta = P (IDs::maxDelta) > 0.5f;
    s.dither = (int) P (IDs::dither);
    return s;
}
Settings LumenMasterProcessor::currentSettings() const { return readSettings ([this] (const juce::String& id) { return param (id); }); }

void LumenMasterProcessor::setParam (const juce::String& id, float v)
{
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id))) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture(); }
}

float LumenMasterProcessor::targetLufs() const
{
    const int p = (int) param (IDs::platform), g = (int) param (IDs::genre);
    if (p == 6) return param (IDs::customLufs);
    if (p > 0) return platforms()[(size_t) p].lufs;
    if (g == kTargetReference && referenceSlot().valid.load()) return referenceSlot().lufs.load();
    return genres()[(size_t) juce::jlimit (0, kGenreCount - 1, g)].lufs;
}
float LumenMasterProcessor::targetCeiling() const { return platforms()[(size_t) juce::jlimit (0, 6, (int) param (IDs::platform))].tp; }

// ------------------------------------------------------------------------------------------------
void LumenMasterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepared = false;
    fs = sampleRate; maxBlock = std::max (64, samplesPerBlock);
    chain.prepare (fs, maxBlock);
    chain.eq.setBands (currentSettings().eq); chain.buildEqNow();
    inMeter.prepare (fs); outMeter.prepare (fs); inMeter.withTruePeak = outMeter.withTruePeak = false; tpL.reset(); tpR.reset(); tpMax = tpHold = 0;
    const int maxLat = chain.latency (false) + 8;
    for (auto& d : dryDelay) d.prepare (maxLat);
    dryBuf.setSize (2, maxBlock);
    curLowLat = param (IDs::lowLat) > 0.5f;
    applyLatency (curLowLat);
    capMax = (int) (fs * 20.0);
    capture.setSize (2, capMax);
    capLen.store (0);
    matchG = 1.0f;
    prepared = true;
}

void LumenMasterProcessor::applyLatency (bool lowLat)
{
    const int lat = chain.latency (lowLat);
    for (auto& d : dryDelay) { d.setDelay (lat); d.clear(); }
    setLatencySamples (lat);
}

void LumenMasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || ! prepared) return;
    if (n > maxBlock)
    {
        juce::MidiBuffer dummy;
        for (int st = 0; st < n; st += maxBlock)
        {
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), st, std::min (maxBlock, n - st));
            processBlock (sub, dummy);
        }
        return;
    }
    if (wantReset.exchange (false)) { inMeter.reset(); outMeter.reset(); tpMax = 0; tpHold = 0; }
    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);
    auto s = currentSettings();
    if (s.lowLat != curLowLat) { curLowLat = s.lowLat; applyLatency (curLowLat); chain.shaper.reset(); }

    // input metering + assistant capture + dry copy (for bypass compare)
    inMeter.process (L, R, n);
    float inPk = 0;
    for (int i = 0; i < n; ++i) { inPk = std::max (inPk, std::max (std::abs (L[i]), std::abs (R[i]))); dryBuf.setSample (0, i, dryDelay[0].tick (L[i])); dryBuf.setSample (1, i, dryDelay[1].tick (R[i])); }
    if (aState.load() == Listening)
    {
        const int have = capLen.load();
        if (inPk > 1e-4f && have < capMax)
        {
            const int k = std::min (n, capMax - have);
            for (int c = 0; c < 2; ++c) capture.copyFrom (c, have, c == 0 ? L : R, k);      // raw input (the assistant sets the input gain)
            capLen.store (have + k);
            if (have + k >= (int) (fs * 15.0)) aState.store (Analysing);
        }
    }
    {
        int p = scopePos.load();
        for (int i = 0; i < n; ++i) { scope[0][(size_t) p] = 0.5f * (L[i] + R[i]); p = (p + 1) & (kScope - 1); }
    }

    chain.process (L, R, n, s);

    outMeter.process (L, R, n);
    for (int i = 0; i < n; ++i) { const float t = std::max (tpL.push (L[i]), tpR.push (R[i])); tpMax = std::max (tpMax, t); tpHold = std::max (t, tpHold * 0.99995f); }
    // correlation (slow)
    for (int i = 0; i < n; ++i) { corrNum += (double) L[i] * R[i]; corrDen1 += (double) L[i] * L[i]; corrDen2 += (double) R[i] * R[i]; }
    corrNum *= 0.995; corrDen1 *= 0.995; corrDen2 *= 0.995;
    // level match for bypass compare: input made as loud as the output (short-term)
    const float iS = inMeter.shortTerm(), oS = outMeter.shortTerm();
    if (iS > -60 && oS > -60) { const float want = (float) dbToGain (juce::jlimit (-24.0f, 24.0f, oS - iS)); matchG += (want - matchG) * std::min (1.0f, (float) n / (float) (fs * 1.5)); }
    meters.matchDb.store ((float) gainToDb (matchG));
    {
        int p = scopePos.load();
        for (int i = 0; i < n; ++i) { scope[1][(size_t) p] = 0.5f * (L[i] + R[i]); p = (p + 1) & (kScope - 1); }
        scopePos.store (p);
    }
    float pl = 0, pr = 0; for (int i = 0; i < n; ++i) { pl = std::max (pl, std::abs (L[i])); pr = std::max (pr, std::abs (R[i])); }
    meters.outPkL.store (pl); meters.outPkR.store (pr); meters.inPk.store (inPk);
    if (param (IDs::bypass) > 0.5f)
    {
        const float g = param (IDs::match) > 0.5f ? matchG : 1.0f;
        for (int i = 0; i < n; ++i) { L[i] = dryBuf.getSample (0, i) * g; R[i] = dryBuf.getSample (1, i) * g; }
    }
    if (param (IDs::mono) > 0.5f) for (int i = 0; i < n; ++i) { const float m = 0.5f * (L[i] + R[i]); L[i] = R[i] = m; }
    meters.inM.store (inMeter.momentary()); meters.inS.store (iS); meters.inI.store (inMeter.integrated());
    meters.outM.store (outMeter.momentary()); meters.outS.store (oS); meters.outI.store (outMeter.integrated());
    meters.lra.store (outMeter.range()); meters.tp.store ((float) gainToDb (tpMax)); meters.tpNow.store ((float) gainToDb (tpHold));
    meters.corr.store (corrDen1 > 1e-9 && corrDen2 > 1e-9 ? (float) (corrNum / std::sqrt (corrDen1 * corrDen2)) : 1.0f);
}

float LumenMasterProcessor::moduleGr (int m) const
{
    switch (m)
    {
        case MBass: return chain.bass.grDb.load();
        case MGlue: return chain.glue.grNow.load();
        case MClip: return -chain.clip.clipDb.load();
        case MMax:  return -chain.maxi.grDb.load();
        case MMulti: { float w = 0; for (auto& g : chain.multi.gr) w = std::min (w, g.load()); return w; }
        default: return 0;
    }
}
double LumenMasterProcessor::eqResponseDb (double f, int role) const
{
    auto s = currentSettings();
    return gainToDb (eqMagnitude (s.eq, f, role));
}

int LumenMasterProcessor::copyScope (bool output, float* dst, int n) const
{
    n = std::min (n, kScope);
    const int p = scopePos.load();
    for (int i = 0; i < n; ++i) dst[i] = scope[output ? 1 : 0][(size_t) ((p - n + i + kScope) & (kScope - 1))];
    return n;
}

// ================================================================================================
// Master Assistant
void LumenMasterProcessor::startAssistant()
{
    capLen.store (0);
    { const juce::ScopedLock l (msgLock); aMsg = "Listening... play the loudest part of your track (drop / chorus)"; }
    aState.store (Listening);
}
void LumenMasterProcessor::cancelAssistant()
{
    if (aState.load() == Listening && capLen.load() > (int) (fs * 4.0)) { aState.store (Analysing); return; }   // stop early = analyse what we have
    aState.store (Idle);
    const juce::ScopedLock l (msgLock); aMsg = "Assistant cancelled";
}
float LumenMasterProcessor::assistProgress() const { return capMax > 0 ? juce::jlimit (0.0f, 1.0f, (float) capLen.load() / (float) (fs * 15.0)) : 0.0f; }

Curve LumenMasterProcessor::spectrumCurve (const juce::AudioBuffer<float>& b, int len, double sr)
{
    const int order = 13, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> w ((size_t) (2 * N)), win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * kPi * (i + 0.5) / N));
    std::vector<double> pw ((size_t) (N / 2 + 1), 0.0);
    int frames = 0;
    for (int st = 0; st + N <= len; st += N / 2)
    {
        for (int c = 0; c < std::min (2, b.getNumChannels()); ++c)
        {
            std::fill (w.begin(), w.end(), 0.0f);
            for (int i = 0; i < N; ++i) w[(size_t) i] = b.getSample (c, st + i) * win[(size_t) i];
            fft.performRealOnlyForwardTransform (w.data(), true);
            for (int k = 0; k <= N / 2; ++k) pw[(size_t) k] += (double) w[(size_t) (2 * k)] * w[(size_t) (2 * k)] + (double) w[(size_t) (2 * k + 1)] * w[(size_t) (2 * k + 1)];
        }
        ++frames;
    }
    Curve c {};
    const double binHz = sr / N;
    for (int i = 0; i < kCurvePts; ++i)
    {
        const double fc = curveFreq (i), wo = 1.0 / 6.0;
        const double lo = fc * std::pow (2.0, -wo), hi = fc * std::pow (2.0, wo);
        const int k0 = std::max (1, (int) std::floor (lo / binHz)), k1 = std::min (N / 2, (int) std::ceil (hi / binHz));
        double s = 0, ws = 0;
        for (int k = k0; k <= k1; ++k) { const double d = std::abs (std::log2 (k * binHz / fc)) / wo, wt = std::max (0.0, 1.0 - d); s += wt * pw[(size_t) k]; ws += wt; }
        if (ws <= 0) { const int k = juce::jlimit (1, N / 2, (int) std::round (fc / binHz)); s = pw[(size_t) k]; ws = 1; }
        c[(size_t) i] = (float) (10.0 * std::log10 (s / ws / std::max (1, frames) + 1e-20));
    }
    return c;
}

static float integratedLufs (const juce::AudioBuffer<float>& b, int len, double fs)
{
    lumen::meter::LoudnessMeter m; m.prepare (fs);
    for (int st = 0; st < len; st += 4096) { const int n = std::min (4096, len - st); m.process (b.getReadPointer (0, st), b.getReadPointer (1, st), n); }
    return m.integrated();
}

void LumenMasterProcessor::runAssistant()
{
    const int len = capLen.load();
    if (len < (int) (fs * 2.0)) { aState.store (Failed); const juce::ScopedLock l (msgLock); aMsg = "Not enough audio - play at least a few seconds"; return; }
    juce::AudioBuffer<float> in (2, len);
    for (int c = 0; c < 2; ++c) in.copyFrom (c, 0, capture, c, 0, len);
    // ---- analysis
    const float inLufs = integratedLufs (in, len, fs);
    const auto meas = spectrumCurve (in, len, fs);
    const int genre = (int) param (IDs::genre);
    const auto& tg = targetCurve (genre);
    double offs = 0; int no = 0;
    for (int i = 0; i < kCurvePts; ++i) { const double f = curveFreq (i); if (f >= 60 && f <= 12000) { offs += meas[(size_t) i] - tg[(size_t) i]; ++no; } }
    offs /= std::max (1, no);
    // wanted correction (dB), 1/3-octave smoothed, gentle
    std::array<double, kCurvePts> want {};
    for (int i = 0; i < kCurvePts; ++i)
    {
        double s = 0, ws = 0;
        for (int j = std::max (0, i - 2); j <= std::min (kCurvePts - 1, i + 2); ++j) { const double w = 3 - std::abs (i - j); s += w * (tg[(size_t) j] + offs - meas[(size_t) j]); ws += w; }
        want[(size_t) i] = std::clamp (0.6 * s / ws, -4.0, 4.0);
    }
    // fit 6 bands (fixed frequencies) by Gauss-Newton on the analog response
    struct BandDef { int type; double f, q; };
    static const BandDef defs[6] { { LowShelf, 60, 0.7 }, { Bell, 180, 0.9 }, { Bell, 500, 0.9 }, { Bell, 1800, 0.8 }, { Bell, 4500, 0.8 }, { HighShelf, 11000, 0.7 } };
    std::array<double, 6> g {};
    auto respDb = [&] (const std::array<double, 6>& gg, double f) { double m = 1; for (int b = 0; b < 6; ++b) m *= analogMag (defs[b].type, f, defs[b].f, defs[b].q, gg[(size_t) b], 1); return gainToDb (m); };
    for (int it = 0; it < 8; ++it)
    {
        // normal equations J^T J dg = J^T r (numeric Jacobian)
        double JTJ[6][6] {}, JTr[6] {};
        for (int i = 0; i < kCurvePts; ++i)
        {
            const double f = curveFreq (i); if (f < 25 || f > 17000) continue;
            const double r = want[(size_t) i] - respDb (g, f);
            double J[6];
            for (int b = 0; b < 6; ++b) { auto gp = g; gp[(size_t) b] += 0.1; J[b] = (respDb (gp, f) - respDb (g, f)) / 0.1; }
            for (int a = 0; a < 6; ++a) { JTr[a] += J[a] * r; for (int b = 0; b < 6; ++b) JTJ[a][b] += J[a] * J[b]; }
        }
        for (int a = 0; a < 6; ++a) JTJ[a][a] += 0.5;     // damping
        // solve 6x6 (Gauss elimination)
        double A[6][7]; for (int a = 0; a < 6; ++a) { for (int b = 0; b < 6; ++b) A[a][b] = JTJ[a][b]; A[a][6] = JTr[a]; }
        for (int col = 0; col < 6; ++col)
        {
            int piv = col; for (int r = col + 1; r < 6; ++r) if (std::abs (A[r][col]) > std::abs (A[piv][col])) piv = r;
            for (int k = 0; k < 7; ++k) std::swap (A[col][k], A[piv][k]);
            const double d = A[col][col]; if (std::abs (d) < 1e-12) continue;
            for (int k = col; k < 7; ++k) A[col][k] /= d;
            for (int r = 0; r < 6; ++r) if (r != col) { const double m = A[r][col]; for (int k = col; k < 7; ++k) A[r][k] -= m * A[col][k]; }
        }
        for (int b = 0; b < 6; ++b) g[(size_t) b] = std::clamp (g[(size_t) b] + A[b][6], -4.0, 4.0);
    }
    // stereo / dynamics facts
    double mE = 0, sE = 0, lowS = 0, lowM = 0;
    { juce::dsp::IIR::Filter<float> lp; lp.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (fs, 120.0);
      for (int i = 0; i < len; ++i)
      {
          const float l = in.getSample (0, i), r = in.getSample (1, i);
          const double m = 0.5 * (l + r), s = 0.5 * (l - r); mE += m * m; sE += s * s;
          const float sl = lp.processSample ((float) s); lowS += (double) sl * sl; lowM += m * m * 0.0;
      } }
    const double widthDb = gainToDb (std::sqrt ((sE + 1e-12) / (mE + 1e-12)));
    // loud-section level of the mix (for the glue threshold)
    std::vector<float> rms;
    for (int st = 0; st + 2400 <= len; st += 2400) { double e = 0; for (int i = st; i < st + 2400; ++i) e += 0.5 * ((double) in.getSample (0, i) * in.getSample (0, i) + (double) in.getSample (1, i) * in.getSample (1, i)); rms.push_back ((float) gainToDb (std::sqrt (e / 2400) + 1e-9)); }
    std::sort (rms.begin(), rms.end());
    const float loud = rms.empty() ? -20.0f : rms[(size_t) (rms.size() * 9 / 10)];
    const float target = targetLufs(), ceiling = targetCeiling();
    const bool clubLoud = target > -9.5f;

    // ---- the proposal
    std::map<juce::String, float> v;
    v[IDs::eqOn] = 1; v[IDs::eqMode] = 1;
    for (int b = 0; b < 6; ++b)
    {
        v[IDs::eq (b, "on")] = 1; v[IDs::eq (b, "type")] = (float) defs[b].type; v[IDs::eq (b, "freq")] = (float) defs[b].f;
        v[IDs::eq (b, "q")] = (float) defs[b].q; v[IDs::eq (b, "gain")] = (float) std::round (g[(size_t) b] * 10.0) / 10.0f; v[IDs::eq (b, "ch")] = 0;
    }
    v[IDs::shOn] = 1; v[IDs::shAmount] = 0.3f; v[IDs::shTame] = 0.3f; v[IDs::shBoost] = 0.4f; v[IDs::shSpeed] = 0.5f;
    v[IDs::bassOn] = 1; v[IDs::bassFreq] = 110; v[IDs::bassMono] = 1; v[IDs::bassPunch] = genre <= 10 ? 0.2f : 0.0f; v[IDs::bassSustain] = 0; v[IDs::bassGain] = 0;
    v[IDs::glueOn] = 1; v[IDs::glueThr] = juce::jlimit (-30.0f, 0.0f, loud - 1.5f); v[IDs::glueRatio] = 1; v[IDs::glueAttack] = 4; v[IDs::glueRelease] = 4; v[IDs::glueMix] = 1; v[IDs::glueMakeup] = 0; v[IDs::glueSc] = 2;
    v[IDs::satOn] = genre == 12 ? 1.0f : (clubLoud ? 1.0f : 0.0f); v[IDs::satMode] = 0; v[IDs::satDrive] = 2.5f; v[IDs::satMix] = 0.4f; v[IDs::satTone] = 0; v[IDs::satExciter] = 0;
    v[IDs::imgOn] = 1; v[IDs::imgLow] = 1.0f; v[IDs::imgMid] = widthDb < -12 ? 1.1f : 1.0f; v[IDs::imgHigh] = widthDb < -9 ? 1.2f : widthDb < -6 ? 1.1f : 1.0f;
    v[IDs::clipOn] = clubLoud ? 1.0f : 0.0f; v[IDs::clipSoft] = 0.6f;
    v[IDs::maxOn] = 1; v[IDs::maxCeiling] = ceiling; v[IDs::maxMode] = target > -7.5f ? 2.0f : 0.0f; v[IDs::maxRelease] = 100; v[IDs::maxLink] = 1; v[IDs::maxDelta] = 0;
    v[IDs::mbOn] = 0;
    // bring very quiet mixes up to a healthy level first (so the maximizer is not asked for 25 dB)
    v[IDs::inGain] = (float) std::round (juce::jlimit (0.0f, 12.0f, -16.0f - inLufs) * 10.0f) / 10.0f;

    auto getter = [&] (const juce::String& id) { auto it = v.find (id); return it != v.end() ? it->second : param (id); };
    auto s = readSettings (getter);
    // stage A: everything before the clipper / maximizer, rendered once
    juce::AudioBuffer<float> a (2, len);
    {
        MasterChain ch; ch.prepare (fs, 512);
        auto sa = s; sa.lowLat = false; sa.on[MClip] = false; sa.on[MMax] = false; sa.on[MDither] = false; sa.outGain = 0;
        ch.eq.setBands (sa.eq); ch.buildEqNow();
        for (int c = 0; c < 2; ++c) a.copyFrom (c, 0, in, c, 0, len);
        // pre-roll so the adaptive stages settle, then the real pass
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int c = 0; c < 2; ++c) a.copyFrom (c, 0, in, c, 0, len);
            for (int st = 0; st < len; st += 512) ch.process (a.getWritePointer (0, st), a.getWritePointer (1, st), std::min (512, len - st), sa);
            if (aState.load() != Analysing) return;
        }
        // remove the chain latency so A lines up with the input
        const int lat = ch.latency (false);
        for (int c = 0; c < 2; ++c) { auto* d = a.getWritePointer (c); std::memmove (d, d + std::min (lat, len), sizeof (float) * (size_t) (len - std::min (lat, len))); std::fill (d + len - std::min (lat, len), d + len, 0.0f); }
    }
    // stage B: find the total gain that lands on the loudness target
    const int evalLen = std::max (1, len - (int) (fs * 0.3));
    auto render = [&] (double G, juce::AudioBuffer<float>& out, float& grMax)
    {
        out.makeCopyOf (a);
        Clipper cl; cl.prepare (fs, 512); Maximizer mx; mx.prepare (fs);
        ClipParams cp; cp.on = s.on[MClip]; cp.soft = s.clip.soft; cp.drive = (float) (cp.on ? std::min (3.0, 0.35 * G) : 0.0);
        MaxParams mp = s.mx; mp.drive = (float) (G - cp.drive); mp.delta = false;
        grMax = 0;
        for (int st = 0; st < len; st += 512)
        {
            const int n = std::min (512, len - st);
            cl.process (out.getWritePointer (0, st), out.getWritePointer (1, st), n, cp);
            mx.process (out.getWritePointer (0, st), out.getWritePointer (1, st), n, mp);
            grMax = std::max (grMax, mx.grDb.load());
        }
        return std::pair<float, float> { cp.drive, mp.drive };
    };
    juce::AudioBuffer<float> out;
    double lo = 0, hi = s.on[MClip] ? 27.0 : 24.0, best = 0; float grAtBest = 0, lufsAtBest = -100;
    for (int it = 0; it < 14; ++it)
    {
        const double mid = 0.5 * (lo + hi); float gr = 0;
        render (mid, out, gr);
        const float l = integratedLufs (out, evalLen, fs);
        if (l < target) lo = mid; else hi = mid;
        best = mid; grAtBest = gr; lufsAtBest = l;
        if (aState.load() != Analysing) return;
        if (std::abs (l - target) < 0.05f) break;
    }
    const auto drives = render (best, out, grAtBest);
    lufsAtBest = integratedLufs (out, evalLen, fs);
    v[IDs::clipDrive] = (float) std::round (drives.first * 10) / 10.0f;
    v[IDs::maxDrive] = (float) std::round (drives.second * 100) / 100.0f;

    juce::String eqTxt; for (int b = 0; b < 6; ++b) if (std::abs (g[(size_t) b]) >= 0.3) eqTxt << (g[(size_t) b] > 0 ? "+" : "") << juce::String (g[(size_t) b], 1) << " dB @ " << (defs[b].f >= 1000 ? juce::String (defs[b].f / 1000.0, 1) + "k" : juce::String ((int) defs[b].f)) << "   ";
    juce::String msg;
    msg << "Analysed " << juce::String (len / fs, 1) << " s:  " << juce::String (inLufs, 1) << " LUFS in -> " << juce::String (lufsAtBest, 1) << " LUFS out (target " << juce::String (target, 1)
        << ", ceiling " << juce::String (ceiling, 1) << " dBTP, peak GR " << juce::String (grAtBest, 1) << " dB).  EQ: " << (eqTxt.isEmpty() ? juce::String ("balanced already") : eqTxt.trim());
    {
        const juce::ScopedLock l (msgLock);
        aMsg = msg; analysed = meas; hasAnalysed = true;
        for (auto& x : analysed) x = (float) (x - offs);
    }
    // apply on the message thread
    auto flag = alive;
    juce::MessageManager::callAsync ([this, v, flag]
    {
        if (! flag->load()) return;
        for (auto& kv : v) setParam (kv.first, kv.second);
        aState.store (Done);
    });
    while (aState.load() == Analysing && ! worker->threadShouldExit()) juce::Thread::sleep (5);
}

bool LumenMasterProcessor::loadReference (const juce::File& f, juce::String& msg)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { msg = "Can't read " + f.getFileName(); return false; }
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 240.0));
    if (len < (int) (r->sampleRate * 3)) { msg = "Reference is too short"; return false; }
    juce::AudioBuffer<float> b (2, len);
    r->read (&b, 0, len, 0, true, true);
    const float l = integratedLufs (b, len, r->sampleRate);
    auto c = spectrumCurve (b, len, r->sampleRate);
    referenceSlot().set (c, l);
    { const juce::ScopedLock lk (msgLock); refName = f.getFileNameWithoutExtension(); }
    setParam (IDs::genre, (float) kTargetReference);
    msg = "Reference: " + f.getFileNameWithoutExtension() + "  (" + juce::String (l, 1) + " LUFS) - now the target for Smart Balance and the Assistant";
    return true;
}

// ================================================================================================
int LumenMasterProcessor::getNumPrograms() { return (int) presets().size(); }
const juce::String LumenMasterProcessor::getProgramName (int i) { return juce::isPositiveAndBelow (i, (int) presets().size()) ? presets()[(size_t) i].name : ""; }
void LumenMasterProcessor::setCurrentProgram (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) presets().size())) return;
    currentProgram = i;
    const int keepGenre = (int) param (IDs::genre);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->paramID != IDs::genre && rp->paramID != IDs::bypass && rp->paramID != IDs::lowLat) rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& kv : presets()[(size_t) i].v) setParam (kv.first, kv.second);
    setParam (IDs::genre, (float) keepGenre);
}

void LumenMasterProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenMasterProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenMasterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    if (referenceSlot().valid.load())
    {
        juce::StringArray c; for (float x : referenceSlot().get()) c.add (juce::String (x, 3));
        st.setProperty ("refCurve", c.joinIntoString (","), nullptr);
        st.setProperty ("refLufs", referenceSlot().lufs.load(), nullptr);
        st.setProperty ("refName", referenceName(), nullptr);
    }
    st.setProperty ("program", currentProgram, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenMasterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto vt = juce::ValueTree::fromXml (*xml);
    if (vt.hasProperty ("refCurve"))
    {
        juce::StringArray t; t.addTokens (vt.getProperty ("refCurve").toString(), ",", "");
        if (t.size() == kCurvePts)
        {
            Curve c {}; for (int i = 0; i < kCurvePts; ++i) c[(size_t) i] = t[i].getFloatValue();
            referenceSlot().set (c, (float) vt.getProperty ("refLufs", -9.0f));
            const juce::ScopedLock l (msgLock); refName = vt.getProperty ("refName").toString();
        }
    }
    currentProgram = (int) vt.getProperty ("program", 0);
    for (auto id : { "refCurve", "refLufs", "refName", "program" }) vt.removeProperty (id, nullptr);
    apvts.replaceState (vt);
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            auto child = vt.getChildWithProperty ("id", rp->paramID);
            if (child.isValid() && child.hasProperty ("value")) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
        }
}

juce::AudioProcessorEditor* LumenMasterProcessor::createEditor() { return new LumenMasterEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenMasterProcessor(); }
