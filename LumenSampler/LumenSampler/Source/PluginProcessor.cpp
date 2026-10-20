#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lumen::sampler;

// Integer files are kept as their exact integer codes (left-justified 32-bit), so writing them back through
// FLAC / WAV reproduces the original bit for bit. Returns false if the data is not integer-exact at that depth.
static bool exactInts (const SampleSource& s, int a, int b, int bits, bool rev, std::vector<std::vector<int>>& out)
{
    if (bits < 8 || bits > 24) return false;
    const double scale = (double) (1 << (bits - 1)), lim = scale - 1;
    out.assign ((size_t) s.channels, std::vector<int> ((size_t) (b - a)));
    for (int c = 0; c < s.channels; ++c)
    {
        const float* d = s.data (0, c);
        for (int i = 0; i < b - a; ++i)
        {
            const double v = (double) d[rev ? b - 1 - i : a + i] * scale;
            if (v != std::floor (v) || v < -scale || v > lim) return false;
            out[(size_t) c][(size_t) i] = (int) v * (1 << (32 - bits));
        }
    }
    return true;
}
static bool writeInts (juce::AudioFormatWriter& w, const std::vector<std::vector<int>>& d)
{
    std::vector<const int*> ptr; for (auto& v : d) ptr.push_back (v.data()); ptr.push_back (nullptr);
    return w.write (ptr.data(), (int) d[0].size());
}


LumenSamplerProcessor::LumenSamplerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenSampler", createLayout())
{
    for (auto id : { IDs::sliceMode, IDs::sliceSens, IDs::sliceGrid }) apvts.addParameterListener (id, this);
    for (auto& p : playPos) p.store (0);
    audioSlices.reserve (256);
}
LumenSamplerProcessor::~LumenSamplerProcessor()
{
    cancelPendingUpdate();
    for (auto id : { IDs::sliceMode, IDs::sliceSens, IDs::sliceGrid }) apvts.removeParameterListener (id, this);
}

bool LumenSamplerProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void LumenSamplerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate; maxBlock = std::max (samplesPerBlock, 4096);
    engine.prepare (fs);
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    oversampler->initProcessing ((size_t) maxBlock);
    vPh = 0; holdL = holdR = 0; lpL[0] = lpL[1] = lpR[0] = lpR[1] = 0;
    keyboardState.reset();
}

