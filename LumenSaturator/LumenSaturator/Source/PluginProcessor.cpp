#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenSaturatorProcessor::LumenSaturatorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenSaturator", createParameterLayout())
{
    for (int b = 0; b < 3; ++b)
    {
        pMode[b] = p (IDs::mode (b)); pDrive[b] = p (IDs::drive (b)); pMix[b] = p (IDs::mix (b));
        pLevel[b] = p (IDs::level (b)); pSolo[b] = p (IDs::solo (b));
    }
    pXLow = p (IDs::xLow); pXHigh = p (IDs::xHigh); pOut = p (IDs::output); pAuto = p (IDs::autoGain); pOs = p (IDs::oversample);
}

bool LumenSaturatorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void LumenSaturatorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, samplesPerBlock);
    for (int k = 1; k <= 4; ++k)
    {
        os[k] = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) k, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os[k]->initProcessing ((size_t) maxBlock);
    }
    analyzer.prepare (sampleRate);
    monoScratch.assign ((size_t) maxBlock, 0.0f);
    osIndex = -1;
    setOversampling (juce::jlimit (0, 4, (int) pOs->load()));
}

void LumenSaturatorProcessor::setOversampling (int index)
{
    osIndex = index;
    if (osIndex > 0) os[osIndex]->reset();
    engine.prepare (sr * (double) (1 << osIndex));
    lastXLow = lastXHigh = -1.0f;
    setLatencySamples (osIndex > 0 ? (int) std::round (os[osIndex]->getLatencyInSamples()) : 0);
}

void LumenSaturatorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, getTotalNumOutputChannels());
    const int n = buffer.getNumSamples();
    if (n == 0 || numCh == 0) return;

    if (n > maxBlock)
    {
        juce::MidiBuffer dummy;
        for (int start = 0; start < n; start += maxBlock)
        {
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, juce::jmin (maxBlock, n - start));
            processBlock (sub, dummy);
        }
        return;
    }

    const int wantOs = juce::jlimit (0, 4, (int) pOs->load());
    if (wantOs != osIndex) setOversampling (wantOs);

    if (std::abs (pXLow->load() - lastXLow) > 0.01f || std::abs (pXHigh->load() - lastXHigh) > 0.01f)
    {
        lastXLow = pXLow->load(); lastXHigh = pXHigh->load();
        engine.setCrossovers (lastXLow, lastXHigh);
    }

    if (numCh == 1 && buffer.getNumChannels() > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    if (R == nullptr) { std::copy (L, L + n, monoScratch.begin()); R = monoScratch.data(); }   // mono host

    analyzer.process (L, R, n);
    float inPk = 0.0f;
    for (int i = 0; i < n; ++i) inPk = std::max (inPk, std::max (std::abs (L[i]), std::abs (R[i])));

    lumen::SatEngine::Params sp;
    for (int b = 0; b < 3; ++b)
    {
        sp.band[b].mode = juce::jlimit (0, 4, (int) pMode[b]->load());
        sp.band[b].driveDb = pDrive[b]->load();
        sp.band[b].mix = pMix[b]->load() * 0.01f;
        sp.band[b].levelDb = pLevel[b]->load();
        sp.band[b].solo = pSolo[b]->load() > 0.5f;
    }
    sp.outDb = pOut->load();
    sp.autoGain = pAuto->load() > 0.5f;
    engine.beginBlock (sp);

    if (osIndex > 0)
    {
        float* ptrs[2] = { L, R };
        juce::dsp::AudioBlock<float> block (ptrs, 2, (size_t) n);
        auto up = os[osIndex]->processSamplesUp (block);
        engine.process (up.getChannelPointer (0), up.getChannelPointer (1), (int) up.getNumSamples());
        os[osIndex]->processSamplesDown (block);
    }
    else
    {
        engine.process (L, R, n);
    }

    float outPk = 0.0f;
    for (int i = 0; i < n; ++i) outPk = std::max (outPk, std::max (std::abs (L[i]), std::abs (R[i])));
    inLevel.store (inPk);
    outLevel.store (outPk);
}

juce::AudioProcessorEditor* LumenSaturatorProcessor::createEditor() { return new LumenSaturatorEditor (*this); }

void LumenSaturatorProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void LumenSaturatorProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenSaturatorProcessor(); }
