#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Smart.h"

using namespace lumen::synth;

namespace
{
std::vector<lumen::CurvePoint> defaultCurve() { return { { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } }; }
}

LumenSynthProcessor::LumenSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenSynth", createLayout())
{
    for (auto& s : specs()) raw[s.id.toStdString()] = apvts.getRawParameterValue (s.id);
    for (auto& v : voiceLevel) v.store (0.0f);
    for (auto& v : ottBand) v.store (1.0f);
    for (auto& v : lfoPhase) v.store (0.0f);
    for (auto& v : srcView) v.store (0.0f);
    for (auto& p : userTablePtr) p.store (nullptr);
    for (int l = 0; l < kLfo; ++l) { lutActive[l].store (0); lfoPts[l] = defaultCurve(); rebuildLut (l); }
    for (int i = 0; i < kNumFx; ++i) fxOrder[i].store (i);
    favourites = juce::StringArray::fromLines (favouritesFile().loadFileAsString());
    favourites.removeEmptyStrings();
    loadPreset (0);
    undoStack.clear();
}

bool LumenSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void LumenSynthProcessor::prepareToPlay (double sampleRate, int)
{
    baseRate = sampleRate;
    engineFactor = P ("hq") > 0.5f ? 2 : 1;
    engine.prepare (sampleRate * engineFactor, sampleRate);
    fx.prepare (sampleRate, kChunk);
    decim.reset();
    arp.prepare (sampleRate);
    osL.assign ((size_t) (2 * kChunk), 0.0f); osR.assign ((size_t) (2 * kChunk), 0.0f);
    fillPatch();
    engine.setPatch (patch);
    keyboardState.reset();
}

