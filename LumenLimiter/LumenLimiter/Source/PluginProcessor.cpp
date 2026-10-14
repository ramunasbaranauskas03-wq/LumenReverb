#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenLimiterProcessor::LumenLimiterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenLimiter", createParameterLayout())
{
    pGain = p (IDs::gain);   pCeil = p (IDs::ceiling);   pRel = p (IDs::release);  pAutoRel = p (IDs::autoRel);
    pLa = p (IDs::lookahead); pLink = p (IDs::link);     pStyle = p (IDs::style);  pClipOn = p (IDs::clipOn);
    pClipLvl = p (IDs::clipLevel); pClipShape = p (IDs::clipShape); pDither = p (IDs::dither);
    pDelta = p (IDs::delta); pMatch = p (IDs::gainMatch); pOs = p (IDs::oversample);
    for (auto& h : histOut) h.store (-100.0f);
    for (auto& h : histGr)  h.store (0.0f);
}

float LumenLimiterProcessor::targetLufs() const
{
    const int t = (int) apvts.getRawParameterValue (IDs::target)->load();
    const auto& all = targets();
    if (t >= (int) all.size() - 1) return apvts.getRawParameterValue (IDs::customLufs)->load();
    return all[(size_t) t].lufs;
}

float LumenLimiterProcessor::targetTp() const
{
    const int t = (int) apvts.getRawParameterValue (IDs::target)->load();
    const auto& all = targets();
    if (t >= (int) all.size() - 1) return apvts.getRawParameterValue (IDs::ceiling)->load();
    return all[(size_t) t].tp;
}

bool LumenLimiterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void LumenLimiterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, samplesPerBlock);

    // Pre-build every oversampler (2x..16x) so switching never allocates on the audio thread
    for (int k = 1; k <= 4; ++k)
    {
        os[k] = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) k, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os[k]->initProcessing ((size_t) maxBlock);
    }
    osIndex = juce::jlimit (0, 4, (int) pOs->load());
    osLatency = osIndex > 0 ? (int) std::round (os[osIndex]->getLatencyInSamples()) : 0;
    const int maxOsLatency = (int) std::round (os[4]->getLatencyInSamples()) + 8;

    const int maxW = (int) std::ceil (0.005 * sampleRate) + 4;
    for (int c = 0; c < 2; ++c)
    {
        gpath[c].prepare (maxW);
        det[c].reset(); outTp[c].reset();
        audioDelay[c].prepare (maxW + 32);
        dryDelay[c].prepare (maxW + 32 + maxOsLatency);
    }
    dryBuf.setSize (2, maxBlock);
    loud.prepare (sampleRate);
    gainSm.reset (sampleRate, 0.05);
    gainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pGain->load()));
    histInterval = juce::jmax (64, (int) (sampleRate * 6.0 / kHistory));
    tpMaxLin = 0.0f;
    curW = -1;
    configure ((int) std::round (pLa->load() * 0.001 * sampleRate));
}

void LumenLimiterProcessor::configure (int W)
{
    W = juce::jmax (1, W);
    curW = W;
    for (int c = 0; c < 2; ++c)
    {
        gpath[c].setWindow (W);
        audioDelay[c].setDelay (W + lumen::TruePeak::kDelay - 1);
        dryDelay[c].setDelay (osLatency + W + lumen::TruePeak::kDelay - 1);
        det[c].reset();
    }
    setLatencySamples (osLatency + W + lumen::TruePeak::kDelay - 1);
}

void LumenLimiterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, getTotalNumOutputChannels());
    const int n = buffer.getNumSamples();
    if (n == 0 || numCh == 0) return;

    if (n > maxBlock)   // host sent a bigger block than announced: process in chunks
    {
        juce::MidiBuffer dummy;
        for (int start = 0; start < n; start += maxBlock)
        {
            const int len = juce::jmin (maxBlock, n - start);
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, len);
            processBlock (sub, dummy);
        }
        return;
    }

    if (resetMeters.exchange (false)) { loud.reset(); tpMaxLin = 0.0f; }

    const int W = (int) std::round (pLa->load() * 0.001 * sr);
    const int wantOs = juce::jlimit (0, 4, (int) pOs->load());
    if (wantOs != osIndex)
    {
        osIndex = wantOs;
        if (osIndex > 0) os[osIndex]->reset();
        osLatency = osIndex > 0 ? (int) std::round (os[osIndex]->getLatencyInSamples()) : 0;
        curW = -1;
    }
    if (W != curW) configure (W);

    // ---- Release settings per style ----
    const int style = (int) pStyle->load();
    const float relMul = style == 0 ? 1.6f : style == 2 ? 0.45f : 1.0f;
    const float relMs  = juce::jmax (1.0f, pRel->load() * relMul);
    const bool  dual   = pAutoRel->load() > 0.5f && style != 2;
    const float rFast  = std::exp (-1.0f / ((dual ? relMs * 0.4f : relMs) * 0.001f * (float) sr));
    const float rSlow  = std::exp (-1.0f / (relMs * 3.0f * 0.001f * (float) sr));
    const float aSlow  = std::exp (-1.0f / (0.35f * (float) sr));
    for (auto& g : gpath) g.setRelease (rFast, rSlow, aSlow, dual);

    const float ceilDb  = pCeil->load();
    const float ceil    = juce::Decibels::decibelsToGain (ceilDb) * 0.9966f;   // 0.03 dB safety for true peak
    const float hardCeil = juce::Decibels::decibelsToGain (ceilDb);
    const float link    = pLink->load() * 0.01f;
    const bool  clipOn  = pClipOn->load() > 0.5f;
    const float clipT   = juce::Decibels::decibelsToGain (ceilDb + pClipLvl->load());
    const float clipH   = pClipShape->load() * 0.01f;
    const bool  delta   = pDelta->load() > 0.5f;
    const bool  match   = pMatch->load() > 0.5f;
    const int   dither  = (int) pDither->load();
    const float gainDb  = pGain->load();
    gainSm.setTargetValue (juce::Decibels::decibelsToGain (gainDb));
    const float matchG  = match ? juce::Decibels::decibelsToGain (-gainDb) : 1.0f;

    if (numCh == 1) buffer.copyFrom (1 % buffer.getNumChannels(), 0, buffer, 0, 0, n);

    float* ch[2] = { buffer.getWritePointer (0), buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0) };

    // ---- Input gain + dry copy (for delta) ----
    float inPk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = gainSm.getNextValue();
        for (int c = 0; c < 2; ++c)
        {
            inPk = juce::jmax (inPk, std::abs (ch[c][i]));
            ch[c][i] *= g;
            dryBuf.setSample (c, i, ch[c][i]);
        }
    }

    // ---- Clipper at 1x..16x (oversampler always runs so latency stays constant) ----
    if (osIndex > 0)
    {
        float* ptrs[2] = { ch[0], ch[1] };
        juce::dsp::AudioBlock<float> block (ptrs, 2, (size_t) n);
        auto up = os[osIndex]->processSamplesUp (block);
        if (clipOn)
            for (size_t c = 0; c < up.getNumChannels(); ++c)
            {
                auto* d = up.getChannelPointer (c);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                    d[i] = clipT * lumen::clipShape (d[i] / clipT, clipH);
            }
        os[osIndex]->processSamplesDown (block);
    }
    else if (clipOn)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                ch[c][i] = clipT * lumen::clipShape (ch[c][i] / clipT, clipH);
    }

    // ---- True-peak lookahead limiter ----
    float maxGr = 0.0f, outPk = 0.0f;
    const float lsb = dither == 1 ? 1.0f / 8388608.0f : dither == 2 ? 1.0f / 32768.0f : 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float req[2];
        for (int c = 0; c < 2; ++c)
        {
            const float pk = det[c].push (ch[c][i]);
            req[c] = pk > ceil ? ceil / pk : 1.0f;
        }
        const float lmin = juce::jmin (req[0], req[1]);
        float out[2], grNow = 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            const float r = req[c] + (lmin - req[c]) * link;
            const float g = gpath[c].process (r);
            float y = audioDelay[c].process (ch[c][i]) * g;
            y = juce::jlimit (-hardCeil, hardCeil, y);          // sample-peak safety
            grNow = juce::jmax (grNow, -juce::Decibels::gainToDecibels (g, -60.0f));

            const float dry = dryDelay[c].process (dryBuf.getSample (c, i));
            if (delta) y = dry - y;
            y *= matchG;
            if (lsb > 0.0f) y = std::round ((y + (uni (rng) + uni (rng)) * lsb) / lsb) * lsb;
            out[c] = y;
        }
        for (int c = 0; c < 2; ++c)
        {
            ch[c][i] = out[c];
            tpMaxLin = juce::jmax (tpMaxLin, outTp[c].push (out[c]));
            outPk = juce::jmax (outPk, std::abs (out[c]));
        }
        loud.process (out[0], out[1]);

        histOutAcc = juce::jmax (histOutAcc, juce::jmax (std::abs (out[0]), std::abs (out[1])));
        maxGr = juce::jmax (maxGr, grNow);
        histGrAcc  = juce::jmax (histGrAcc, grNow);
        if (++histCounter >= histInterval)
        {
            const int w = histWrite.load (std::memory_order_relaxed);
            histOut[(size_t) w].store (juce::Decibels::gainToDecibels (histOutAcc, -100.0f), std::memory_order_relaxed);
            histGr[(size_t) w].store (histGrAcc, std::memory_order_relaxed);
            histWrite.store ((w + 1) % kHistory, std::memory_order_release);
            histCounter = 0; histOutAcc = 0.0f; histGrAcc = 0.0f;
        }
    }

    momentary.store (loud.momentary);
    shortTerm.store (loud.shortTerm);
    integrated.store (loud.integrated);
    tpMax.store (juce::Decibels::gainToDecibels (tpMaxLin, -100.0f));
    grDb.store (maxGr);
    inLevel.store (inPk);
    outLevel.store (outPk);
}

juce::AudioProcessorEditor* LumenLimiterProcessor::createEditor() { return new LumenLimiterEditor (*this); }

void LumenLimiterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void LumenLimiterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenLimiterProcessor(); }
