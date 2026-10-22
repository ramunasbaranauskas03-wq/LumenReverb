#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lumen::mixcheck;

static float lmMemoryShared[64];   // per-device level-match memory (per process is fine: it only seeds the estimate)

static float streamGainFor (const Device& d, float integ, float tpDb)
{
    if (d.special != Stream || d.target >= 0.0f || integ < -69.0f) return 0.0f;
    float g = d.target - integ;
    if (! d.boostAllowed) g = std::min (g, 0.0f);
    else if (g > 0.0f) g = std::min (g, std::max (0.0f, -1.0f - tpDb));     // boost only up to -1 dBTP
    return g;
}

// ================================================================================================ ANALYZE ALL worker
class LumenMixCheckProcessor::Analyzer : public juce::Thread
{
public:
    explicit Analyzer (LumenMixCheckProcessor& p) : juce::Thread ("MixCheck analyzer"), proc (p) {}
    juce::AudioBuffer<float> clip;
    void run() override
    {
        auto r = LumenMixCheckProcessor::analyse (clip, proc.fs, proc.irLoaded.load(), proc.irLoaded.load() ? &irConv : nullptr,
                                                  [this] (float f) { proc.anProgress.store (f); });
        { const juce::ScopedLock l (proc.repLock); proc.rep = std::move (r); }
        ++proc.repVersion;
        proc.analyzing.store (false);
    }
    juce::dsp::Convolution irConv;
private:
    LumenMixCheckProcessor& proc;
};

// ================================================================================================ processor
LumenMixCheckProcessor::LumenMixCheckProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenMixCheck", createLayout())
{
    analyzer = std::make_unique<Analyzer> (*this);
}

LumenMixCheckProcessor::~LumenMixCheckProcessor() { if (analyzer) analyzer->stopThread (8000); }

bool LumenMixCheckProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void LumenMixCheckProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate; maxBlock = juce::jlimit (32, 16384, samplesPerBlock);
    juce::dsp::ProcessSpec spec { fs, (juce::uint32) maxBlock, 2 };
    for (int k = 0; k < 2; ++k)
    {
        conv[k].prepare (spec);
        chain[k].prepare (fs, (unsigned) (k + 1));
        chain[k].customIR = &conv[k]; chain[k].customLoaded = irLoaded.load();
        chain[k].lmMemory = lmMemoryShared;
        chain[k].setControls (readControls());
    }
    active = 0;
    chain[0].setDevice ((int) P (IDs::device));
    inMeter.prepare (fs); outMeter.prepare (fs);
    hpEq.prepare (fs); hpEqView.prepare (fs); hpModelNow = -1; updateHeadphoneEq (false);
    scratch.setSize (2, maxBlock);
    capLen = (int) (fs * 10.0); capture.setSize (2, capLen); capture.clear(); capPos = 0; capFilled.store (0);
    setLatencySamples (0);
}

lumen::mixcheck::Controls LumenMixCheckProcessor::readControls() const
{
    Controls c;
    c.room = P (IDs::room); c.noise = P (IDs::noise); c.drive = P (IDs::drive); c.volume = P (IDs::volume);
    c.headphones = P (IDs::headphones) > 0.5f; c.levelMatch = P (IDs::levelMatch) > 0.5f; c.outDb = P (IDs::out);
    return c;
}

void LumenMixCheckProcessor::updateStreamGain (DeviceChain& c)
{
    const auto& d = c.info();
    if (d.special != Stream) { c.setStreamGain (std::pow (10.0f, 0.0f)); streamGainDb.store (0); return; }
    const float integ = inMeter.blocks() >= 30 && inMeter.hasIntegrated() ? inMeter.integrated() : inMeter.shortTerm();
    const float tp = juce::Decibels::gainToDecibels (inMeter.truePeakMax(), -100.0f);
    const float g = streamGainFor (d, integ, tp);
    streamGainDb.store (g);
    c.setStreamGain (std::pow (10.0f, g / 20.0f));
}

void LumenMixCheckProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    // a monitoring tool must never end up in the bounce
    if (P (IDs::renderBypass) > 0.5f && isNonRealtime()) { renderBypassed.store (true); return; }
    renderBypassed.store (false);

    int dev = (int) P (IDs::device);
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            const bool playing = pos->getIsPlaying();
            if (playing && ! wasPlaying) resetMeters.store (true);
            wasPlaying = playing;
            const int cyc = (int) P (IDs::cycle);
            if (cyc > 0 && playing)
                if (auto ppq = pos->getPpqPosition())
                {
                    static const int bars[] { 1, 2, 4, 8 };
                    double barLen = 4.0;
                    if (auto ts = pos->getTimeSignature()) barLen = ts->numerator * 4.0 / std::max (1, ts->denominator);
                    const auto idx = (long long) std::floor (std::max (0.0, *ppq) / (barLen * bars[cyc - 1]));
                    dev = cycleList()[(size_t) (idx % (long long) cycleList().size())];
                }
        }
    if (resetMeters.exchange (false)) { inMeter.reset(); outMeter.reset(); }
    shownDevice.store (dev);

    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);
    inMeter.process (L, R, n);
    const int rp = ringPos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        const int q = (rp + i) & (kRing - 1); ring[0][q] = L[i]; ring[1][q] = R[i];
        capture.setSample (0, capPos, L[i]); capture.setSample (1, capPos, R[i]); if (++capPos >= capLen) capPos = 0;
    }
    capFilled.store (std::min (capLen, capFilled.load() + n));

    const auto ctl = readControls();
    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = std::min (maxBlock, n - start);
        float* l = L + start; float* r = R + start;
        auto& A = chain[active];
        if (dev != A.device())
        {
            // switch with a short crossfade: the new chain runs on a copy, then blend
            auto& B = chain[1 - active];
            B.syncInputPower (A);
            B.setControls (ctl); B.setDevice (dev); B.setControls (ctl); updateStreamGain (B);
            scratch.copyFrom (0, 0, l, len); scratch.copyFrom (1, 0, r, len);
            A.setControls (ctl); A.process (l, r, len);
            B.process (scratch.getWritePointer (0), scratch.getWritePointer (1), len);
            for (int i = 0; i < len; ++i)
            {
                const float t = (float) (i + 1) / (float) len;
                l[i] += (scratch.getSample (0, i) - l[i]) * t; r[i] += (scratch.getSample (1, i) - r[i]) * t;
            }
            active = 1 - active;
        }
        else { A.setControls (ctl); updateStreamGain (A); A.process (l, r, len); }
    }

    // your headphones, corrected (always last)
    updateHeadphoneEq (true);
    hpEq.process (L, R, n);

    outMeter.process (L, R, n);
    const double k = 1.0 - std::exp (-(double) 1 / (0.3 * fs));
    for (int i = 0; i < n; ++i)
    {
        const int q = (rp + i) & (kRing - 1); ring[2][q] = L[i]; ring[3][q] = R[i];
        corrLR += ((double) L[i] * R[i] - corrLR) * k; corrLL += ((double) L[i] * L[i] - corrLL) * k; corrRR += ((double) R[i] * R[i] - corrRR) * k;
    }
    ringPos.store ((rp + n) & (kRing - 1), std::memory_order_release);
    inLufsM.store (inMeter.momentary()); inLufsS.store (inMeter.shortTerm()); inLufsI.store (inMeter.integrated());
    inTp.store (juce::Decibels::gainToDecibels (inMeter.truePeakMax(), -100.0f));
    outLufsS.store (outMeter.shortTerm()); outTp.store (juce::Decibels::gainToDecibels (outMeter.truePeakNow(), -100.0f));
    levelMatchDb.store (chain[active].levelMatchDb()); limGr.store (chain[active].limiterGrDb());
    corrOut.store (corrLL + corrRR > 1e-10 ? (float) (corrLR / std::sqrt (corrLL * corrRR)) : 1.0f);
}

