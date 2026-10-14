#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenEQProcessor::LumenEQProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "LumenEQ", createParameterLayout())
{
    for (int b = 0; b < 6; ++b)
    {
        pOn[b] = p (IDs::on (b)); pType[b] = p (IDs::type (b)); pFreq[b] = p (IDs::freq (b)); pGain[b] = p (IDs::gain (b));
        pQ[b] = p (IDs::q (b));   pDyn[b] = p (IDs::dyn (b));   pThr[b] = p (IDs::thr (b));   pSc[b] = p (IDs::sc (b));
    }
    pAtk = p (IDs::attack); pRel = p (IDs::release); pOut = p (IDs::output);
    bufTrack.assign (kFifo, 0.0f);
    bufSc.assign (kFifo, 0.0f);
}

lumen::EqBand LumenEQProcessor::bandFromParams (int b) const
{
    lumen::EqBand e;
    e.on = pOn[b]->load() > 0.5f; e.type = (int) pType[b]->load(); e.freq = pFreq[b]->load(); e.gainDb = pGain[b]->load();
    e.q = pQ[b]->load(); e.dynDb = pDyn[b]->load(); e.threshDb = pThr[b]->load(); e.external = pSc[b]->load() > 0.5f;
    return e;
}

bool LumenEQProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    if (out != mono && out != stereo) return false;
    if (in != out) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto s = layouts.getChannelSet (true, 1);
        if (! s.isDisabled() && s != mono && s != stereo) return false;
    }
    return true;
}

void LumenEQProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare (sampleRate);
    outSm.reset (sampleRate, 0.03);
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    monoTmp.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    scTmp.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    capT.assign ((size_t) (sampleRate * 4.0), 0.0f);
    capS.assign ((size_t) (sampleRate * 4.0), 0.0f);
    capPos = 0;
}

void LumenEQProcessor::push (juce::AbstractFifo& f, std::vector<float>& b, const float* d, int n)
{
    if (f.getFreeSpace() < n) return;   // UI not reading: drop
    int s1, n1, s2, n2;
    f.prepareToWrite (n, s1, n1, s2, n2);
    if (n1 > 0) std::copy (d, d + n1, b.begin() + s1);
    if (n2 > 0) std::copy (d + n1, d + n1 + n2, b.begin() + s2);
    f.finishedWrite (n1 + n2);
}

void LumenEQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, false, 0);
    const int numCh = main.getNumChannels(), n = buffer.getNumSamples();
    if (n == 0 || numCh == 0) return;
    if ((int) monoTmp.size() < n) { monoTmp.resize ((size_t) n); scTmp.resize ((size_t) n); }   // rare: host exceeded block size

    const float* scL = nullptr; const float* scR = nullptr;
    if (getBusCount (true) > 1)
        if (auto* b = getBus (true, 1); b != nullptr && b->isEnabled())
        {
            auto s = getBusBuffer (buffer, true, 1);
            if (s.getNumChannels() > 0) { scL = s.getReadPointer (0); scR = s.getNumChannels() > 1 ? s.getReadPointer (1) : scL; }
        }
    scConnected.store (scL != nullptr);

    lumen::EqBand bands[6];
    for (int b = 0; b < 6; ++b) bands[b] = bandFromParams (b);
    engine.setBands (bands);
    engine.setTimes (pAtk->load(), pRel->load());
    outSm.setTargetValue (juce::Decibels::decibelsToGain (pOut->load()));

    if (captureRequested.exchange (false)) { capPos = 0; capProgress.store (0.0f); capReady.store (false); capturing.store (true); }
    const bool cap = capturing.load();

    float* L = main.getWritePointer (0);
    float* R = numCh > 1 ? main.getWritePointer (1) : nullptr;
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R != nullptr ? R[i] : L[i];
        const float m = 0.5f * (l + r);
        const float k = scL != nullptr ? 0.5f * (scL[i] + scR[i]) : 0.0f;
        if (cap && capPos < (int) capT.size()) { capT[(size_t) capPos] = m; capS[(size_t) capPos] = k; ++capPos; }

        engine.process (l, r, m, k);
        const float g = outSm.getNextValue();
        l *= g; r *= g;
        L[i] = R != nullptr ? l : 0.5f * (l + r);
        if (R != nullptr) R[i] = r;
        monoTmp[(size_t) i] = 0.5f * (l + r);
        scTmp[(size_t) i] = k;
    }

    if (cap)
    {
        capProgress.store ((float) capPos / (float) capT.size());
        if (capPos >= (int) capT.size()) { capturing.store (false); capReady.store (true, std::memory_order_release); }
    }

    push (fifoTrack, bufTrack, monoTmp.data(), n);
    if (scL != nullptr) push (fifoSc, bufSc, scTmp.data(), n);
    for (int b = 0; b < 6; ++b) dynGain[b].store (engine.dynGain (b));
}

bool LumenEQProcessor::takeCapture (std::vector<float>& track, std::vector<float>& sc)
{
    if (! capReady.exchange (false, std::memory_order_acquire)) return false;
    track = capT; sc = capS;
    return true;
}

juce::AudioProcessorEditor* LumenEQProcessor::createEditor() { return new LumenEQEditor (*this); }

void LumenEQProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void LumenEQProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenEQProcessor(); }