Params LumenSamplerProcessor::readParams() const
{
    Params p;
    // float parameters with a 0 (or 1) default come back as e.g. -1.2e-7 after normalisation: snap them so the defaults stay exact
    auto Z = [this] (const juce::String& id) { const float v = P (id); return std::abs (v) < 1e-5f ? 0.0f : v; };
    p.mode = (int) P (IDs::mode); p.root = (int) P (IDs::root); p.tune = Z (IDs::tune); p.fine = Z (IDs::fine);
    p.warp = (int) P (IDs::warp); p.sampleBpm = P (IDs::sampleBpm); p.hostBpm = hostBpm.load();
    p.start = P (IDs::start); p.end = P (IDs::end); p.reverse = P (IDs::reverse) > 0.5f;
    p.loop = P (IDs::loop) > 0.5f; p.loopMode = (int) P (IDs::loopMode); p.loopStart = P (IDs::loopStart); p.loopEnd = P (IDs::loopEnd); p.xfadeMs = P (IDs::xfade);
    p.A = Z (IDs::attack); p.D = P (IDs::decay); p.S = P (IDs::sustain) > 0.99999f ? 1.0f : P (IDs::sustain); p.R = P (IDs::release); p.velSens = Z (IDs::velSens);
    p.fOn = P (IDs::fOn) > 0.5f; p.fType = (int) P (IDs::fType); p.cutoff = P (IDs::cutoff); p.res = P (IDs::res); p.fEnv = Z (IDs::fEnv);
    p.fA = P (IDs::fA); p.fD = P (IDs::fD); p.fS = P (IDs::fS); p.fR = P (IDs::fR); p.keyTrack = P (IDs::keyTrack); p.velCut = Z (IDs::velCut);
    for (int i = 0; i < 2; ++i)
    {
        auto& L = p.lfo[i];
        L.shape = (int) P (IDs::lfo (i, "shape"));
        L.hz = P (IDs::lfo (i, "sync")) > 0.5f ? (float) (hostBpm.load() / 60.0 / divBeats ((int) P (IDs::lfo (i, "div")))) : P (IDs::lfo (i, "rate"));
        L.pitch = Z (IDs::lfo (i, "pitch")); L.filter = Z (IDs::lfo (i, "filter")); L.vol = Z (IDs::lfo (i, "vol"));
    }
    p.poly = (int) P (IDs::poly); p.legato = P (IDs::legato) > 0.5f; p.glide = Z (IDs::glide); p.bendRange = P (IDs::bend);
    const float v = P (IDs::volume); p.volume = v <= -59.9f ? 0.0f : std::abs (v) < 1e-4f ? 1.0f : juce::Decibels::decibelsToGain (v);
    p.pan = Z (IDs::pan); p.width = std::abs (P (IDs::width) - 1.0f) < 1e-5f ? 1.0f : P (IDs::width);
    p.pEnvAmt = Z (IDs::pEnvAmt); p.pEnvDecay = P (IDs::pEnvDecay);
    p.uni = (int) P (IDs::uniVoices); p.uniDetune = P (IDs::uniDetune); p.uniSpread = P (IDs::uniSpread);
    const auto iv = chordIntervals ((int) P (IDs::chord));
    p.chordN = (int) std::min<size_t> (6, iv.size()); for (int i = 0; i < p.chordN; ++i) p.chord[(size_t) i] = iv[(size_t) i];
    p.humStart = Z (IDs::humStart); p.humPitch = Z (IDs::humPitch); p.humVol = Z (IDs::humVol);
    p.fadeIn = Z (IDs::fadeIn); p.fadeOut = Z (IDs::fadeOut); p.choke = P (IDs::choke) > 0.5f;
    return p;
}

void LumenSamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0) return;
    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) hostBpm.store ((float) juce::jlimit (20.0, 999.0, *b));
    // source swap (message thread installs, audio thread adopts)
    auto* s = srcPtr.load();
    if (s != audioSrc) { engine.kill(); audioSrc = s; engine.setSource (s); }
    if (slicesDirty.load())
    {
        const juce::ScopedTryLock sl (srcLock);
        if (sl.isLocked()) { audioSlices.assign (slices.begin(), slices.end()); slicesDirty.store (false); }
    }
    auto p = readParams();
    p.slices = &audioSlices;
    engine.setParams (p);
    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);
    int pos = 0;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        const int at = juce::jlimit (0, n, m.samplePosition);
        if (at > pos) { engine.render (L + pos, R + pos, at - pos, bendNorm * p.bendRange); pos = at; }
        if (msg.isNoteOn()) engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff()) engine.noteOff (msg.getNoteNumber());
        else if (msg.isPitchWheel()) bendNorm = (msg.getPitchWheelValue() - 8192) / 8192.0f;
        else if (msg.isSustainPedalOn()) engine.setSustain (true);
        else if (msg.isSustainPedalOff()) engine.setSustain (false);
        else if (msg.isAllSoundOff()) engine.kill();
        else if (msg.isAllNotesOff()) engine.allNotesOff();
    }
    if (pos < n) engine.render (L + pos, R + pos, n - pos, bendNorm * p.bendRange);
    // output stage (skipped entirely at the defaults so the sample stays bit-identical)
    if (p.width != 1.0f)
        for (int i = 0; i < n; ++i) { const float m = 0.5f * (L[i] + R[i]), sd = 0.5f * (L[i] - R[i]) * p.width; L[i] = m + sd; R[i] = m - sd; }
    if (p.pan != 0.0f)
    {
        const float gl = std::min (1.0f, 1.0f - p.pan), gr = std::min (1.0f, 1.0f + p.pan);
        juce::FloatVectorOperations::multiply (L, gl, n); juce::FloatVectorOperations::multiply (R, gr, n);
    }
    if (P (IDs::normalize) > 0.5f && normGain.load() != 1.0f) { const float ng = normGain.load(); juce::FloatVectorOperations::multiply (L, ng, n); juce::FloatVectorOperations::multiply (R, ng, n); }
    colour (L, R, n);
    if (p.volume != 1.0f) { juce::FloatVectorOperations::multiply (L, p.volume, n); juce::FloatVectorOperations::multiply (R, p.volume, n); }
    double pp[8]; const int k = engine.positions (pp, 8);
    for (int i = 0; i < k; ++i) playPos[(size_t) i].store (pp[i]);
    playCount.store (k);
    outPeak.store (buffer.getMagnitude (0, n));
    midi.clear();
}

