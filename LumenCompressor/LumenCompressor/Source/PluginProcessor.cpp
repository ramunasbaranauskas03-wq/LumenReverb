#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
enum PIdx
{
    kStyle, kThr, kRatio, kKnee, kAtk, kRel, kAutoRel, kHold, kRange, kLa, kAutoThr,
    kChan, kLink, kScSrc, kScHp, kScLp, kBellF, kBellG, kListen, kDiv, kOffset,
    kChar, kDrive, kCharPost, kQuality, kMakeup, kAutoGain, kMix, kOut, kDelta,
    kPhOn, kPhRot, kPhDly, kPhFoc, kNumP
};
static_assert (kNumP <= 40, "pointer table too small");
}

LumenCompressorProcessor::LumenCompressorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "LumenCompressor2", createParameterLayout())
{
    const char* ids[] { IDs::style, IDs::threshold, IDs::ratio, IDs::knee, IDs::attack, IDs::release, IDs::autoRel, IDs::hold, IDs::range, IDs::lookahead, IDs::autoThr,
                        IDs::chanMode, IDs::link, IDs::scSource, IDs::scHp, IDs::scLp, IDs::scBellF, IDs::scBellG, IDs::scListen, IDs::division, IDs::offset,
                        IDs::character, IDs::drive, IDs::charPost, IDs::quality, IDs::makeup, IDs::autoGain, IDs::mix, IDs::output, IDs::delta,
                        IDs::phaseOn, IDs::phaseRot, IDs::phaseDly, IDs::phaseFoc };
    static_assert (sizeof (ids) / sizeof (ids[0]) == kNumP, "id list");
    for (int k = 0; k < kNumP; ++k) { pr[(size_t) k] = apvts.getRawParameterValue (ids[k]); jassert (pr[(size_t) k] != nullptr); }
}

bool LumenCompressorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    if (out != mono && out != stereo) return false;
    if (in != out) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != mono && sc != stereo) return false;
    }
    return true;
}

void LumenCompressorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate; maxBlock = juce::jmax (16, samplesPerBlock);
    for (int k = 0; k < 3; ++k)
    {
        os[k] = std::make_unique<juce::dsp::Oversampling<float>> (4, (size_t) (k + 1), juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os[k]->initProcessing ((size_t) maxBlock);
    }
    work.setSize (4, maxBlock);
    engine.prepare (fs, maxBlock);
    analyzer.prepare (fs);
    phase.prepare (fs);
    lastFocus = -1.0f;
    quality = -1; latencyNow = -1;
    engine.setParams (readParams());
    applyQuality ((int) pr[kQuality]->load());
}

void LumenCompressorProcessor::applyQuality (int q)
{
    q = juce::jlimit (0, 3, q);
    quality = q; osFactor = 1 << q;
    if (q > 0) os[q - 1]->reset();
    osLatency = q > 0 ? juce::roundToInt (os[q - 1]->getLatencyInSamples()) : 0;
    engine.prepareOs (fs * osFactor);
    updateLatency();
}

void LumenCompressorProcessor::updateLatency()
{
    const int l = engine.lookaheadSamples() + osLatency;
    if (l != latencyNow) { latencyNow = l; setLatencySamples (l); }
}

lumen::comp2::Params LumenCompressorProcessor::readParams() const
{
    auto v = [this] (int i) { return pr[(size_t) i]->load(); };
    auto b = [&] (int i) { return v (i) > 0.5f; };
    lumen::comp2::Params p;
    p.style = (int) v (kStyle); p.threshold = v (kThr); p.ratio = v (kRatio) >= 99.0f ? 1000.0f : v (kRatio); p.knee = v (kKnee);
    p.attack = v (kAtk); p.release = v (kRel); p.autoRelease = b (kAutoRel); p.hold = v (kHold); p.range = v (kRange);
    p.lookahead = v (kLa); p.autoThreshold = b (kAutoThr);
    p.chanMode = (int) v (kChan); p.link = v (kLink) * 0.01f;
    p.scSource = (int) v (kScSrc); p.scHp = v (kScHp); p.scLp = v (kScLp); p.scBellF = v (kBellF); p.scBellG = v (kBellG); p.scListen = b (kListen);
    p.divisionQ = divisionQuarters ((int) v (kDiv)); p.offset = v (kOffset) * 0.01f;
    p.character = (int) v (kChar); p.drive = v (kDrive) * 0.01f; p.charPost = b (kCharPost);
    p.makeupDb = v (kMakeup); p.autoGain = b (kAutoGain); p.mix = v (kMix) * 0.01f; p.outDb = v (kOut); p.delta = b (kDelta);
    return p;
}

void LumenCompressorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, false, 0);
    const int nCh = main.getNumChannels(), n = buffer.getNumSamples();
    if (n == 0 || nCh == 0) return;

    const float* scL = nullptr; const float* scR = nullptr;
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            if (sc.getNumChannels() > 0) { scL = sc.getReadPointer (0); scR = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : scL; }
        }
    float scPk = 0;
    if (scL != nullptr) for (int i = 0; i < n; ++i) scPk = std::max (scPk, std::max (std::abs (scL[i]), std::abs (scR[i])));
    scLevel.store (scPk); scActive.store (scPk > 1.0e-4f);

    const int q = (int) pr[kQuality]->load();
    if (q != quality) applyQuality (q);
    const auto prm = readParams();
    engine.setParams (prm);
    updateLatency();

    // transport for Tempo Sync
    lumen::comp2::Transport tp;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) tp.bpm = juce::jlimit (20.0, 400.0, *bpm);
            if (auto ppq = pos->getPpqPosition()) { tp.ppq = *ppq; tp.valid = true; }
            tp.playing = pos->getIsPlaying();
        }

    float* L = main.getWritePointer (0);
    float* R = nCh > 1 ? main.getWritePointer (1) : nullptr;

    // kick <-> bass phase alignment (before the compressor)
    phase.handleRequests();
    if (std::abs (pr[kPhFoc]->load() - lastFocus) > 0.01f) { lastFocus = pr[kPhFoc]->load(); phase.setFocus (lastFocus); }
    phase.setParams (pr[kPhOn]->load() > 0.5f, pr[kPhRot]->load(), pr[kPhDly]->load());
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R != nullptr ? R[i] : L[i];
        const float k = scL != nullptr ? 0.5f * (scL[i] + scR[i]) : 0.0f;
        phase.processSample (l, r, k, scL != nullptr);
        L[i] = l; if (R != nullptr) R[i] = r;
    }
    corrBefore.store (phase.corrBefore()); corrAfter.store (phase.corrAfter());
    lowEndPresent.store (scL != nullptr && phase.hasLowEnd());
    analyzer.process (L, R, n);

    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - start);
        float* wl = work.getWritePointer (0); float* wr = work.getWritePointer (1);
        for (int i = 0; i < len; ++i) { wl[i] = L[start + i]; wr[i] = R != nullptr ? R[start + i] : L[start + i]; }

        engine.detect (wl, wr, scL != nullptr ? scL + start : nullptr, scR != nullptr ? scR + start : nullptr, len, tp);
        // the dry signal rides through the same oversampler (channels 2/3) -> dry and wet stay phase-identical
        float* dl = work.getWritePointer (2); float* dr = work.getWritePointer (3);
        std::copy (engine.dryData (0), engine.dryData (0) + len, dl);
        std::copy (engine.dryData (1), engine.dryData (1) + len, dr);
        if (tp.valid) tp.ppq += len * tp.bpm / 60.0 / fs;

        if (osFactor == 1)
        {
            for (int i = 0; i < len; ++i) engine.shapeSample (wl[i], wr[i], engine.gainAt (0, i, 1.0f), engine.gainAt (1, i, 1.0f));
        }
        else
        {
            auto& o = *os[quality - 1];
            juce::dsp::AudioBlock<float> blk (work);
            auto sub = blk.getSubBlock (0, (size_t) len);
            auto up = o.processSamplesUp (sub);
            float* ul = up.getChannelPointer (0); float* ur = up.getChannelPointer (1);
            const int un = (int) up.getNumSamples();
            const float inv = 1.0f / (float) osFactor;
            for (int j = 0; j < un; ++j)
            {
                const int i = juce::jmin (len - 1, j / osFactor);
                const float frac = (float) (j % osFactor + 1) * inv;
                engine.shapeSample (ul[j], ur[j], engine.gainAt (0, i, frac), engine.gainAt (1, i, frac));
            }
            o.processSamplesDown (sub);
        }
        engine.endOsBlock (len);
        engine.finish (wl, wr, dl, dr, len);

        for (int i = 0; i < len; ++i)
        {
            if (R != nullptr) { L[start + i] = wl[i]; R[start + i] = wr[i]; }
            else L[start + i] = 0.5f * (wl[i] + wr[i]);
        }
    }
}

