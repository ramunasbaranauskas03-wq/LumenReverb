#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lumen::chord;

LumenChordProcessor::LumenChordProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenChord", createLayout())
{
    for (int i = 0; i < 16; ++i) synth.addVoice (new PreviewVoice (soundType));
    synth.addSound (new PreviewSound());
    parseProgression ("0,5,2,6");
}

bool LumenChordProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) && layouts.getMainInputChannelSet().isDisabled();
}

void LumenChordProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    engine.prepare (fs);
    synth.setCurrentPlaybackSampleRate (fs);
}

Params LumenChordProcessor::readParams() const
{
    Params p;
    p.key = (int) P (IDs::key); p.scale = (int) P (IDs::scale); p.type = (int) P (IDs::type); p.mode = (int) P (IDs::mode);
    p.inversion = (int) P (IDs::inversion); p.spread = (int) P (IDs::spread); p.octave = (int) P (IDs::octave);
    p.voiceLead = P (IDs::voiceLead) > 0.5f; p.bass = P (IDs::bass) > 0.5f;
    p.strumMs = P (IDs::strum); p.humanize = P (IDs::humanize); p.velocity = P (IDs::velocity); p.useInputVel = P (IDs::inVel) > 0.5f;
    static const double cb[] { 2.0, 4.0, 8.0, 16.0 };
    p.chordBeats = cb[juce::jlimit (0, 3, (int) P (IDs::bars))];
    p.numSlots = (int) P (IDs::numSlots);
    p.arpOn = P (IDs::arpOn) > 0.5f; p.arpMode = (int) P (IDs::arpMode); p.arpRate = (int) P (IDs::arpRate); p.arpOct = (int) P (IDs::arpOct);
    p.arpGate = P (IDs::arpGate); p.arpSwing = P (IDs::arpSwing); p.latch = P (IDs::latch) > 0.5f;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        for (int i = 0; i < 8; ++i) p.slots[i] = slots[i];
        for (int i = 0; i < 16; ++i) p.stepLevel[i] = steps[i];
        p.ratchet = ratchet;
    }
    return p;
}

void LumenChordProcessor::setParam (const juce::String& id, float v)
{
    if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
    { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture(); }
}

void LumenChordProcessor::parseProgression (const juce::String& s)
{
    juce::StringArray t; t.addTokens (s, ",", "");
    const juce::SpinLock::ScopedLockType l (lock);
    for (int i = 0; i < 8; ++i)
    {
        Slot sl;
        if (i < t.size())
        {
            juce::String tok = t[i].trim();
            if (tok.startsWithChar ('b')) { sl.shift = -1; tok = tok.substring (1); }
            sl.degree = tok.getIntValue();
        }
        else if (t.size() > 0) { juce::String tok = t[i % t.size()].trim(); if (tok.startsWithChar ('b')) { sl.shift = -1; tok = tok.substring (1); } sl.degree = tok.getIntValue(); }
        slots[i] = sl;
    }
}

void LumenChordProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    const auto& pr = factoryPresets()[(size_t) index];
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (rp->paramID == IDs::sound || rp->paramID == IDs::soundLvl) continue;
            float v = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : pr.v) if (rp->paramID == kv.first) v = kv.second;
            if (rp->paramID == IDs::numSlots) { juce::StringArray t; t.addTokens (pr.prog, ",", ""); bool has = false; for (auto& kv : pr.v) has |= juce::String (kv.first) == IDs::numSlots; if (! has) v = (float) t.size(); }
            rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture();
        }
    parseProgression (pr.prog);
    {
        const juce::SpinLock::ScopedLockType l (lock);
        const juce::String st = pr.steps != nullptr ? juce::String (pr.steps) : juce::String ("XXXXXXXXXXXXXXXX");
        for (int i = 0; i < 16; ++i) { const auto ch = st[i % st.length()]; steps[i] = ch == 'X' ? 1.0f : ch == 'x' ? 0.55f : 0.0f; }
        ratchet = 0;
    }
    ++programVersion;
}

juce::String LumenChordProcessor::slotName (int i)
{
    juce::String nm;
    ChordEngine e; e.prepare (fs); auto p = readParams(); p.voiceLead = false; e.setParams (p);
    e.voiceSlot (getSlot (i), 3, &nm);
    return nm;
}
juce::String LumenChordProcessor::slotRoman (int i)
{
    const auto p = readParams();
    const auto& sc = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, p.scale)];
    const auto s = getSlot (i);
    return romanFor (sc, s.degree, s.shift, chordIntervals (sc, s.degree, Triad, s.quality));
}

