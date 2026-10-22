#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenOTTProcessor::LumenOTTProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenOTT", createLayout())
{
    for (int b = 0; b < 3; ++b) { bandLvl[b].store (-100); bandGain[b].store (0); bandOff[b].store (0); }
}

bool LumenOTTProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    if (out != mono && out != stereo) return false;
    if (in != mono && in != stereo) return false;
    return ! (in == stereo && out == mono);
}

void LumenOTTProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate; maxBlock = juce::jlimit (64, 8192, samplesPerBlock);
    for (int k = 0; k < 3; ++k)
    {
        using OS = juce::dsp::Oversampling<float>;
        os[k] = std::make_unique<OS> (2, (size_t) k, OS::filterHalfBandFIREquiripple, true, true);
        os[k]->initProcessing ((size_t) maxBlock);
    }
    curOs = -1;
}

lumen::ott::Params LumenOTTProcessor::readParams() const
{
    lumen::ott::Params p;
    p.x[0] = P (IDs::x1); p.x[1] = P (IDs::x2);
    for (int b = 0; b < 3; ++b)
    {
        auto& bp = p.band[b];
        bp.downThr = P (IDs::dthr (b)); bp.upThr = P (IDs::uthr (b)); bp.gainDb = P (IDs::gain (b));
        bp.downRatio = bandDefaults (b).ratio; bp.upRatio = 4.0f;
        bp.solo = P (IDs::solo (b)) > 0.5f; bp.bypass = P (IDs::byp (b)) > 0.5f;
    }
    p.depth = P (IDs::depth); p.time = P (IDs::time); p.upward = P (IDs::upward); p.downward = P (IDs::downward);
    p.inDb = P (IDs::in); p.outDb = P (IDs::out); p.style = (int) P (IDs::style); p.adaptive = P (IDs::adaptive) > 0.5f;
    p.maxBoost = P (IDs::maxBoost); p.floorDb = P (IDs::floor) <= -99.5f ? -400.0f : P (IDs::floor); p.link = P (IDs::link);
    p.autoGain = P (IDs::autoGain) > 0.5f;
    return p;
}

void LumenOTTProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const int n = buffer.getNumSamples();
    if (n == 0 || main.getNumChannels() == 0) return;
    if (getMainBusNumInputChannels() == 1 && main.getNumChannels() > 1) main.copyFrom (1, 0, main, 0, 0, n);
    const int nch = juce::jmin (2, main.getNumChannels());
    const int want = juce::jlimit (0, 2, (int) P (IDs::os));
    if (want != curOs)
    {
        curOs = want;
        os[curOs]->reset();
        engine.prepare (fs * (1 << curOs));
        const int lat = (int) std::lround ((curOs > 0 ? os[curOs]->getLatencyInSamples() : 0.0) + (double) engine.latency() / (1 << curOs));
        setLatencySamples (lat);
    }
    engine.setParams (readParams());
    float ip = 0; for (int c = 0; c < nch; ++c) ip = juce::jmax (ip, main.getMagnitude (c, 0, n));
    inPk.store (ip);
    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - start);
        float* ch[2] { main.getWritePointer (0) + start, main.getWritePointer (nch - 1) + start };
        if (curOs == 0) { engine.process (ch[0], ch[1], len); continue; }
        juce::dsp::AudioBlock<float> block (ch, (size_t) nch, (size_t) len);
        auto up = os[curOs]->processSamplesUp (block);
        float* L = up.getChannelPointer (0); float* R = up.getNumChannels() > 1 ? up.getChannelPointer (1) : L;
        engine.process (L, R, (int) up.getNumSamples());
        os[curOs]->processSamplesDown (block);
    }
    if (P (IDs::clip) > 0.5f)
        for (int c = 0; c < nch; ++c)
        {
            float* d = main.getWritePointer (c);
            for (int i = 0; i < n; ++i) { const float a = std::abs (d[i]); if (a > 0.7f) { const float y = 0.7f + 0.3f * std::tanh ((a - 0.7f) / 0.3f); d[i] = d[i] < 0 ? -y : y; } }
        }
    float op = 0; for (int c = 0; c < nch; ++c) op = juce::jmax (op, main.getMagnitude (c, 0, n));
    outPk.store (op);
    for (int b = 0; b < 3; ++b) { bandLvl[b].store (engine.meterLvl[b]); bandGain[b].store (engine.meterGain[b]); bandOff[b].store (engine.thresholdOffset (b)); }
    engine.resetMeters();
    agDb.store (engine.autoGainDb());
}

void LumenOTTProcessor::setParam (const juce::String& id, float v)
{
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id))) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture(); }
}

void LumenOTTProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (rp->paramID == IDs::os) continue;
            float v = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : factoryPresets()[(size_t) index].v) if (rp->paramID == kv.first) v = kv.second;
            rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture();
        }
    ++programVersion;
}

void LumenOTTProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (ab[abSlot]); abSlot = to;
    if (ab[to].getSize() > 0) setStateInformation (ab[to].getData(), (int) ab[to].getSize());
}
void LumenOTTProcessor::copyAB() { getStateInformation (ab[1 - abSlot]); }

void LumenOTTProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto st = apvts.copyState(); st.setProperty ("program", currentProgram, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, d);
}
void LumenOTTProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto vt = juce::ValueTree::fromXml (*xml);
    currentProgram = (int) vt.getProperty ("program", 0); vt.removeProperty ("program", nullptr);
    apvts.replaceState (vt);
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            auto child = vt.getChildWithProperty ("id", rp->paramID);
            if (child.isValid() && child.hasProperty ("value")) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
        }
    ++programVersion;
}

juce::AudioProcessorEditor* LumenOTTProcessor::createEditor() { return new LumenOTTEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenOTTProcessor(); }
