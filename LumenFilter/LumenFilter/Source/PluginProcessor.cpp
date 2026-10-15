#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenFilterProcessor::LumenFilterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "LumenFilter", createLayout())
{
}

bool LumenFilterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    if (out != mono && out != stereo) return false;
    if (in != mono && in != stereo) return false;
    if (in == stereo && out == mono) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != mono && sc != stereo) return false;
    }
    return true;
}

void LumenFilterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    const int maxBlock = juce::jmax (4096, samplesPerBlock);
    for (int k = 0; k < 3; ++k)
    {
        os[k] = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) k, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os[k]->initProcessing ((size_t) maxBlock);
    }
    envBuf.setSize (2, maxBlock);
    envOs.setSize (2, maxBlock * 4);
    curOs = (int) P (IDs::os);
    engine.prepare (fs * (1 << curOs));
    setLatencySamples ((int) std::round (os[curOs]->getLatencyInSamples()));
}

void LumenFilterProcessor::setCurrentProgram (int index)
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

void LumenFilterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const int n = buffer.getNumSamples();
    const int numIn = getMainBusNumInputChannels(), numOut = getMainBusNumOutputChannels();
    if (n == 0) return;
    if (numIn == 1 && numOut > 1) main.copyFrom (1, 0, main, 0, 0, n);

    // oversampling factor change -> re-prepare (rare, user action)
    const int want = (int) P (IDs::os);
    if (want != curOs)
    {
        curOs = want;
        os[curOs]->reset();
        engine.prepare (fs * (1 << curOs));
        setLatencySamples ((int) std::round (os[curOs]->getLatencyInSamples()));
    }

    double bpm = 120.0, ppq = 0.0; bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }

    lumen::flt::Params p;
    p.type = (int) P (IDs::type); p.cutoff = P (IDs::cutoff); p.res = P (IDs::res); p.drive = P (IDs::drive); p.mix = P (IDs::mix);
    p.outDb = P (IDs::out); p.spread = P (IDs::spread); p.keyMode = (int) P (IDs::keyMode); p.root = (int) P (IDs::root);
    p.lfoOn = P (IDs::lfoOn) > 0.5f; p.lfoShape = (int) P (IDs::lfoShape); p.lfoSync = P (IDs::lfoSync) > 0.5f; p.lfoBeats = divBeats ((int) P (IDs::lfoDiv));
    p.lfoRate = P (IDs::lfoRate); p.lfoDepth = P (IDs::lfoDepth); p.lfoStereo = P (IDs::lfoStereo);
    p.envOn = P (IDs::envOn) > 0.5f; p.envAmt = P (IDs::envAmt); p.envAttMs = P (IDs::envAtt); p.envRelMs = P (IDs::envRel);
    p.rise = P (IDs::rise); p.riseMode = (int) P (IDs::riseMode);
    engine.setParams (p, bpm, playing, ppq);

    // envelope source: input or sidechain
    const float* scP[2] { nullptr, nullptr };
    if (P (IDs::envSrc) > 0.5f && getBusCount (true) > 1)
        if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            if (sc.getNumChannels() > 0) { scP[0] = sc.getReadPointer (0); scP[1] = sc.getReadPointer (sc.getNumChannels() > 1 ? 1 : 0); }
        }

    float ip = 0.0f; for (int c = 0; c < juce::jmin (2, main.getNumChannels()); ++c) ip = juce::jmax (ip, main.getMagnitude (c, 0, n));
    inPeak.store (ip);

    const int chunk = envBuf.getNumSamples();
    const int f = 1 << curOs;
    const int nch = juce::jmin (2, main.getNumChannels());
    for (int start = 0; start < n; start += chunk)
    {
        const int len = juce::jmin (chunk, n - start);
        for (int c = 0; c < 2; ++c)
        {
            const float* src = scP[0] != nullptr ? scP[c] + start : main.getReadPointer (juce::jmin (c, nch - 1)) + start;
            envBuf.copyFrom (c, 0, src, len);
        }
        float* chans[2] { main.getWritePointer (0) + start, main.getWritePointer (nch - 1) + start };
        juce::dsp::AudioBlock<float> block (chans, (size_t) nch, (size_t) len);
        auto up = os[curOs]->processSamplesUp (block);
        const int un = (int) up.getNumSamples();
        for (int c = 0; c < 2; ++c)
        {
            const float* src = envBuf.getReadPointer (c);
            float* dst = envOs.getWritePointer (c);
            for (int i = 0; i < un; ++i) dst[i] = src[juce::jmin (len - 1, i / f)];
        }
        float* L = up.getChannelPointer (0);
        float* R = up.getNumChannels() > 1 ? up.getChannelPointer (1) : L;
        engine.process (L, R, envOs.getReadPointer (0), envOs.getReadPointer (1), un);
        os[curOs]->processSamplesDown (block);
    }

    float op = 0.0f; for (int c = 0; c < juce::jmin (2, main.getNumChannels()); ++c) op = juce::jmax (op, main.getMagnitude (c, 0, n));
    outPeak.store (op);
    cutL.store (engine.cutoffL); cutR.store (engine.cutoffR); lfoVal.store (engine.lfoValue); envVal.store (engine.envValue);
    riseHp.store (engine.riseHpHz); riseLp.store (engine.riseLpHz);
}

void LumenFilterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenFilterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            currentProgram = (int) vt.getProperty ("program", 0);
            apvts.replaceState (vt);
            for (auto* prm : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                {
                    auto child = vt.getChildWithProperty ("id", rp->paramID);
                    if (child.isValid() && child.hasProperty ("value")) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
                }
            ++programVersion;
        }
}

juce::AudioProcessorEditor* LumenFilterProcessor::createEditor() { return new LumenFilterEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenFilterProcessor(); }
