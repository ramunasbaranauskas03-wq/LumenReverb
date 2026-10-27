#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lumen::kick2;

struct LumenKickProcessor::Ptrs
{
    std::atomic<float> *root, *keyTrack, *velSens, *pitch, *drvType, *drvAmt, *eqLow, *eqMidF, *eqMidG, *eqHigh, *clip, *out;
    std::atomic<float> *on[3], *solo[3], *inv[3], *filt[3], *syn[3], *smp[3], *spitch[3], *sstart[3], *nudge[3], *att[3], *dec[3], *curve[3];
    std::atomic<float> *lStart, *lPdec, *lDrive, *mFreq, *mStart, *mPdec, *mWave, *mDrive, *tType, *tTone, *tTdec;
};

LumenKickProcessor::LumenKickProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenKick2", createLayout())
{
    formats.registerBasicFormats();
    auto g = [this] (const juce::String& id) { auto* p = apvts.getRawParameterValue (id); jassert (p != nullptr); return p; };
    ptr = std::make_unique<Ptrs>();
    auto& t = *ptr;
    t.root = g (IDs::root); t.keyTrack = g (IDs::keyTrack); t.velSens = g (IDs::velSens); t.pitch = g (IDs::pitch);
    t.drvType = g (IDs::drvType); t.drvAmt = g (IDs::drvAmt); t.eqLow = g (IDs::eqLow); t.eqMidF = g (IDs::eqMidF); t.eqMidG = g (IDs::eqMidG); t.eqHigh = g (IDs::eqHigh);
    t.clip = g (IDs::clip); t.out = g (IDs::out);
    for (int k = 0; k < 3; ++k)
    {
        t.on[k] = g (IDs::L (k, "on")); t.solo[k] = g (IDs::L (k, "solo")); t.inv[k] = g (IDs::L (k, "inv")); t.filt[k] = g (IDs::L (k, "filt"));
        t.syn[k] = g (IDs::L (k, "syn")); t.smp[k] = g (IDs::L (k, "smp")); t.spitch[k] = g (IDs::L (k, "spitch")); t.sstart[k] = g (IDs::L (k, "sstart"));
        t.nudge[k] = g (IDs::L (k, "nudge")); t.att[k] = g (IDs::L (k, "att")); t.dec[k] = g (IDs::L (k, "dec")); t.curve[k] = g (IDs::L (k, "curve"));
    }
    t.lStart = g (IDs::L (2, "start")); t.lPdec = g (IDs::L (2, "pdec")); t.lDrive = g (IDs::L (2, "drive"));
    t.mFreq = g (IDs::L (1, "freq")); t.mStart = g (IDs::L (1, "start")); t.mPdec = g (IDs::L (1, "pdec")); t.mWave = g (IDs::L (1, "wave")); t.mDrive = g (IDs::L (1, "drive"));
    t.tType = g (IDs::L (0, "type")); t.tTone = g (IDs::L (0, "tone")); t.tTdec = g (IDs::L (0, "tdec"));
    for (auto& l : live) l.store (nullptr);
}

LumenKickProcessor::~LumenKickProcessor() = default;

bool LumenKickProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void LumenKickProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate; maxBlock = juce::jmax (32, samplesPerBlock);
    engine.prepare (fs, maxBlock);
    setLatencySamples (engine.latency());
}

