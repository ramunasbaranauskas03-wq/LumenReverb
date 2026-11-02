#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::StringArray keyNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const int barsValues[] = { 1, 2, 4, 8 };

const char* regenParams[] = { "style", "bars", "range", "density", "sync", "kickAvoid", "octJump",
                              "glide", "ghost", "variation", "accent", "humanize", "fill" };

juce::StringArray namesOf (int count, const char* (*fn) (int))
{
    juce::StringArray a;
    for (int i = 0; i < count; ++i) a.add (fn (i));
    return a;
}
} // namespace

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout LumenBasslineProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def,
                                                      AudioParameterFloatAttributes().withStringFromValueFunction (
                                                          [] (float v, int) { return String (juce::roundToInt (v * 100.0f)) + " %"; }));
    };

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "style", 1 }, "Style",
                  namesOf (lb::numStyles(), [] (int i) { return lb::style (i).name; }), 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", keyNames, 1));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale",
                  namesOf (lb::numScales(), [] (int i) { return lb::scale (i).name; }), 0));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "octave", 1 }, "Octave", -2, 2, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "bars", 1 }, "Bars", StringArray { "1", "2", "4", "8" }, 2));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "range", 1 }, "Range", StringArray { "Tight", "Normal", "Wide" }, 1));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "progression", 1 }, "Progression",
                  namesOf (lb::numProgressions(), [] (int i) { return lb::progression (i).name; }), 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "follow", 1 }, "Follow MIDI Root", false));

    p.push_back (pct ("density",   "Density",     0.5f));
    p.push_back (pct ("sync",      "Syncopation", 0.5f));
    p.push_back (pct ("kickAvoid", "Kick Avoid",  0.85f));
    p.push_back (pct ("swing",     "Swing",       0.15f));
    p.push_back (pct ("gate",      "Gate",        0.8f));
    p.push_back (pct ("octJump",   "Octave Jump", 0.3f));
    p.push_back (pct ("glide",     "Glide",       0.3f));
    p.push_back (pct ("ghost",     "Ghost Notes", 0.2f));
    p.push_back (pct ("variation", "Variation",   0.35f));
    p.push_back (pct ("accent",    "Accent",      0.35f));
    p.push_back (pct ("humanize",  "Humanize",    0.25f));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "fill", 1 }, "Fills", true));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "liveRegen", 1 }, "Live Reshape", true));

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "sound", 1 }, "Sound", BassSynth::soundNames(), 1));
    p.push_back (pct ("cutoff", "Cutoff",     0.42f));
    p.push_back (pct ("reso",   "Resonance",  0.3f));
    p.push_back (pct ("envAmt", "Env Amount", 0.5f));
    p.push_back (pct ("decay",  "Decay",      0.35f));
    p.push_back (pct ("drive",  "Drive",      0.35f));
    p.push_back (pct ("sub",    "Sub",        0.4f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "glideTime", 1 }, "Glide Time",
                  NormalisableRange<float> (5.0f, 400.0f, 0.0f, 0.4f), 60.0f,
                  AudioParameterFloatAttributes().withStringFromValueFunction (
                      [] (float v, int) { return String (juce::roundToInt (v)) + " ms"; })));
    p.push_back (pct ("volume", "Volume",     0.75f));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "drumsOn", 1 }, "Preview Drums", true));
    p.push_back (pct ("drums",  "Drum Level", 0.6f));
    p.push_back (pct ("duck",   "Sidechain",  0.5f));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "preview", 1 }, "Preview Play", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "synthOn", 1 }, "Internal Synth", true));

    return { p.begin(), p.end() };
}

//==============================================================================
LumenBasslineProcessor::LumenBasslineProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenBassline", createLayout())
{
    for (auto* id : regenParams) apvts.addParameterListener (id, this);
    std::fill (std::begin (noteOnPitch), std::end (noteOnPitch), -1);
    std::fill (std::begin (noteSounding), std::end (noteSounding), false);
    seed = (uint32_t) seedRandom.nextInt (999999) + 1;
    generateNew();
}

LumenBasslineProcessor::~LumenBasslineProcessor()
{
    for (auto* id : regenParams) apvts.removeParameterListener (id, this);
    cancelPendingUpdate();
}

