#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lumen;

LumenKickProcessor::LumenKickProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("Lumen Drum Split"),
      apvts (*this, nullptr, "LumenKick", createLayout())
{
    formats.registerBasicFormats();
}

LumenKickProcessor::~LumenKickProcessor()
{
    signalThreadShouldExit();
    stopThread (8000);
}

bool LumenKickProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) && layouts.getMainInputChannelSet().isDisabled();
}

kick::Params LumenKickProcessor::readParams() const
{
    kick::Params p;
    p.tuneHz = P (IDs::tune); p.keyTrack = P (IDs::keyTrack) > 0.5f;
    p.pitchSt = P (IDs::pitch); p.pitchMs = P (IDs::pitchTime); p.pitchCurve = P (IDs::pitchCurve);
    p.attackMs = P (IDs::attack); p.holdMs = P (IDs::hold); p.decayMs = P (IDs::decay); p.decayCurve = P (IDs::decayCurve);
    p.shape = P (IDs::shape); p.phaseDeg = P (IDs::phase); p.subDb = P (IDs::subLvl);
    p.clickOn = P (IDs::clickOn) > 0.5f; p.clickType = (int) P (IDs::clickType); p.clickDb = P (IDs::clickLvl); p.clickMs = P (IDs::clickDecay); p.clickTone = P (IDs::clickTone);
    p.sampleOn = P (IDs::smpOn) > 0.5f; p.sampleDb = P (IDs::smpLvl); p.sampleSt = P (IDs::smpPitch); p.sampleStartMs = P (IDs::smpStart); p.sampleDecayMs = P (IDs::smpDecay);
    p.sampleHp = P (IDs::smpHp); p.sampleTrack = P (IDs::smpTrack) > 0.5f;
    p.driveDb = P (IDs::drive); p.punch = P (IDs::punch); p.lowDb = P (IDs::low); p.highCut = P (IDs::highCut); p.clip = P (IDs::clip) > 0.5f;
    p.velSens = P (IDs::vel); p.outDb = P (IDs::out);
    return p;
}

void LumenKickProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    synth.prepare (fs);
    synth.setParams (readParams());
}

void LumenKickProcessor::setParam (const juce::String& id, float v)
{
    if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
    { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture(); }
}

void LumenKickProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (rp->paramID.startsWith ("split_")) continue;
            float v = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : factoryPresets()[(size_t) index].v) if (rp->paramID == kv.first) v = kv.second;
            rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture();
        }
    ++programVersion;
}

void LumenKickProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0 || buffer.getNumChannels() == 0) return;
    synth.setParams (readParams());
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (juce::jmin (1, buffer.getNumChannels() - 1));
    float tmp[512];
    const bool mono = buffer.getNumChannels() < 2;
    int pos = 0;
    auto run = [&] (int to)
    {
        while (pos < to)
        {
            const int len = juce::jmin (512, to - pos);
            synth.render (L + pos, mono ? tmp : R + pos, len);
            pos += len;
        }
    };
    if (auditionPending.exchange (false)) synth.noteOn (60, 1.0f);
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            run (juce::jlimit (0, n, meta.samplePosition));
            synth.noteOn (m.getNoteNumber(), m.getFloatVelocity());
            lastNote.store (m.getNoteNumber());
        }
    }
    run (n);
    // stem preview
    const int pc = previewCls.load();
    if (pc >= 0)
        if (auto sp = getSplit())
        {
            const auto& st = sp->stems[pc];
            const double ratio = sp->rate / fs;
            int pp = previewPos.load();
            for (int i = 0; i < n; ++i)
            {
                const int si = (int) (pp * ratio);
                if (si >= st.getNumSamples() - 1) { previewCls.store (-1); break; }
                L[i] += st.getSample (0, si); if (! mono) R[i] += st.getSample (std::min (1, st.getNumChannels() - 1), si);
                ++pp;
            }
            previewPos.store (pp);
        }
    outPeak.store (buffer.getMagnitude (0, n));
}

//==============================================================================
// sample layer
void LumenKickProcessor::setSampleBuffer (const juce::AudioBuffer<float>& b, double rate, const juce::String& name, const juce::String& path)
{
    auto sd = std::make_shared<kick::SampleData>();
    sd->buf.makeCopyOf (b); sd->rate = rate; sd->name = name; sd->path = path;
    synth.setSample (std::move (sd));
    renderHash = 0; ++programVersion;
}

bool LumenKickProcessor::loadSample (const juce::File& f, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    if (r == nullptr) { error = "can't read " + f.getFileName(); return false; }
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 10.0));
    juce::AudioBuffer<float> b ((int) std::min (2u, std::max (1u, r->numChannels)), len);
    r->read (&b, 0, len, 0, true, b.getNumChannels() > 1);
    setSampleBuffer (b, r->sampleRate, f.getFileNameWithoutExtension(), f.getFullPathName());
    return true;
}