void LumenChordProcessor::generate (Mood m)
{
    // progressions in minor-scale degrees that EDM keeps coming back to, mutated a little each time
    static const std::vector<std::vector<int>> euph { { 5, 6, 0, 0 }, { 5, 6, 4, 0 }, { 0, 5, 2, 6 }, { 3, 5, 6, 0 }, { 5, 2, 6, 0 } };
    static const std::vector<std::vector<int>> emo  { { 0, 5, 2, 6 }, { 5, 6, 0, 4 }, { 0, 3, 5, 6 }, { 3, 0, 6, 5 }, { 0, 6, 5, 6 } };
    static const std::vector<std::vector<int>> dark { { 0, 0, 5, 4 }, { 0, 5, 0, 6 }, { 0, 3, 0, 4 }, { 0, 6, 5, 4 } };
    static const std::vector<std::vector<int>> epic { { 5, 6, 0, 0 }, { 0, 6, 5, 6 }, { 3, 4, 5, 6 }, { 5, 3, 0, 4 } };
    static const std::vector<std::vector<int>> chill { { 3, 2, 1, 0 }, { 1, 4, 0, 5 }, { 0, 3, 6, 2 }, { 5, 1, 4, 0 } };
    const std::vector<std::vector<int>>* bank = m == Euphoric ? &euph : m == Emotional ? &emo : m == Dark ? &dark : m == Epic ? &epic : &chill;
    if (m == Random) { static const std::vector<std::vector<int>>* all[] { &euph, &emo, &dark, &epic, &chill }; bank = all[rng.nextInt (5)]; }
    const auto& base = (*bank)[(size_t) rng.nextInt ((int) bank->size())];
    const int len = rng.nextFloat() < 0.35f ? 8 : 4;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        for (int i = 0; i < 8; ++i)
        {
            Slot s; s.degree = base[(size_t) (i % 4)];
            // second half: a variation (cadence change)
            if (i >= 4 && len == 8 && i == 7) s.degree = rng.nextBool() ? 4 : 6;
            if (rng.nextFloat() < 0.15f && i > 0) s.degree = (s.degree + (rng.nextBool() ? 2 : 5)) % 7;
            slots[i] = s;
        }
    }
    setParam (IDs::numSlots, (float) len);
    const int types[] { m == Chill ? 2 : m == Dark ? 6 : m == Epic ? 7 : m == Emotional ? 3 : 1 };
    setParam (IDs::type, (float) types[0]);
    if (m == Dark && rng.nextBool()) setParam (IDs::scale, 5.0f);
    else if (m != Chill) setParam (IDs::scale, 0.0f);
    ++programVersion;
}

void LumenChordProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    auto p = readParams();
    engine.setParams (p);
    soundType = (int) P (IDs::soundType);
    double bpm = 120.0, beat = freeBeat; bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition())
    {
        if (auto b = pos->getBpm()) bpm = *b;
        playing = pos->getIsPlaying();
        if (playing) if (auto q = pos->getPpqPosition()) beat = *q;
    }
    const double bps = bpm / 60.0 / fs;
    juce::MidiBuffer out, extra;
    const int au = auditionSlot.exchange (-1);
    if (au >= 0) engine.auditionSlot (extra, 0, au, (int64_t) (fs * 1.2));
    // (the arp keeps a free clock while the transport is stopped, so it runs when you just play keys)
    engine.process (midi, out, n, beat, bps, playing);
    if (! extra.isEmpty()) out.addEvents (extra, 0, n, 0);
    freeBeat = playing ? beat + bps * n : freeBeat + bps * n;
    if (P (IDs::sound) > 0.5f)
    {
        synth.renderNextBlock (buffer, out, 0, n);
        buffer.applyGain (juce::Decibels::decibelsToGain (P (IDs::soundLvl)));
    }
    else synth.allNotesOff (0, false);
    for (int i = 0; i < 128; ++i) sounding[(size_t) i].store (false);
    for (int v : engine.soundingNotes()) sounding[(size_t) v].store (true);
    for (int i = 0; i < 128; ++i) inChord[(size_t) i].store (false);
    if (p.arpOn) for (int v : engine.chordNotes()) if (v >= 0 && v < 128) inChord[(size_t) v].store (true);
    currentChordSlot.store (engine.currentSlot());
    outPeak.store (buffer.getMagnitude (0, n));
    midi.swapWith (out);
}