//==============================================================================
void LumenSynthProcessor::fillPatch()
{
    const auto& bank = lumen::WavetableBank::get();
    for (int o = 0; o < kOsc; ++o)
    {
        const std::string p = o == 0 ? "a_" : o == 1 ? "b_" : "c_";
        auto g = [&] (const char* k) { return raw.at (p + k)->load (std::memory_order_relaxed); };
        auto& op = patch.osc[o];
        op.on = g ("on") > 0.5f;
        const int t = (int) g ("table");
        op.tbl = t < kNumFactoryTables ? &bank.table (t) : userTablePtr[o].load (std::memory_order_acquire);
        op.pos = g ("pos"); op.warpMode = (int) g ("warpm"); op.warp = g ("warp");
        op.oct = (int) std::lround (g ("oct")); op.semi = (int) std::lround (g ("semi")); op.fine = g ("fine");
        op.level = g ("level"); op.pan = g ("pan"); op.uni = (int) std::lround (g ("uni"));
        op.det = g ("det"); op.blend = g ("blend"); op.width = g ("width"); op.rand = g ("rand"); op.umode = (int) g ("umode");
    }
    patch.subOn = P ("sub_on") > 0.5f; patch.subShape = (int) P ("sub_shape"); patch.subOct = (int) P ("sub_oct"); patch.subLevel = P ("sub_level");
    patch.noiseOn = P ("noise_on") > 0.5f; patch.noiseLevel = P ("noise_level"); patch.noiseColor = P ("noise_color");
    {
        auto& f = patch.flt[0];
        f.on = P ("flt_on") > 0.5f; f.type = (int) P ("flt_type"); f.cutoff = P ("flt_cut"); f.res = P ("flt_res");
        f.drive = P ("flt_drive"); f.env = P ("flt_env"); f.key = P ("flt_key");
        auto& f2 = patch.flt[1];
        f2.on = P ("f2_on") > 0.5f; f2.type = (int) P ("f2_type"); f2.cutoff = P ("f2_cut"); f2.res = P ("f2_res");
        f2.drive = P ("f2_drive"); f2.env = P ("f2_env"); f2.key = P ("f2_key");
        patch.route = (int) P ("flt_route");
    }
    static const char* envIds[3][4] = { { "e1_a", "e1_d", "e1_s", "e1_r" }, { "e2_a", "e2_d", "e2_s", "e2_r" }, { "e3_a", "e3_d", "e3_s", "e3_r" } };
    for (int e = 0; e < 3; ++e) for (int k = 0; k < 4; ++k) patch.env[e][k] = P (envIds[e][k]);
    for (int l = 0; l < kLfo; ++l)
    {
        const std::string p = "l" + std::to_string (l + 1) + "_";
        patch.lfoShape[l] = (int) raw.at (p + "shape")->load(); patch.lfoRate[l] = raw.at (p + "rate")->load();
        patch.lfoSync[l] = raw.at (p + "sync")->load() > 0.5f; patch.lfoBeats[l] = divBeats ((int) raw.at (p + "div")->load());
        patch.lfoMode[l] = (int) raw.at (p + "mode")->load();
        patch.lfoLut[l] = lut[l][lutActive[l].load (std::memory_order_acquire)];
    }
    for (int m = 0; m < kSlots; ++m)
    {
        const std::string p = "m" + std::to_string (m + 1) + "_";
        patch.mod[m].src = (int) raw.at (p + "src")->load(); patch.mod[m].dst = (int) raw.at (p + "dst")->load(); patch.mod[m].amt = raw.at (p + "amt")->load();
    }
    static const char* macroIds[4] = { "macro1", "macro2", "macro3", "macro4" };
    for (int m = 0; m < 4; ++m) patch.macro[m] = P (macroIds[m]);
    patch.voices = (int) std::lround (P ("voices")); patch.legato = P ("legato") > 0.5f; patch.glide = P ("glide");
    patch.bend = (int) std::lround (P ("bend")); patch.velSens = P ("velsens"); patch.drift = P ("drift"); patch.warmth = P ("warmth");

    fxp.hypOn = P ("hyp_on") > 0.5f; fxp.hypDetune = P ("hyp_detune"); fxp.hypDim = P ("hyp_dim"); fxp.hypMix = P ("hyp_mix");
    fxp.distOn = P ("dist_on") > 0.5f; fxp.distMode = (int) P ("dist_mode"); fxp.distDrive = P ("dist_drive"); fxp.distMix = P ("dist_mix");
    fxp.flaOn = P ("fla_on") > 0.5f; fxp.flaRate = P ("fla_rate"); fxp.flaDepth = P ("fla_depth"); fxp.flaFb = P ("fla_fb"); fxp.flaMix = P ("fla_mix");
    fxp.phaOn = P ("pha_on") > 0.5f; fxp.phaRate = P ("pha_rate"); fxp.phaDepth = P ("pha_depth"); fxp.phaFb = P ("pha_fb"); fxp.phaMix = P ("pha_mix");
    fxp.choOn = P ("cho_on") > 0.5f; fxp.choRate = P ("cho_rate"); fxp.choDepth = P ("cho_depth"); fxp.choMix = P ("cho_mix");
    fxp.dlyOn = P ("dly_on") > 0.5f; fxp.dlyBeats = divBeats ((int) P ("dly_div")); fxp.dlyFb = P ("dly_fb"); fxp.dlyTone = P ("dly_tone");
    fxp.dlyMix = P ("dly_mix"); fxp.dlyPing = P ("dly_ping") > 0.5f;
    fxp.cmpOn = P ("cmp_on") > 0.5f; fxp.cmpThresh = P ("cmp_thresh"); fxp.cmpRatio = P ("cmp_ratio"); fxp.cmpAttack = P ("cmp_attack");
    fxp.cmpRelease = P ("cmp_release"); fxp.cmpGain = P ("cmp_gain");
    fxp.revOn = P ("rev_on") > 0.5f; fxp.revSize = P ("rev_size"); fxp.revDecay = P ("rev_decay"); fxp.revDamp = P ("rev_damp"); fxp.revMix = P ("rev_mix");
    fxp.eqOn = P ("eq_on") > 0.5f; fxp.eqLow = P ("eq_low"); fxp.eqMid = P ("eq_mid"); fxp.eqMidFreq = P ("eq_mfreq"); fxp.eqHigh = P ("eq_high");
    fxp.ottOn = P ("ott_on") > 0.5f; fxp.ottDepth = P ("ott_depth"); fxp.ottTime = P ("ott_time");
    static const double pumpBeats[] = { 4, 2, 1, 0.5, 0.25 };
    fxp.pumpOn = P ("pump_on") > 0.5f; fxp.pumpBeats = pumpBeats[juce::jlimit (0, 4, (int) P ("pump_div"))];
    fxp.pumpDepth = P ("pump_depth"); fxp.pumpCurve = P ("pump_curve");
    fxp.volDb = P ("vol"); fxp.clip = P ("clip") > 0.5f;
    for (int i = 0; i < kNumFx; ++i) fxp.order[i] = fxOrder[i].load (std::memory_order_relaxed);
}

void LumenSynthProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())                   engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())             engine.noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())          engine.setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isChannelPressure())     engine.setAftertouch ((float) m.getChannelPressureValue() / 127.0f);
    else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
    else if (m.isController())
    {
        if (m.getControllerNumber() == 1)  engine.setModWheel ((float) m.getControllerValue() / 127.0f);
        if (m.getControllerNumber() == 64) engine.setSustain (m.getControllerValue() >= 64);
    }
}

void LumenSynthProcessor::renderSynth (float* L, float* R, int n, juce::MidiBuffer& midi, int off, int factor)
{
    float* rl = L; float* rr = R;
    if (factor == 2)
    {
        std::fill (osL.begin(), osL.begin() + 2 * n, 0.0f);
        std::fill (osR.begin(), osR.begin() + 2 * n, 0.0f);
        rl = osL.data(); rr = osR.data();
    }
    int pos = 0;
    for (auto it = midi.findNextSamplePosition (off); it != midi.end(); ++it)
    {
        const auto meta = *it;
        if (meta.samplePosition >= off + n) break;
        const int s = (meta.samplePosition - off) * factor;
        if (s > pos) { engine.render (rl + pos, rr + pos, s - pos); pos = s; }
        handleMidi (meta.getMessage());
    }
    if (pos < n * factor) engine.render (rl + pos, rr + pos, n * factor - pos);
    if (factor == 2) decim.process (osL.data(), osR.data(), L, R, n);
}

void LumenSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0) return;

    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    fillPatch();

    double bpm = 120.0, ppq = 0.0; bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }
    hostBpm.store ((float) bpm, std::memory_order_relaxed);

    const int factor = P ("hq") > 0.5f ? 2 : 1;
    if (factor != engineFactor) { engineFactor = factor; engine.prepare (baseRate * factor, baseRate); decim.reset(); }
    engine.setPatch (patch);
    engine.setTransport (bpm, playing, ppq);
    engine.beginBlock();

    Arp::Params ap;
    ap.on = P ("arp_on") > 0.5f; ap.mode = (int) P ("arp_mode"); ap.beats = arpBeats ((int) P ("arp_rate"));
    ap.octaves = (int) std::lround (P ("arp_oct")); ap.gate = P ("arp_gate"); ap.swing = P ("arp_swing"); ap.chord = (int) P ("chord");
    arp.process (midi, n, ap, bpm, playing, ppq);
    if (ap.on) for (const auto m : midi) if (m.getMessage().isNoteOn()) { ++arpStepFlash; break; }

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    juce::HeapBlock<float> tmpR;
    if (R == nullptr) { tmpR.calloc ((size_t) n); R = tmpR.get(); }

    for (int start = 0; start < n; start += kChunk)
        renderSynth (L + start, R + start, juce::jmin (kChunk, n - start), midi, start, factor);

    float src[kNumSrc];
    engine.globalSources (src);
    engine.globalFxMods (src, fxp.mod);
    fxp.applyMods();
    fx.process (L, R, n, fxp, bpm, playing, ppq);

    if (buffer.getNumChannels() == 1)
        for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + R[i]);

    // ---- meters / scope / UI views ----
    float pl = 0.0f, pr = 0.0f;
    int sp = scopePos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        pl = std::max (pl, std::fabs (L[i])); pr = std::max (pr, std::fabs (R[i]));
        scope[sp] = 0.5f * (L[i] + R[i]); sp = (sp + 1) & (kScope - 1);
    }
    scopePos.store (sp, std::memory_order_release);
    peakL.store (pl); peakR.store (pr);
    float lv[16]; engine.voiceLevels (lv);
    for (int k = 0; k < 16; ++k) voiceLevel[k].store (lv[k], std::memory_order_relaxed);
    activeVoices.store (engine.activeVoices(), std::memory_order_relaxed);
    pumpGain.store (fxp.pumpOn ? fx.pumpGain() : 1.0f, std::memory_order_relaxed);
    compGr.store (fxp.cmpOn ? fx.compGrDb() : 0.0f, std::memory_order_relaxed);
    for (int k = 0; k < 3; ++k) ottBand[k].store (fxp.ottOn ? fx.ottBand (k) : 1.0f, std::memory_order_relaxed);
    for (int l = 0; l < kLfo; ++l) lfoPhase[l].store (engine.globalLfoPhase (l), std::memory_order_relaxed);
    for (int s = 0; s < kNumSrc; ++s) srcView[s].store (src[s], std::memory_order_relaxed);
    midi.clear();
}

