#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
enum PIdx
{
    qQuality, qIn, qOut, qMix, qX1, qX2, qClip, qCompare, qDelta, qDuck, qDuckRel, qDuckMid,
    sOn, sSolo, sMono, sTight, sGen, sGenMode, sPhone, sChar, sLevel,
    mOn, mSolo, mType, mDrive, mTone, mMix, mSquash, mWidth, mLevel,
    tOn, tSolo, tType, tDrive, tTone, tMix, tSquash, tWidth, tLevel, kNumP
};
static_assert (kNumP <= 40, "pointer table too small");
}

LumenBassProcessor::LumenBassProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "LumenBass2", createParameterLayout())
{
    const char* g[] { IDs::quality, IDs::input, IDs::output, IDs::mix, IDs::x1, IDs::x2, IDs::clip, IDs::compare, IDs::delta, IDs::duck, IDs::duckRel, IDs::duckMid,
                      IDs::subOn, IDs::subSolo, IDs::subMono, IDs::subTight, IDs::subGen, IDs::subGenMode, IDs::subPhone, IDs::subChar, IDs::subLevel };
    int k = 0;
    for (auto* id : g) pr[(size_t) k++] = apvts.getRawParameterValue (id);
    for (auto* b : { "mid", "top" })
        for (auto* p : { "on", "solo", "type", "drive", "tone", "mix", "squash", "width", "level" })
            pr[(size_t) k++] = apvts.getRawParameterValue (bandId (b, p));
    jassert (k == kNumP);
}

bool LumenBassProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void LumenBassProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate; maxBlock = juce::jmax (16, samplesPerBlock);
    for (int k = 0; k < 3; ++k)
    {
        os[k] = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) (k + 1), juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        os[k]->initProcessing ((size_t) maxBlock);
    }
    work.setSize (2, maxBlock); duckGain.assign ((size_t) maxBlock, 1.0f);
    duck.prepare (fs);
    corrL.set (150, fs); corrR.set (150, fs); corrL.reset(); corrR.reset();
    for (int c = 0; c < 2; ++c) { phIn[c].set (c == 0 ? 200 : 1200, fs); phOut[c].set (c == 0 ? 200 : 1200, fs); phIn[c].reset(); phOut[c].reset(); }
    eL = eR = eLR = ePhIn = ePhOut = 0; meterC = 1.0f - std::exp (-1.0f / (float) (0.3 * fs));
    cap.assign ((size_t) (fs * 4.0), 0.0f); capPos = 0;
    quality = -1; applyQuality ((int) pr[qQuality]->load());
}

void LumenBassProcessor::applyQuality (int q)
{
    q = juce::jlimit (0, 3, q);
    quality = q; osFactor = 1 << q;
    if (q > 0) os[q - 1]->reset();
    engine.prepare (fs * osFactor, osFactor);
    setLatencySamples (q > 0 ? juce::roundToInt (os[q - 1]->getLatencyInSamples()) : 0);
}

lumen::bass2::Params LumenBassProcessor::readParams() const
{
    auto v = [this] (int i) { return pr[(size_t) i]->load(); };
    auto b = [&] (int i) { return v (i) > 0.5f; };
    lumen::bass2::Params p;
    p.inDb = v (qIn); p.outDb = v (qOut); p.mix = v (qMix) * 0.01f; p.x1 = v (qX1); p.x2 = v (qX2);
    p.clip = b (qClip); p.compare = b (qCompare); p.delta = b (qDelta); p.duckMid = b (qDuckMid);
    p.soloSub = b (sSolo); p.soloMid = b (mSolo); p.soloTop = b (tSolo);
    p.sub.on = b (sOn); p.sub.mono = b (sMono); p.sub.tight = v (sTight) * 0.01f; p.sub.gen = v (sGen) * 0.01f;
    p.sub.genMode = (int) v (sGenMode); p.sub.phone = v (sPhone) * 0.01f; p.sub.character = v (sChar) * 0.01f; p.sub.levelDb = v (sLevel);
    auto band = [&] (int o, lumen::bass2::BandParams& bp)
    {
        bp.on = b (o); bp.type = (int) v (o + 2); bp.drive = v (o + 3) * 0.01f; bp.tone = v (o + 4) * 0.01f; bp.mix = v (o + 5) * 0.01f;
        bp.squash = v (o + 6) * 0.01f; bp.width = v (o + 7) * 0.01f; bp.levelDb = v (o + 8);
    };
    band (mOn, p.mid); band (tOn, p.top);
    return p;
}

void LumenBassProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

    const int q = (int) pr[qQuality]->load();
    if (q != quality) applyQuality (q);
    if (learnRequest.exchange (false)) { capPos = 0; capReady.store (false); capProgress.store (0); capturing.store (true); }
    const bool cap0 = capturing.load();

    const float duckDepth = pr[qDuck]->load();
    duck.setRelease (pr[qDuckRel]->load());
    const auto prm = readParams();
    engine.setParams (prm);
    const bool clipOn = prm.clip && ! prm.delta;

    float ip = 0, op = 0, scE = 0;
    int rp = ringPos.load (std::memory_order_relaxed);
    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - start);
        float* L = main.getWritePointer (0) + start;
        float* R = nCh > 1 ? main.getWritePointer (1) + start : nullptr;
        float* wl = work.getWritePointer (0); float* wr = work.getWritePointer (1);
        for (int i = 0; i < len; ++i)
        {
            const float l = L[i], r = R != nullptr ? R[i] : L[i];
            wl[i] = l; wr[i] = r;
            const float m = 0.5f * (l + r);
            ip = std::max (ip, std::max (std::abs (l), std::abs (r)));
            ring[0][(rp + i) & (kRing - 1)] = m;
            const float ph = (float) phIn[1].lp (phIn[0].hp (m)); ePhIn += (ph * ph - ePhIn) * meterC;
            if (cap0 && capPos < (int) cap.size()) cap[(size_t) capPos++] = m;
            if (scL != nullptr)
            {
                const float s = 0.5f * (scL[start + i] + scR[start + i]);
                scE = std::max (scE, std::abs (s));
                ring[2][(rp + i) & (kRing - 1)] = s;
                duckGain[(size_t) i] = duckDepth > 0.05f ? duck.process (s, duckDepth) : 1.0f;
            }
            else { duckGain[(size_t) i] = 1.0f; ring[2][(rp + i) & (kRing - 1)] = 0.0f; }
        }

        if (osFactor == 1)
        {
            for (int i = 0; i < len; ++i) engine.process (wl[i], wr[i], duckGain[(size_t) i]);
        }
        else
        {
            auto& o = *os[quality - 1];
            juce::dsp::AudioBlock<float> blk (work);
            auto sub = blk.getSubBlock (0, (size_t) len);
            auto up = o.processSamplesUp (sub);
            float* ul = up.getChannelPointer (0); float* ur = up.getChannelPointer (1);
            const int un = (int) up.getNumSamples();
            for (int j = 0; j < un; ++j) engine.process (ul[j], ur[j], duckGain[(size_t) juce::jmin (len - 1, j / osFactor)]);
            o.processSamplesDown (sub);
        }

        for (int i = 0; i < len; ++i)
        {
            float l = wl[i], r = wr[i];
            if (clipOn) { l = juce::jlimit (-0.999f, 0.999f, l); r = juce::jlimit (-0.999f, 0.999f, r); }   // catch filter overshoot after downsampling
            if (R != nullptr) { L[i] = l; R[i] = r; } else L[i] = 0.5f * (l + r);
            op = std::max (op, std::max (std::abs (l), std::abs (r)));
            const float m = 0.5f * (l + r);
            ring[1][(rp + i) & (kRing - 1)] = m;
            const float cl = (float) corrL.lp (l), cr = (float) corrR.lp (r);
            eL += (cl * cl - eL) * meterC; eR += (cr * cr - eR) * meterC; eLR += (cl * cr - eLR) * meterC;
            const float ph = (float) phOut[1].lp (phOut[0].hp (m)); ePhOut += (ph * ph - ePhOut) * meterC;
        }
        rp = (rp + len) & (kRing - 1);
    }
    ringPos.store (rp, std::memory_order_release);

    if (cap0)
    {
        capProgress.store ((float) capPos / (float) cap.size());
        if (capPos >= (int) cap.size()) { capturing.store (false); capReady.store (true, std::memory_order_release); }
    }

    inPk.store (std::max (ip, inPk.load() * 0.9f)); outPk.store (std::max (op, outPk.load() * 0.9f));
    subGr.store (engine.subGrDb()); midSq.store (engine.midSqDb()); topSq.store (engine.topSqDb());
    duckDb.store (duckDepth > 0.05f && scL != nullptr ? duck.gainDb() : 0.0f);
    scActive.store (scE > 1.0e-4f);
    lowCorr.store (eL * eR > 1.0e-14f ? eLR / std::sqrt (eL * eR) : 1.0f);
    phoneDb.store (ePhIn > 1.0e-10f ? 10.0f * std::log10 (std::max (ePhOut, 1.0e-12f) / ePhIn) : 0.0f);
    x1Now.store (engine.x1Now()); x2Now.store (engine.x2Now());
}