juce::File LumenChordProcessor::exportMidi (int barsPerSlotOverride)
{
    // render the progression offline in AUTO mode at 120 bpm -> MIDI file (960 ppq)
    auto p = readParams(); p.mode = Auto;
    if (barsPerSlotOverride > 0) p.chordBeats = 4.0 * barsPerSlotOverride;
    ChordEngine e; const double rate = 48000.0; e.prepare (rate); e.setParams (p);
    const double bps = 2.0 / rate;
    const double totalBeats = p.chordBeats * std::max (1, p.numSlots);
    const int total = (int) std::ceil (totalBeats / bps);
    juce::MidiMessageSequence seq;
    juce::MidiBuffer in, out;
    const int blk = 512;
    for (int pos = 0; pos < total; pos += blk)
    {
        const int len = std::min (blk, total - pos);
        e.process (in, out, len, pos * bps, bps, true);
        for (const auto meta : out)
        {
            auto m = meta.getMessage();
            const double beats = (pos + meta.samplePosition) * bps;
            m.setTimeStamp (beats * 960.0);
            seq.addEvent (m);
        }
    }
    juce::MidiBuffer endOut; e.allNotesOff (endOut, 0);
    for (const auto meta : endOut) { auto m = meta.getMessage(); m.setTimeStamp (totalBeats * 960.0); seq.addEvent (m); }
    seq.updateMatchedPairs();
    juce::MidiFile mf; mf.setTicksPerQuarterNote (960);
    juce::MidiMessageSequence meta;
    meta.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0); meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    mf.addTrack (meta); mf.addTrack (seq);
    juce::String nm;
    for (int i = 0; i < std::max (1, p.numSlots); ++i) nm << (i ? "-" : "") << slotName (i);
    nm = nm.replaceCharacters ("#/\\:*?\"<>|", "s_________");
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Lumen Chord");
    dir.createDirectory();
    auto f = dir.getNonexistentChildFile ("Lumen_" + keyNames()[p.key].replace ("#", "s") + "_" + nm + (p.arpOn ? "_arp" : ""), ".mid");
    juce::FileOutputStream os (f);
    if (! os.openedOk()) return {};
    mf.writeTo (os);
    return f;
}

void LumenChordProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenChordProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenChordProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    juce::StringArray sl, st;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        for (auto& s : slots) sl.add (juce::String (s.degree) + ":" + juce::String (s.shift) + ":" + juce::String (s.quality) + ":" + juce::String (s.inversion) + ":" + juce::String (s.octave) + ":" + juce::String ((int) s.on));
        for (float v : steps) st.add (juce::String (v, 3));
        state.setProperty ("ratchet", (int) ratchet, nullptr);
    }
    state.setProperty ("slots", sl.joinIntoString (","), nullptr);
    state.setProperty ("steps", st.joinIntoString (","), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenChordProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            currentProgram = (int) vt.getProperty ("program", 0);
            {
                const juce::SpinLock::ScopedLockType l (lock);
                juce::StringArray sl; sl.addTokens (vt.getProperty ("slots", "").toString(), ",", "");
                for (int i = 0; i < 8 && i < sl.size(); ++i)
                {
                    juce::StringArray f; f.addTokens (sl[i], ":", "");
                    if (f.size() >= 6) { slots[i].degree = f[0].getIntValue(); slots[i].shift = f[1].getIntValue(); slots[i].quality = f[2].getIntValue(); slots[i].inversion = f[3].getIntValue(); slots[i].octave = f[4].getIntValue(); slots[i].on = f[5].getIntValue() != 0; }
                }
                juce::StringArray st; st.addTokens (vt.getProperty ("steps", "").toString(), ",", "");
                for (int i = 0; i < 16 && i < st.size(); ++i) steps[i] = st[i].getFloatValue();
                ratchet = (uint32_t) (int) vt.getProperty ("ratchet", 0);
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

juce::AudioProcessorEditor* LumenChordProcessor::createEditor() { return new LumenChordEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenChordProcessor(); }
