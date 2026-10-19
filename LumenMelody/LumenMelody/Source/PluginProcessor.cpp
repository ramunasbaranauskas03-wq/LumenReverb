#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lumen::melody;

namespace
{
const juce::StringArray leadRegenIds { "lead_style", "lead_density", "lead_catchy", "lead_range", "lead_contour", "hook_length", "pattern_length", "harmony", "progression", "chord_length" };
const juce::StringArray bassRegenIds { "bass_style", "bass_variation", "bass_approach", "pattern_length" };
}

LumenMelodyProcessor::LumenMelodyProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenMelody", createLayout())
{
    for (int i = 0; i < 8; ++i) leadSynth.addVoice (new PreviewVoice (leadType, false));
    leadSynth.addSound (new PreviewSound (1));
    for (int i = 0; i < 2; ++i) bassSynth.addVoice (new PreviewVoice (bassType, true));
    bassSynth.addSound (new PreviewSound (2));
    for (auto& id : leadRegenIds) apvts.addParameterListener (id, this);
    for (auto& id : bassRegenIds) if (! leadRegenIds.contains (id)) apvts.addParameterListener (id, this);
    lead.reserve (512); bass.reserve (512); leadA.reserve (512); bassA.reserve (512);
    setCurrentProgram (0);
}

LumenMelodyProcessor::~LumenMelodyProcessor()
{
    cancelPendingUpdate();
    for (auto& id : leadRegenIds) apvts.removeParameterListener (id, this);
    for (auto& id : bassRegenIds) apvts.removeParameterListener (id, this);
}

bool LumenMelodyProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void LumenMelodyProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    player.prepare (fs);
    leadSynth.setCurrentPlaybackSampleRate (fs);
    bassSynth.setCurrentPlaybackSampleRate (fs);
    dirty.store (true);
}

PlayParams LumenMelodyProcessor::readPlay() const
{
    PlayParams p;
    p.key = (int) P (IDs::key); p.scale = (int) P (IDs::scale); p.source = (int) P (IDs::source); p.harmony = (int) P (IDs::harmony);
    const auto& pr = progressions()[(size_t) juce::jlimit (0, (int) progressions().size() - 1, (int) P (IDs::prog))];
    p.numSlots = (int) pr.deg.size(); for (int i = 0; i < 8; ++i) p.prog[i] = pr.deg[(size_t) (i % p.numSlots)];
    static const double cb[] { 2.0, 4.0, 8.0, 16.0 };
    p.chordBeats = cb[juce::jlimit (0, 3, (int) P (IDs::chordLen))];
    static const int bars[] { 1, 2, 4, 8 };
    p.bars = bars[juce::jlimit (0, 3, (int) P (IDs::bars))];
    p.leadOn = P (IDs::leadOn) > 0.5f; p.bassOn = P (IDs::bassOn) > 0.5f;
    p.leadOct = (int) P (IDs::leadOct); p.bassOct = (int) P (IDs::bassOct);
    p.leadGate = P (IDs::leadGate); p.bassGate = P (IDs::bassGate);
    p.swing = P (IDs::swing); p.humanize = P (IDs::humanize); p.velocity = P (IDs::velocity);
    return p;
}

LeadSettings LumenMelodyProcessor::leadSettings() const
{
    LeadSettings s; const auto p = readPlay();
    s.style = (int) P (IDs::leadStyle); s.bars = p.bars;
    static const int hb[] { 1, 2, 4 };
    s.hookBars = std::min (p.bars, hb[juce::jlimit (0, 2, (int) P (IDs::hook))]);
    s.contour = (int) P (IDs::contour); s.range = (int) P (IDs::range); s.density = P (IDs::density); s.catchy = P (IDs::catchy);
    s.seed = leadSeed;
    return s;
}

BassSettings LumenMelodyProcessor::bassSettings() const
{
    BassSettings s;
    s.style = (int) P (IDs::bassStyle); s.bars = readPlay().bars; s.variation = P (IDs::variation); s.approach = P (IDs::approach) > 0.5f; s.seed = bassSeed;
    return s;
}

int LumenMelodyProcessor::chordAtStep (int64_t s) const
{
    Player tmp; auto p = readPlay(); p.source = Internal; tmp.setParams (p);
    return tmp.chordAtStep (s);
}