//==============================================================================
// Wavetables
void LumenSynthProcessor::setUserTable (int osc, std::shared_ptr<lumen::Table> t)
{
    {
        const juce::ScopedLock sl (extrasLock);
        if (userTables[osc] != nullptr) retired.push_back (userTables[osc]);
        userTables[osc] = std::move (t);
        userTablePtr[osc].store (userTables[osc].get(), std::memory_order_release);
        while (retired.size() > 6) retired.erase (retired.begin());
    }
    ++tableVersion;
}

bool LumenSynthProcessor::loadWavetableFile (int osc, const juce::File& f, juce::String& error)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr) { error = "Can't read this file (use WAV/AIFF/FLAC)"; return false; }
    const auto len = (int) std::min<juce::int64> (reader->lengthInSamples, 256 * 2048 * 2);
    if (len < 16) { error = "File is empty"; return false; }
    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, reader->numChannels), len);
    reader->read (&buf, 0, len, 0, true, true);
    std::vector<float> mono ((size_t) len, 0.0f);
    for (int c = 0; c < buf.getNumChannels(); ++c)
        for (int i = 0; i < len; ++i) mono[(size_t) i] += buf.getSample (c, i) / (float) buf.getNumChannels();
    auto t = lumen::Table::fromAudio (mono, f.getFileNameWithoutExtension().toStdString());
    if (t == nullptr) { error = "Import failed"; return false; }
    setUserTable (osc, t);
    const juce::String id = juce::String (osc == 0 ? "a" : osc == 1 ? "b" : "c") + "_table";
    if (auto* prm = apvts.getParameter (id)) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) kNumFactoryTables)); prm->endChangeGesture(); }
    if (auto* prm = apvts.getParameter (juce::String (osc == 0 ? "a" : osc == 1 ? "b" : "c") + "_on")) prm->setValueNotifyingHost (1.0f);
    return true;
}

juce::String LumenSynthProcessor::userTableName (int osc) const
{
    const juce::ScopedLock sl (extrasLock);
    return userTables[osc] != nullptr ? juce::String (userTables[osc]->name) : juce::String();
}

const lumen::Table* LumenSynthProcessor::tableFor (int osc) const
{
    const char* id = osc == 0 ? "a_table" : osc == 1 ? "b_table" : "c_table";
    const int t = (int) P (id);
    return t < kNumFactoryTables ? &lumen::WavetableBank::get().table (t) : userTablePtr[osc].load();
}

//==============================================================================
// LFO curves
void LumenSynthProcessor::rebuildLut (int l)
{
    const int inactive = 1 - lutActive[l].load();
    lumen::renderCurve (lfoPts[l], 0.0f, lut[l][inactive], kLut);
    lutActive[l].store (inactive, std::memory_order_release);
}

std::vector<lumen::CurvePoint> LumenSynthProcessor::getLfoPoints (int l) const
{
    const juce::ScopedLock sl (extrasLock);
    return lfoPts[l];
}

