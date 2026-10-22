#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lumen::sampler;
using namespace lumen::ss;

// ================================================================================================
// background analysis (harmonic resynthesis tables are built off the audio thread)
class LumenSynthSamplerProcessor::Analyser : public juce::Thread
{
public:
    explicit Analyser (LumenSynthSamplerProcessor& p) : juce::Thread ("Lumen Synth Sampler analysis"), proc (p) {}
    void queue (int layer, std::shared_ptr<SampleSource> s) { { const juce::ScopedLock l (lock); jobs.push_back ({ layer, std::move (s) }); } busy.store (true); notify(); }
    bool isBusy() const { return busy.load(); }
    void run() override
    {
        while (! threadShouldExit())
        {
            std::pair<int, std::shared_ptr<SampleSource>> job { -1, nullptr };
            { const juce::ScopedLock l (lock); if (! jobs.empty()) { job = jobs.front(); jobs.erase (jobs.begin()); } }
            if (job.first < 0) { busy.store (false); wait (50); continue; }
            auto rs = ResynthAnalyser::analyse (*job.second);
            if (threadShouldExit()) return;
            proc.finishAnalysis (job.first, job.second, rs);
            { const juce::ScopedLock l (lock); if (jobs.empty()) busy.store (false); }
        }
    }
private:
    LumenSynthSamplerProcessor& proc;
    juce::CriticalSection lock;
    std::vector<std::pair<int, std::shared_ptr<SampleSource>>> jobs;
    std::atomic<bool> busy { false };
};

// ================================================================================================
namespace
{
struct Preset { const char* name; std::vector<std::pair<juce::String, float>> v; };
const std::vector<Preset>& presets()
{
    static const std::vector<Preset> p {
        { "Init (identical sample)", {} },
        { "Supersaw Stack", { { "uni", 7 }, { "uni_det", 22 }, { "uni_spread", 0.9f }, { "hyp_on", 1 }, { "hyp_mix", 0.3f }, { "rev_on", 1 }, { "rev_mix", 0.15f }, { "amp_r", 0.25f } } },
        { "Pluck Filter", { { "f_on", 1 }, { "f_type", 2 }, { "cutoff", 600 }, { "res", 0.25f }, { "f_env", 0.55f }, { "fenv_a", 0.001f }, { "fenv_d", 0.25f }, { "fenv_s", 0 },
                            { "amp_d", 0.6f }, { "amp_s", 0 }, { "amp_r", 0.2f }, { "dly_on", 1 }, { "dly_mix", 0.18f } } },
        { "Wide Lead", { { "uni", 3 }, { "uni_det", 12 }, { "cho_on", 1 }, { "dly_on", 1 }, { "dly_mix", 0.2f }, { "rev_on", 1 }, { "rev_mix", 0.18f }, { "glide", 0.04f }, { "amp_r", 0.2f } } },
        { "808 Glide", { { "poly", 1 }, { "legato", 1 }, { "glide", 0.12f }, { "sub_on", 1 }, { "sub_level", 0.35f }, { "dist_on", 1 }, { "dist_drive", 0.25f }, { "dist_mix", 0.5f } } },
        { "Evolving Pad", { { "amp_a", 0.6f }, { "amp_r", 2.0f }, { "a_loop", 1 }, { "rev_on", 1 }, { "rev_mix", 0.35f }, { "rev_size", 0.8f }, { "f_on", 1 }, { "cutoff", 4000 },
                            { "mod1_src", 1 }, { "mod1_dst", 9 }, { "mod1_amt", 0.15f }, { "lfo1_div", 2 }, { "uni", 4 }, { "uni_det", 10 } } },
        { "Wobble Bass", { { "poly", 1 }, { "f_on", 1 }, { "f_type", 0 }, { "cutoff", 300 }, { "res", 0.4f }, { "mod1_src", 1 }, { "mod1_dst", 9 }, { "mod1_amt", 0.5f },
                           { "lfo1_div", 9 }, { "lfo1_retrig", 1 }, { "dist_on", 1 }, { "dist_drive", 0.4f }, { "ott_on", 1 } } },
        { "Arp Pluck", { { "arp_on", 1 }, { "arp_rate", 3 }, { "arp_oct", 2 }, { "amp_d", 0.3f }, { "amp_s", 0 }, { "amp_r", 0.15f }, { "dly_on", 1 }, { "dly_mix", 0.25f }, { "rev_on", 1 }, { "rev_mix", 0.2f } } },
    };
    return p;
}
static bool exactInts (const SampleSource& s, int bits, std::vector<std::vector<int>>& out)
{
    if (bits < 8 || bits > 24) return false;
    const double scale = (double) (1 << (bits - 1)), lim = scale - 1;
    out.assign ((size_t) s.channels, std::vector<int> ((size_t) s.length));
    for (int c = 0; c < s.channels; ++c)
    {
        const float* d = s.data (0, c);
        for (int i = 0; i < s.length; ++i)
        {
            const double v = (double) d[i] * scale;
            if (v != std::floor (v) || v < -scale || v > lim) return false;
            out[(size_t) c][(size_t) i] = (int) v * (1 << (32 - bits));
        }
    }
    return true;
}
}