void LumenMelodyProcessor::generate (int part, bool newSeed)
{
    if (part == 0 || part == 2)
    {
        if (newSeed) leadSeed = (uint32_t) (juce::Random::getSystemRandom().nextInt (1000000) + 1);
        const auto s = leadSettings(); auto pp = readPlay(); pp.source = Internal;
        Player tmp; tmp.setParams (pp);
        const bool keep = pp.harmony == KeepLine;
        auto pat = generateLead (s, [&] (int st) { return keep ? tmp.chordAtStep (st) : 0; });
        const juce::SpinLock::ScopedLockType l (lock); lead = pat;
    }
    if (part == 1 || part == 2)
    {
        if (newSeed) bassSeed = (uint32_t) (juce::Random::getSystemRandom().nextInt (1000000) + 1);
        auto pat = generateBass (bassSettings());
        const juce::SpinLock::ScopedLockType l (lock); bass = pat;
    }
    dirty.store (true); ++patternVersion;
}

void LumenMelodyProcessor::setPattern (int part, const Pattern& p)
{
    { const juce::SpinLock::ScopedLockType l (lock); (part == 0 ? lead : bass) = p; }
    dirty.store (true); ++patternVersion;
}

void LumenMelodyProcessor::parameterChanged (const juce::String& id, float)
{
    if (loading) return;
    if (leadRegenIds.contains (id)) regenLead.store (true);
    if (bassRegenIds.contains (id)) regenBass.store (true);
    triggerAsyncUpdate();
}

void LumenMelodyProcessor::handleAsyncUpdate()
{
    if (regenLead.exchange (false)) generate (0, false);
    if (regenBass.exchange (false)) generate (1, false);
}

void LumenMelodyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (dirty.load())
    {
        const juce::SpinLock::ScopedTryLockType l (lock);
        if (l.isLocked()) { leadA.assign (lead.begin(), lead.end()); bassA.assign (bass.begin(), bass.end()); player.setPatterns (leadA, bassA); dirty.store (false); }
    }
    player.setParams (readPlay());
    leadType = (int) P (IDs::leadSound); bassType = (int) P (IDs::bassSound);
    double bpm = 120.0, beat = 0.0; bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition())
    {
        if (auto b = pos->getBpm()) bpm = *b;
        playing = pos->getIsPlaying();
        if (auto q = pos->getPpqPosition()) beat = *q; else playing = false;
    }
    const double bps = juce::jlimit (20.0, 999.0, bpm) / 60.0 / fs;
    if (! playing && previewOn.load()) { playing = true; beat = freeBeat; freeBeat += bps * n; }
    else if (! previewOn.load()) freeBeat = 0.0;
    juce::MidiBuffer out;
    player.process (midi, out, n, beat, bps, playing);
    if (P (IDs::sound) > 0.5f)
    {
        juce::AudioBuffer<float> tmp (2, n);
        tmp.clear(); leadSynth.renderNextBlock (tmp, out, 0, n);
        buffer.addFrom (0, 0, tmp, 0, 0, n, juce::Decibels::decibelsToGain (P (IDs::leadLvl)));
        buffer.addFrom (1, 0, tmp, 1, 0, n, juce::Decibels::decibelsToGain (P (IDs::leadLvl)));
        tmp.clear(); bassSynth.renderNextBlock (tmp, out, 0, n);
        buffer.addFrom (0, 0, tmp, 0, 0, n, juce::Decibels::decibelsToGain (P (IDs::bassLvl)));
        buffer.addFrom (1, 0, tmp, 1, 0, n, juce::Decibels::decibelsToGain (P (IDs::bassLvl)));
    }
    else { leadSynth.allNotesOff (0, false); bassSynth.allNotesOff (0, false); }
    for (int i = 0; i < 128; ++i) { leadOn[(size_t) i].store (false); bassOnN[(size_t) i].store (false); }
    for (int v : player.sounding (0)) leadOn[(size_t) v].store (true);
    for (int v : player.sounding (1)) bassOnN[(size_t) v].store (true);
    playStep.store (playing ? player.lastStep() : -1);
    inputDegree.store (player.currentInputDegree());
    outPeak.store (buffer.getMagnitude (0, n));
    midi.swapWith (out);
}

