#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lumen::drums;

LumenDrumsProcessor::LumenDrumsProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Main", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Out 2 (Kick)", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 3 (Snare/Clap)", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 4 (Hats)", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 5 (Perc/FX)", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "LumenDrums", createLayout())
{
    for (auto& p : samplePtr) p.store (nullptr);
    for (auto& f : padFlash) f.store (0);
    setCurrentProgram (1);
}

bool LumenDrumsProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < l.outputBuses.size(); ++i)
        if (! l.outputBuses[i].isDisabled() && l.outputBuses[i] != juce::AudioChannelSet::stereo()) return false;
    return l.inputBuses.isEmpty() || l.getMainInputChannelSet().isDisabled();
}

void LumenDrumsProcessor::prepareToPlay (double sampleRate, int maxBlock)
{
    fs = sampleRate;
    for (auto& p : pads) p.prepare (fs);
    master.prepare (fs);
    player.reset();
    for (auto& b : bus) b.setSize (2, juce::jmax (512, maxBlock), false, false, true);
    midiOffs.clear(); midiOffs.reserve (256);
    hits.reserve (256); events.reserve (256);
    dirty.store (true);
}

PadP LumenDrumsProcessor::padParams (int p) const
{
    PadP q;
    q.model = (int) P (IDs::pad (p, "model"));
    const float lv = P (IDs::pad (p, "level"));
    q.level = lv <= -47.9f ? 0.0f : juce::Decibels::decibelsToGain (lv);
    q.pan = P (IDs::pad (p, "pan")); q.tune = P (IDs::pad (p, "tune")); q.decay = P (IDs::pad (p, "decay"));
    q.tone = P (IDs::pad (p, "tone")); q.snap = P (IDs::pad (p, "snap")); q.drive = P (IDs::pad (p, "drive"));
    q.start = P (IDs::pad (p, "start")); q.reverse = P (IDs::pad (p, "rev")) > 0.5f;
    q.choke = (int) P (IDs::pad (p, "choke")); q.out = (int) P (IDs::pad (p, "out"));
    q.useSample = samplePtr[(size_t) p].load() != nullptr;
    return q;
}

void LumenDrumsProcessor::triggerPad (int pad, float vel, int offset, bool fromSeq, juce::MidiBuffer& midiOut, int)
{
    if (! juce::isPositiveAndBelow (pad, kPads)) return;
    const auto& q = pp[(size_t) pad];
    if (q.choke > 0)
        for (int o = 0; o < kPads; ++o)
            if (o != pad && pp[(size_t) o].choke == q.choke) pads[(size_t) o].choke();
    pads[(size_t) pad].trigger (pad, q, vel, samplePtr[(size_t) pad].load());
    padFlash[(size_t) pad].store (vel);
    if (fromSeq)
    {
        const int note = padNote (pad);
        midiOut.addEvent (juce::MidiMessage::noteOn (10, note, juce::jlimit (0.01f, 1.0f, vel)), offset);
        midiOffs.push_back ({ sampleClock + offset + (int64_t) (0.06 * fs), note });
    }
}

void LumenDrumsProcessor::renderSegment (int from, int to)
{
    if (to <= from) return;
    for (int p = 0; p < kPads; ++p)
    {
        const int o = juce::jlimit (0, kAux, pp[(size_t) p].out);
        auto& b = bus[(size_t) o];
        pads[(size_t) p].render (b.getWritePointer (0, from), b.getWritePointer (1, from), to - from);
    }
}

void LumenDrumsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0) return;
    for (auto& b : bus) { if (b.getNumSamples() < n) b.setSize (2, n, false, false, true); b.clear (0, n); }
    bool anySolo = false;
    for (int p = 0; p < kPads; ++p) { pp[(size_t) p] = padParams (p); anySolo |= P (IDs::pad (p, "solo")) > 0.5f; }
    auto muted = [this, anySolo] (int p) { return P (IDs::pad (p, "mute")) > 0.5f || (anySolo && P (IDs::pad (p, "solo")) < 0.5f); };
    // active pattern copy
    const int pi = juce::jlimit (0, kPatterns - 1, (int) P (IDs::pattern));
    if (dirty.load() || pi != audioPatternIndex)
    {
        const juce::SpinLock::ScopedTryLockType l (lock);
        if (l.isLocked()) { audioPattern = patterns[(size_t) pi]; audioPatternIndex = pi; dirty.store (false); }
    }
    double bpm = 120.0, beat = 0.0; bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition())
    {
        if (auto b = pos->getBpm()) bpm = *b;
        playing = pos->getIsPlaying();
        if (auto q = pos->getPpqPosition()) beat = *q; else playing = false;
    }
    bpm = juce::jlimit (20.0, 999.0, bpm);
    hostBpm.store ((float) bpm);
    const double bps = bpm / 60.0 / fs;
    // ---- gather events: MIDI pads / pattern notes, sequencer hits, audition
    events.clear();
    juce::MidiBuffer out;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (! msg.isNoteOn()) continue;
        const int note = msg.getNoteNumber();
        if (note >= kPatternNoteBase && note < kPatternNoteBase + kPatterns)
        {
            if (auto* prm = apvts.getParameter (IDs::pattern)) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (note - kPatternNoteBase)));
            continue;
        }
        const int pad = padForNote (note);
        if (pad >= 0 && ! muted (pad)) events.push_back ({ m.samplePosition, pad, msg.getFloatVelocity(), false });
    }
    player.setSwing (P (IDs::swing));
    if (P (IDs::seqOn) > 0.5f)
    {
        player.process (audioPattern, n, beat, bps, playing, hits, muted);
        const float hum = P (IDs::humanize);
        for (auto& h : hits)
        {
            float v = h.vel;
            if (hum > 0) { humRng ^= humRng << 13; humRng ^= humRng >> 17; humRng ^= humRng << 5; v *= 1.0f - hum * 0.3f * (float) (humRng & 0xffff) / 65535.0f; }
            events.push_back ({ h.offset, h.pad, v, true });
        }
    }
    else player.reset();
    if (const int a = auditionPad.exchange (-1); a >= 0) events.push_back ({ 0, a, auditionVel.load(), false });
    std::stable_sort (events.begin(), events.end(), [] (const Ev& x, const Ev& y) { return x.offset < y.offset; });
    // ---- render between events
    int pos = 0;
    for (auto& e : events)
    {
        const int off = juce::jlimit (0, n - 1, e.offset);
        renderSegment (pos, off); pos = off;
        triggerPad (e.pad, e.vel, off, e.seq, out, n);
    }
    renderSegment (pos, n);
    // MIDI note-offs for the sequencer's output
    for (size_t k = 0; k < midiOffs.size();)
        if (midiOffs[k].at < sampleClock + n) { out.addEvent (juce::MidiMessage::noteOff (10, midiOffs[k].note), (int) juce::jlimit<int64_t> (0, n - 1, midiOffs[k].at - sampleClock)); midiOffs[k] = midiOffs.back(); midiOffs.pop_back(); }
        else ++k;
    // ---- master on the main bus, aux buses raw (unconnected aux outs fold into main)
    const int numOut = getBusCount (false);
    for (int o = 1; o <= kAux; ++o)
    {
        const bool enabled = o < numOut && getBus (false, o) != nullptr && getBus (false, o)->isEnabled();
        if (enabled)
        {
            auto ab = getBusBuffer (buffer, false, o);
            for (int c = 0; c < juce::jmin (2, ab.getNumChannels()); ++c) ab.copyFrom (c, 0, bus[(size_t) o], c, 0, n);
        }
        else for (int c = 0; c < 2; ++c) bus[0].addFrom (c, 0, bus[(size_t) o], c, 0, n);
    }
    master.process (bus[0].getWritePointer (0), bus[0].getWritePointer (1), n, P (IDs::glue), P (IDs::clip), juce::Decibels::decibelsToGain (P (IDs::volume)));
    {
        auto mb = getBusBuffer (buffer, false, 0);
        for (int c = 0; c < juce::jmin (2, mb.getNumChannels()); ++c) mb.copyFrom (c, 0, bus[0], c, 0, n);
    }
    sampleClock += n;
    playStep.store (playing && P (IDs::seqOn) > 0.5f ? player.currentStep() : -1);
    outPeak.store (bus[0].getMagnitude (0, n));
    glueGr.store (master.reduction());
    midi.swapWith (out);
}