LumenSynthSamplerProcessor::LumenSynthSamplerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenSynthSampler", ssp::createLayout())
{
    analyser = std::make_unique<Analyser> (*this);
    analyser->startThread();
}
LumenSynthSamplerProcessor::~LumenSynthSamplerProcessor() { analyser->stopThread (5000); }

bool LumenSynthSamplerProcessor::isBusesLayoutSupported (const BusesLayout& l) const { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }

void LumenSynthSamplerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate; maxBlock = std::max (64, samplesPerBlock);
    engine.prepare (fs);
    fx.prepare (fs, maxBlock);
    arp.prepare (fs);
    keyboardState.reset();
}

// ------------------------------------------------------------------------------------------------
Params LumenSynthSamplerProcessor::readParams() const
{
    Params p;
    auto P = [this] (const juce::String& id) { return param (id); };
    auto Z = [&] (const juce::String& id) { const float v = P (id); return std::abs (v) < 1e-5f ? 0.0f : v; };
    for (int k = 0; k < kLayers; ++k)
    {
        auto& L = p.layer[k];
        L.on = P (ssp::L (k, "on")) > 0.5f; L.mode = (int) P (ssp::L (k, "mode")); L.root = (int) P (ssp::L (k, "root"));
        L.oct = (int) P (ssp::L (k, "oct")); L.semi = (int) P (ssp::L (k, "semi")); L.fine = Z (ssp::L (k, "fine"));
        const float lv = P (ssp::L (k, "level")); L.level = std::abs (lv) < 1e-4f ? 1.0f : (lv <= -35.9f ? 0.0f : juce::Decibels::decibelsToGain (lv));
        L.pan = Z (ssp::L (k, "pan")); L.start = Z (ssp::L (k, "start"));
        const float sp = P (ssp::L (k, "speed")); L.speed = std::abs (sp - 1.0f) < 1e-4f ? 1.0f : sp;
        L.loop = P (ssp::L (k, "loop")) > 0.5f; L.loopStart = P (ssp::L (k, "lstart")); L.loopEnd = P (ssp::L (k, "lend")); L.xfadeMs = P (ssp::L (k, "xfade"));
        L.exactRoot = P (ssp::L (k, "exact")) > 0.5f; L.reverse = P (ssp::L (k, "rev")) > 0.5f; L.transientMs = Z (ssp::L (k, "trans"));
    }
    p.subOn = P ("sub_on") > 0.5f; p.subShape = (int) P ("sub_shape"); p.subOct = (int) P ("sub_oct"); p.subLevel = P ("sub_level");
    p.noiseOn = P ("noise_on") > 0.5f; p.noiseLevel = P ("noise_level"); p.noiseColor = P ("noise_color");
    p.fOn = P ("f_on") > 0.5f; p.fType = (int) P ("f_type"); p.cutoff = P ("cutoff"); p.res = P ("res"); p.drive = Z ("drive"); p.fEnv = Z ("f_env"); p.keyTrack = Z ("key_track");
    for (int e = 0; e < 3; ++e) { p.env[e][0] = Z (ssp::env (e, "a")); p.env[e][1] = P (ssp::env (e, "d")); const float s = P (ssp::env (e, "s")); p.env[e][2] = s > 0.99999f ? 1.0f : s; p.env[e][3] = P (ssp::env (e, "r")); }
    for (int l = 0; l < 2; ++l) { p.lfoShape[l] = (int) P (ssp::lfo (l, "shape")); p.lfoRetrig[l] = P (ssp::lfo (l, "retrig")) > 0.5f; }
    for (int s = 0; s < kSlots; ++s) { p.mod[s].src = (int) P (ssp::mod (s, "src")); p.mod[s].dst = (int) P (ssp::mod (s, "dst")); p.mod[s].amt = Z (ssp::mod (s, "amt")); }
    for (int m = 0; m < 4; ++m) p.macro[m] = P (ssp::macro (m));
    p.poly = (int) P ("poly"); p.legato = P ("legato") > 0.5f; p.glide = Z ("glide"); p.bend = (int) P ("bend"); p.velSens = Z ("vel_sens");
    p.uni = (int) P ("uni"); p.uniDetune = P ("uni_det"); p.uniSpread = P ("uni_spread");
    return p;
}