void LumenSynthProcessor::setLfoPoints (int l, std::vector<lumen::CurvePoint> pts)
{
    if (pts.size() < 2) return;
    std::sort (pts.begin(), pts.end(), [] (auto a, auto b) { return a.x < b.x; });
    pts.front().x = 0.0f; pts.back().x = 1.0f;
    for (auto& p : pts) { p.x = juce::jlimit (0.0f, 1.0f, p.x); p.y = juce::jlimit (0.0f, 1.0f, p.y); }
    {
        const juce::ScopedLock sl (extrasLock);
        lfoPts[l] = std::move (pts);
        rebuildLut (l);
    }
}

void LumenSynthProcessor::setLfoPointsFromShape (int l, int shape)
{
    std::vector<lumen::CurvePoint> pts;
    if (shape == lSquare) pts = { { 0, 1 }, { 0.499f, 1 }, { 0.5f, 0 }, { 1, 0 } };
    else if (shape == lSawUp) pts = { { 0, 0 }, { 1, 1 } };
    else if (shape == lSawDown) pts = { { 0, 1 }, { 1, 0 } };
    else if (shape == lTri) pts = { { 0, 0.5f }, { 0.25f, 1 }, { 0.75f, 0 }, { 1, 0.5f } };
    else
    {
        const int N = 16;
        for (int i = 0; i <= N; ++i)
        {
            const float x = (float) i / N;
            pts.push_back ({ x, 0.5f + 0.5f * lfoShape (shape == lCustom ? lSine : shape, juce::jmin (x, 0.9999f), 0.0f, 0.5f) });
        }
    }
    setLfoPoints (l, pts);
    if (auto* prm = apvts.getParameter ("l" + juce::String (l + 1) + "_shape"))
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) lCustom));
}

//==============================================================================
// FX order
std::array<int, kNumFx> LumenSynthProcessor::getFxOrder() const
{
    std::array<int, kNumFx> o {};
    for (int i = 0; i < kNumFx; ++i) o[(size_t) i] = fxOrder[i].load();
    return o;
}

void LumenSynthProcessor::moveFx (int slot, int delta)
{
    const int other = slot + delta;
    if (slot < 0 || slot >= kNumFx || other < 0 || other >= kNumFx) return;
    const int a = fxOrder[slot].load(), b = fxOrder[other].load();
    fxOrder[slot].store (b); fxOrder[other].store (a);
    ++orderVersion;
}

//==============================================================================
// Modulation
int LumenSynthProcessor::assignMod (int src, int dst, float amount)
{
    if (src <= 0 || dst <= 0) return -1;
    int freeSlot = -1;
    for (int m = 0; m < kSlots; ++m)
    {
        const std::string p = "m" + std::to_string (m + 1) + "_";
        const int s = (int) raw.at (p + "src")->load(), d = (int) raw.at (p + "dst")->load();
        if (s == src && d == dst) return m;
        if (freeSlot < 0 && (s == 0 || d == 0)) freeSlot = m;
    }
    if (freeSlot < 0) return -1;
    const juce::String p = "m" + juce::String (freeSlot + 1) + "_";
    auto set = [this] (const juce::String& id, float v)
    { if (auto* prm = apvts.getParameter (id)) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture(); } };
    set (p + "src", (float) src); set (p + "dst", (float) dst); set (p + "amt", amount);
    return freeSlot;
}

void LumenSynthProcessor::clearModSlot (int slot)
{
    const juce::String p = "m" + juce::String (slot + 1) + "_";
    for (auto id : { p + "src", p + "dst", p + "amt" })
        if (auto* prm = apvts.getParameter (id)) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (0.0f)); prm->endChangeGesture(); }
}

void LumenSynthProcessor::setModAmount (int slot, float amt)
{
    if (auto* prm = apvts.getParameter ("m" + juce::String (slot + 1) + "_amt"))
    { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (-1.0f, 1.0f, amt))); prm->endChangeGesture(); }
}

std::vector<LumenSynthProcessor::ModInfo> LumenSynthProcessor::modsForDest (int dst) const
{
    std::vector<ModInfo> r;
    if (dst <= 0) return r;
    for (int m = 0; m < kSlots; ++m)
    {
        const std::string p = "m" + std::to_string (m + 1) + "_";
        const int s = (int) raw.at (p + "src")->load(), d = (int) raw.at (p + "dst")->load();
        const float a = raw.at (p + "amt")->load();
        if (s > 0 && d == dst) r.push_back ({ m, s, a });
    }
    return r;
}