void LumenKickProcessor::clearSample() { synth.setSample (nullptr); renderHash = 0; ++programVersion; }

juce::String LumenKickProcessor::cloneFromSample()
{
    auto s = synth.getSample();
    if (s == nullptr) return "load a kick sample first";
    const auto c = kick::cloneKick (*s);
    if (! c.ok) return c.message;
    setParam (IDs::tune, c.tuneHz); setParam (IDs::pitch, c.pitchSt); setParam (IDs::pitchTime, c.pitchMs); setParam (IDs::pitchCurve, 1.0f);
    setParam (IDs::hold, c.holdMs); setParam (IDs::decay, c.decayMs); setParam (IDs::decayCurve, 1.0f); setParam (IDs::attack, 0.0f);
    setParam (IDs::shape, 0.0f);
    // layer: the synth carries the sub, the sample keeps only its top
    setParam (IDs::smpHp, std::max (180.0f, c.tuneHz * 3.5f));
    renderHash = 0; ++programVersion;
    return "CLONED: " + c.message;
}

//==============================================================================
// offline render for the display / export
void LumenKickProcessor::renderIfNeeded()
{
    const auto p = readParams();
    juce::uint64 h = 1469598103934665603ull;
    auto mixIn = [&h] (float v) { juce::uint32 u; std::memcpy (&u, &v, 4); h = (h ^ u) * 1099511628211ull; };
    for (auto* prm : getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) mixIn (rp->getValue());
    const auto s = synth.getSample();
    if (h == renderHash && s.get() == renderSamplePtr && render.getNumSamples() > 0) return;
    renderHash = h; renderSamplePtr = s.get();
    kick::KickSynth k; k.prepare (fs); k.setParams (p); k.setSample (s);
    const int len = (int) (fs * 3.0);
    render.setSize (2, len); render.clear();
    k.noteOn (60, 1.0f);
    int done = 0;
    while (done < len)
    {
        const int b = std::min (1024, len - done);
        k.render (render.getWritePointer (0, done), render.getWritePointer (1, done), b);
        done += b;
        if (! k.isActive()) break;
    }
    // trim the silent tail
    int last = done;
    while (last > 1 && std::abs (render.getSample (0, last - 1)) < 1e-4f && std::abs (render.getSample (1, last - 1)) < 1e-4f) --last;
    render.setSize (2, std::max (64, last + (int) (fs * 0.005)), true, true, false);
    ++renderVer;
}

juce::File LumenKickProcessor::userFolder (const juce::String& sub)
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Lumen Kick Designer").getChildFile (sub);
    d.createDirectory();
    return d;
}

bool LumenKickProcessor::writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double rate)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (b.getNumChannels()).withBitsPerSample (24));
    if (w == nullptr) return false;
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

juce::File LumenKickProcessor::exportKick()
{
    renderIfNeeded();
    const auto p = readParams();
    juce::String nm = currentProgram > 0 ? getProgramName (currentProgram) : juce::String ("Kick");
    nm = nm.replaceCharacters (" /\\:*?\"<>|", "__________") + "_" + noteName (p.tuneHz).upToFirstOccurrenceOf (" ", false, false).replace ("#", "s");
    auto f = userFolder ("Kicks").getNonexistentChildFile ("Lumen_" + nm, ".wav");
    return writeWav (f, render, fs) ? f : juce::File();
}

//==============================================================================
// drum split
bool LumenKickProcessor::startSplit (const juce::File& f, juce::String& error)
{
    if (splitting.load()) { error = "already splitting"; return false; }
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    if (r == nullptr) { error = "can't read " + f.getFileName(); return false; }
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 60.0));
    loop.setSize ((int) std::min (2u, std::max (1u, r->numChannels)), len);
    r->read (&loop, 0, len, 0, true, loop.getNumChannels() > 1);
    loopRate = r->sampleRate; loopName = f.getFileNameWithoutExtension();
    splitProg.store (0); splitDone.store (false); splitting.store (true);
    splitMessage = "splitting...";
    startThread (juce::Thread::Priority::low);
    return true;
}

void LumenKickProcessor::run()
{
    auto res = std::make_shared<split::Result> (split::splitDrums (loop, loopRate, P (IDs::splitSens), P (IDs::splitSep),
                                                                   [this] (float pr) { splitProg.store (pr); return ! threadShouldExit(); }));
    if (res->ok)
    {
        const auto folder = userFolder ("Splits").getChildFile (loopName.replaceCharacters ("/\\:*?\"<>|", "_________"));
        folder.createDirectory();
        static const char* suffix[] { "Kick", "Snare", "Hats", "Perc" };
        for (int c = 0; c < split::kClasses; ++c)
        {
            stemFiles[c] = folder.getChildFile (loopName + "_" + suffix[c] + ".wav");
            writeWav (stemFiles[c], res->stems[c], res->rate);
        }
    }
    pendingSplit = res;
    splitDone.store (true);
}