bool LumenBasslineProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void LumenBasslineProcessor::prepareToPlay (double sampleRate, int)
{
    synth.prepare (sampleRate);
    std::fill (std::begin (noteSounding), std::end (noteSounding), false);
    lastPpqEnd = -1.0;
}

//==============================================================================
lb::GenParams LumenBasslineProcessor::currentGenParams() const
{
    auto f = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    lb::GenParams g;
    g.style       = (int) f ("style");
    g.bars        = barsValues[juce::jlimit (0, 3, (int) f ("bars"))];
    g.range       = (int) f ("range");
    g.density     = f ("density");
    g.syncopation = f ("sync");
    g.kickAvoid   = f ("kickAvoid");
    g.octaveJump  = f ("octJump");
    g.glide       = f ("glide");
    g.ghost       = f ("ghost");
    g.variation   = f ("variation");
    g.accent      = f ("accent");
    g.humanize    = f ("humanize");
    g.fill        = f ("fill") > 0.5f;
    return g;
}

void LumenBasslineProcessor::publishPattern (const lb::Pattern& p, bool pushHistory)
{
    {
        const juce::SpinLock::ScopedLockType sl (patternLock);
        pattern = p;
    }
    if (pushHistory)
    {
        if (historyIndex < (int) history.size() - 1)
            history.erase (history.begin() + historyIndex + 1, history.end());
        history.push_back (p);
        if (history.size() > 64) history.erase (history.begin());
        historyIndex = (int) history.size() - 1;
    }
    else if (historyIndex >= 0 && historyIndex < (int) history.size())
        history[(size_t) historyIndex] = p;

    ++patternVersion;
}

void LumenBasslineProcessor::generateNew()
{
    seed = (uint32_t) seedRandom.nextInt (999999) + 1;
    lb::Pattern out, cur = getPattern();
    lb::generate (out, cur, currentGenParams(), seed);
    publishPattern (out, true);
}

void LumenBasslineProcessor::regenerateSameSeed()
{
    lb::Pattern out, cur = getPattern();
    lb::generate (out, cur, currentGenParams(), seed);
    publishPattern (out, false);
}

void LumenBasslineProcessor::mutatePattern()
{
    auto p = getPattern();
    const auto g = currentGenParams();
    lb::mutate (p, g, (uint32_t) seedRandom.nextInt (999999) + 1, juce::jmax (0.2f, g.variation));
    publishPattern (p, true);
}

void LumenBasslineProcessor::historyBack()
{
    if (! canGoBack()) return;
    --historyIndex;
    const juce::SpinLock::ScopedLockType sl (patternLock);
    pattern = history[(size_t) historyIndex];
    ++patternVersion;
}

void LumenBasslineProcessor::historyForward()
{
    if (! canGoForward()) return;
    ++historyIndex;
    const juce::SpinLock::ScopedLockType sl (patternLock);
    pattern = history[(size_t) historyIndex];
    ++patternVersion;
}

void LumenBasslineProcessor::clearLocks()
{
    auto p = getPattern();
    for (int i = 0; i < p.numNotes; ++i) p.notes[(size_t) i].locked = false;
    publishPattern (p, false);
}

lb::Pattern LumenBasslineProcessor::getPattern() const
{
    const juce::SpinLock::ScopedLockType sl (patternLock);
    return pattern;
}

void LumenBasslineProcessor::setPatternFromEditor (const lb::Pattern& p) { publishPattern (p, false); }

void LumenBasslineProcessor::parameterChanged (const juce::String& id, float)
{
    if (loadingState.load()) return;
    const bool structural = (id == "style" || id == "bars");
    if (structural || apvts.getRawParameterValue ("liveRegen")->load() > 0.5f)
    {
        pendingRegen = true;
        triggerAsyncUpdate();
    }
}

void LumenBasslineProcessor::handleAsyncUpdate()
{
    if (pendingRegen.exchange (false))
        regenerateSameSeed();
}