// colour stage: drive (4x oversampled soft clip) + vintage sampler emulation. Bypassed completely when off.
void LumenSamplerProcessor::colour (float* L, float* R, int n)
{
    const float drive = P (IDs::drive) < 1e-5f ? 0.0f : P (IDs::drive); const int vin = (int) P (IDs::vintage);
    if (drive > 0.0f && oversampler != nullptr && n <= maxBlock)
    {
        float* ch[2] { L, R };
        juce::dsp::AudioBlock<float> blk (ch, 2, (size_t) n);
        auto up = oversampler->processSamplesUp (blk);
        // warm asymmetric soft clip, blended in with the knob so small settings stay subtle
        const float k = 1.0f + 9.0f * drive * drive, bias = 0.15f * drive, nk = 1.0f / std::tanh (k), tb = std::tanh (bias);
        for (size_t c = 0; c < 2; ++c)
        {
            auto* d = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i) { const float x = d[i]; const float sat = (std::tanh (k * x + bias) - tb) * nk; d[i] = x + drive * (sat - x); }
        }
        oversampler->processSamplesDown (blk);
        drivePrimed = true;
    }
    else if (drivePrimed) { if (oversampler) oversampler->reset(); drivePrimed = false; }
    if (vin == 0) return;
    // target rate / bits of the classic machines
    static const double rates[] { 0, 26040.0, 40000.0, 39375.0, 11025.0 };
    static const int bits[] { 24, 12, 12, 12, 8 };
    const double ratio = rates[vin] / fs;
    const float q = (float) (1 << (bits[vin] - 1));
    const bool post = vin == 2 || vin == 3;                   // MPC60 / S950 had output smoothing filters
    const double fc = std::min (0.45 * rates[vin], 0.45 * fs);
    const double a = std::exp (-2.0 * M_PI * fc / fs);
    for (int i = 0; i < n; ++i)
    {
        vPh += ratio;
        if (vPh >= 1.0) { vPh -= 1.0; holdL = std::round (L[i] * q) / q; holdR = std::round (R[i] * q) / q; }
        float l = holdL, r = holdR;
        if (post || vin == 4) { lpL[0] = (float) (l + a * (lpL[0] - l)); lpR[0] = (float) (r + a * (lpR[0] - r)); lpL[1] = (float) (lpL[0] + a * (lpL[1] - lpL[0])); lpR[1] = (float) (lpR[0] + a * (lpR[1] - lpR[0])); l = lpL[1]; r = lpR[1]; }
        L[i] = l; R[i] = r;
    }
}

