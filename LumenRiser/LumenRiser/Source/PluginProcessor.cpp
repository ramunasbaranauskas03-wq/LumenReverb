#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lumen::riser;

LumenRiserProcessor::LumenRiserProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenRiser", createLayout())
{
}

bool LumenRiserProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet(), i = l.getMainInputChannelSet();
    if (o != juce::AudioChannelSet::stereo()) return false;
    return i == juce::AudioChannelSet::stereo() || i == juce::AudioChannelSet::mono() || i.isDisabled();
}

void LumenRiserProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    engine.prepare (fs);
    engine.setParams (readParams());
}

Params LumenRiserProcessor::readParams() const
{
    auto g = [this] (const juce::String& id) { const float d = P (id); return d <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (d); };
    Params p;
    p.mode = (int) P (IDs::mode); p.rise = P (IDs::rise); p.length = (int) P (IDs::length); p.period = (int) P (IDs::period);
    p.offset = (int) P (IDs::offset); p.curve = P (IDs::curve); p.gap = (int) P (IDs::gap); p.key = (int) P (IDs::key);
    p.noiseOn = P (IDs::noiseOn) > 0.5f; p.noiseLvl = g (IDs::noiseLvl); p.pink = P (IDs::noiseType) > 0.5f; p.noiseFilter = (int) P (IDs::noiseFilter); p.noiseRes = P (IDs::noiseRes);
    p.synthOn = P (IDs::synthOn) > 0.5f; p.synthLvl = g (IDs::synthLvl); p.stack = (int) P (IDs::stack); p.synthOct = (int) P (IDs::synthOct); p.synthPitch = P (IDs::synthPitch); p.detune = P (IDs::detune);
    p.shepOn = P (IDs::shepOn) > 0.5f; p.shepLvl = g (IDs::shepLvl); p.shepSpeed = P (IDs::shepSpeed);
    p.impactOn = P (IDs::impactOn) > 0.5f; p.impactLvl = g (IDs::impactLvl); p.impactDecay = P (IDs::impactDecay);
    p.downOn = P (IDs::downOn) > 0.5f; p.downLvl = g (IDs::downLvl); p.downLen = (int) P (IDs::downLen);
    p.space = P (IDs::space); p.size = P (IDs::size); p.tailCut = P (IDs::tailCut) > 0.5f;
    p.fxOn = P (IDs::fxOn) > 0.5f; p.fxFilter = (int) P (IDs::fxFilter); p.fxAmt = P (IDs::fxAmt); p.fxRes = P (IDs::fxRes);
    p.stutter = P (IDs::stutter); p.roll = (int) P (IDs::roll); p.fxVerb = P (IDs::fxVerb); p.width = P (IDs::width); p.fxMix = P (IDs::fxMix);
    p.levelMatch = P (IDs::levelMatch) > 0.5f; p.clubSafe = P (IDs::clubSafe) > 0.5f; p.genLvl = g (IDs::genLvl);
    p.outGain = juce::Decibels::decibelsToGain (P (IDs::out));
    return p;
}

void LumenRiserProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels();
    if (buffer.getNumChannels() < 2) return;
    if (inCh == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    else if (inCh == 0) buffer.clear();
    const auto p = readParams();
    engine.setParams (p);
    double bpm = 120.0, beat = 0.0, bpb = 4.0; bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition())
    {
        if (auto b = pos->getBpm()) bpm = *b;
        if (auto ts = pos->getTimeSignature()) if (ts->numerator > 0 && ts->denominator > 0) bpb = 4.0 * ts->numerator / ts->denominator;
        playing = pos->getIsPlaying();
        if (auto q = pos->getPpqPosition()) beat = *q; else playing = false;
    }
    bpm = juce::jlimit (20.0, 999.0, bpm);
    hostBpm.store ((float) bpm);
    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, beat, bpm / 60.0 / fs, playing, bpb, midi);
    if (playing)
    {
        const double P_ = periodBars (p.period);
        double ph = std::fmod (beat / bpb - p.offset, P_); if (ph < 0) ph += P_;
        periodPhase.store ((float) ph);
    }
    else periodPhase.store (-1.0f);
    progress.store (engine.progressOut.load()); state.store (engine.stateOut.load()); stutterDiv.store (engine.divOut.load()); drops.store (engine.dropCount.load());
    outPeak.store (buffer.getMagnitude (0, n));
    midi.clear();
}