// ------------------------------------------------------------------------------------------------
void LumenDrumsProcessor::setPattern (int i, const Pattern& p) { { const juce::SpinLock::ScopedLockType l (lock); patterns[(size_t) juce::jlimit (0, 7, i)] = p; } bumpPatterns(); }
void LumenDrumsProcessor::setStep (int pat, int pad, int step, Step s)
{
    { const juce::SpinLock::ScopedLockType l (lock); patterns[(size_t) pat].tr[(size_t) pad].s[(size_t) juce::jlimit (0, kMaxSteps - 1, step)] = s; }
    bumpPatterns();
}
void LumenDrumsProcessor::setLength (int pat, int pad, int len)
{
    { const juce::SpinLock::ScopedLockType l (lock); patterns[(size_t) pat].tr[(size_t) pad].length = juce::jlimit (1, kMaxSteps, len); }
    bumpPatterns();
}
void LumenDrumsProcessor::selectPattern (int i)
{
    if (auto* prm = apvts.getParameter (IDs::pattern)) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) juce::jlimit (0, 7, i))); prm->endChangeGesture(); }
    bumpPatterns();
}

void LumenDrumsProcessor::applyGenreKit (int genre)
{
    const auto& G = genres()[(size_t) juce::jlimit (0, (int) genres().size() - 1, genre)];
    auto set = [this] (const juce::String& id, float v) { if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id))) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture(); } };
    for (int p = 0; p < kPads; ++p)
    {
        set (IDs::pad (p, "model"), (float) G.model[p]);
        if (samplePtr[(size_t) p].load() != nullptr) continue;      // your own samples keep their settings
        set (IDs::pad (p, "tune"), G.tune[p]);
        set (IDs::pad (p, "decay"), G.decay[p]); set (IDs::pad (p, "tone"), G.tone[p]);
    }
    set (IDs::swing, G.swing);
    set (IDs::genre, (float) genre);
}

void LumenDrumsProcessor::generate()
{
    genSeed = (uint32_t) juce::Random::getSystemRandom().nextInt (1000000) + 1;
    setPattern (currentPattern(), generatePattern ({ (int) P (IDs::genre), P (IDs::complexity), genSeed }));
}
void LumenDrumsProcessor::generateTrack (int pad)
{
    auto cur = getPattern (currentPattern());
    const auto fresh = generatePattern ({ (int) P (IDs::genre), P (IDs::complexity), (uint32_t) juce::Random::getSystemRandom().nextInt (1000000) + 1 });
    cur.tr[(size_t) pad] = fresh.tr[(size_t) pad];
    setPattern (currentPattern(), cur);
}
void LumenDrumsProcessor::generateSong()
{
    genSeed = (uint32_t) juce::Random::getSystemRandom().nextInt (1000000) + 1;
    const auto song = lumen::drums::generateSong ({ (int) P (IDs::genre), P (IDs::complexity), genSeed });
    { const juce::SpinLock::ScopedLockType l (lock); patterns = song; }
    applyGenreKit ((int) P (IDs::genre));
    bumpPatterns();
}
void LumenDrumsProcessor::fill() { auto p = getPattern (currentPattern()); addFill (p, (uint32_t) juce::Random::getSystemRandom().nextInt (100000)); setPattern (currentPattern(), p); }
void LumenDrumsProcessor::variation() { auto p = getPattern (currentPattern()); vary (p, { (int) P (IDs::genre), P (IDs::complexity), (uint32_t) juce::Random::getSystemRandom().nextInt (100000) + 1 }); setPattern (currentPattern(), p); }
void LumenDrumsProcessor::clearPattern() { Pattern p; setPattern (currentPattern(), p); }
void LumenDrumsProcessor::copyPatternTo (int dest) { setPattern (dest, getPattern (currentPattern())); }

void LumenDrumsProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    const auto& pr = factoryPresets()[(size_t) index];
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            // pads holding your samples keep their tune / decay / tone / level
            bool keep = false;
            for (int p = 0; p < kPads && ! keep; ++p)
                if (samplePtr[(size_t) p].load() != nullptr && rp->paramID.startsWith ("p" + juce::String (p) + "_") && ! rp->paramID.endsWith ("_model")) keep = true;
            if (keep) continue;
            rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->getDefaultValue()); rp->endChangeGesture();
        }
    if (pr.genre >= 0)
    {
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (IDs::complexity))) rp->setValueNotifyingHost (rp->convertTo0to1 (pr.complexity));
        applyGenreKit (pr.genre);
        genSeed = pr.seed;
        const auto song = lumen::drums::generateSong ({ pr.genre, pr.complexity, pr.seed });
        { const juce::SpinLock::ScopedLockType l (lock); patterns = song; }
        selectPattern (2);    // start on the drop
    }
    else { const juce::SpinLock::ScopedLockType l (lock); patterns = {}; }
    // loaded samples stay (they are your sounds); the X button on the pad removes one
    bumpPatterns();
    ++programVersion;
}

// ------------------------------------------------------------------------------------------------
bool LumenDrumsProcessor::loadSample (int pad, const juce::File& f, juce::String& error)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { error = "Can't read this file (WAV / AIFF / FLAC / MP3)"; return false; }
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 10.0));
    if (len < 8) { error = "File is empty"; return false; }
    auto sd = std::make_shared<SampleData>();
    sd->buf.setSize (2, len + 2); sd->buf.clear();
    juce::AudioBuffer<float> tmp ((int) juce::jmax (1u, r->numChannels), len);
    r->read (&tmp, 0, len, 0, true, true);
    for (int c = 0; c < 2; ++c) sd->buf.copyFrom (c, 0, tmp, juce::jmin (c, tmp.getNumChannels() - 1), 0, len);
    sd->rate = r->sampleRate; sd->name = f.getFileNameWithoutExtension();
    {
        const juce::ScopedLock sl (sampleLock);
        if (samples[(size_t) pad] != nullptr) retired.push_back (samples[(size_t) pad]);
        samples[(size_t) pad] = sd;
        samplePtr[(size_t) pad].store (sd.get());
        while (retired.size() > 12) retired.erase (retired.begin());
    }
    // a fresh sample plays as recorded: full length, original pitch, no tone filter
    auto set = [this] (const juce::String& id, float v) { if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id))) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture(); } };
    set (IDs::pad (pad, "decay"), 1.0f); set (IDs::pad (pad, "tune"), 0.0f); set (IDs::pad (pad, "tone"), 0.0f); set (IDs::pad (pad, "start"), 0.0f);
    ++programVersion;
    return true;
}
void LumenDrumsProcessor::clearSample (int pad)
{
    const juce::ScopedLock sl (sampleLock);
    if (samples[(size_t) pad] != nullptr) retired.push_back (samples[(size_t) pad]);
    samples[(size_t) pad] = nullptr; samplePtr[(size_t) pad].store (nullptr);
    while (retired.size() > 12) retired.erase (retired.begin());
}
juce::String LumenDrumsProcessor::sampleName (int pad) const { const juce::ScopedLock sl (sampleLock); return samples[(size_t) pad] != nullptr ? samples[(size_t) pad]->name : juce::String(); }
std::shared_ptr<SampleData> LumenDrumsProcessor::sampleFor (int pad) const { const juce::ScopedLock sl (sampleLock); return samples[(size_t) pad]; }

// ------------------------------------------------------------------------------------------------
juce::MidiFile LumenDrumsProcessor::renderMidi (int bars) const
{
    const auto pat = getPattern (currentPattern());
    juce::MidiMessageSequence seq;
    const double swing = P (IDs::swing);
    const int steps = bars * 16;
    for (int k = 0; k < steps; ++k)
    {
        const double b0 = k * 0.25 + ((k & 1) ? 0.25 * swing / 3.0 : 0.0);
        for (int t = 0; t < kPads; ++t)
        {
            if (P (IDs::pad (t, "mute")) > 0.5f) continue;
            const auto& tr = pat.tr[(size_t) t];
            const auto& s = tr.s[(size_t) (k % std::max (1, tr.length))];
            if (s.vel == 0) continue;
            const int r = juce::jlimit (1, 4, (int) s.ratchet);
            for (int j = 0; j < r; ++j)
            {
                const double tb = b0 + 0.25 * j / r;
                auto on = juce::MidiMessage::noteOn (10, padNote (t), (juce::uint8) juce::jlimit (1, 127, (int) (s.vel * (j == 0 ? 1.0f : 0.85f))));
                on.setTimeStamp (tb * 960.0); seq.addEvent (on);
                auto off = juce::MidiMessage::noteOff (10, padNote (t)); off.setTimeStamp ((tb + 0.25 / r * 0.9) * 960.0); seq.addEvent (off);
            }
        }
    }
    seq.updateMatchedPairs();
    juce::MidiFile mf; mf.setTicksPerQuarterNote (960);
    auto tempo = juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / hostBpm.load())); tempo.setTimeStamp (0);
    seq.addEvent (tempo); seq.sort(); seq.updateMatchedPairs();
    mf.addTrack (seq);
    return mf;
}