//==============================================================================
void LumenCompressorProcessor::setParam (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

void LumenCompressorProcessor::setCurrentProgram (int i)
{
    if (! juce::isPositiveAndBelow (i, getNumPrograms())) return;
    currentProgram = i;
    const auto& pre = factoryPresets()[(size_t) i];
    const juce::StringArray keep { IDs::delta, IDs::scListen, IDs::output, IDs::phaseOn, IDs::phaseRot, IDs::phaseDly, IDs::phaseFoc, IDs::quality };
    const bool presetSetsQuality = std::any_of (pre.v.begin(), pre.v.end(), [] (auto& kv) { return kv.first == IDs::quality; });
    for (auto* ap : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ap))
        {
            const auto id = rp->getParameterID();
            if (keep.contains (id) && ! (id == IDs::quality && presetSetsQuality)) continue;
            float v = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : pre.v) if (kv.first == id) v = kv.second;
            rp->setValueNotifyingHost (rp->convertTo0to1 (v));
        }
    ++programVersion;
}

juce::String LumenCompressorProcessor::applyLearn (const lumen::Features& f)
{
    using M = lumen::SmartAssist::Material;
    const M m = lumen::SmartAssist::classify (f);
    if (m == M::Silence) return "LEARN: no audio heard - press play and try again";
    struct S { int style; float ratio, knee, attack, release; bool autoRel; float scHp, targetGr; const char* what; };
    S s {};
    switch (m)
    {
        case M::Percussive: s = { 2, 4.0f, 3, 10.0f,  80, false,  20, 5.0f, "drums -> Punch 4:1, slow attack keeps the transients" }; break;
        case M::Bass:       s = { 3, 4.0f, 6, 15.0f, 150, false,  20, 5.0f, "bass -> Opto 4:1, even level" }; break;
        case M::Pad:        s = { 4, 2.0f, 8, 30.0f, 350, true,   20, 2.5f, "pad -> Vari-Mu 2:1, gentle glue" }; break;
        case M::FullMix:    s = { 1, 2.0f, 6, 30.0f, 200, true,   90, 2.0f, "full mix -> Glue 2:1, SC high-pass 90 Hz" }; break;
        case M::VocalLead:
        case M::Silence:
        default:            s = { 5, 3.0f, 6,  5.0f, 120, false,  90, 5.0f, "vocal / lead -> Vocal 3:1" }; break;
    }
    // threshold from the measured level, so a hot or a quiet recording gets the same amount of compression
    const bool rmsStyle = s.style == 3 || s.style == 4 || s.style == 5 || s.style == 1;
    const float spread = f.loudDb > -99.0f ? juce::jlimit (0.0f, 20.0f, f.loudDb - f.levelDb) : 0.0f;
    const float refDb  = f.levelDb + spread * juce::jlimit (0.0f, 0.85f, spread / 16.0f);
    const float detDb  = refDb + (rmsStyle ? 3.0f : 3.0f + 0.5f * juce::jlimit (0.0f, 12.0f, f.crestDb - 3.0f));
    const float over   = s.targetGr * s.ratio / std::max (0.5f, s.ratio - 1.0f);
    const float thr    = juce::jlimit (-60.0f, 0.0f, detDb - over);
    setParam (IDs::style, (float) s.style); setParam (IDs::ratio, s.ratio); setParam (IDs::knee, s.knee);
    setParam (IDs::attack, s.attack); setParam (IDs::release, s.release); setParam (IDs::autoRel, s.autoRel ? 1.0f : 0.0f);
    setParam (IDs::scHp, s.scHp); setParam (IDs::threshold, thr); setParam (IDs::scSource, 0);
    return "LEARN: " + juce::String (s.what) + ", threshold " + juce::String (thr, 1) + " dB (~" + juce::String (s.targetGr, 1) + " dB GR)";
}

void LumenCompressorProcessor::switchAB (int to)
{
    if (to == abSlot) return;
    getStateInformation (ab[abSlot]);
    abSlot = to;
    if (ab[to].getSize() > 0) setStateInformation (ab[to].getData(), (int) ab[to].getSize());
    ++programVersion;
}
void LumenCompressorProcessor::copyAB() { getStateInformation (ab[1 - abSlot]); }

juce::AudioProcessorEditor* LumenCompressorProcessor::createEditor() { return new LumenCompressorEditor (*this); }

void LumenCompressorProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto st = apvts.copyState();
    st.setProperty ("program", currentProgram, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, d);
}

void LumenCompressorProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (tree);
    currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty ("program", 0));
    for (auto* ap : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ap))
        {
            auto child = tree.getChildWithProperty ("id", rp->getParameterID());
            if (child.isValid() && child.hasProperty ("value"))
                rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
        }
    ++programVersion;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenCompressorProcessor(); }