juce::AudioBuffer<float> LumenRiserProcessor::render (RenderKind k, double rate, double bpm) const
{
    auto p = readParams();
    p.genOnly = true; p.fxOn = false; p.mode = MidiHold;
    if (k == RenderImpact) { p.noiseOn = p.synthOn = p.shepOn = p.downOn = false; p.impactOn = true; }
    if (k == RenderDown)   { p.noiseOn = p.synthOn = p.shepOn = p.impactOn = false; p.downOn = true; }
    if (k == RenderRiser)  { p.impactOn = p.downOn = false; }
    const double bpb = 4.0, bps = bpm / 60.0 / rate;
    const double buildS = lengthBars (p.length) * bpb / (bpm / 60.0);
    const double postS = std::max ({ p.impactOn ? p.impactDecay * 3.0 + 1.5 : 0.0, p.downOn ? downBars (p.downLen) * bpb / (bpm / 60.0) + 1.5 : 0.0, 0.5 });
    double total = k == RenderRiser ? buildS : k == RenderFull ? buildS + postS : postS;
    const int len = (int) std::ceil (total * rate);
    juce::AudioBuffer<float> out (2, len); out.clear();
    auto e = std::make_unique<RiserEngine>();
    e->prepare (rate); e->setParams (p);
    if (k == RenderRiser || k == RenderFull) e->preview(); else e->triggerDrop();
    juce::MidiBuffer none;
    for (int pos = 0; pos < len; pos += 512)
    {
        const int n = std::min (512, len - pos);
        e->process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), n, 0.0, bps, false, bpb, none);
    }
    if (k != RenderRiser)
    {
        // trim the silent end, 30 ms fade
        int end = len;
        while (end > (int) (0.2 * rate) && std::max (std::abs (out.getSample (0, end - 1)), std::abs (out.getSample (1, end - 1))) < 3.0e-4f) --end;
        end = std::min (len, end + (int) (0.03 * rate));
        out.setSize (2, end, true);
        const int f = std::min (end, (int) (0.03 * rate));
        out.applyGainRamp (end - f, f, 1.0f, 0.0f);
    }
    return out;
}

juce::File LumenRiserProcessor::exportWav (RenderKind k)
{
    const double bpm = hostBpm.load();
    auto buf = render (k, fs, bpm);
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Lumen Riser");
    dir.createDirectory();
    static const char* names[] { "Riser", "Impact", "Downlifter", "Riser+Impact" };
    const auto p = readParams();
    juce::String nm = juce::String ("Lumen_") + names[k] + "_" + juce::String (juce::roundToInt (bpm)) + "bpm";
    if (k == RenderRiser || k == RenderFull) nm << "_" << juce::String (lengthBars (p.length), 0) << "bars";
    auto f = dir.getNonexistentChildFile (nm, ".wav");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return {};
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (fs).withNumChannels (2).withBitsPerSample (24));
    if (w == nullptr) return {};
    w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    w.reset();
    return f;
}

void LumenRiserProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    // reset every parameter to its default, then apply the preset's values
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (rp->paramID != IDs::rise) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->getDefaultValue()); rp->endChangeGesture(); }
    for (auto& [id, v] : factoryPresets()[(size_t) index].v)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
        { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture(); }
    ++programVersion;
}

void LumenRiserProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenRiserProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenRiserProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state_ = apvts.copyState();
    state_.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state_.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenRiserProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* LumenRiserProcessor::createEditor() { return new LumenRiserEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenRiserProcessor(); }