void LumenSynthSamplerProcessor::fillFx (lumen::synth::FxP& f) const
{
    auto P = [this] (const char* id) { return param (id); };
    f.hypOn = P ("hyp_on") > 0.5f; f.hypDetune = P ("hyp_detune"); f.hypDim = P ("hyp_dim"); f.hypMix = P ("hyp_mix");
    f.distOn = P ("dist_on") > 0.5f; f.distMode = (int) P ("dist_mode"); f.distDrive = P ("dist_drive"); f.distMix = P ("dist_mix");
    f.flaOn = P ("fla_on") > 0.5f; f.flaRate = P ("fla_rate"); f.flaDepth = P ("fla_depth"); f.flaFb = P ("fla_fb"); f.flaMix = P ("fla_mix");
    f.phaOn = P ("pha_on") > 0.5f; f.phaRate = P ("pha_rate"); f.phaDepth = P ("pha_depth"); f.phaFb = P ("pha_fb"); f.phaMix = P ("pha_mix");
    f.choOn = P ("cho_on") > 0.5f; f.choRate = P ("cho_rate"); f.choDepth = P ("cho_depth"); f.choMix = P ("cho_mix");
    f.dlyOn = P ("dly_on") > 0.5f; f.dlyBeats = ssp::divBeats ((int) P ("dly_div")); f.dlyFb = P ("dly_fb"); f.dlyTone = P ("dly_tone"); f.dlyMix = P ("dly_mix"); f.dlyPing = P ("dly_ping") > 0.5f;
    f.cmpOn = P ("cmp_on") > 0.5f; f.cmpThresh = P ("cmp_thresh"); f.cmpRatio = P ("cmp_ratio"); f.cmpAttack = P ("cmp_attack"); f.cmpRelease = P ("cmp_release"); f.cmpGain = P ("cmp_gain");
    f.revOn = P ("rev_on") > 0.5f; f.revSize = P ("rev_size"); f.revDecay = P ("rev_decay"); f.revDamp = P ("rev_damp"); f.revMix = P ("rev_mix");
    f.eqOn = P ("eq_on") > 0.5f; f.eqLow = P ("eq_low"); f.eqMid = P ("eq_mid"); f.eqMidFreq = P ("eq_mfreq"); f.eqHigh = P ("eq_high");
    f.ottOn = P ("ott_on") > 0.5f; f.ottDepth = P ("ott_depth"); f.ottTime = P ("ott_time");
    static const double pumpBeats[] = { 4, 2, 1, 0.5, 0.25 };
    f.pumpOn = P ("pump_on") > 0.5f; f.pumpBeats = pumpBeats[juce::jlimit (0, 4, (int) P ("pump_div"))]; f.pumpDepth = P ("pump_depth"); f.pumpCurve = P ("pump_curve");
    const float v = P ("vol"); f.volDb = std::abs (v) < 1e-4f ? 0.0f : v; f.clip = P ("clip") > 0.5f;
}

void LumenSynthSamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0) return;
    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    double bpm = 120, ppq = ppqLast + n / fs * 2.0; bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition())
    {
        if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 999.0, *b);
        if (auto q = pos->getPpqPosition()) ppq = *q;
        playing = pos->getIsPlaying();
    }
    ppqLast = ppq;
    // adopt new samples
    bool changed = false;
    for (int k = 0; k < kLayers; ++k)
    {
        auto* s = srcPtr[k].load(); auto* r = rsPtr[k].load();
        if (s != audioLayers[k].src || r != audioLayers[k].rs) { audioLayers[k].src = s; audioLayers[k].rs = r; changed = true; }
    }
    if (changed) { engine.kill(); engine.setLayers (audioLayers); }
    engine.setLayers (audioLayers);
    // parameters
    auto p = readParams();
    double lfoHz[2];
    for (int l = 0; l < 2; ++l) lfoHz[l] = param (ssp::lfo (l, "sync")) > 0.5f ? bpm / 60.0 / ssp::divBeats ((int) param (ssp::lfo (l, "div"))) : param (ssp::lfo (l, "rate"));
    engine.setParams (p, lfoHz[0], lfoHz[1]);
    // arp
    lumen::synth::Arp::Params ap;
    ap.on = param ("arp_on") > 0.5f; ap.mode = (int) param ("arp_mode"); ap.beats = ssp::arpBeats ((int) param ("arp_rate")); ap.octaves = (int) param ("arp_oct");
    ap.gate = param ("arp_gate"); ap.swing = param ("arp_swing"); ap.chord = (int) param ("arp_chord");
    arp.process (midi, n, ap, bpm, playing, ppq);
    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);
    int pos = 0;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        const int at = juce::jlimit (0, n, m.samplePosition);
        if (at > pos) { engine.render (L + pos, R + pos, at - pos); pos = at; }
        if (msg.isNoteOn()) engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff()) engine.noteOff (msg.getNoteNumber());
        else if (msg.isPitchWheel()) { bendNorm = (msg.getPitchWheelValue() - 8192) / 8192.0f; engine.setBend (bendNorm * p.bend); }
        else if (msg.isSustainPedalOn()) engine.setSustain (true);
        else if (msg.isSustainPedalOff()) engine.setSustain (false);
        else if (msg.isControllerOfType (1)) engine.setModWheel (msg.getControllerValue() / 127.0f);
        else if (msg.isChannelPressure()) engine.setAftertouch (msg.getChannelPressureValue() / 127.0f);
        else if (msg.isAftertouch()) engine.setAftertouch (msg.getAfterTouchValue() / 127.0f);
        else if (msg.isAllSoundOff()) engine.kill();
        else if (msg.isAllNotesOff()) engine.allNotesOff();
    }
    if (pos < n) engine.render (L + pos, R + pos, n - pos);
    lumen::synth::FxP fp; fillFx (fp);
    fx.process (L, R, n, fp, bpm, playing, ppq);
    outPeak.store (buffer.getMagnitude (0, n));
    voicesActive.store (engine.activeVoices());
    midi.clear();
}

// ------------------------------------------------------------------------------------------------
bool LumenSynthSamplerProcessor::loadFile (int layer, const juce::File& f, juce::String& error)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { error = "Can't read this file (WAV / AIFF / FLAC / OGG / MP3)"; return false; }
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 60.0));
    if (len < 64) { error = "File is empty"; return false; }
    juce::AudioBuffer<float> b ((int) juce::jlimit (1u, 2u, r->numChannels), len);
    r->read (&b, 0, len, 0, true, b.getNumChannels() > 1);
    return loadBuffer (layer, b, r->sampleRate, f.getFileNameWithoutExtension(), (int) r->bitsPerSample, r->usesFloatingPointData, true);
}

