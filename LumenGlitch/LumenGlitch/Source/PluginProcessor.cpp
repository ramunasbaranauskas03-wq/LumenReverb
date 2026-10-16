#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lumen::glitch;

LumenGlitchProcessor::LumenGlitchProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenGlitch", createLayout())
{
}

bool LumenGlitchProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    if (out != mono && out != stereo) return false;
    if (in != mono && in != stereo) return false;
    return ! (in == stereo && out == mono);
}

void LumenGlitchProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    engine.prepare (fs);
    setLatencySamples (0);
}

void LumenGlitchProcessor::setParam (const juce::String& id, float v)
{
    if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
    { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture(); }
}

void LumenGlitchProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    const auto& pr = factoryPresets()[(size_t) index];
    const int s = slot();
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (rp->paramID == IDs::slot || rp->paramID == IDs::midi) continue;
            float v = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : pr.v) if (rp->paramID == kv.first) v = kv.second;
            rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture();
        }
    // the preset's pattern goes into the selected slot -> build a bank of 8 from presets
    for (int l = 0; l < kLanes; ++l) pat[s][l].store (0);
    for (auto& ln : pr.lanes) if (juce::isPositiveAndBelow (ln.first, kLanes)) pat[s][ln.first].store (patternBits (ln.second));
    ++programVersion;
}

void LumenGlitchProcessor::setStep (int lane, int step, bool on)
{
    if (! juce::isPositiveAndBelow (lane, kLanes) || ! juce::isPositiveAndBelow (step, 32)) return;
    auto& a = pat[slot()][lane];
    const uint32_t m = 1u << step;
    a.store (on ? (a.load() | m) : (a.load() & ~m));
}
void LumenGlitchProcessor::clearSlot() { for (int l = 0; l < kLanes; ++l) pat[slot()][l].store (0); ++programVersion; }
void LumenGlitchProcessor::copySlotTo (int dst)
{
    if (! juce::isPositiveAndBelow (dst, 8)) return;
    for (int l = 0; l < kLanes; ++l) pat[dst][l].store (pat[slot()][l].load());
}

void LumenGlitchProcessor::generate (Style st)
{
    const int steps = P (IDs::steps) > 0.5f ? 32 : 16;
    uint32_t L[kLanes] {};
    auto on = [&] (int lane, int a, int b) { for (int i = a; i < b && i < steps; ++i) L[lane] |= 1u << i; };
    auto chance = [&] (float c) { return rng.nextFloat() < c; };
    const int bar = 16, last = steps - bar;                                  // the last 16 steps = "end"
    switch (st)
    {
        case BuildUp:
            setParam (IDs::repRate, (float) (2 + rng.nextInt (2))); setParam (IDs::repRamp, 0.5f + 0.4f * rng.nextFloat());
            on (Repeat, last + 8, steps); on (Filter, last, steps); setParam (IDs::fltType, 1.0f); setParam (IDs::fltAmt, 0.8f);
            if (chance (0.5f)) on (TapeStop, steps - 2, steps);
            break;
        case DropFill:
        {
            setParam (IDs::repRate, 3.0f); setParam (IDs::repRamp, 0.4f);
            const int s0 = last + 8 + rng.nextInt (3);
            on (Repeat, s0, s0 + 4);
            if (chance (0.6f)) on (Reverse, last + 4, last + 6);
            on (TapeStop, steps - 2, steps);
            if (chance (0.5f)) on (Echo, last + 7, last + 8);
            break;
        }
        case StutterRoll:
            setParam (IDs::repRate, (float) (3 + rng.nextInt (3))); setParam (IDs::repRamp, rng.nextFloat() * 0.8f);
            for (int i = 0; i < steps; ++i) if ((i % 16) >= 12 || (chance (0.12f) && i % 4 == 2)) L[Repeat] |= 1u << i;
            break;
        case Dubstep:
            setParam (IDs::repRate, 6.0f);
            for (int i = 0; i < steps; ++i) { if (i % 4 == 0 && chance (0.5f)) L[Repeat] |= 1u << i; if (i % 2 == 0) L[Filter] |= 1u << i; }
            on (HalfSpeed, last + 12, steps); setParam (IDs::fltType, 0.0f); setParam (IDs::fltAmt, 0.6f);
            break;
        case Trap:
            setParam (IDs::repRate, chance (0.5f) ? 4.0f : 7.0f); setParam (IDs::repRamp, 0.2f);
            for (int i = 0; i < steps; ++i) if ((i % 4 == 3 && chance (0.6f)) || ((i % 16) >= 13 && chance (0.7f))) L[Repeat] |= 1u << i;
            if (chance (0.5f)) on (Echo, steps - 2, steps - 1);
            break;
        case HalfTime:
            on (HalfSpeed, 0, steps / 2); if (chance (0.5f)) on (Crush, steps / 4, steps / 2);
            if (chance (0.5f)) on (TapeStop, steps - 2, steps);
            break;
        case Random:
        default:
            for (int l = 0; l < kLanes; ++l)
            {
                const float density = l == Gate ? 0.5f : l == Echo ? 0.06f : 0.12f;
                for (int i = 0; i < steps; ++i) if (chance (density)) L[l] |= 1u << i;
            }
            break;
    }
    const int s = slot();
    for (int l = 0; l < kLanes; ++l) { uint32_t b = L[l]; if (steps == 16) b |= b << 16; pat[s][l].store (b); }
    ++programVersion;
}

