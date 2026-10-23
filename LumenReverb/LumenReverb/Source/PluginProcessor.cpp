#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenAudioProcessor::LumenAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenReverb", createLayout())
{
}

bool LumenAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void LumenAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate; maxBlock = juce::jlimit (32, 16384, samplesPerBlock);
    engine.prepare (fs);
    analyzer.prepare (fs);
    wet.setSize (2, maxBlock); dry.setSize (2, maxBlock);
    for (auto& t : wetTree) { t.set (fs, 250, 1500, 6000); t.reset(); }
    dryTree.set (fs, 250, 1500, 6000); dryTree.reset();
    for (int b = 0; b < 4; ++b) { envDry[b] = envWet[b] = 0; bandG[b] = 1; }
    duckEnv = 0; duckG = 1;
    float mix = 0; (void) readParams (0, mix);       // the engine gets its first parameters (with the host tempo) in processBlock
    drySm = std::cos (mix * juce::MathConstants<float>::halfPi); wetSm = std::sin (mix * juce::MathConstants<float>::halfPi);
    outSm = juce::Decibels::decibelsToGain (P (IDs::out));
    setLatencySamples (0);
}

lumen::rv2::Params LumenAudioProcessor::readParams (double bpm, float& mixOut)
{
    lumen::rv2::Params p;
    const float adapt = P (IDs::adapt);
    p.algo = (int) P (IDs::algo);
    p.decay = P (IDs::decay) * (1.0f - 0.45f * adapt * smTrans);
    p.size = P (IDs::size);
    p.preDelayMs = P (IDs::preDelay);
    const int sync = (int) P (IDs::preSync);
    if (sync > 0 && bpm > 30.0) p.preDelayMs = (float) juce::jlimit (0.0, 500.0, preSyncBeats (sync) * 60000.0 / bpm);
    p.diffusion = P (IDs::diffusion); p.early = P (IDs::early);
    p.modDepth = P (IDs::modDepth); p.modRate = P (IDs::modRate);
    p.bassMult = P (IDs::bassMult); p.trebleMult = P (IDs::trebleMult) * (1.0f - 0.4f * adapt * smBright);
    p.xLow = P (IDs::xLow); p.xHigh = P (IDs::xHigh);
    p.lowCut = juce::jmin (1000.0f, P (IDs::lowCut) * (1.0f + 1.5f * adapt * smLow)); p.highCut = P (IDs::highCut);
    p.width = P (IDs::width); p.bassMono = P (IDs::bassMono) < 1.0f ? 0.0f : P (IDs::bassMono);
    p.shimmer = P (IDs::shimmer); p.shimmerSemis = pitchSemis ((int) P (IDs::shimmerPitch));
    p.freeze = P (IDs::freeze) > 0.5f;
    mixOut = P (IDs::mix);
    effDecay.store (p.decay); effPre.store (p.preDelayMs);
    return p;
}

void LumenAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    double bpm = 0;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) bpm = *b;
    hostBpm.store (bpm);

    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);
    analyzer.process (L, R, n);
    const auto f = analyzer.getLive();
    if (f.levelDb > -55.0f)
    {
        const float k = 1.0f - std::exp (-(float) n / (0.5f * (float) fs));
        smTrans += (juce::jlimit (0.0f, 1.0f, f.onsetRate / 6.0f) - smTrans) * k;
        smBright += (juce::jlimit (0.0f, 1.0f, (f.highRatio - 0.05f) / 0.2f) - smBright) * k;
        smLow += (juce::jlimit (0.0f, 1.0f, (f.lowRatio - 0.3f) / 0.4f) - smLow) * k;
    }

    float mix = 0;
    engine.setParams (readParams (bpm, mix));
    const float adapt = P (IDs::adapt);
    const float duckAmt = juce::jlimit (0.0f, 1.0f, P (IDs::duck) + 0.35f * adapt * smTrans);
    const float clarity = P (IDs::clarity);
    const float dryT = std::cos (mix * juce::MathConstants<float>::halfPi), wetT = std::sin (mix * juce::MathConstants<float>::halfPi);
    const float outT = juce::Decibels::decibelsToGain (P (IDs::out));
    const float sm = 1.0f - std::exp (-1.0f / (0.03f * (float) fs));
    const float dA = 1.0f - std::exp (-1.0f / (0.005f * (float) fs)), dR = 1.0f - std::exp (-1.0f / (0.12f * (float) fs));
    const float eA = 1.0f - std::exp (-1.0f / (0.005f * (float) fs)), eR = 1.0f - std::exp (-1.0f / (0.05f * (float) fs));     // dry: fast
    const float wA = 1.0f - std::exp (-1.0f / (0.01f * (float) fs)), wR = 1.0f - std::exp (-1.0f / (0.3f * (float) fs));      // wet: smooth
    const float gS = 1.0f - std::exp (-1.0f / (0.04f * (float) fs));
    float ip = 0, op = 0, wp = 0, minDuck = 1, minBand[4] { 1, 1, 1, 1 };

    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = std::min (maxBlock, n - start);
        float* l = L + start; float* r = R + start;
        float* wl = wet.getWritePointer (0); float* wr = wet.getWritePointer (1);
        engine.process (l, r, wl, wr, len);
        for (int i = 0; i < len; ++i)
        {
            const float xl = l[i], xr = r[i];
            ip = std::max (ip, std::max (std::abs (xl), std::abs (xr)));
            // CLARITY: per band, the reverb steps back where the dry signal is busy
            const float dm = 0.5f * (xl + xr);
            float db[4], bl[4], br[4];
            dryTree.process (dm, db); wetTree[0].process (wl[i], bl); wetTree[1].process (wr[i], br);
            float yl = 0, yr = 0;
            for (int b = 0; b < 4; ++b)
            {
                const float ed = std::abs (db[b]), ew = 0.5f * (std::abs (bl[b]) + std::abs (br[b]));
                envDry[b] += (ed - envDry[b]) * (ed > envDry[b] ? eA : eR);
                envWet[b] += (ew - envWet[b]) * (ew > envWet[b] ? wA : wR);
                float want = 1.0f;
                if (clarity > 0.001f)
                {
                    const float ratio = envDry[b] / (envWet[b] + 1e-5f);
                    want = juce::jmax (0.125f, 1.0f / (1.0f + clarity * 1.6f * std::sqrt (ratio)));
                }
                bandG[b] += (want - bandG[b]) * gS;
                minBand[b] = std::min (minBand[b], bandG[b]);
                yl += bl[b] * bandG[b]; yr += br[b] * bandG[b];
            }
            // DUCK: the whole reverb dips while the dry signal plays
            const float a = std::max (std::abs (xl), std::abs (xr));
            duckEnv += (a - duckEnv) * (a > duckEnv ? dA : dR);
            const float envDb = juce::Decibels::gainToDecibels (duckEnv, -100.0f);
            const float grDb = -duckAmt * 20.0f * juce::jlimit (0.0f, 1.0f, (envDb + 36.0f) / 24.0f);
            duckG += (juce::Decibels::decibelsToGain (grDb) - duckG) * sm;
            minDuck = std::min (minDuck, duckG);
            yl *= duckG; yr *= duckG;
            wp = std::max (wp, std::max (std::abs (yl), std::abs (yr)));
            histAcc += 0.5 * ((double) yl * yl + (double) yr * yr);
            if (++histCount >= (int) (fs * 0.01))
            {
                const int hp = histPos.load (std::memory_order_relaxed);
                hist[hp] = (float) (10.0 * std::log10 (histAcc / histCount + 1e-12));
                histPos.store ((hp + 1) % kHist, std::memory_order_release);
                histAcc = 0; histCount = 0;
            }
            drySm += (dryT - drySm) * sm; wetSm += (wetT - wetSm) * sm; outSm += (outT - outSm) * sm;
            l[i] = (xl * drySm + yl * wetSm) * outSm; r[i] = (xr * drySm + yr * wetSm) * outSm;
            op = std::max (op, std::max (std::abs (l[i]), std::abs (r[i])));
        }
    }
    inLevel.store (ip); outLevel.store (op); wetLevel.store (wp);
    duckDb.store (juce::Decibels::gainToDecibels (minDuck, -60.0f));
    for (int b = 0; b < 4; ++b) clarityDb[b].store (juce::Decibels::gainToDecibels (minBand[b], -60.0f));
}

// ================================================================================================ Smart Assist
void LumenAudioProcessor::applySuggestion (const lumen::SmartAssist::Suggestion& s)
{
    getStateInformation (undoState);
    setParam (IDs::algo, (float) s.algo); setParam (IDs::decay, s.decay); setParam (IDs::size, s.size); setParam (IDs::preDelay, s.preDelay);
    setParam (IDs::preSync, 0); setParam (IDs::diffusion, s.diffusion); setParam (IDs::early, s.early);
    setParam (IDs::bassMult, s.bassMult); setParam (IDs::trebleMult, s.trebleMult); setParam (IDs::lowCut, s.lowCut); setParam (IDs::highCut, s.highCut);
    setParam (IDs::width, s.width); setParam (IDs::modDepth, s.modDepth); setParam (IDs::duck, s.duck); setParam (IDs::clarity, s.clarity);
    if (P (IDs::mix) < 0.95f) setParam (IDs::mix, s.mix);              // used as a send: keep 100 % wet
}
void LumenAudioProcessor::undoLearn()
{
    if (undoState.getSize() == 0) return;
    juce::MemoryBlock m (undoState); undoState.reset();
    setStateInformation (m.getData(), (int) m.getSize());
}

// ================================================================================================ state / presets
void LumenAudioProcessor::setParam (const juce::String& id, float v)
{
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id))) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture(); }
}

void LumenAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (rp->paramID == IDs::freeze) continue;
            float v = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : factoryPresets()[(size_t) index].v) if (rp->paramID == kv.first) v = kv.second;
            rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture();
        }
    ++programVersion;
}

void LumenAudioProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (ab[abSlot]); abSlot = to;
    if (ab[to].getSize() > 0) setStateInformation (ab[to].getData(), (int) ab[to].getSize());
}
void LumenAudioProcessor::copyAB() { getStateInformation (ab[1 - abSlot]); }

void LumenAudioProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto st = apvts.copyState(); st.setProperty ("program", currentProgram, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, d);
}
void LumenAudioProcessor::setStateInformation (const void* data, int size)
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

juce::AudioProcessorEditor* LumenAudioProcessor::createEditor() { return new LumenAudioEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenAudioProcessor(); }