bool LumenSynthSamplerProcessor::loadBuffer (int layer, const juce::AudioBuffer<float>& b, double rate, const juce::String& name, int bits, bool isFloat, bool setRoot)
{
    if (! juce::isPositiveAndBelow (layer, kLayers)) return false;
    std::vector<std::vector<float>> ch ((size_t) b.getNumChannels());
    for (int c = 0; c < b.getNumChannels(); ++c) ch[(size_t) c].assign (b.getReadPointer (c), b.getReadPointer (c) + b.getNumSamples());
    auto s = SampleSource::fromChannels (ch, rate, name.toStdString());
    const double root = detectRoot (*s);
    install (layer, s, nullptr, bits, isFloat, root, true);
    if (setRoot)
    {
        if (root > 0) setParam (ssp::L (layer, "root"), (float) juce::jlimit (0, 127, (int) std::lround (root)));
        setParam (ssp::L (layer, "on"), 1.0f);
    }
    analyser->queue (layer, s);
    return true;
}

void LumenSynthSamplerProcessor::finishAnalysis (int layer, std::shared_ptr<SampleSource> s, std::shared_ptr<Resynth> rs)
{
    bool same;
    { const juce::ScopedLock l (slotLock); same = slots[(size_t) layer].src == s; }
    if (! same) return;
    {
        const juce::ScopedLock l (slotLock);
        auto& sl = slots[(size_t) layer];
        if (sl.rs) retired.push_back (sl.rs);
        sl.rs = rs; sl.analysing = false;
        if (rs && rs->valid) sl.detectedRoot = 69.0 + 12.0 * std::log2 (rs->f0 / 440.0);
    }
    rsPtr[layer].store (rs && rs->valid ? rs.get() : nullptr);
    ++sampleVersion;
}

void LumenSynthSamplerProcessor::install (int layer, std::shared_ptr<SampleSource> s, std::shared_ptr<Resynth> rs, int bits, bool isFloat, double root, bool analysing)
{
    {
        const juce::ScopedLock l (slotLock);
        auto& sl = slots[(size_t) layer];
        if (sl.src) retired.push_back (sl.src);
        if (sl.rs) retired.push_back (sl.rs);
        sl.src = s; sl.rs = rs; sl.bits = bits; sl.isFloat = isFloat; sl.detectedRoot = root; sl.analysing = analysing && s != nullptr;
        while (retired.size() > 12) retired.erase (retired.begin());
    }
    rsPtr[layer].store (rs && rs->valid ? rs.get() : nullptr);
    srcPtr[layer].store (s.get());
    ++sampleVersion;
}

void LumenSynthSamplerProcessor::clearLayer (int layer) { install (layer, nullptr, nullptr, 24, false, -1, false); }

void LumenSynthSamplerProcessor::waitForAnalysis()
{
    for (int i = 0; i < 2000 && analyser->isBusy(); ++i) juce::Thread::sleep (10);
    juce::Thread::sleep (20);
}

juce::String LumenSynthSamplerProcessor::layerInfo (int layer) const
{
    auto s = slot (layer);
    if (s.src == nullptr) return "drop a sample (bass / pluck / lead / pad ...)";
    juce::String t; t << juce::String (s.src->name) << "   " << juce::String (s.src->length / s.src->rate, 2) << " s   ";
    if (s.analysing) t << "analysing...";
    else if (s.rs && s.rs->valid) t << juce::String (s.rs->f0, 1) << " Hz  " << juce::MidiMessage::getMidiNoteName ((int) std::lround (s.detectedRoot), true, true, 3);
    else t << "unpitched -> STRETCH";
    return t;
}

void LumenSynthSamplerProcessor::setParam (const juce::String& id, float v)
{
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id))) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture(); }
}