//==============================================================================
void LumenBassProcessor::setParam (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

void LumenBassProcessor::setCurrentProgram (int i)
{
    if (! juce::isPositiveAndBelow (i, getNumPrograms())) return;
    currentProgram = i;
    const auto& pre = factoryPresets()[(size_t) i];
    const juce::StringArray keep { IDs::quality, IDs::compare, IDs::delta, IDs::subSolo, "mid_solo", "top_solo", IDs::input, IDs::output };
    for (auto* ap : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ap))
        {
            const auto id = rp->getParameterID();
            if (keep.contains (id)) continue;
            float v = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : pre.v) if (kv.first == id) v = kv.second;
            rp->setValueNotifyingHost (rp->convertTo0to1 (v));
        }
    ++programVersion;
}

float LumenBassProcessor::detectPitch (int which) const
{
    std::vector<float> x ((size_t) kRing);
    const int pos = ringPos.load (std::memory_order_acquire);
    for (int i = 0; i < kRing; ++i) x[(size_t) i] = ring[which][(pos + i) & (kRing - 1)];
    return lumen::bass2::detectPitch (x.data(), kRing, fs);
}

juce::String LumenBassProcessor::learn()
{
    if (! capReady.exchange (false, std::memory_order_acquire)) return {};
    const auto info = lumen::bass2::analyse (cap.data(), (int) cap.size(), fs);
    if (! info.valid) return "LEARN: no bass signal heard - play the bass and try again";
    juce::String msg = "LEARN: ";
    if (info.f0 > 0)
    {
        int oct; float cents; const char* nn = lumen::bass2::noteName (info.f0, oct, cents);
        const float x1 = juce::jlimit (60.0f, 180.0f, info.f0 * 2.2f);
        setParam (IDs::x1, x1);
        msg << "bass around " << nn << oct << " (" << juce::String (info.f0, 1) << " Hz), Sub/Body split " << hzText (x1);
    }
    else msg << "no steady pitch found";
    const bool weakSub = info.subRatio < 0.15f, heavySub = info.subRatio > 0.5f;
    setParam (IDs::subPhone, heavySub ? 55.0f : 30.0f);
    setParam (IDs::subTight, info.crest > 5.0f ? 55.0f : 40.0f);
    setParam (IDs::subGen, weakSub ? 40.0f : 0.0f);
    if (weakSub) setParam (IDs::subGenMode, 0);
    msg << (heavySub ? ", heavy sub -> more phone harmonics" : weakSub ? ", weak sub -> sub generator on" : ", balanced sub");
    return msg;
}

void LumenBassProcessor::switchAB (int to)
{
    if (to == abSlot) return;
    getStateInformation (ab[abSlot]);
    abSlot = to;
    if (ab[to].getSize() > 0) setStateInformation (ab[to].getData(), (int) ab[to].getSize());
    ++programVersion;
}
void LumenBassProcessor::copyAB() { getStateInformation (ab[1 - abSlot]); }

juce::AudioProcessorEditor* LumenBassProcessor::createEditor() { return new LumenBassEditor (*this); }

void LumenBassProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto st = apvts.copyState();
    st.setProperty ("program", currentProgram, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, d);
}

void LumenBassProcessor::setStateInformation (const void* data, int size)
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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenBassProcessor(); }
