#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenAudioProcessor::LumenAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenReverb", createParameterLayout())
{
    pSize = p (IDs::size);       pDecay = p (IDs::decay);   pPre = p (IDs::preDelay);
    pDiff = p (IDs::diffusion);  pEarly = p (IDs::early);   pDamp = p (IDs::damping);
    pLow = p (IDs::lowCut);      pHigh = p (IDs::highCut);  pWidth = p (IDs::width);
    pModD = p (IDs::modDepth);   pModR = p (IDs::modRate);  pDuck = p (IDs::duck);
    pAdapt = p (IDs::adapt);     pMix = p (IDs::mix);       pOut = p (IDs::output);
    pFreeze = p (IDs::freeze);   pSync = p (IDs::sync);
}

bool LumenAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();

    if (out != mono && out != stereo) return false;
    if (in  != mono && in  != stereo) return false;
    return ! (in == stereo && out == mono);
}

void LumenAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.prepare (sampleRate);
    analyzer.prepare (sampleRate);

    drySm.reset (sampleRate, 0.05);
    wetSm.reset (sampleRate, 0.05);
    outSm.reset (sampleRate, 0.05);
    const float mix = pMix->load() * 0.01f;
    drySm.setCurrentAndTargetValue (juce::jmin (1.0f, 2.0f * (1.0f - mix)));
    wetSm.setCurrentAndTargetValue (juce::jmin (1.0f, 2.0f * mix));
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOut->load()));

    duckAtk    = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
    duckRel    = 1.0f - std::exp (-1.0f / (0.250f * (float) sampleRate));
    duckSmooth = 1.0f - std::exp (-1.0f / (0.030f * (float) sampleRate));
    duckEnv = 0.0f; duckGain = 1.0f;
}

void LumenAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (n == 0 || numOut == 0)
        return;

    if (numIn == 0)
        buffer.clear();
    else if (numIn == 1 && numOut > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm.store (*bpm);

    float* L = buffer.getWritePointer (0);
    float* R = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    // ---- Smart analysis ----------------------------------------------------
    analyzer.process (L, R, n);
    const auto f = analyzer.getLive();

    if (f.levelDb > -55.0f) // only follow the material while it is actually playing
    {
        const float k = 1.0f - std::exp (-(float) n / (0.5f * (float) sr));
        smTrans  += (juce::jlimit (0.0f, 1.0f, f.onsetRate / 6.0f) - smTrans) * k;
        smBright += (juce::jlimit (0.0f, 1.0f, (f.highRatio - 0.05f) / 0.2f) - smBright) * k;
        smLow    += (juce::jlimit (0.0f, 1.0f, (f.lowRatio - 0.2f) / 0.4f) - smLow) * k;
    }

    // ---- Parameters + adaptation ------------------------------------------
    const float adapt = pAdapt->load() * 0.01f;

    float decay = pDecay->load();
    const int sync = juce::jlimit (0, 5, (int) pSync->load());
    const double bpm = hostBpm.load();
    if (sync > 0 && bpm > 20.0)
        decay = (float) juce::jlimit (0.2, 20.0, kSyncBeats[sync] * 60.0 / bpm);

    decay *= 1.0f - 0.45f * adapt * smTrans;                        // busy material -> shorter tail
    const float damping = juce::jmax (1000.0f, pDamp->load() * (1.0f - 0.45f * adapt * smBright)); // bright -> darker
    const float lowCut  = juce::jmin (1000.0f, pLow->load() * (1.0f + 1.5f * adapt * smLow));     // bassy -> cleaner
    const float duck    = juce::jlimit (0.0f, 1.0f, pDuck->load() * 0.01f + 0.35f * adapt * smTrans);

    lumen::ReverbEngine::Params rp;
    rp.size       = pSize->load() * 0.01f;
    rp.decay      = decay;
    rp.preDelayMs = pPre->load();
    rp.dampHz     = damping;
    rp.lowCutHz   = lowCut;
    rp.highCutHz  = pHigh->load();
    rp.early      = pEarly->load() * 0.01f;
    rp.diffusion  = pDiff->load() * 0.01f;
    rp.width      = pWidth->load() * 0.01f;
    rp.modDepth   = pModD->load() * 0.01f;
    rp.modRate    = pModR->load();
    rp.freeze     = pFreeze->load() > 0.5f;
    engine.setParams (rp);
    engine.beginBlock (n);

    effDecay.store (decay); effPreDelay.store (rp.preDelayMs); effSize.store (rp.size);
    effEarly.store (rp.early); effDamping.store (damping); effLowCut.store (lowCut); effDuck.store (duck);

    const float mix = pMix->load() * 0.01f;
    drySm.setTargetValue (juce::jmin (1.0f, 2.0f * (1.0f - mix)));
    wetSm.setTargetValue (juce::jmin (1.0f, 2.0f * mix));
    outSm.setTargetValue (juce::Decibels::decibelsToGain (pOut->load()));

    // ---- Audio --------------------------------------------------------------
    float peakIn = 0.0f, peakOut = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float dl = L[i];
        const float dr = R != nullptr ? R[i] : dl;

        float wl, wr;
        engine.processSample (dl, dr, wl, wr);

        const float lvl = juce::jmax (std::abs (dl), std::abs (dr));
        duckEnv += (lvl - duckEnv) * (lvl > duckEnv ? duckAtk : duckRel);
        const float targetGain = 1.0f - 0.9f * duck * juce::jmin (1.0f, duckEnv * 4.0f);
        duckGain += (targetGain - duckGain) * duckSmooth;

        const float dryG = drySm.getNextValue();
        const float wetG = wetSm.getNextValue() * duckGain;
        const float outG = outSm.getNextValue();

        peakIn = juce::jmax (peakIn, lvl);
        if (R != nullptr)
        {
            L[i] = (dl * dryG + wl * wetG) * outG;
            R[i] = (dr * dryG + wr * wetG) * outG;
            peakOut = juce::jmax (peakOut, juce::jmax (std::abs (L[i]), std::abs (R[i])));
        }
        else
        {
            L[i] = (dl * dryG + 0.5f * (wl + wr) * wetG) * outG;
            peakOut = juce::jmax (peakOut, std::abs (L[i]));
        }
    }

    inLevel.store (peakIn);
    outLevel.store (peakOut);
    duckReduction.store (1.0f - duckGain);
}

juce::AudioProcessorEditor* LumenAudioProcessor::createEditor()
{
    return new LumenAudioProcessorEditor (*this);
}

void LumenAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LumenAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LumenAudioProcessor();
}