// ================================================================================================ headphone correction
void LumenMixCheckProcessor::updateHeadphoneEq (bool audioThread)
{
    const int model = juce::jlimit (0, numHeadphones() - 1, (int) P (IDs::hpModel));
    const float amt = P (IDs::hpAmount);
    if (model == hpModelNow && juce::exactlyEqual (amt, hpAmountNow) && (model != customHeadphoneIndex() || customEqUsed == customEqVersion)) return;
    if (model == customHeadphoneIndex())
    {
        if (audioThread)
        {
            const juce::SpinLock::ScopedTryLockType l (eqLock);
            if (! l.isLocked()) return;
            hpEq.set (customEq, amt);
        }
        else { const juce::SpinLock::ScopedLockType l (eqLock); hpEq.set (customEq, amt); hpEqView.set (customEq, amt); }
        customEqUsed = customEqVersion;
    }
    else { hpEq.set (headphoneModels()[(size_t) model].eq, amt); if (! audioThread) hpEqView.set (headphoneModels()[(size_t) model].eq, amt); }
    hpModelNow = model; hpAmountNow = amt;
    viewDirty.store (true);
}

bool LumenMixCheckProcessor::loadHeadphoneEq (const juce::File& f, juce::String& err)
{
    if (! f.existsAsFile()) { err = "File not found"; return false; }
    auto bands = parseParametricEq (f.loadFileAsString(), err);
    if (bands.empty()) return false;
    { const juce::SpinLock::ScopedLockType l (eqLock); customEq = bands; ++customEqVersion; }
    customEqFile = f;
    return true;
}

void LumenMixCheckProcessor::refreshHeadphoneView()
{
    const int model = juce::jlimit (0, numHeadphones() - 1, (int) P (IDs::hpModel));
    const float amt = P (IDs::hpAmount);
    if (model == customHeadphoneIndex()) { const juce::SpinLock::ScopedLockType l (eqLock); hpEqView.set (customEq, amt); }
    else hpEqView.set (headphoneModels()[(size_t) model].eq, amt);
}

// ================================================================================================ custom IR
bool LumenMixCheckProcessor::loadIR (const juce::File& f, juce::String& err)
{
    if (! f.existsAsFile()) { err = "File not found"; return false; }
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr) { err = "Not an audio file"; return false; }
    if (rd->lengthInSamples > (juce::int64) (rd->sampleRate * 12)) { err = "IR longer than 12 s"; return false; }
    for (auto* c : { &conv[0], &conv[1], &analyzer->irConv })
        c->loadImpulseResponse (f, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes, 0, juce::dsp::Convolution::Normalise::yes);
    analyzer->irConv.prepare ({ fs, (juce::uint32) 512, 2 });
    irFile = f; irLoaded.store (true);
    for (auto& c : chain) c.customLoaded = true;
    return true;
}

// ================================================================================================ ANALYZE ALL
void LumenMixCheckProcessor::startAnalyzeAll()
{
    if (analyzing.load()) return;
    const int have = std::min (capFilled.load(), capLen);
    const int len = std::min (have, (int) (fs * 8.0));
    if (len < (int) (fs * 2.0)) return;
    analyzer->stopThread (2000);
    analyzer->clip.setSize (2, len);
    // chronological copy of the last `len` samples
    int src = capPos - len; if (src < 0) src += capLen;
    for (int i = 0; i < len; ++i) { analyzer->clip.setSample (0, i, capture.getSample (0, src)); analyzer->clip.setSample (1, i, capture.getSample (1, src)); if (++src >= capLen) src = 0; }
    anProgress.store (0); analyzing.store (true);
    analyzer->startThread (juce::Thread::Priority::low);
}