//==============================================================================
int LumenBasslineProcessor::pitchForNote (const lb::Note& n, bool includeFollow) const
{
    auto f = [this] (const char* id) { return (int) apvts.getRawParameterValue (id)->load(); };
    const int key = f ("key"), sc = f ("scale"), oct = f ("octave");
    const auto& prog = lb::progression (f ("progression"));
    const int bar = n.step / lb::stepsPerBar;
    const int rootDeg = prog.degrees[bar % prog.length];

    // progression moves the whole bar to a new root: degree relative to that root,
    // pulled down an octave if the new root is high so the bass stays low.
    int semis = lb::degreeToSemis (n.degree + rootDeg, sc);
    if (rootDeg >= 4) semis -= 12;
    // root sits in the 41-78 Hz sweet spot (E1..D#2) at octave 0
    int rootMidi = 24 + key;
    if (rootMidi < 28) rootMidi += 12;
    int pitch = rootMidi + 12 * oct + semis + n.chroma;
    if (includeFollow) pitch += followShift.load();
    while (pitch < 12) pitch += 12;
    while (pitch > 108) pitch -= 12;
    return pitch;
}

BassSynth::Settings LumenBasslineProcessor::currentSynthSettings() const
{
    auto f = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    BassSynth::Settings s;
    s.sound = (int) f ("sound");
    s.cutoff = f ("cutoff"); s.reso = f ("reso"); s.envAmt = f ("envAmt"); s.decay = f ("decay");
    s.drive = f ("drive"); s.sub = f ("sub"); s.glideMs = f ("glideTime"); s.volume = f ("volume");
    s.drums = f ("drums"); s.duck = f ("duck"); s.drumsOn = f ("drumsOn") > 0.5f;
    return s;
}