//==============================================================================
// Presets
int LumenSynthProcessor::getNumPrograms() { return (int) factoryPresets().size(); }
const juce::String LumenSynthProcessor::getProgramName (int index)
{
    const auto& p = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) p.size()) ? p[(size_t) index].name : juce::String();
}
void LumenSynthProcessor::setCurrentProgram (int index) { loadPreset (index); }

void LumenSynthProcessor::applyValues (const std::vector<std::pair<juce::String, float>>& values, bool resetOthers)
{
    for (auto& s : specs())
    {
        float v = s.def;
        bool found = false;
        for (auto& kv : values) if (kv.first == s.id) { v = kv.second; found = true; }
        if (! found && (! resetOthers || s.id == "hq")) continue;
        if (auto* prm = apvts.getParameter (s.id))
        {
            v = juce::jlimit (s.lo, s.hi, v);
            const float norm = prm->convertTo0to1 (v);
            if (std::abs (prm->getValue() - norm) > 1.0e-6f) { prm->beginChangeGesture(); prm->setValueNotifyingHost (norm); prm->endChangeGesture(); }
        }
    }
}

void LumenSynthProcessor::resetExtras()
{
    for (int l = 0; l < kLfo; ++l) setLfoPoints (l, defaultCurve());
    for (int i = 0; i < kNumFx; ++i) fxOrder[i].store (i);
    ++orderVersion;
}

void LumenSynthProcessor::loadPreset (int index)
{
    const auto& p = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) p.size())) return;
    pushUndo();
    currentPreset = index;
    resetExtras();
    applyValues (p[(size_t) index].v, true);
    presetName = p[(size_t) index].name;
    presetCategory = p[(size_t) index].category;
    for (int m = 0; m < 4; ++m) macroNames[m] = p[(size_t) index].macros[m].isNotEmpty() ? p[(size_t) index].macros[m] : "MACRO " + juce::String (m + 1);
    ++presetVersion;
}

juce::File LumenSynthProcessor::presetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Lumen Audio").getChildFile ("Lumen Synth").getChildFile ("Presets");
}

std::vector<LumenSynthProcessor::PresetEntry> LumenSynthProcessor::listPresets() const
{
    std::vector<PresetEntry> r;
    const auto& fp = factoryPresets();
    for (int i = 0; i < (int) fp.size(); ++i) r.push_back ({ fp[(size_t) i].name, fp[(size_t) i].category, false, i, {} });
    const auto folder = presetFolder();
    if (folder.isDirectory())
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, true, "*.lsp"))
        {
            const auto cat = f.getParentDirectory() == folder ? juce::String ("User") : f.getParentDirectory().getFileName();
            r.push_back ({ f.getFileNameWithoutExtension(), cat, true, -1, f });
        }
    return r;
}

bool LumenSynthProcessor::loadPresetEntry (const PresetEntry& e)
{
    if (! e.user) { loadPreset (e.factoryIndex); return true; }
    juce::MemoryBlock mb;
    if (! e.file.loadFileAsData (mb)) return false;
    pushUndo();
    setStateInformation (mb.getData(), (int) mb.getSize());
    presetName = e.name; presetCategory = e.category; currentPreset = -1;
    ++presetVersion;
    return true;
}

bool LumenSynthProcessor::saveUserPreset (const juce::String& name, const juce::String& category)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    const auto dir = presetFolder().getChildFile (juce::File::createLegalFileName (category.isEmpty() ? juce::String ("User") : category));
    if (! dir.createDirectory()) return false;
    presetName = clean; presetCategory = category; currentPreset = -1;
    juce::MemoryBlock mb;
    getStateInformation (mb);
    const bool ok = dir.getChildFile (clean + ".lsp").replaceWithData (mb.getData(), mb.getSize());
    ++presetVersion;
    return ok;
}

bool LumenSynthProcessor::deleteUserPreset (const PresetEntry& e)
{
    if (! e.user) return false;
    if (isFavourite (e)) toggleFavourite (e);
    return e.file.deleteFile();
}

