#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenDelayProcessor::LumenDelayProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "LumenDelay", createParameterLayout())
{
    pSync = p (IDs::sync);       pTime = p (IDs::time);       pDiv = p (IDs::division);
    pOffset = p (IDs::offset);   pMode = p (IDs::mode);       pFb = p (IDs::feedback);
    pDiff = p (IDs::diffusion);  pWidth = p (IDs::width);     pLow = p (IDs::lowCut);
    pHigh = p (IDs::highCut);    pDrive = p (IDs::drive);     pWow = p (IDs::wow);
    pModR = p (IDs::modRate);    pDuck = p (IDs::duck);       pAdapt = p (IDs::adapt);
    pMix = p (IDs::mix);         pOut = p (IDs::output);      pFreeze = p (IDs::freeze);
    pDuckSrc = p (IDs::duckSrc);
}

bool LumenDelayProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();

    if (out != mono && out != stereo) return false;
    if (in  != mono && in  != stereo) return false;
    if (in == stereo && out == mono) return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != mono && sc != stereo)
            return false;
    }
    return true;
}

void LumenDelayProcessor::prepareToPlay (double sampleRate, int)
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

void LumenDelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, false, 0);
    const int numIn  = getMainBusNumInputChannels();
    const int numOut = main.getNumChannels();
    const int n = buffer.getNumSamples();
    if (n == 0 || numOut == 0)
        return;

    if (numIn == 0)
        main.clear();
    else if (numIn == 1 && numOut > 1)
        main.copyFrom (1, 0, main, 0, 0, n);

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
    const bool useSc = pDuckSrc->load() > 0.5f;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm.store (*bpm);

    float* L = main.getWritePointer (0);
    float* R = numOut > 1 ? main.getWritePointer (1) : nullptr;

    // ---- Smart analysis ----------------------------------------------------
    analyzer.process (L, R, n);
    const auto f = analyzer.getLive();
    if (f.levelDb > -55.0f)
    {
        const float k = 1.0f - std::exp (-(float) n / (0.5f * (float) sr));
        smTrans  += (juce::jlimit (0.0f, 1.0f, f.onsetRate / 6.0f) - smTrans) * k;
        smBright += (juce::jlimit (0.0f, 1.0f, (f.highRatio - 0.05f) / 0.2f) - smBright) * k;
        smLow    += (juce::jlimit (0.0f, 1.0f, (f.lowRatio - 0.2f) / 0.4f) - smLow) * k;
    }

    // ---- Time ---------------------------------------------------------------
    float timeL;
    if (pSync->load() > 0.5f)
    {
        const double bpm = hostBpm.load() > 20.0 ? hostBpm.load() : 120.0;
        const int div = juce::jlimit (0, kNumDivisions - 1, (int) pDiv->load());
        timeL = (float) (kDivisionBeats[div] * 60.0 / bpm);
    }
    else
    {
        timeL = pTime->load() * 0.001f;
    }
    timeL = juce::jlimit (0.001f, 4.0f, timeL);
    const float timeR = juce::jlimit (0.001f, lumen::DelayEngine::kMaxSeconds, timeL * (1.0f + pOffset->load() * 0.01f));

    // ---- Adaptation -----------------------------------------------------------
    const float adapt    = pAdapt->load() * 0.01f;
    const float feedback = pFb->load() * 0.01f * (1.0f - 0.35f * adapt * smTrans);
    const float highCut  = juce::jmax (1000.0f, pHigh->load() * (1.0f - 0.4f * adapt * smBright));
    const float lowCut   = juce::jmin (2000.0f, pLow->load() * (1.0f + 1.5f * adapt * smLow));
    const float duck     = juce::jlimit (0.0f, 1.0f, pDuck->load() * 0.01f + 0.4f * adapt * smTrans);

    lumen::DelayEngine::Params dp;
    dp.timeL     = timeL;
    dp.timeR     = timeR;
    dp.feedback  = feedback;
    dp.mode      = juce::jlimit (0, 2, (int) pMode->load());
    dp.lowCutHz  = lowCut;
    dp.highCutHz = highCut;
    dp.drive     = pDrive->load() * 0.01f;
    dp.wow       = pWow->load() * 0.01f;
    dp.modRate   = pModR->load();
    dp.diffusion = pDiff->load() * 0.01f;
    dp.width     = pWidth->load() * 0.01f;
    dp.freeze    = pFreeze->load() > 0.5f;
    engine.setParams (dp);
    engine.beginBlock();

    effTimeL.store (timeL); effTimeR.store (timeR); effFeedback.store (feedback);
    effLowCut.store (lowCut); effHighCut.store (highCut); effDuck.store (duck); effMode.store (dp.mode);

    const float mix = pMix->load() * 0.01f;
    drySm.setTargetValue (juce::jmin (1.0f, 2.0f * (1.0f - mix)));
    wetSm.setTargetValue (juce::jmin (1.0f, 2.0f * mix));
    outSm.setTargetValue (juce::Decibels::decibelsToGain (pOut->load()));

    // ---- Audio --------------------------------------------------------------
    float peakOut = 0.0f, peakSc = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float dl = L[i];
        const float dr = R != nullptr ? R[i] : dl;

        float wl, wr;
        engine.processSample (dl, dr, wl, wr);

        float lvl;
        if (useSc)
        {
            lvl = scL != nullptr ? juce::jmax (std::abs (scL[i]), std::abs (scR[i])) : 0.0f;
            peakSc = juce::jmax (peakSc, lvl);
        }
        else
        {
            lvl = juce::jmax (std::abs (dl), std::abs (dr));
        }
        duckEnv += (lvl - duckEnv) * (lvl > duckEnv ? duckAtk : duckRel);
        const float targetGain = 1.0f - 0.9f * duck * juce::jmin (1.0f, duckEnv * 4.0f);
        duckGain += (targetGain - duckGain) * duckSmooth;

        const float dryG = drySm.getNextValue();
        const float wetG = wetSm.getNextValue() * duckGain;
        const float outG = outSm.getNextValue();

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

    outLevel.store (peakOut);
    scLevel.store (peakSc);
    scConnected.store (scL != nullptr);
    duckReduction.store (1.0f - duckGain);
}

juce::AudioProcessorEditor* LumenDelayProcessor::createEditor()
{
    return new LumenDelayEditor (*this);
}

void LumenDelayProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LumenDelayProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LumenDelayProcessor();
}