juce::MidiFile LumenMelodyProcessor::renderMidi (ExportKind k) const
{
    auto p = readPlay(); p.source = Internal; p.humanize = 0;
    if (k == ExportLead) p.bassOn = false;
    if (k == ExportBass) p.leadOn = false;
    const int progBars = std::max (1, (int) std::ceil (p.numSlots * p.chordBeats / 4.0 - 1e-9));
    int bars = std::lcm (p.bars, progBars); bars = std::min (bars, 16);
    Player e; const double rate = 48000.0; e.prepare (rate); e.setParams (p);
    { const juce::SpinLock::ScopedLockType l (lock); e.setPatterns (lead, bass); }
    const double bps = 2.0 / rate;
    const double totalBeats = bars * 4.0;
    const int total = (int) std::ceil (totalBeats / bps);
    juce::MidiMessageSequence seq[2];
    juce::MidiBuffer in, out;
    for (int pos = 0; pos < total; pos += 512)
    {
        const int len = std::min (512, total - pos);
        e.process (in, out, len, pos * bps, bps, true);
        for (const auto meta : out)
        {
            auto m = meta.getMessage();
            m.setTimeStamp ((pos + meta.samplePosition) * bps * 960.0);
            seq[juce::jlimit (0, 1, m.getChannel() - 1)].addEvent (m);
        }
    }
    juce::MidiBuffer endOut; e.allNotesOff (endOut, 0);
    for (const auto meta : endOut) { auto m = meta.getMessage(); m.setTimeStamp (totalBeats * 960.0); seq[juce::jlimit (0, 1, m.getChannel() - 1)].addEvent (m); }
    juce::MidiFile mf; mf.setTicksPerQuarterNote (960);
    juce::MidiMessageSequence meta;
    meta.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0); meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    if (k == ExportBoth) mf.addTrack (meta);
    for (int c = 0; c < 2; ++c)
    {
        if ((k == ExportLead && c == 1) || (k == ExportBass && c == 0)) continue;
        seq[c].updateMatchedPairs();
        if (k != ExportBoth) { seq[c].addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0); seq[c].sort(); }
        auto nm = juce::MidiMessage::textMetaEvent (3, c == 0 ? "Lumen Lead" : "Lumen Bass"); nm.setTimeStamp (0); seq[c].addEvent (nm); seq[c].sort(); seq[c].updateMatchedPairs();
        mf.addTrack (seq[c]);
    }
    return mf;
}

juce::File LumenMelodyProcessor::exportMidi (ExportKind k)
{
    auto mf = renderMidi (k);
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Lumen Melody Bass");
    dir.createDirectory();
    const auto p = readPlay();
    static const char* kn[] { "Lead", "Bass", "Lead+Bass" };
    const juce::String style = k == ExportBass ? bassStyleNames()[(int) P (IDs::bassStyle)].upToFirstOccurrenceOf (" (", false, false)
                                               : leadStyleNames()[(int) P (IDs::leadStyle)];
    auto f = dir.getNonexistentChildFile (juce::String ("Lumen_") + kn[k] + "_" + keyNames()[p.key].replace ("#", "s") + (p.scale == 0 ? "m" : "") + "_" + style.replace (" ", ""), ".mid");
    juce::FileOutputStream os (f);
    if (! os.openedOk()) return {};
    mf.writeTo (os);
    return f;
}

void LumenMelodyProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    loading = true;
    const auto& pr = factoryPresets()[(size_t) index];
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->getDefaultValue()); rp->endChangeGesture(); }
    for (auto& [id, v] : pr.v)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id))) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture(); }
    loading = false;
    regenLead.store (false); regenBass.store (false); cancelPendingUpdate();
    leadSeed = pr.leadSeed; bassSeed = pr.bassSeed;
    generate (2, false);
    ++programVersion;
}

void LumenMelodyProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenMelodyProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenMelodyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    st.setProperty ("program", currentProgram, nullptr);
    st.setProperty ("leadSeed", (int) leadSeed, nullptr); st.setProperty ("bassSeed", (int) bassSeed, nullptr);
    { const juce::SpinLock::ScopedLockType l (lock); st.setProperty ("lead", patternToString (lead), nullptr); st.setProperty ("bass", patternToString (bass), nullptr); }
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenMelodyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            loading = true;
            currentProgram = (int) vt.getProperty ("program", 0);
            leadSeed = (uint32_t) (int) vt.getProperty ("leadSeed", 1); bassSeed = (uint32_t) (int) vt.getProperty ("bassSeed", 1);
            apvts.replaceState (vt);
            for (auto* prm : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                {
                    auto child = vt.getChildWithProperty ("id", rp->paramID);
                    if (child.isValid() && child.hasProperty ("value")) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
                }
            loading = false;
            regenLead.store (false); regenBass.store (false); cancelPendingUpdate();
            if (vt.hasProperty ("lead")) { const juce::SpinLock::ScopedLockType l (lock); lead = patternFromString (vt.getProperty ("lead").toString()); bass = patternFromString (vt.getProperty ("bass").toString()); }
            else generate (2, false);
            dirty.store (true); ++patternVersion; ++programVersion;
        }
}

juce::AudioProcessorEditor* LumenMelodyProcessor::createEditor() { return new LumenMelodyEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenMelodyProcessor(); }