Params LumenKickProcessor::readParams() const
{
    const auto& t = *ptr;
    Params p;
    auto db = [] (float v) { return dbToGain (v); };
    p.root = kRootBase + (int) t.root->load(); p.keyTrack = (int) t.keyTrack->load(); p.velSens = t.velSens->load(); p.pitch = t.pitch->load();
    p.drvType = (int) t.drvType->load(); p.drvAmt = t.drvAmt->load();
    p.eqLow = t.eqLow->load(); p.eqMidF = t.eqMidF->load(); p.eqMidG = t.eqMidG->load(); p.eqHigh = t.eqHigh->load();
    p.clip = t.clip->load() > 0.5f; p.outDb = t.out->load();
    for (int k = 0; k < 3; ++k)
    {
        auto& L = p.L[(size_t) k];
        L.on = t.on[k]->load() > 0.5f; L.solo = t.solo[k]->load() > 0.5f; L.invert = t.inv[k]->load() > 0.5f; L.filter = t.filt[k]->load() > 0.5f;
        L.synLvl = db (t.syn[k]->load()); L.smpLvl = db (t.smp[k]->load()); L.smpPitch = t.spitch[k]->load(); L.smpStartMs = t.sstart[k]->load();
        L.nudgeMs = t.nudge[k]->load(); L.attackMs = t.att[k]->load(); L.decayMs = t.dec[k]->load(); L.curve = t.curve[k]->load();
    }
    p.L[Low].startSt = t.lStart->load(); p.L[Low].pDecayMs = t.lPdec->load(); p.L[Low].drive = t.lDrive->load();
    p.L[Mid].freq = t.mFreq->load(); p.L[Mid].startSt = t.mStart->load(); p.L[Mid].pDecayMs = t.mPdec->load(); p.L[Mid].wave = t.mWave->load(); p.L[Mid].drive = t.mDrive->load();
    p.L[Top].type = (int) t.tType->load(); p.L[Top].tone = t.tTone->load(); p.L[Top].tDecayMs = t.tTdec->load();
    return p;
}

void LumenKickProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    ++blockCounter;
    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    buffer.clear();
    if (n == 0 || nch == 0) return;
    const auto p = readParams();
    engine.setParams (p);
    engine.setSamples ({ live[0].load(), live[1].load(), live[2].load() });

    if (auditionReq.exchange (false)) { engine.noteOn (p.root, 1.0f); lastNote.store (p.root); lastHitMs.store (juce::Time::getMillisecondCounter()); }

    float* L = buffer.getWritePointer (0);
    float* R = nch > 1 ? buffer.getWritePointer (1) : nullptr;
    float tmpR[512];
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        while (pos < end)
        {
            const int len = juce::jmin (end - pos, maxBlock, 512);
            engine.render (L + pos, R != nullptr ? R + pos : tmpR, len);
            if (R == nullptr) for (int i = 0; i < len; ++i) L[pos + i] = 0.5f * (L[pos + i] + tmpR[i]);
            pos += len;
        }
    };
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        renderTo (juce::jlimit (0, n, meta.samplePosition));
        if (m.isNoteOn()) { engine.noteOn (m.getNoteNumber(), m.getFloatVelocity()); lastNote.store (m.getNoteNumber()); lastHitMs.store (juce::Time::getMillisecondCounter()); }
    }
    renderTo (n);
    float pk = 0; for (int c = 0; c < nch; ++c) pk = std::max (pk, buffer.getMagnitude (c, 0, n));
    outPk.store (std::max (pk, outPk.load() * 0.9f));
}

//==============================================================================
std::shared_ptr<SampleData> LumenKickProcessor::readFile (const juce::File& f, juce::String& err)
{
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    if (r == nullptr) { err = "Can't read " + f.getFileName(); return nullptr; }
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 4.0));
    if (len < 16) { err = f.getFileName() + " is empty"; return nullptr; }
    auto s = std::make_shared<SampleData>();
    s->rate = r->sampleRate; s->name = f.getFileNameWithoutExtension(); s->file = f;
    const int nch = (int) juce::jlimit (1u, 2u, r->numChannels);
    juce::AudioBuffer<float> raw (nch, len);
    r->read (&raw, 0, len, 0, true, nch > 1);
    // trim the silence before the hit, normalise to -1 dBFS, short fade at the end
    float pk = 0; for (int c = 0; c < nch; ++c) pk = std::max (pk, raw.getMagnitude (c, 0, len));
    if (pk < 1.0e-5f) { err = f.getFileName() + " is silent"; return nullptr; }
    int onset = 0;
    for (int i = 0; i < len; ++i) { bool hit = false; for (int c = 0; c < nch; ++c) hit |= std::abs (raw.getSample (c, i)) > pk * 0.03f; if (hit) { onset = i; break; } }
    onset = std::max (0, onset - (int) (0.0005 * s->rate));
    const int n = len - onset;
    s->full.setSize (nch, n);
    for (int c = 0; c < nch; ++c) s->full.copyFrom (c, 0, raw, c, onset, n);
    s->full.applyGain (0.891f / pk);
    const int fade = std::min (n / 4, (int) (0.005 * s->rate));
    for (int c = 0; c < nch; ++c) s->full.applyGainRamp (c, n - fade, fade, 1.0f, 0.0f);
    splitBands (*s, P (IDs::x1), P (IDs::x2));
    // pitch of the low band (the tail decides how it sits with the bass)
    const int a = (int) (0.12 * s->rate) + s->pad, w = (int) (0.2 * s->rate);
    if (a + w < s->band[Low].getNumSamples()) s->lowHz = detectPitch (s->band[Low].getReadPointer (0) + a, w, s->rate);
    return s;
}