// exports the playable region exactly as stored (same rate, same bit depth) - drag into Ableton
juce::File LumenSamplerProcessor::exportRegionWav()
{
    auto s = source(); if (s == nullptr) return {};
    const int a = (int) std::lround (P (IDs::start) * s->length), b = std::max (a + 2, (int) std::lround (P (IDs::end) * s->length));
    const bool rev = P (IDs::reverse) > 0.5f;
    juce::AudioBuffer<float> buf (s->channels, b - a);
    for (int c = 0; c < s->channels; ++c) { buf.copyFrom (c, 0, s->data (0, c) + a, b - a); if (rev) buf.reverse (c, 0, b - a); }
    std::vector<std::vector<int>> ints;
    const int ib = srcBits <= 16 ? 16 : 24;
    const bool asInt = ! srcFloat && exactInts (*s, a, b, ib, rev, ints);
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Lumen Sampler"); dir.createDirectory();
    auto f = dir.getNonexistentChildFile (juce::String (s->name).replace (" ", "_") + (rev ? "_rev" : "") + "_edit", ".wav");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (f));
    if (! static_cast<juce::FileOutputStream*> (os.get())->openedOk()) return {};
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (s->rate).withNumChannels (s->channels).withBitsPerSample (asInt ? ib : 32);
    if (! asInt) opts = opts.withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto w = wav.createWriterFor (os, opts);
    if (w == nullptr) return {};
    if (asInt) writeInts (*w, ints); else w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    w.reset();
    return f;
}

// ------------------------------------------------------------------------------------------------
bool LumenSamplerProcessor::loadFile (const juce::File& f, juce::String& error)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { error = "Can't read this file (WAV / AIFF / FLAC / OGG / MP3)"; return false; }
    const auto maxLen = (juce::int64) (r->sampleRate * 600.0);
    const int len = (int) std::min (r->lengthInSamples, maxLen);
    if (len < 4) { error = "File is empty"; return false; }
    juce::AudioBuffer<float> b ((int) juce::jlimit (1u, 2u, r->numChannels), len);
    r->read (&b, 0, len, 0, true, b.getNumChannels() > 1);
    return loadBuffer (b, r->sampleRate, f.getFileNameWithoutExtension(), (int) r->bitsPerSample, r->usesFloatingPointData);
}

bool LumenSamplerProcessor::loadBuffer (const juce::AudioBuffer<float>& b, double rate, const juce::String& name, int bits, bool isFloat)
{
    std::vector<std::vector<float>> ch ((size_t) b.getNumChannels());
    for (int c = 0; c < b.getNumChannels(); ++c) ch[(size_t) c].assign (b.getReadPointer (c), b.getReadPointer (c) + b.getNumSamples());
    auto s = SampleSource::fromChannels (ch, rate, name.toStdString());
    installSource (s, bits, isFloat);
    // analysis: root key + tempo + slices
    detRoot = detectRoot (*s);
    detBpm = detectBpm (*s);
    if (detRoot > 0) setParam (IDs::root, (float) juce::jlimit (0, 127, (int) std::lround (detRoot)));
    if (detBpm > 0) setParam (IDs::sampleBpm, (float) detBpm);
    setParam (IDs::start, 0); setParam (IDs::end, 1); setParam (IDs::loopStart, 0); setParam (IDs::loopEnd, 1);
    rebuildSlices();
    return true;
}

void LumenSamplerProcessor::installSource (std::shared_ptr<SampleSource> s, int bits, bool isFloat)
{
    {
        const juce::ScopedLock sl (srcLock);
        if (src != nullptr) retired.push_back (src);
        src = s; srcBits = bits; srcFloat = isFloat;
        while (retired.size() > 4) retired.erase (retired.begin());
    }
    float pk = 0;
    if (s != nullptr) for (int c = 0; c < s->channels; ++c) for (int i = 0; i < s->length; ++i) pk = std::max (pk, std::abs (s->data (0, c)[i]));
    normGain = pk > 1e-6f ? 1.0f / pk : 1.0f;
    srcPtr.store (s.get());
    ++sampleVersion;
}

void LumenSamplerProcessor::clearSample() { installSource (nullptr, 24, false); { const juce::ScopedLock sl (srcLock); slices.clear(); } slicesDirty.store (true); }