// ------------------------------------------------------------------------------------------------
int LumenSynthSamplerProcessor::getNumPrograms() { return (int) presets().size(); }
const juce::String LumenSynthSamplerProcessor::getProgramName (int i) { return juce::isPositiveAndBelow (i, (int) presets().size()) ? presets()[(size_t) i].name : ""; }
void LumenSynthSamplerProcessor::setCurrentProgram (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) presets().size())) return;
    currentProgram = i;
    // keep the loaded samples and their layer settings, reset the sound design
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = rp->paramID;
            if (id.startsWith ("a_") || id.startsWith ("b_") || id.startsWith ("c_")) continue;
            rp->setValueNotifyingHost (rp->getDefaultValue());
        }
    for (int k = 0; k < kLayers; ++k) { setParam (ssp::L (k, "loop"), 0); }
    for (auto& kv : presets()[(size_t) i].v) setParam (kv.first, kv.second);
}

void LumenSynthSamplerProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenSynthSamplerProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenSynthSamplerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    st.setProperty ("program", currentProgram, nullptr);
    for (int k = 0; k < kLayers; ++k)
    {
        const auto sl = slot (k);
        if (sl.src == nullptr) continue;
        const auto& s = *sl.src;
        juce::MemoryBlock mb; bool flacOk = false;
        std::vector<std::vector<int>> ints;
        const int fb = sl.bits <= 16 ? 16 : 24;
        if (! sl.isFloat && exactInts (s, fb, ints))
        {
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::OutputStream> os (new juce::MemoryOutputStream (mb, false));
            if (auto w = flac.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (s.rate).withNumChannels (s.channels).withBitsPerSample (fb)))
            {
                std::vector<const int*> ptr; for (auto& v : ints) ptr.push_back (v.data()); ptr.push_back (nullptr);
                flacOk = w->write (ptr.data(), s.length);
            }
        }
        if (! flacOk)
        {
            mb.reset();
            juce::MemoryOutputStream raw (mb, false);
            juce::GZIPCompressorOutputStream gz (raw, 6);
            for (int c = 0; c < s.channels; ++c) gz.write (s.data (0, c), sizeof (float) * (size_t) s.length);
            gz.flush();
        }
        const juce::String P = "smp" + juce::String (k);
        st.setProperty (P + "Data", mb.toBase64Encoding(), nullptr);
        st.setProperty (P + "Codec", flacOk ? "flac" : "f32gz", nullptr);
        st.setProperty (P + "Name", juce::String (s.name), nullptr);
        st.setProperty (P + "Rate", s.rate, nullptr); st.setProperty (P + "Ch", s.channels, nullptr); st.setProperty (P + "Len", s.length, nullptr);
        st.setProperty (P + "Bits", sl.bits, nullptr); st.setProperty (P + "Float", sl.isFloat, nullptr);
    }
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenSynthSamplerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto vt = juce::ValueTree::fromXml (*xml);
    for (int k = 0; k < kLayers; ++k)
    {
        const juce::String P = "smp" + juce::String (k);
        if (! vt.hasProperty (P + "Data")) { if (slot (k).src != nullptr) clearLayer (k); continue; }
        juce::MemoryBlock mb; mb.fromBase64Encoding (vt.getProperty (P + "Data").toString());
        const int ch = (int) vt.getProperty (P + "Ch", 1), len = (int) vt.getProperty (P + "Len", 0);
        const double rate = (double) vt.getProperty (P + "Rate", 48000.0);
        juce::AudioBuffer<float> b (juce::jmax (1, ch), juce::jmax (1, len)); b.clear();
        if (vt.getProperty (P + "Codec").toString() == "flac")
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
        loadBuffer (k, b, rate, vt.getProperty (P + "Name").toString(), (int) vt.getProperty (P + "Bits", 24), (bool) vt.getProperty (P + "Float", false), false);
        for (auto id : { "Data", "Codec", "Name", "Rate", "Ch", "Len", "Bits", "Float" }) vt.removeProperty (P + id, nullptr);
    }
    currentProgram = (int) vt.getProperty ("program", 0); vt.removeProperty ("program", nullptr);
    apvts.replaceState (vt);
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            auto child = vt.getChildWithProperty ("id", rp->paramID);
            if (child.isValid() && child.hasProperty ("value")) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
        }
}

juce::AudioProcessorEditor* LumenSynthSamplerProcessor::createEditor() { return new LumenSynthSamplerEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenSynthSamplerProcessor(); }