std::vector<DeviceReport> LumenMixCheckProcessor::analyse (const juce::AudioBuffer<float>& clip, double sr, bool includeCustom, juce::dsp::Convolution* ir,
                                                          std::function<void (float)> progress)
{
    const int N = clip.getNumSamples(), nd = numDevices();
    std::vector<DeviceReport> out ((size_t) nd);
    const int skip = std::min (N / 4, (int) (sr * 0.5));
    constexpr int kOrd = 12, kF = 1 << kOrd;
    juce::dsp::FFT fft (kOrd);
    std::vector<float> win (kF), buf (2 * kF);
    for (int i = 0; i < kF; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * (float) kPi * (float) i / (float) kF);
    auto bands = [&] (const juce::AudioBuffer<float>& b, double* e)
    {
        for (int k = 0; k < DeviceReport::kBands; ++k) e[k] = 1e-20;
        for (int st = skip; st + kF <= N; st += kF / 2)
            for (int c = 0; c < 2; ++c)
            {
                std::fill (buf.begin(), buf.end(), 0.0f);
                for (int i = 0; i < kF; ++i) buf[(size_t) i] = b.getSample (c, st + i) * win[(size_t) i];
                fft.performFrequencyOnlyForwardTransform (buf.data());
                for (int k = 1; k < kF / 2; ++k)
                {
                    const float f = (float) (k * sr / kF);
                    for (int bnd = 0; bnd < DeviceReport::kBands; ++bnd)
                        if (f >= reportBandEdge (bnd) && f < reportBandEdge (bnd + 1)) { e[bnd] += (double) buf[(size_t) k] * buf[(size_t) k]; break; }
                }
            }
    };
    auto loud = [&] (const juce::AudioBuffer<float>& b, float* tpDb = nullptr)
    {
        lumen::meter::LoudnessMeter m; m.prepare (sr); m.process (b.getReadPointer (0, skip), b.getReadPointer (1, skip), N - skip);
        if (tpDb) *tpDb = juce::Decibels::gainToDecibels (m.truePeakMax(), -100.0f);
        return m.hasIntegrated() ? m.integrated() : m.shortTerm();
    };
    float inTp = -100; const float inL = loud (clip, &inTp);
    double ein[DeviceReport::kBands]; bands (clip, ein);

    Controls ctl; ctl.levelMatch = false; ctl.noise = 0;
    juce::AudioBuffer<float> a (2, N), b (2, N);
    auto render = [&] (int d, float drive, juce::AudioBuffer<float>& dst)
    {
        DeviceChain ch; ch.prepare (sr, 3); ch.customIR = ir; ch.customLoaded = ir != nullptr;
        Controls c = ctl; c.drive = drive; ch.setControls (c); ch.setDevice (d); ch.setControls (c);
        ch.setStreamGain (std::pow (10.0f, streamGainFor (devices()[(size_t) d], inL, inTp) / 20.0f));
        if (ir) ir->reset();
        dst.makeCopyOf (clip);
        for (int st = 0; st < N; st += 512) { const int len = std::min (512, N - st); ch.process (dst.getWritePointer (0, st), dst.getWritePointer (1, st), len); }
    };
    for (int d = 1; d < nd; ++d)
    {
        const auto& dv = devices()[(size_t) d];
        if (progress) progress ((float) d / (float) nd);
        auto& r = out[(size_t) d];
        if (dv.special == Custom && ! includeCustom) continue;
        render (d, 1.0f, a);
        const float outL = loud (a);
        double eo[DeviceReport::kBands]; bands (a, eo);
        const double dl = (double) (outL - inL);
        for (int k = 0; k < DeviceReport::kBands; ++k) r.bandDb[k] = (float) juce::jlimit (-60.0, 30.0, 10 * std::log10 (eo[k] / ein[k]) - dl);
        r.streamDb = streamGainFor (dv, inL, inTp);
        if (dv.driveDb > 0.01f)
        {
            render (d, 0.0f, b);
            double ea = 0, eb = 0, ed = 0, ab = 0;
            for (int c = 0; c < 2; ++c) for (int i = skip; i < N; ++i) { const double x = a.getSample (c, i), y = b.getSample (c, i); ea += x * x; eb += y * y; ab += x * y; }
            const double g = ab / (eb + 1e-30);    // best gain match of the clean version
            for (int c = 0; c < 2; ++c) for (int i = skip; i < N; ++i) { const double e = a.getSample (c, i) - g * b.getSample (c, i); ed += e * e; }
            r.distortionPct = (float) (100.0 * std::sqrt (ed / (ea + 1e-30)));
        }
        // verdict
        juce::StringArray v;
        if (dv.special == Stream)
        {
            if (dv.target >= 0) v.add ("no normalisation, plays as loud as you master it");
            else if (r.streamDb < -0.5f) v.add ("played " + juce::String (-r.streamDb, 1) + " dB quieter");
            else if (r.streamDb > 0.5f) v.add ("turned up " + juce::String (r.streamDb, 1) + " dB");
            else v.add ("plays at your level");
        }
        int lo = 0, hi = 0;
        for (int k = 1; k < 4; ++k) { if (r.bandDb[k] < r.bandDb[lo]) lo = k; if (r.bandDb[k] > r.bandDb[hi]) hi = k; }
        if (r.bandDb[0] < r.bandDb[lo] && r.bandDb[0] < -20.0f && dv.hp > 100) { /* no sub on small speakers is expected */ }
        if (r.bandDb[lo] < -9.0f) v.add (juce::String (reportBandName (lo)) + " " + juce::String (juce::roundToInt (r.bandDb[lo])) + " dB");
        if (r.bandDb[hi] > 4.0f) v.add (juce::String (reportBandName (hi)) + " +" + juce::String (juce::roundToInt (r.bandDb[hi])) + " dB");
        if (r.distortionPct > 6.0f) v.add ("distorts " + juce::String (juce::roundToInt (r.distortionPct)) + " %");
        if (v.isEmpty()) v.add ("translates well");
        r.verdict = v.joinIntoString (",  ");
        r.valid = true;
    }
    if (progress) progress (1.0f);
    return out;
}

