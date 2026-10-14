#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenCompressorProcessor::LumenCompressorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "LumenCompressor", createParameterLayout())
{
    pThr = p (IDs::threshold);  pRatio = p (IDs::ratio);     pKnee = p (IDs::knee);
    pAtk = p (IDs::attack);     pRel = p (IDs::release);     pAutoRel = p (IDs::autoRel);
    pRange = p (IDs::range);    pLa = p (IDs::lookahead);    pLink = p (IDs::link);
    pScHp = p (IDs::scHp);      pScLp = p (IDs::scLp);       pScSrc = p (IDs::scSource);
    pListen = p (IDs::scListen); pStyle = p (IDs::style);    pChan = p (IDs::channel);
    pDrive = p (IDs::drive);    pMakeup = p (IDs::makeup);   pAutoGain = p (IDs::autoGain);
    pMix = p (IDs::mix);        pOut = p (IDs::output);
    pPhOn = p (IDs::phaseOn);   pPhRot = p (IDs::phaseRot); pPhDly = p (IDs::phaseDly);
    pPhFoc = p (IDs::phaseFoc);

    for (auto& h : histIn) h.store (-100.0f);
    for (auto& h : histGr) h.store (0.0f);
}

bool LumenCompressorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();

    if (out != mono && out != stereo) return false;
    if (in != out) return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != mono && sc != stereo)
            return false;
    }
    return true;
}

void LumenCompressorProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.prepare (sampleRate);
    analyzer.prepare (sampleRate);
    phase.prepare (sampleRate);
    lastFocus = -1.0f;
    histInterval = juce::jmax (64, (int) (sampleRate * 6.0 / kHistory)); // ~6 s on screen
    histCounter = 0; histInAcc = 0.0f; histGrAcc = 0.0f;

    currentLatency = engine.getLatencySamples (pLa->load());
    setLatencySamples (currentLatency);
}

lumen::CompressorEngine::Params LumenCompressorProcessor::makeParams() const
{
    lumen::CompressorEngine::Params cp;
    cp.thresholdDb = pThr->load();
    cp.ratio       = pRatio->load();
    cp.kneeDb      = pKnee->load();
    cp.attackMs    = pAtk->load();
    cp.releaseMs   = pRel->load();
    cp.autoRelease = pAutoRel->load() > 0.5f;
    cp.rangeDb     = pRange->load();
    cp.lookaheadMs = pLa->load();
    cp.link        = pLink->load() * 0.01f;
    cp.scHpHz      = pScHp->load();
    cp.scLpHz      = pScLp->load();
    cp.external    = pScSrc->load() > 0.5f;
    cp.listen      = pListen->load() > 0.5f;
    cp.style       = juce::jlimit (0, 3, (int) pStyle->load());
    cp.channel     = juce::jlimit (0, 1, (int) pChan->load());
    cp.drive       = pDrive->load() * 0.01f;
    cp.makeupDb    = pMakeup->load();
    cp.autoGain    = pAutoGain->load() > 0.5f;
    cp.mix         = pMix->load() * 0.01f;
    cp.outputDb    = pOut->load();
    return cp;
}

void LumenCompressorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, false, 0);
    const int numCh = main.getNumChannels();
    const int n = buffer.getNumSamples();
    if (n == 0 || numCh == 0)
        return;

    // Sidechain bus (optional)
    const float* scL = nullptr;
    const float* scR = nullptr;
    if (getBusCount (true) > 1)
        if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            if (sc.getNumChannels() > 0)
            {
                scL = sc.getReadPointer (0);
                scR = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : scL;
            }
        }

    float* L = main.getWritePointer (0);
    float* R = numCh > 1 ? main.getWritePointer (1) : nullptr;

    // ---- Kick/bass phase alignment (before dynamics) ----
    phase.handleRequests();
    if (std::abs (pPhFoc->load() - lastFocus) > 0.01f) { lastFocus = pPhFoc->load(); phase.setFocus (lastFocus); }
    phase.setParams (pPhOn->load() > 0.5f, pPhRot->load(), pPhDly->load());
    {
        const bool kick = scL != nullptr;
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R != nullptr ? R[i] : L[i];
            const float k = kick ? 0.5f * (scL[i] + scR[i]) : 0.0f;
            phase.processSample (l, r, k, kick);
            L[i] = l;
            if (R != nullptr) R[i] = r;
        }
        corrBefore.store (phase.corrBefore());
        corrAfter.store (phase.corrAfter());
        lowEndPresent.store (kick && phase.hasLowEnd());
    }

    analyzer.process (L, R, n);

    // Latency follows lookahead
    const int lat = engine.getLatencySamples (pLa->load());
    if (lat != currentLatency)
    {
        currentLatency = lat;
        setLatencySamples (lat);
    }

    const auto cp = makeParams();
    engine.setParams (cp);
    engine.beginBlock();
    autoMakeupDb.store (engine.getAutoMakeupDb());

    float peakIn = 0.0f, peakOut = 0.0f, peakSc = 0.0f, maxGr = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float xl = L[i];
        const float xr = R != nullptr ? R[i] : xl;
        const float sl = scL != nullptr ? scL[i] : 0.0f;
        const float sr_ = scR != nullptr ? scR[i] : 0.0f;

        float yl, yr, gr;
        engine.processSample (xl, xr, sl, sr_, yl, yr, gr);

        L[i] = R != nullptr ? yl : 0.5f * (yl + yr);
        if (R != nullptr) R[i] = yr;

        const float inPk = juce::jmax (std::abs (xl), std::abs (xr));
        peakIn  = juce::jmax (peakIn, inPk);
        peakOut = juce::jmax (peakOut, juce::jmax (std::abs (yl), std::abs (yr)));
        peakSc  = juce::jmax (peakSc, juce::jmax (std::abs (sl), std::abs (sr_)));
        maxGr   = juce::jmax (maxGr, gr);

        histInAcc = juce::jmax (histInAcc, inPk);
        histGrAcc = juce::jmax (histGrAcc, gr);
        if (++histCounter >= histInterval)
        {
            const int w = histWrite.load (std::memory_order_relaxed);
            histIn[(size_t) w].store (juce::Decibels::gainToDecibels (histInAcc, -100.0f), std::memory_order_relaxed);
            histGr[(size_t) w].store (histGrAcc, std::memory_order_relaxed);
            histWrite.store ((w + 1) % kHistory, std::memory_order_release);
            histCounter = 0; histInAcc = 0.0f; histGrAcc = 0.0f;
        }
    }

    inLevel.store (peakIn);
    outLevel.store (peakOut);
    scLevel.store (peakSc);
    grDb.store (maxGr);
    scConnected.store (scL != nullptr);
}

juce::AudioProcessorEditor* LumenCompressorProcessor::createEditor()
{
    return new LumenCompressorEditor (*this);
}

void LumenCompressorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LumenCompressorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LumenCompressorProcessor();
}
