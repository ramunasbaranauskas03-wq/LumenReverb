#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenLoFiProcessor::LumenLoFiProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenLoFi", createLayout())
{
}

bool LumenLoFiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    if (out != mono && out != stereo) return false;
    if (in != mono && in != stereo) return false;
    return ! (in == stereo && out == mono);
}

lumen::lofi::Params LumenLoFiProcessor::readParams() const
{
    lumen::lofi::Params p;
    p.age = P (IDs::age);
    p.satOn = P (IDs::satOn) > 0.5f; p.satType = (int) P (IDs::satType); p.drive = P (IDs::drive); p.bias = P (IDs::bias);
    p.noiseOn = P (IDs::noiseOn) > 0.5f; p.noiseType = (int) P (IDs::noiseType); p.noiseDb = P (IDs::noiseLvl); p.noiseTone = P (IDs::noiseTone); p.noiseDuck = P (IDs::noiseDuck);
    p.wobOn = P (IDs::wobOn) > 0.5f; p.wow = P (IDs::wow); p.flutter = P (IDs::flutter); p.wobRate = P (IDs::wobRate); p.drift = P (IDs::drift); p.wobStereo = P (IDs::wobStereo);
    p.magOn = P (IDs::magOn) > 0.5f; p.wear = P (IDs::wear); p.bump = P (IDs::bump); p.dropouts = P (IDs::dropouts);
    p.digOn = P (IDs::digOn) > 0.5f; p.rate = P (IDs::rate); p.bits = P (IDs::bits); p.digFilter = P (IDs::digFilter);
    p.spaceOn = P (IDs::spaceOn) > 0.5f; p.size = P (IDs::size); p.decay = P (IDs::decay); p.spaceMix = P (IDs::spaceMix);
    p.lowCut = P (IDs::lowCut); p.highCut = P (IDs::highCut); p.width = P (IDs::width); p.monoBass = P (IDs::monoBass) > 0.5f;
    p.flux = P (IDs::flux); p.mix = P (IDs::mix); p.outDb = P (IDs::out);
    return p;
}

void LumenLoFiProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    engine.prepare (fs);
    engine.setParams (readParams());
    setLatencySamples (engine.latency());
}

void LumenLoFiProcessor::setParam (const juce::String& id, float v)
{
    if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
    { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture(); }
}

void LumenLoFiProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            float v = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : factoryPresets()[(size_t) index].v) if (rp->paramID == kv.first) v = kv.second;
            rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture();
        }
    ++programVersion;
}

void LumenLoFiProcessor::randomize()
{
    auto r = [this] (float a, float b) { return a + (b - a) * rng.nextFloat(); };
    auto coin = [this] (float pr) { return rng.nextFloat() < pr ? 1.0f : 0.0f; };
    setParam (IDs::satOn, 1); setParam (IDs::satType, (float) rng.nextInt (4)); setParam (IDs::drive, r (2, 14)); setParam (IDs::bias, r (0, 0.5f));
    setParam (IDs::noiseOn, coin (0.8f)); setParam (IDs::noiseType, (float) rng.nextInt (3)); setParam (IDs::noiseLvl, r (-56, -36)); setParam (IDs::noiseTone, r (0.2f, 0.8f)); setParam (IDs::noiseDuck, r (-0.6f, 0.2f));
    setParam (IDs::wobOn, coin (0.85f)); setParam (IDs::wow, r (0.05f, 0.7f)); setParam (IDs::flutter, r (0.05f, 0.5f)); setParam (IDs::wobRate, r (0.3f, 1.4f)); setParam (IDs::drift, r (0, 0.7f));
    setParam (IDs::magOn, coin (0.8f)); setParam (IDs::wear, r (0.1f, 0.7f)); setParam (IDs::bump, r (0.1f, 0.7f)); setParam (IDs::dropouts, r (0, 0.4f));
    setParam (IDs::digOn, coin (0.35f)); setParam (IDs::rate, r (11000, 32000)); setParam (IDs::bits, r (8, 13)); setParam (IDs::digFilter, r (0.2f, 0.9f));
    setParam (IDs::spaceOn, coin (0.25f)); setParam (IDs::spaceMix, r (0.1f, 0.3f));
    setParam (IDs::highCut, coin (0.5f) > 0 ? r (6000, 16000) : 20000.0f); setParam (IDs::lowCut, coin (0.3f) > 0 ? r (40, 150) : 20.0f);
    ++programVersion;
}

void LumenLoFiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() == 0) return;
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    engine.setParams (readParams());
    if (engine.latency() != getLatencySamples()) setLatencySamples (engine.latency());
    inPeak.store (buffer.getMagnitude (0, n));
    float* L = buffer.getWritePointer (0);
    if (buffer.getNumChannels() > 1) engine.process (L, buffer.getWritePointer (1), n);
    else
    {
        float tmp[1024];
        for (int s = 0; s < n; s += 1024) { const int len = juce::jmin (1024, n - s); std::memcpy (tmp, L + s, sizeof (float) * (size_t) len); engine.process (L + s, tmp, len); }
    }
    outPeak.store (buffer.getMagnitude (0, n));
}

void LumenLoFiProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenLoFiProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenLoFiProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenLoFiProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* LumenLoFiProcessor::createEditor() { return new LumenLoFiEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenLoFiProcessor(); }