// ================================================================================================ state
void LumenMixCheckProcessor::setParam (const juce::String& id, float v)
{
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id))) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture(); }
}

void LumenMixCheckProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto st = apvts.copyState(); st.setProperty ("ir", irFile.getFullPathName(), nullptr);
    { const juce::SpinLock::ScopedLockType l (eqLock);
      juce::String eqText; for (auto& b : customEq) eqText << b.type << " " << b.f << " " << b.g << " " << b.q << ";";
      st.setProperty ("hpeq", eqText, nullptr); st.setProperty ("hpeqName", customEqFile.getFullPathName(), nullptr); }
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, d);
}
void LumenMixCheckProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto vt = juce::ValueTree::fromXml (*xml);
    const juce::String ir = vt.getProperty ("ir", ""); vt.removeProperty ("ir", nullptr);
    {
        const juce::String eqText = vt.getProperty ("hpeq", ""); customEqFile = juce::File (vt.getProperty ("hpeqName", "").toString().isNotEmpty() && juce::File::isAbsolutePath (vt.getProperty ("hpeqName", "").toString()) ? vt.getProperty ("hpeqName", "").toString() : juce::String());
        vt.removeProperty ("hpeq", nullptr); vt.removeProperty ("hpeqName", nullptr);
        std::vector<PeqBand> b;
        for (auto& t : juce::StringArray::fromTokens (eqText, ";", ""))
        { auto v = juce::StringArray::fromTokens (t, " ", ""); if (v.size() == 4) b.push_back ({ v[0].getIntValue(), v[1].getFloatValue(), v[2].getFloatValue(), v[3].getFloatValue() }); }
        const juce::SpinLock::ScopedLockType l (eqLock); customEq = b; ++customEqVersion;
    }
    apvts.replaceState (vt);
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            auto child = vt.getChildWithProperty ("id", rp->paramID);
            if (child.isValid() && child.hasProperty ("value")) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
        }
    if (ir.isNotEmpty() && juce::File::isAbsolutePath (ir)) { juce::String e; loadIR (juce::File (ir), e); }
}

juce::AudioProcessorEditor* LumenMixCheckProcessor::createEditor() { return new LumenMixCheckEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenMixCheckProcessor(); }