juce::String LumenSynthProcessor::favKey (const PresetEntry& e) const
{
    return e.user ? "U:" + e.category + "/" + e.name : "F:" + e.name;
}
bool LumenSynthProcessor::isFavourite (const PresetEntry& e) const { return favourites.contains (favKey (e)); }
void LumenSynthProcessor::toggleFavourite (const PresetEntry& e)
{
    const auto k = favKey (e);
    if (favourites.contains (k)) favourites.removeString (k); else favourites.add (k);
    presetFolder().createDirectory();
    favouritesFile().replaceWithText (favourites.joinIntoString ("\n"));
}

//==============================================================================
// Smart
void LumenSynthProcessor::pushUndo()
{
    juce::MemoryBlock mb;
    getStateInformation (mb);
    undoStack.push_back (std::move (mb));
    if (undoStack.size() > 24) undoStack.erase (undoStack.begin());
}

bool LumenSynthProcessor::undo()
{
    if (undoStack.empty()) return false;
    const auto mb = undoStack.back(); undoStack.pop_back();
    setStateInformation (mb.getData(), (int) mb.getSize());
    return true;
}

void LumenSynthProcessor::generate (int category, juce::Random& rnd)
{
    pushUndo();
    resetExtras();
    applyValues (lumen::synth::generatePatch (category, rnd), true);
    presetName = lumen::synth::generatedName (category, rnd);
    presetCategory = categories()[category];
    currentPreset = -1;
    ++presetVersion;
}

void LumenSynthProcessor::mutate (float amount, juce::Random& rnd)
{
    pushUndo();
    std::vector<std::pair<juce::String, float>> vals;
    for (auto& s : specs())
    {
        auto* prm = apvts.getParameter (s.id);
        const float cur = prm->convertFrom0to1 (prm->getValue());
        vals.push_back ({ s.id, lumen::synth::mutateValue (s, cur, prm->getValue(), amount, rnd,
                                                           [prm] (float x) { return prm->convertFrom0to1 (x); }) });
    }
    applyValues (vals, false);
    if (! presetName.endsWith ("*")) presetName << " *";
    ++presetVersion;
}

//==============================================================================
// State
void LumenSynthProcessor::writeExtras (juce::ValueTree& vt) const
{
    juce::ValueTree ex ("Extras");
    ex.setProperty ("presetName", presetName, nullptr);
    ex.setProperty ("presetCategory", presetCategory, nullptr);
    ex.setProperty ("presetIndex", currentPreset, nullptr);
    { juce::StringArray mn; for (auto& m : macroNames) mn.add (m); ex.setProperty ("macroNames", mn.joinIntoString ("|"), nullptr); }
    juce::StringArray ord;
    for (int i = 0; i < kNumFx; ++i) ord.add (juce::String (fxOrder[i].load()));
    ex.setProperty ("fxOrder", ord.joinIntoString (","), nullptr);
    const juce::ScopedLock sl (extrasLock);
    for (int o = 0; o < kOsc; ++o)
    {
        if (userTables[o] == nullptr) continue;
        const auto& src = userTables[o]->source;
        float peak = 1.0e-9f; for (float x : src) peak = std::max (peak, std::abs (x));
        juce::MemoryBlock mb (src.size() * sizeof (juce::int16));
        auto* d = static_cast<juce::int16*> (mb.getData());
        for (size_t i = 0; i < src.size(); ++i) d[i] = (juce::int16) juce::jlimit (-32767, 32767, (int) std::lround (src[i] / peak * 32767.0f));
        juce::ValueTree w ("Wavetable");
        w.setProperty ("osc", o, nullptr);
        w.setProperty ("name", juce::String (userTables[o]->name), nullptr);
        w.setProperty ("frames", userTables[o]->frames, nullptr);
        w.setProperty ("peak", peak, nullptr);
        w.setProperty ("data", mb.toBase64Encoding(), nullptr);
        ex.appendChild (w, nullptr);
    }
    for (int l = 0; l < kLfo; ++l)
    {
        juce::StringArray pts;
        for (auto& p : lfoPts[l]) pts.add (juce::String (p.x, 4) + "," + juce::String (p.y, 4));
        juce::ValueTree lt ("Lfo");
        lt.setProperty ("idx", l, nullptr);
        lt.setProperty ("points", pts.joinIntoString (";"), nullptr);
        ex.appendChild (lt, nullptr);
    }
    vt.appendChild (ex, nullptr);
}