void LumenKickProcessor::finishSplitIfReady()
{
    if (! splitDone.exchange (false)) return;
    stopThread (100);
    if (pendingSplit != nullptr)
    {
        splitMessage = pendingSplit->ok ? pendingSplit->message : "split failed: " + pendingSplit->message;
        std::atomic_store (&splitRes, std::shared_ptr<const split::Result> (pendingSplit));
        pendingSplit.reset();
    }
    splitting.store (false);
    ++programVersion;
}

void LumenKickProcessor::stemToSampleLayer (int c)
{
    auto sp = getSplit();
    if (sp == nullptr || ! juce::isPositiveAndBelow (c, split::kClasses)) return;
    // take the loudest single hit of that stem (from its onset to the next onset of the same class)
    const auto& st = sp->stems[c];
    int bestS = 0, bestE = std::min (st.getNumSamples(), (int) (sp->rate * 0.6)); float bestPk = 0;
    for (size_t i = 0; i < sp->onsets.size(); ++i)
    {
        if (sp->onsets[i].cls != c) continue;
        const int s0 = std::max (0, sp->onsets[i].sample - (int) (sp->rate * 0.004));
        int e = st.getNumSamples();
        for (size_t j = i + 1; j < sp->onsets.size(); ++j) if (sp->onsets[j].cls == c) { e = sp->onsets[j].sample - (int) (sp->rate * 0.004); break; }
        e = std::min (e, s0 + (int) (sp->rate * 1.5));
        const float pk = st.getMagnitude (s0, std::max (1, std::min (e - s0, (int) (sp->rate * 0.05))));
        if (pk > bestPk) { bestPk = pk; bestS = s0; bestE = e; }
    }
    juce::AudioBuffer<float> hit (st.getNumChannels(), std::max (64, bestE - bestS));
    for (int ch = 0; ch < st.getNumChannels(); ++ch)
    {
        hit.copyFrom (ch, 0, st, ch, bestS, hit.getNumSamples());
        const int fo = std::min (hit.getNumSamples() / 4, (int) (sp->rate * 0.01));
        for (int i = 0; i < fo; ++i) hit.setSample (ch, hit.getNumSamples() - 1 - i, hit.getSample (ch, hit.getNumSamples() - 1 - i) * (float) i / (float) fo);
    }
    setSampleBuffer (hit, sp->rate, loopName + " " + split::className (c), stemFiles[c].getFullPathName());
}

//==============================================================================
void LumenKickProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenKickProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenKickProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    // the sample is stored inside the project (FLAC-free: raw float, max 10 s) so it never goes missing
    if (auto s = synth.getSample())
    {
        juce::MemoryOutputStream mo;
        mo.writeInt (s->buf.getNumChannels()); mo.writeInt (s->buf.getNumSamples()); mo.writeDouble (s->rate);
        for (int c = 0; c < s->buf.getNumChannels(); ++c) mo.write (s->buf.getReadPointer (c), sizeof (float) * (size_t) s->buf.getNumSamples());
        state.setProperty ("sampleData", mo.getMemoryBlock().toBase64Encoding(), nullptr);
        state.setProperty ("sampleName", s->name, nullptr);
        state.setProperty ("samplePath", s->path, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenKickProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            currentProgram = (int) vt.getProperty ("program", 0);
            const juce::String sd = vt.getProperty ("sampleData", "").toString();
            if (sd.isNotEmpty())
            {
                juce::MemoryBlock mb; mb.fromBase64Encoding (sd);
                juce::MemoryInputStream mi (mb, false);
                const int ch = mi.readInt(), ns = mi.readInt(); const double rate = mi.readDouble();
                if (ch > 0 && ch <= 2 && ns > 0 && ns < 48000 * 20)
                {
                    juce::AudioBuffer<float> b (ch, ns);
                    for (int c = 0; c < ch; ++c) mi.read (b.getWritePointer (c), (int) sizeof (float) * ns);
                    setSampleBuffer (b, rate, vt.getProperty ("sampleName", "").toString(), vt.getProperty ("samplePath", "").toString());
                }
            }
            else synth.setSample (nullptr);
            vt.removeProperty ("sampleData", nullptr);
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

juce::AudioProcessorEditor* LumenKickProcessor::createEditor() { return new LumenKickEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenKickProcessor(); }