void LumenKickProcessor::publish()
{
    for (int k = 0; k < 3; ++k)
    {
        const auto* old = live[(size_t) k].exchange (owned[(size_t) k].get());
        juce::ignoreUnused (old);
    }
    ++sampleVersion;
}

void LumenKickProcessor::collectGarbage()
{
    // the audio thread picks up the new pointers at its next block; old data is freed a few blocks later
    // (or after 2 s when no audio is running at all)
    const auto now = blockCounter.load();
    const auto ms = juce::Time::getMillisecondCounter();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(), [&] (const Retired& r)
    { return now > r.block + 3 || ms - r.ms > 2000; }), graveyard.end());
}

juce::String LumenKickProcessor::loadKick (const juce::File& f)
{
    juce::String err;
    auto s = readFile (f, err);
    if (s == nullptr) return err;
    for (int k = 0; k < 3; ++k)
    {
        if (owned[(size_t) k] != nullptr) graveyard.push_back ({ owned[(size_t) k], blockCounter.load(), juce::Time::getMillisecondCounter() });
        owned[(size_t) k] = s;
        setParam (IDs::L (k, "syn"), -60.0f);            // pure sample: the three bands add up to the original
        setParam (IDs::L (k, "smp"), 0.0f);
        setParam (IDs::L (k, "filt"), 1.0f);
        setParam (IDs::L (k, "spitch"), 0.0f); setParam (IDs::L (k, "sstart"), 0.0f); setParam (IDs::L (k, "nudge"), 0.0f);
        setParam (IDs::L (k, "att"), 0.0f); setParam (IDs::L (k, "dec"), (float) juce::jmin (3000.0, 1000.0 * s->full.getNumSamples() / s->rate + 5.0));
        setParam (IDs::L (k, "curve"), 0.0f);
    }
    setParam (IDs::drvAmt, 0.0f);
    publish();
    return "Loaded " + s->name + " - split into TOP / MID / LOW" + (s->lowHz > 0 ? " (low end " + hzNote (s->lowHz) + ")" : juce::String());
}

juce::String LumenKickProcessor::loadLayer (int k, const juce::File& f)
{
    juce::String err;
    auto s = readFile (f, err);
    if (s == nullptr) return err;
    if (owned[(size_t) k] != nullptr) graveyard.push_back ({ owned[(size_t) k], blockCounter.load(), juce::Time::getMillisecondCounter() });
    owned[(size_t) k] = s;
    if (P (IDs::L (k, "smp")) <= -59.9f) setParam (IDs::L (k, "smp"), 0.0f);
    const float lenMs = (float) (1000.0 * s->full.getNumSamples() / s->rate);
    if (P (IDs::L (k, "dec")) < lenMs && P (IDs::L (k, "syn")) <= -59.9f) setParam (IDs::L (k, "dec"), juce::jmin (3000.0f, lenMs + 5.0f));
    publish();
    return layerNames()[k] + ": " + s->name;
}