juce::AudioBuffer<float> LumenDrumsProcessor::renderAudio (int bars, double rate, double bpm)
{
    const auto pat = getPattern (currentPattern());
    std::array<Pad, kPads> rp; for (auto& p : rp) p.prepare (rate);
    Player pl; pl.reset(); pl.setSwing (P (IDs::swing));
    Master m; m.prepare (rate);
    std::array<PadP, kPads> q {}; for (int p = 0; p < kPads; ++p) { q[(size_t) p] = padParams (p); q[(size_t) p].out = 0; }
    std::array<std::shared_ptr<SampleData>, kPads> smp; for (int p = 0; p < kPads; ++p) smp[(size_t) p] = sampleFor (p);
    const double bps = bpm / 60.0 / rate;
    const int len = (int) std::ceil (bars * 4.0 / bps), tail = (int) (rate * 1.0);
    juce::AudioBuffer<float> outB (2, len + tail); outB.clear();
    std::vector<Hit> h;
    bool anySolo = false; for (int p = 0; p < kPads; ++p) anySolo |= P (IDs::pad (p, "solo")) > 0.5f;
    auto muted = [this, anySolo] (int p) { return P (IDs::pad (p, "mute")) > 0.5f || (anySolo && P (IDs::pad (p, "solo")) < 0.5f); };
    for (int pos = 0; pos < len + tail; pos += 256)
    {
        const int n = std::min (256, len + tail - pos);
        pl.process (pat, n, pos * bps, bps, pos < len, h, muted);
        int at = 0;
        float* L = outB.getWritePointer (0, pos); float* R = outB.getWritePointer (1, pos);
        for (auto& e : h)
        {
            if (pos + e.offset >= len) continue;
            for (auto& p : rp) p.render (L + at, R + at, e.offset - at);
            at = e.offset;
            const auto& qq = q[(size_t) e.pad];
            if (qq.choke > 0) for (int o = 0; o < kPads; ++o) if (o != e.pad && q[(size_t) o].choke == qq.choke) rp[(size_t) o].choke();
            rp[(size_t) e.pad].trigger (e.pad, qq, e.vel, smp[(size_t) e.pad].get());
        }
        for (auto& p : rp) p.render (L + at, R + at, n - at);
        m.process (L, R, n, P (IDs::glue), P (IDs::clip), juce::Decibels::decibelsToGain (P (IDs::volume)));
    }
    // loop-ready: the ring-out of the last hits is folded back onto the start, then the file is cut on the bar line
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < tail && i < len; ++i) outB.setSample (c, i, juce::jlimit (-1.0f, 1.0f, outB.getSample (c, i) + outB.getSample (c, len + i)));
    outB.setSize (2, len, true);
    return outB;
}

juce::File LumenDrumsProcessor::exportMidi (int bars)
{
    auto mf = renderMidi (bars);
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Lumen Drums");
    dir.createDirectory();
    const juce::String g = genres()[(size_t) juce::jlimit (0, (int) genres().size() - 1, (int) P (IDs::genre))].name;
    auto f = dir.getNonexistentChildFile ("Lumen_Drums_" + g.replace (" ", "") + "_" + patternNames()[currentPattern()] + "_" + juce::String (bars) + "bar", ".mid");
    juce::FileOutputStream os (f);
    if (! os.openedOk()) return {};
    mf.writeTo (os);
    return f;
}