void LumenSynthProcessor::readExtras (const juce::ValueTree& vt)
{
    const auto ex = vt.getChildWithName ("Extras");
    presetName = ex.getProperty ("presetName", "Init Saw").toString();
    presetCategory = ex.getProperty ("presetCategory", "Lead").toString();
    currentPreset = (int) ex.getProperty ("presetIndex", 0);
    {
        auto mn = juce::StringArray::fromTokens (ex.getProperty ("macroNames", "").toString(), "|", "");
        for (int m = 0; m < 4; ++m) macroNames[m] = m < mn.size() && mn[m].isNotEmpty() ? mn[m] : "MACRO " + juce::String (m + 1);
    }
    {
        auto ord = juce::StringArray::fromTokens (ex.getProperty ("fxOrder", "").toString(), ",", "");
        bool ok = ord.size() == kNumFx;
        std::array<bool, kNumFx> seen {};
        for (int i = 0; ok && i < kNumFx; ++i) { const int v = ord[i].getIntValue(); ok = v >= 0 && v < kNumFx && ! seen[(size_t) v]; if (ok) seen[(size_t) v] = true; }
        for (int i = 0; i < kNumFx; ++i) fxOrder[i].store (ok ? ord[i].getIntValue() : i);
        ++orderVersion;
    }
    bool hasTable[kOsc] {};
    for (const auto& c : ex)
    {
        if (c.hasType ("Wavetable"))
        {
            const int o = (int) c.getProperty ("osc", -1);
            if (o < 0 || o >= kOsc) continue;
            juce::MemoryBlock mb;
            if (! mb.fromBase64Encoding (c.getProperty ("data").toString())) continue;
            const int frames = juce::jlimit (1, 256, (int) c.getProperty ("frames", 1));
            const float peak = (float) c.getProperty ("peak", 1.0f);
            const size_t count = std::min ((size_t) frames * lumen::Table::kSize, mb.getSize() / sizeof (juce::int16));
            std::vector<float> cyc ((size_t) frames * lumen::Table::kSize, 0.0f);
            const auto* d = static_cast<const juce::int16*> (mb.getData());
            for (size_t i = 0; i < count; ++i) cyc[i] = (float) d[i] / 32767.0f * peak;
            setUserTable (o, lumen::Table::fromCycles (cyc, frames, c.getProperty ("name").toString().toStdString()));
            hasTable[o] = true;
        }
        else if (c.hasType ("Lfo"))
        {
            const int l = (int) c.getProperty ("idx", -1);
            if (l < 0 || l >= kLfo) continue;
            std::vector<lumen::CurvePoint> pts;
            for (auto& tok : juce::StringArray::fromTokens (c.getProperty ("points").toString(), ";", ""))
            {
                const auto xy = juce::StringArray::fromTokens (tok, ",", "");
                if (xy.size() == 2) pts.push_back ({ xy[0].getFloatValue(), xy[1].getFloatValue() });
            }
            setLfoPoints (l, pts.size() >= 2 ? pts : defaultCurve());
        }
    }
    juce::ignoreUnused (hasTable);
}

void LumenSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    for (int i = state.getNumChildren(); --i >= 0;) if (state.getChild (i).hasType ("Extras")) state.removeChild (i, nullptr);
    writeExtras (state);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenSynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            readExtras (vt);
            for (int i = vt.getNumChildren(); --i >= 0;) if (vt.getChild (i).hasType ("Extras")) vt.removeChild (i, nullptr);
            apvts.replaceState (vt);
            for (auto& sp : specs())
            {
                auto child = vt.getChildWithProperty ("id", sp.id);
                auto* prm = apvts.getParameter (sp.id);
                if (prm == nullptr) continue;
                const float v = child.isValid() && child.hasProperty ("value") ? (float) child.getProperty ("value") : sp.def;
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            }
            ++presetVersion;
        }
}

juce::AudioProcessorEditor* LumenSynthProcessor::createEditor() { return new LumenSynthEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenSynthProcessor(); }