//==============================================================================
void LumenBasslineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    // --- incoming MIDI: chord-follow root detection (input is not passed through)
    const bool follow = apvts.getRawParameterValue ("follow")->load() > 0.5f;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())       heldInput[m.getNoteNumber()]++;
        else if (m.isNoteOff()) heldInput[m.getNoteNumber()] = juce::jmax (0, heldInput[m.getNoteNumber()] - 1);
    }
    midi.clear();
    if (follow)
    {
        const int key = (int) apvts.getRawParameterValue ("key")->load();
        for (int n = 0; n < 128; ++n)
            if (heldInput[n] > 0)
            {
                int shift = ((n - key) % 12 + 12) % 12;
                if (shift > 6) shift -= 12;
                followShift = shift;
                break;
            }
    }
    else followShift = 0;

    // --- pick up pattern edits
    bool patternChanged = false;
    if (audioPatternVersion != patternVersion.load())
    {
        const juce::SpinLock::ScopedTryLockType tl (patternLock);
        if (tl.isLocked())
        {
            audioPattern = pattern;
            audioPatternVersion = patternVersion.load();
            patternChanged = true;
        }
    }

    // --- transport
    double bpm = 124.0, hostPpq = 0.0;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) hostPpq = *q;
            hostPlaying = pos->getIsPlaying();
        }
    lastBpm = bpm;

    const bool preview = apvts.getRawParameterValue ("preview")->load() > 0.5f;
    const bool playing = hostPlaying || preview;
    const double ppqPerSample = bpm / 60.0 / getSampleRate();
    double ppqStart = hostPlaying ? hostPpq : internalPpq;
    const double ppqEnd = ppqStart + ppqPerSample * numSamples;

    const bool synthOn = apvts.getRawParameterValue ("synthOn")->load() > 0.5f;
    synth.setSettings (currentSynthSettings());

    auto allNotesOff = [&] (int sampleOffset)
    {
        for (int i = 0; i < lb::maxNotes; ++i)
            if (noteSounding[i])
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, noteOnPitch[i]), sampleOffset);
                noteSounding[i] = false;
            }
        synth.allOff();
    };

    const bool jumped = lastPpqEnd >= 0.0 && std::abs (ppqStart - lastPpqEnd) > ppqPerSample * 4.0;
    if (! playing || patternChanged || jumped || (playing && ! wasPlaying))
        allNotesOff (0);

    if (! playing)
    {
        if (! hostPlaying) internalPpq = 0.0;
        wasPlaying = false;
        lastPpqEnd = -1.0;
        playingNow = false;
        playheadStep = -1.0f;
        if (synthOn)
            synth.render (buffer.getWritePointer (0), buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0), numSamples);
        return;
    }
    wasPlaying = true;
    playingNow = true;

    // --- schedule events in this block
    struct Ev { int sample; int type; int note; }; // type: 0 off, 1 on, 2 kick, 3 hat
    Ev evs[1024];
    int numEvs = 0;
    auto push = [&] (double ppq, int type, int note)
    {
        if (numEvs >= 1024) return;
        const int s = juce::jlimit (0, numSamples - 1, (int) ((ppq - ppqStart) / ppqPerSample));
        evs[numEvs++] = { s, type, note };
    };

    const double loopLen = audioPattern.bars * 4.0;
    const float swing = juce::jlimit (0.0f, 1.0f, apvts.getRawParameterValue ("swing")->load()
                                                   + lb::style ((int) apvts.getRawParameterValue ("style")->load()).swing);
    const float gateAmt = 0.3f + 0.7f * apvts.getRawParameterValue ("gate")->load();
    auto stepPpq = [swing] (int step) { return step * 0.25 + ((step & 1) ? swing * 0.125 : 0.0); };

    const int kFirst = (int) std::floor (ppqStart / loopLen) - 1;
    const int kLast  = (int) std::floor (ppqEnd / loopLen);

    for (int i = 0; i < audioPattern.numNotes; ++i)
    {
        const auto& n = audioPattern.notes[(size_t) i];
        const double on = stepPpq (n.step);
        double off;
        if (n.slide) off = stepPpq (n.step + n.len) + 0.03;
        else         off = on + juce::jmax (0.06, n.len * 0.25 * gateAmt);

        for (int k = kFirst; k <= kLast; ++k)
        {
            const double a = k * loopLen + on, b = k * loopLen + off;
            if (a >= ppqStart && a < ppqEnd) push (a, 1, i);
            if (b >= ppqStart && b < ppqEnd) push (b, 0, i);
        }
    }
    // preview groove: 4x4 kick + offbeat hats
    for (double q = std::ceil (ppqStart * 2.0) / 2.0; q < ppqEnd; q += 0.5)
        push (q, (std::fmod (q, 1.0) < 0.25) ? 2 : 3, -1);

    std::sort (evs, evs + numEvs, [] (const Ev& a, const Ev& b)
    {
        if (a.sample != b.sample) return a.sample < b.sample;
        return a.type < b.type; // offs before ons at the same instant
    });

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);
    int rendered = 0;

    for (int e = 0; e < numEvs; ++e)
    {
        const auto& ev = evs[e];
        if (synthOn && ev.sample > rendered)
        {
            synth.render (L + rendered, R + rendered, ev.sample - rendered);
            rendered = ev.sample;
        }
        if (ev.type == 2) { synth.triggerKick(); continue; }
        if (ev.type == 3) { synth.triggerHat();  continue; }

        const auto& n = audioPattern.notes[(size_t) ev.note];
        if (ev.type == 1)
        {
            if (noteSounding[ev.note])
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, noteOnPitch[ev.note]), ev.sample);
                noteSounding[ev.note] = false;
            }
            bool legato = false;
            for (int j = 0; j < audioPattern.numNotes; ++j) if (noteSounding[j]) { legato = true; break; }

            const int pitch = pitchForNote (n, true);
            noteOnPitch[ev.note] = pitch;
            noteSounding[ev.note] = true;
            midi.addEvent (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) n.vel), ev.sample);
            synth.noteOn (pitch, n.vel / 127.0f, legato, n.accent);
        }
        else if (noteSounding[ev.note])
        {
            midi.addEvent (juce::MidiMessage::noteOff (1, noteOnPitch[ev.note]), ev.sample);
            synth.noteOff (noteOnPitch[ev.note]);
            noteSounding[ev.note] = false;
        }
    }
    if (synthOn && rendered < numSamples)
        synth.render (L + rendered, R + rendered, numSamples - rendered);

    playheadStep = (float) (std::fmod (ppqStart, loopLen) * 4.0);
    lastPpqEnd = ppqEnd;
    if (! hostPlaying) internalPpq = ppqEnd;
}

