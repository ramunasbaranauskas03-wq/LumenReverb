#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenBassProcessor::LumenBassProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "LumenBass", createParameterLayout())
{
    pMonoOn = p (IDs::monoOn); pMonoHz = p (IDs::monoHz); pWidth = p (IDs::width); pSub = p (IDs::sub);
    pOut = p (IDs::output);    pHarm = p (IDs::harmAmt);  pFocus = p (IDs::harmFocus); pChar = p (IDs::harmChar);
    pTone = p (IDs::harmTone); pListen = p (IDs::listen);
    pPhOn = p (IDs::phaseOn);  pPhRot = p (IDs::phaseRot); pPhDly = p (IDs::phaseDly); pPhFoc = p (IDs::phaseFoc);
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

void LumenBassProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.prepare (sampleRate);
    phase.prepare (sampleRate);
    lastFocus = -1.0f;
    capBuf.assign ((size_t) (sampleRate * 4.0), 0.0f);
    capPos = 0;
}

void LumenBassProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, false, 0);
    const int numCh = main.getNumChannels(), n = buffer.getNumSamples();
    if (n == 0 || numCh == 0) return;

    const float* scL = nullptr; const float* scR = nullptr;
    if (getBusCount (true) > 1)
        if (auto* b = getBus (true, 1); b != nullptr && b->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            if (sc.getNumChannels() > 0) { scL = sc.getReadPointer (0); scR = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : scL; }
        }
    scConnected.store (scL != nullptr);

    // Phase align (kick on sidechain)
    phase.handleRequests();
    if (std::abs (pPhFoc->load() - lastFocus) > 0.01f) { lastFocus = pPhFoc->load(); phase.setFocus (lastFocus); }
    phase.setParams (pPhOn->load() > 0.5f, pPhRot->load(), pPhDly->load());

    lumen::BassEngine::Params bp;
    bp.monoOn = pMonoOn->load() > 0.5f;  bp.monoHz = pMonoHz->load();  bp.width = pWidth->load() * 0.01f;
    bp.harmAmt = pHarm->load() * 0.01f;   bp.harmFocus = pFocus->load(); bp.harmChar = pChar->load() * 0.01f;
    bp.harmTone = pTone->load();          bp.subDb = pSub->load();       bp.outDb = pOut->load();
    bp.listen = pListen->load() > 0.5f;
    engine.beginBlock (bp);

    if (captureRequested.exchange (false)) { capPos = 0; capProgress.store (0.0f); capReady.store (false); capturing.store (true); }
    const bool cap = capturing.load();

    float* L = main.getWritePointer (0);
    float* R = numCh > 1 ? main.getWritePointer (1) : nullptr;
    float inPk = 0.0f, outPk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R != nullptr ? R[i] : L[i];
        inPk = std::max (inPk, std::max (std::abs (l), std::abs (r)));
        if (cap && capPos < (int) capBuf.size()) capBuf[(size_t) capPos++] = 0.5f * (l + r);

        const float k = scL != nullptr ? 0.5f * (scL[i] + scR[i]) : 0.0f;
        phase.processSample (l, r, k, scL != nullptr);
        engine.process (l, r);

        L[i] = R != nullptr ? l : 0.5f * (l + r);
        if (R != nullptr) R[i] = r;
        outPk = std::max (outPk, std::max (std::abs (l), std::abs (r)));
    }

    if (cap)
    {
        capProgress.store ((float) capPos / (float) capBuf.size());
        if (capPos >= (int) capBuf.size()) { capturing.store (false); capReady.store (true, std::memory_order_release); }
    }

    corrBefore.store (phase.corrBefore());
    corrAfter.store (phase.corrAfter());
    lowEndPresent.store (scL != nullptr && phase.hasLowEnd());
    lowCorrIn.store (engine.lowCorrIn());
    lowCorrOut.store (engine.lowCorrOut());
    phoneIn.store (engine.phoneIn());
    phoneOut.store (engine.phoneOut());
    hasBass.store (engine.hasBass());
    inLevel.store (inPk);
    outLevel.store (outPk);
}

bool LumenBassProcessor::takeCapture (std::vector<float>& out)
{
    if (! capReady.exchange (false, std::memory_order_acquire)) return false;
    out = capBuf;
    return true;
}

juce::AudioProcessorEditor* LumenBassProcessor::createEditor() { return new LumenBassEditor (*this); }

void LumenBassProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void LumenBassProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenBassProcessor(); }