juce::String LumenSamplerProcessor::sampleInfo() const
{
    auto s = source();
    if (s == nullptr) return "no sample - drop a file here";
    juce::String r; r << juce::String (s->name) << "   " << juce::String (s->length / s->rate, 2) << " s   " << juce::String (s->rate / 1000.0, 1) << " kHz   "
                      << (srcFloat ? juce::String ("32-bit float") : juce::String (srcBits) + "-bit") << "   " << (s->channels > 1 ? "stereo" : "mono");
    return r;
}

void LumenSamplerProcessor::applyDetectedRoot() { if (detRoot > 0) setParam (IDs::root, (float) juce::jlimit (0, 127, (int) std::lround (detRoot))); }

void LumenSamplerProcessor::rebuildSlices()
{
    auto s = source();
    std::vector<int> v;
    if (s != nullptr)
    {
        if ((int) P (IDs::sliceMode) == 0) v = detectTransients (*s, P (IDs::sliceSens), 64);
        else
        {
            static const int g[] { 4, 8, 16, 32, 64 };
            const int count = g[juce::jlimit (0, 4, (int) P (IDs::sliceGrid))];
            const int a = (int) std::lround (P (IDs::start) * s->length), b = (int) std::lround (P (IDs::end) * s->length);
            for (int i = 0; i < count; ++i) v.push_back (nearestZeroCrossing (*s, a + (int) ((int64_t) (b - a) * i / count), 200));
        }
    }
    { const juce::ScopedLock sl (srcLock); slices = v; }
    slicesDirty.store (true);
    ++sampleVersion;
}

void LumenSamplerProcessor::parameterChanged (const juce::String&, float) { triggerAsyncUpdate(); }
void LumenSamplerProcessor::handleAsyncUpdate() { rebuildSlices(); }

void LumenSamplerProcessor::setParam (const juce::String& id, float v)
{
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id))) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture(); }
}

juce::File LumenSamplerProcessor::exportSliceMidi()
{
    auto s = source(); const auto sl = getSlices();
    if (s == nullptr || sl.empty()) return {};
    const double bpm = P (IDs::sampleBpm);
    juce::MidiMessageSequence seq;
    for (size_t i = 0; i < sl.size() && i < 64; ++i)
    {
        const double b0 = sl[i] / s->rate * bpm / 60.0;
        const double b1 = (i + 1 < sl.size() ? sl[i + 1] : s->length) / s->rate * bpm / 60.0;
        auto on = juce::MidiMessage::noteOn (1, 36 + (int) i, (juce::uint8) 100); on.setTimeStamp (b0 * 960.0); seq.addEvent (on);
        auto off = juce::MidiMessage::noteOff (1, 36 + (int) i); off.setTimeStamp (std::max (b0 + 0.01, b1 - 0.001) * 960.0); seq.addEvent (off);
    }
    seq.updateMatchedPairs();
    juce::MidiFile mf; mf.setTicksPerQuarterNote (960); mf.addTrack (seq);
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Lumen Sampler"); dir.createDirectory();
    auto f = dir.getNonexistentChildFile ("Lumen_Slices_" + juce::String (s->name).replace (" ", "_"), ".mid");
    juce::FileOutputStream os (f); if (! os.openedOk()) return {};
    mf.writeTo (os);
    return f;
}

void LumenSamplerProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenSamplerProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