//==============================================================================
juce::File LumenBasslineProcessor::writeMidiFile (const juce::File& target) const
{
    const auto pat = getPattern();
    const int tpq = 960;
    const float gateAmt = 0.3f + 0.7f * apvts.getRawParameterValue ("gate")->load();
    const float swing = juce::jlimit (0.0f, 1.0f, apvts.getRawParameterValue ("swing")->load()
                                                   + lb::style ((int) apvts.getRawParameterValue ("style")->load()).swing);
    auto stepTick = [&] (int step) { return (step * 0.25 + ((step & 1) ? swing * 0.125 : 0.0)) * tpq; };

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / getBpm())), 0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Lumen Bassline"), 0);

    for (int i = 0; i < pat.numNotes; ++i)
    {
        const auto& n = pat.notes[(size_t) i];
        const int pitch = pitchForNote (n, false);
        const double on = stepTick (n.step);
        const double off = n.slide ? stepTick (n.step + n.len) + tpq * 0.03
                                   : on + juce::jmax (0.06, n.len * 0.25 * gateAmt) * tpq;
        seq.addEvent (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) n.vel), on);
        seq.addEvent (juce::MidiMessage::noteOff (1, pitch), off);
    }
    seq.updateMatchedPairs();
    seq.sort();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (seq);
    target.deleteFile();
    if (auto out = target.createOutputStream())
        mf.writeTo (*out);
    return target;
}

juce::File LumenBasslineProcessor::writeTempMidi() const
{
    const auto keyName = keyNames[(int) apvts.getRawParameterValue ("key")->load()].replace ("#", "sharp");
    const juce::String styleName = lb::style ((int) apvts.getRawParameterValue ("style")->load()).name;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("LumenBassline");
    dir.createDirectory();
    auto name = juce::File::createLegalFileName ("Lumen Bassline " + styleName + " " + keyName + " " + juce::String (seed) + ".mid");
    return writeMidiFile (dir.getChildFile (name));
}

//==============================================================================
void LumenBasslineProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    auto xml = state.createXml();
    auto* pat = xml->createNewChildElement ("Pattern");
    const auto p = getPattern();
    pat->setAttribute ("bars", p.bars);
    pat->setAttribute ("seed", (int) seed);
    for (int i = 0; i < p.numNotes; ++i)
    {
        const auto& n = p.notes[(size_t) i];
        auto* e = pat->createNewChildElement ("N");
        e->setAttribute ("s", n.step); e->setAttribute ("l", n.len); e->setAttribute ("d", n.degree);
        e->setAttribute ("c", n.chroma); e->setAttribute ("v", n.vel);
        e->setAttribute ("f", (n.slide ? 1 : 0) | (n.accent ? 2 : 0) | (n.ghost ? 4 : 0) | (n.locked ? 8 : 0));
    }
    copyXmlToBinary (*xml, dest);
}

void LumenBasslineProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;

    lb::Pattern p;
    bool hasPattern = false;
    if (auto* pat = xml->getChildByName ("Pattern"))
    {
        hasPattern = true;
        p.bars = juce::jlimit (1, lb::maxBars, pat->getIntAttribute ("bars", 4));
        seed = (uint32_t) pat->getIntAttribute ("seed", 1);
        for (auto* e : pat->getChildWithTagNameIterator ("N"))
        {
            lb::Note n;
            n.step = (int16_t) e->getIntAttribute ("s"); n.len = (int16_t) e->getIntAttribute ("l", 1);
            n.degree = (int16_t) e->getIntAttribute ("d"); n.chroma = (int8_t) e->getIntAttribute ("c");
            n.vel = (uint8_t) e->getIntAttribute ("v", 100);
            const int f = e->getIntAttribute ("f");
            n.slide = f & 1; n.accent = f & 2; n.ghost = f & 4; n.locked = f & 8;
            p.add (n);
        }
        xml->removeChildElement (pat, true);
    }

    loadingState = true;
    if (xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
    loadingState = false;
    pendingRegen = false;

    if (hasPattern)
    {
        history.clear(); historyIndex = -1;
        publishPattern (p, true);
    }
}

juce::AudioProcessorEditor* LumenBasslineProcessor::createEditor() { return new LumenBasslineEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenBasslineProcessor(); }