juce::String LumenKickProcessor::stepLayer (int k, int dir)
{
    auto cur = owned[(size_t) k];
    if (cur == nullptr || ! cur->file.existsAsFile()) return "Load a sample first";
    auto files = cur->file.getParentDirectory().findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    if (files.isEmpty()) return {};
    struct Cmp { int compareElements (const juce::File& a, const juce::File& b) const { return a.getFileName().compareNatural (b.getFileName()); } } cmp;
    files.sort (cmp);
    int idx = files.indexOf (cur->file);
    idx = ((idx < 0 ? 0 : idx + dir) % files.size() + files.size()) % files.size();
    return loadLayer (k, files[idx]);
}

void LumenKickProcessor::clearLayer (int k)
{
    if (owned[(size_t) k] != nullptr) graveyard.push_back ({ owned[(size_t) k], blockCounter.load(), juce::Time::getMillisecondCounter() });
    owned[(size_t) k] = nullptr;
    publish();
}

void LumenKickProcessor::resplitIfNeeded()
{
    const float x1 = P (IDs::x1), x2 = P (IDs::x2);
    std::vector<std::pair<SampleData*, std::shared_ptr<SampleData>>> done;
    bool changed = false;
    for (int k = 0; k < 3; ++k)
    {
        auto& o = owned[(size_t) k];
        if (o == nullptr || (std::abs (o->x1 - x1) < 0.5f && std::abs (o->x2 - x2) < 0.5f)) continue;
        std::shared_ptr<SampleData> repl;
        for (auto& d : done) if (d.first == o.get()) repl = d.second;
        if (repl == nullptr)
        {
            repl = std::make_shared<SampleData> (*o);
            splitBands (*repl, x1, x2);
            done.push_back ({ o.get(), repl });
        }
        graveyard.push_back ({ o, blockCounter.load(), juce::Time::getMillisecondCounter() });
        o = repl; changed = true;
    }
    if (changed) publish();
}

juce::String LumenKickProcessor::exportWav (juce::File& written)
{
    juce::AudioBuffer<float> out;
    Engine::renderHit (readParams(), samplePointers(), fs > 0 ? fs : 48000.0, kRootBase + (int) P (IDs::root), out, nullptr);
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Lumen Kick");
    dir.createDirectory();
    const auto name = getProgramName (currentProgram).replaceCharacters ("/\\:*?\"<>|", "---------") + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    written = dir.getChildFile (name + ".wav");
    juce::WavAudioFormat wav;
    written.deleteFile();
    std::unique_ptr<juce::OutputStream> os (written.createOutputStream());
    if (os == nullptr) return "Can't write " + written.getFullPathName();
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (fs > 0 ? fs : 48000.0).withNumChannels (2).withBitsPerSample (24));
    if (w == nullptr) return "Can't write WAV";
    w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    return "Saved " + written.getFileName() + " (Documents / Lumen Kick)";
}

//==============================================================================
void LumenKickProcessor::setParam (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
}

void LumenKickProcessor::setCurrentProgram (int i)
{
    if (! juce::isPositiveAndBelow (i, getNumPrograms())) return;
    currentProgram = i;
    const auto& pre = factoryPresets()[(size_t) i];
    const juce::StringArray keep { IDs::x1, IDs::x2 };
    for (auto* ap : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ap))
        {
            const auto id = rp->getParameterID();
            if (keep.contains (id)) continue;
            // sample settings of loaded layers stay as they are
            if (id.endsWith ("_smp") || id.endsWith ("_spitch") || id.endsWith ("_sstart") || id.endsWith ("_filt")) continue;
            float v = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : pre.v) if (kv.first == id) v = kv.second;
            rp->setValueNotifyingHost (rp->convertTo0to1 (v));
        }
    ++programVersion;
}

void LumenKickProcessor::switchAB (int to)
{
    if (to == abSlot) return;
    getStateInformation (ab[abSlot]);
    abSlot = to;
    if (ab[to].getSize() > 0) setStateInformation (ab[to].getData(), (int) ab[to].getSize());
    ++programVersion;
}
void LumenKickProcessor::copyAB() { getStateInformation (ab[1 - abSlot]); }