void LumenGlitchProcessor::syncMidiSlot()
{
    const int m = midiSlot.exchange (-1);
    if (m >= 0 && m != slot()) { setParam (IDs::slot, (float) m); ++programVersion; }
}

void LumenGlitchProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() == 0) return;
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    if (inCh == 1 && outCh > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (juce::jmin (1, buffer.getNumChannels() - 1));
    float tmp[2048] {};
    const bool monoOut = buffer.getNumChannels() < 2;

    // parameters
    Params p;
    p.steps = P (IDs::steps) > 0.5f ? 32 : 16; p.rate = (int) P (IDs::rate); p.swing = P (IDs::swing); p.play = (int) P (IDs::play);
    p.xfadeMs = P (IDs::xfade); p.repRate = (int) P (IDs::repRate); p.repRamp = P (IDs::repRamp);
    p.stopTime = P (IDs::stopTime); p.stopCurve = P (IDs::stopCurve); p.scratch = P (IDs::scratch); p.shufRange = (int) P (IDs::shuf);
    p.gateDuty = P (IDs::gateDuty); p.gateSmooth = P (IDs::gateSmooth); p.bits = P (IDs::bits); p.down = P (IDs::down);
    p.fltType = (int) P (IDs::fltType); p.fltAmount = P (IDs::fltAmt); p.fltRes = P (IDs::fltRes);
    p.echoTime = (int) P (IDs::echoTime); p.echoFb = P (IDs::echoFb); p.echoLevel = P (IDs::echoLvl);
    for (int l = 0; l < kLanes; ++l) p.prob[l] = P (IDs::prob (l));
    p.mix = P (IDs::mix); p.outDb = P (IDs::out);

    // transport
    double bpm = 120.0, beat = freeBeat; bool isPlaying = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition())
    {
        if (auto b = pos->getBpm()) bpm = *b;
        if (auto ts = pos->getTimeSignature()) { p.barBeatsNum = juce::jmax (1, ts->numerator); p.barBeatsDen = juce::jmax (1, ts->denominator); }
        isPlaying = pos->getIsPlaying();
        if (isPlaying) if (auto q = pos->getPpqPosition()) beat = *q;
    }
    playing.store (isPlaying);
    const double bps = bpm / 60.0 / fs;
    engine.setParams (p);

    // MIDI: C1..A1 hold lanes, C2..G2 pick the pattern slot
    const bool midiOn = P (IDs::midi) > 0.5f;
    int effSlot = slot();
    struct Ev { int pos; uint32_t held; };
    Ev evs[64]; int nev = 0;
    uint32_t h = heldLanes.load();
    if (midiOn)
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            const int note = m.getNoteNumber();
            if (m.isNoteOn() && note >= 48 && note < 56) { midiSlot.store (note - 48); effSlot = note - 48; continue; }
            if ((m.isNoteOn() || m.isNoteOff()) && note >= 36 && note < 36 + kLanes)
            {
                if (m.isNoteOn()) h |= 1u << (note - 36); else h &= ~(1u << (note - 36));
                if (nev < 64) evs[nev++] = { meta.samplePosition, h };
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff()) { h = 0; if (nev < 64) evs[nev++] = { meta.samplePosition, h }; }
        }
    for (int l = 0; l < kLanes; ++l) engine.setPattern (l, pat[effSlot][l].load());

    inPeak.store (buffer.getMagnitude (0, n));
    // process in segments split at MIDI events
    int pos0 = 0;
    auto run = [&] (int a, int b)
    {
        for (int s = a; s < b; s += 2048)
        {
            const int len = juce::jmin (2048, b - s);
            float* r = R + s;
            if (monoOut) { std::memcpy (tmp, L + s, sizeof (float) * (size_t) len); r = tmp; }
            engine.process (L + s, r, len, beat + bps * s, bps);
        }
    };
    for (int e = 0; e < nev; ++e)
    {
        const int at = juce::jlimit (pos0, n, evs[e].pos);
        run (pos0, at); pos0 = at;
        engine.setHeld (evs[e].held);
    }
    run (pos0, n);
    heldLanes.store (h);
    freeBeat = isPlaying ? beat + bps * n : freeBeat + bps * n;
    outPeak.store (buffer.getMagnitude (0, n));
}

//==============================================================================
void LumenGlitchProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenGlitchProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenGlitchProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    juce::StringArray sa;
    for (int s = 0; s < 8; ++s) for (int l = 0; l < kLanes; ++l) sa.add (juce::String::toHexString ((juce::int64) pat[s][l].load()));
    state.setProperty ("patterns", sa.joinIntoString (","), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenGlitchProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            currentProgram = (int) vt.getProperty ("program", 0);
            juce::StringArray sa; sa.addTokens (vt.getProperty ("patterns", "").toString(), ",", "");
            for (int s = 0; s < 8; ++s) for (int l = 0; l < kLanes; ++l)
            {
                const int i = s * kLanes + l;
                pat[s][l].store (i < sa.size() ? (uint32_t) sa[i].getHexValue64() : 0u);
            }
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

juce::AudioProcessorEditor* LumenGlitchProcessor::createEditor() { return new LumenGlitchEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenGlitchProcessor(); }