// ------------------------------------------------------------------------------------------------
// state: parameters + the sample, lossless (integer sources as FLAC at their own bit depth, float sources as raw float)
void LumenSamplerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    auto s = source();
    if (s != nullptr)
    {
        juce::MemoryBlock mb;
            bool flacOk = false;
        std::vector<std::vector<int>> ints;
        const int fb = srcBits <= 16 ? 16 : 24;
        if (! srcFloat && exactInts (*s, 0, s->length, fb, false, ints))
        {
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::OutputStream> os (new juce::MemoryOutputStream (mb, false));
            if (auto w = flac.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (s->rate).withNumChannels (s->channels).withBitsPerSample (fb)))
                flacOk = writeInts (*w, ints);
        }
        if (! flacOk)
        {
            mb.reset();
            juce::MemoryOutputStream raw (mb, false);
            juce::GZIPCompressorOutputStream gz (raw, 6);
            for (int c = 0; c < s->channels; ++c) gz.write (s->data (0, c), sizeof (float) * (size_t) s->length);
            gz.flush();
        }
        st.setProperty ("smpData", mb.toBase64Encoding(), nullptr);
        st.setProperty ("smpCodec", flacOk ? "flac" : "f32gz", nullptr);
        st.setProperty ("smpName", juce::String (s->name), nullptr);
        st.setProperty ("smpRate", s->rate, nullptr);
        st.setProperty ("smpCh", s->channels, nullptr);
        st.setProperty ("smpLen", s->length, nullptr);
        st.setProperty ("smpBits", srcBits, nullptr);
        st.setProperty ("smpFloat", srcFloat, nullptr);
        st.setProperty ("detRoot", detRoot, nullptr); st.setProperty ("detBpm", detBpm, nullptr);
        juce::StringArray sl; for (int x : getSlices()) sl.add (juce::String (x));
        st.setProperty ("slices", sl.joinIntoString (","), nullptr);
    }
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenSamplerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto vt = juce::ValueTree::fromXml (*xml);
    if (vt.hasProperty ("smpData"))
    {
        juce::MemoryBlock mb; mb.fromBase64Encoding (vt.getProperty ("smpData").toString());
        const int ch = (int) vt.getProperty ("smpCh", 1), len = (int) vt.getProperty ("smpLen", 0);
        const double rate = (double) vt.getProperty ("smpRate", 48000.0);
        juce::AudioBuffer<float> b (juce::jmax (1, ch), juce::jmax (1, len)); b.clear();
        if (vt.getProperty ("smpCodec").toString() == "flac")
        {
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::AudioFormatReader> r (flac.createReaderFor (new juce::MemoryInputStream (mb, false), true));
            if (r != nullptr) r->read (&b, 0, len, 0, true, ch > 1);
        }
        else
        {
            juce::MemoryInputStream raw (mb, false);
            juce::GZIPDecompressorInputStream gz (raw);
            for (int c = 0; c < ch; ++c) gz.read (b.getWritePointer (c), (int) (sizeof (float) * (size_t) len));
        }
        std::vector<std::vector<float>> chans ((size_t) b.getNumChannels());
        for (int c = 0; c < b.getNumChannels(); ++c) chans[(size_t) c].assign (b.getReadPointer (c), b.getReadPointer (c) + len);
        installSource (SampleSource::fromChannels (chans, rate, vt.getProperty ("smpName").toString().toStdString()), (int) vt.getProperty ("smpBits", 24), (bool) vt.getProperty ("smpFloat", false));
        detRoot = (double) vt.getProperty ("detRoot", -1.0); detBpm = (double) vt.getProperty ("detBpm", 0.0);
        std::vector<int> sl; juce::StringArray t; t.addTokens (vt.getProperty ("slices").toString(), ",", "");
        for (auto& x : t) if (x.isNotEmpty()) sl.push_back (x.getIntValue());
        { const juce::ScopedLock l (srcLock); slices = sl; }
        slicesDirty.store (true);
        for (auto id : { "smpData", "smpCodec", "smpName", "smpRate", "smpCh", "smpLen", "smpBits", "smpFloat", "detRoot", "detBpm", "slices" }) vt.removeProperty (id, nullptr);
    }
    apvts.replaceState (vt);
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            auto child = vt.getChildWithProperty ("id", rp->paramID);
            if (child.isValid() && child.hasProperty ("value")) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
        }
    cancelPendingUpdate();
    ++sampleVersion;
}

juce::AudioProcessorEditor* LumenSamplerProcessor::createEditor() { return new LumenSamplerEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenSamplerProcessor(); }