juce::File LumenDrumsProcessor::exportWav (int bars)
{
    const double bpm = hostBpm.load();
    auto b = renderAudio (bars, fs, bpm);
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Lumen Drums");
    dir.createDirectory();
    const juce::String g = genres()[(size_t) juce::jlimit (0, (int) genres().size() - 1, (int) P (IDs::genre))].name;
    auto f = dir.getNonexistentChildFile ("Lumen_Drums_" + g.replace (" ", "") + "_" + patternNames()[currentPattern()] + "_" + juce::String (juce::roundToInt (bpm)) + "bpm_" + juce::String (bars) + "bar", ".wav");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return {};
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (fs).withNumChannels (2).withBitsPerSample (24));
    if (w == nullptr) return {};
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return f;
}

// ------------------------------------------------------------------------------------------------
void LumenDrumsProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenDrumsProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenDrumsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    st.setProperty ("program", currentProgram, nullptr);
    st.setProperty ("seed", (int) genSeed, nullptr);
    {
        const juce::SpinLock::ScopedLockType l (lock);
        for (int i = 0; i < kPatterns; ++i) st.setProperty ("pat" + juce::String (i), patternToString (patterns[(size_t) i]), nullptr);
    }
    // samples embedded (16-bit WAV, base64) so the song opens anywhere
    for (int p = 0; p < kPads; ++p)
    {
        auto s = sampleFor (p);
        const juce::Identifier id ("smp" + juce::String (p));
        if (s == nullptr) { st.removeProperty (id, nullptr); continue; }
        juce::MemoryBlock mb;
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> os (new juce::MemoryOutputStream (mb, false));
            if (auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (s->rate).withNumChannels (2).withBitsPerSample (16)))
                w->writeFromAudioSampleBuffer (s->buf, 0, s->buf.getNumSamples() - 2);
        }
        st.setProperty (id, mb.toBase64Encoding(), nullptr);
        st.setProperty ("smpname" + juce::String (p), s->name, nullptr);
    }
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenDrumsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            currentProgram = (int) vt.getProperty ("program", 0);
            genSeed = (uint32_t) (int) vt.getProperty ("seed", 1);
            {
                const juce::SpinLock::ScopedLockType l (lock);
                for (int i = 0; i < kPatterns; ++i)
                    if (vt.hasProperty ("pat" + juce::String (i))) patterns[(size_t) i] = patternFromString (vt.getProperty ("pat" + juce::String (i)).toString());
            }
            for (int p = 0; p < kPads; ++p)
            {
                const juce::Identifier id ("smp" + juce::String (p));
                if (! vt.hasProperty (id)) { clearSample (p); continue; }
                juce::MemoryBlock mb; mb.fromBase64Encoding (vt.getProperty (id).toString());
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (new juce::MemoryInputStream (mb, false), true));
                if (r == nullptr) { clearSample (p); continue; }
                auto sd = std::make_shared<SampleData>();
                const int len = (int) r->lengthInSamples;
                sd->buf.setSize (2, len + 2); sd->buf.clear();
                r->read (&sd->buf, 0, len, 0, true, true);
                sd->rate = r->sampleRate; sd->name = vt.getProperty ("smpname" + juce::String (p)).toString();
                const juce::ScopedLock sl (sampleLock);
                if (samples[(size_t) p] != nullptr) retired.push_back (samples[(size_t) p]);
                samples[(size_t) p] = sd; samplePtr[(size_t) p].store (sd.get());
            }
            for (int i = 0; i < kPatterns; ++i) vt.removeProperty ("pat" + juce::String (i), nullptr);
            for (int p = 0; p < kPads; ++p) { vt.removeProperty ("smp" + juce::String (p), nullptr); vt.removeProperty ("smpname" + juce::String (p), nullptr); }
            apvts.replaceState (vt);
            for (auto* prm : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                {
                    auto child = vt.getChildWithProperty ("id", rp->paramID);
                    if (child.isValid() && child.hasProperty ("value")) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
                }
            bumpPatterns(); ++programVersion;
        }
}

juce::AudioProcessorEditor* LumenDrumsProcessor::createEditor() { return new LumenDrumsEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenDrumsProcessor(); }