juce::AudioProcessorEditor* LumenKickProcessor::createEditor() { return new LumenKickEditor (*this); }

// samples are embedded in the project (compressed), so the kick still works on another computer
void LumenKickProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto st = apvts.copyState();
    st.setProperty ("program", currentProgram, nullptr);
    juce::ValueTree samples ("Samples");
    std::vector<const SampleData*> seen;
    for (int k = 0; k < 3; ++k)
    {
        const auto* s = owned[(size_t) k].get();
        if (s == nullptr) continue;
        int id = -1;
        for (int j = 0; j < (int) seen.size(); ++j) if (seen[(size_t) j] == s) id = j;
        if (id < 0)
        {
            id = (int) seen.size(); seen.push_back (s);
            juce::MemoryOutputStream mo;
            {
                juce::GZIPCompressorOutputStream gz (mo, 6);
                const int nch = s->full.getNumChannels(), n = s->full.getNumSamples();
                gz.writeInt (nch); gz.writeInt (n);
                for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) gz.writeShort ((short) juce::jlimit (-32767, 32767, (int) std::lround (s->full.getSample (c, i) * 32767.0f)));
            }
            juce::ValueTree e ("S");
            e.setProperty ("id", id, nullptr); e.setProperty ("name", s->name, nullptr); e.setProperty ("path", s->file.getFullPathName(), nullptr);
            e.setProperty ("rate", s->rate, nullptr); e.setProperty ("data", mo.getMemoryBlock().toBase64Encoding(), nullptr);
            samples.appendChild (e, nullptr);
        }
        st.setProperty ("layer" + juce::String (k), id, nullptr);
    }
    st.appendChild (samples, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, d);
}

void LumenKickProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    auto samples = tree.getChildWithName ("Samples");
    std::vector<std::shared_ptr<SampleData>> loaded;
    for (int i = 0; i < samples.getNumChildren(); ++i)
    {
        auto e = samples.getChild (i);
        juce::MemoryBlock mb; mb.fromBase64Encoding (e.getProperty ("data").toString());
        juce::MemoryInputStream mi (mb, false);
        juce::GZIPDecompressorInputStream gz (mi);
        const int nch = gz.readInt(), n = gz.readInt();
        if (nch < 1 || nch > 2 || n < 1 || n > 48000 * 8) { loaded.push_back (nullptr); continue; }
        auto s = std::make_shared<SampleData>();
        s->full.setSize (nch, n);
        for (int c = 0; c < nch; ++c) for (int k = 0; k < n; ++k) s->full.setSample (c, k, (float) gz.readShort() / 32767.0f);
        s->rate = (double) e.getProperty ("rate", 48000.0); s->name = e.getProperty ("name").toString(); s->file = juce::File (e.getProperty ("path").toString());
        loaded.push_back (s);
    }
    tree.removeChild (samples, nullptr);
    apvts.replaceState (tree);
    currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty ("program", 0));
    for (auto* ap : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ap))
        {
            auto child = tree.getChildWithProperty ("id", rp->getParameterID());
            if (child.isValid() && child.hasProperty ("value")) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
        }
    for (auto& s : loaded)
        if (s != nullptr)
        {
            splitBands (*s, P (IDs::x1), P (IDs::x2));
            const int a = (int) (0.12 * s->rate) + s->pad, w = (int) (0.2 * s->rate);
            if (a + w < s->band[Low].getNumSamples()) s->lowHz = detectPitch (s->band[Low].getReadPointer (0) + a, w, s->rate);
        }
    for (int k = 0; k < 3; ++k)
    {
        if (owned[(size_t) k] != nullptr) graveyard.push_back ({ owned[(size_t) k], blockCounter.load(), juce::Time::getMillisecondCounter() });
        const int id = (int) tree.getProperty ("layer" + juce::String (k), -1);
        owned[(size_t) k] = juce::isPositiveAndBelow (id, (int) loaded.size()) ? loaded[(size_t) id] : nullptr;
    }
    publish();
    ++programVersion;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenKickProcessor(); }
