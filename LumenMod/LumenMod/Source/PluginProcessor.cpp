#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenModProcessor::LumenModProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenMod", createLayout())
{
}

bool LumenModProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void LumenModProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    engine.prepare (fs);
    cLR = 0; cLL = cRR = 1e-9f;
}

void LumenModProcessor::setChoice (const juce::String& id, int index)
{
    if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
    { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) index)); prm->endChangeGesture(); }
}

void LumenModProcessor::setCurrentProgram (int index)
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

void LumenModProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);

    lumen::mod::Params p;
    p.mode = (int) P (IDs::mode); p.variant = (int) P (IDs::variant); p.shape = (int) P (IDs::shape);
    p.rateHz = P (IDs::rate); p.depth = P (IDs::depth); p.delay = P (IDs::delay); p.feedback = P (IDs::feedback);
    p.voices = (int) P (IDs::voices); p.width = P (IDs::width); p.mix = P (IDs::mix); p.tone = P (IDs::tone);
    p.character = P (IDs::character); p.drift = P (IDs::drift); p.bassMono = P (IDs::bassMono); p.duck = P (IDs::duck);
    p.env = P (IDs::env); p.outDb = P (IDs::out);

    double synced = -1.0;
    if (P (IDs::sync) > 0.5f)
    {
        double bpm = 120.0, ppq = 0.0; bool playing = false;
        if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }
        const double beats = divBeats ((int) P (IDs::div));
        synced = bpm / 60.0 / beats;
        if (playing) engine.syncPhase (ppq / beats);
    }
    engine.setParams (p, synced);
    rateNow.store ((float) (synced > 0 ? synced : p.rateHz));

    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);
    inPk.store (std::max (buffer.getMagnitude (0, 0, n), buffer.getMagnitude (1, 0, n)));
    engine.process (L, R, n);
    outPk.store (std::max (buffer.getMagnitude (0, 0, n), buffer.getMagnitude (1, 0, n)));
    const float c = 1.0f - std::exp (-1.0f / (0.3f * (float) fs));
    for (int i = 0; i < n; ++i) { cLR += (L[i] * R[i] - cLR) * c; cLL += (L[i] * L[i] - cLL) * c; cRR += (R[i] * R[i] - cRR) * c; }
    corr.store (cLL * cRR > 1e-14f ? cLR / std::sqrt (cLL * cRR) : 1.0f);
    voiceCount.store (engine.voiceCount);
    for (int v = 0; v < lumen::mod::kMaxVoices; ++v) voicePos[v].store (engine.voicePos[v]);
    notch[0].store (engine.phaserNotch[0]); notch[1].store (engine.phaserNotch[1]);
}

void LumenModProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenModProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenModProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenModProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* LumenModProcessor::createEditor() { return new LumenModEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenModProcessor(); }
