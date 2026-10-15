#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Smart.h"

using namespace lumen::synth;

LumenSynthProcessor::LumenSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenSynth", createLayout())
{
    for (auto& s : specs()) raw[s.id.toStdString()] = apvts.getRawParameterValue (s.id);
    for (auto& v : voiceLevel) v.store (0.0f);
    loadPreset (0);
    undoStack.clear(); undoNames.clear();
}

bool LumenSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void LumenSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    fx.prepare (sampleRate, juce::jmax (16, samplesPerBlock));
    fillPatch();
    engine.setPatch (patch);
    keyboardState.reset();
}

void LumenSynthProcessor::fillPatch()
{
    for (int o = 0; o < 2; ++o)
    {
        const std::string p = o == 0 ? "a_" : "b_";
        auto g = [&] (const char* k) { return raw.at (p + k)->load (std::memory_order_relaxed); };
        auto& op = patch.osc[o];
        op.on = g ("on") > 0.5f; op.table = (int) g ("table"); op.pos = g ("pos");
        op.warpMode = (int) g ("warpm"); op.warp = g ("warp");
        op.oct = (int) std::lround (g ("oct")); op.semi = (int) std::lround (g ("semi")); op.fine = g ("fine");
        op.level = g ("level"); op.pan = g ("pan"); op.uni = (int) std::lround (g ("uni"));
        op.det = g ("det"); op.blend = g ("blend"); op.width = g ("width"); op.rand = g ("rand");
    }
    patch.subOn = P ("sub_on") > 0.5f; patch.subShape = (int) P ("sub_shape"); patch.subOct = (int) P ("sub_oct"); patch.subLevel = P ("sub_level");
    patch.noiseOn = P ("noise_on") > 0.5f; patch.noiseLevel = P ("noise_level"); patch.noiseColor = P ("noise_color");
    patch.fltOn = P ("flt_on") > 0.5f; patch.fltType = (int) P ("flt_type"); patch.cutoff = P ("flt_cut"); patch.res = P ("flt_res");
    patch.drive = P ("flt_drive"); patch.fltEnv = P ("flt_env"); patch.keytrack = P ("flt_key");
    static const char* envIds[3][4] = { { "e1_a", "e1_d", "e1_s", "e1_r" }, { "e2_a", "e2_d", "e2_s", "e2_r" }, { "e3_a", "e3_d", "e3_s", "e3_r" } };
    for (int e = 0; e < 3; ++e) for (int k = 0; k < 4; ++k) patch.env[e][k] = P (envIds[e][k]);
    static const char* lfoIds[2][5] = { { "l1_shape", "l1_rate", "l1_sync", "l1_div", "l1_retrig" }, { "l2_shape", "l2_rate", "l2_sync", "l2_div", "l2_retrig" } };
    for (int l = 0; l < 2; ++l)
    {
        patch.lfoShape[l] = (int) P (lfoIds[l][0]); patch.lfoRate[l] = P (lfoIds[l][1]); patch.lfoSync[l] = P (lfoIds[l][2]) > 0.5f;
        patch.lfoDiv[l] = (int) P (lfoIds[l][3]); patch.lfoRetrig[l] = P (lfoIds[l][4]) > 0.5f;
        patch.lfoBeats[l] = divBeats (patch.lfoDiv[l]);
    }
    static const char* modIds[8][3] = { { "m1_src", "m1_dst", "m1_amt" }, { "m2_src", "m2_dst", "m2_amt" }, { "m3_src", "m3_dst", "m3_amt" }, { "m4_src", "m4_dst", "m4_amt" },
                                        { "m5_src", "m5_dst", "m5_amt" }, { "m6_src", "m6_dst", "m6_amt" }, { "m7_src", "m7_dst", "m7_amt" }, { "m8_src", "m8_dst", "m8_amt" } };
    for (int m = 0; m < 8; ++m) { patch.mod[m].src = (int) P (modIds[m][0]); patch.mod[m].dst = (int) P (modIds[m][1]); patch.mod[m].amt = P (modIds[m][2]); }
    static const char* macroIds[4] = { "macro1", "macro2", "macro3", "macro4" };
    for (int m = 0; m < 4; ++m) patch.macro[m] = P (macroIds[m]);
    patch.voices = (int) std::lround (P ("voices")); patch.legato = P ("legato") > 0.5f; patch.glide = P ("glide");
    patch.bend = (int) std::lround (P ("bend")); patch.velSens = P ("velsens");

    fxp.distOn = P ("dist_on") > 0.5f; fxp.distMode = (int) P ("dist_mode"); fxp.distDrive = P ("dist_drive"); fxp.distMix = P ("dist_mix");
    fxp.ottOn = P ("ott_on") > 0.5f; fxp.ottDepth = P ("ott_depth"); fxp.ottTime = P ("ott_time");
    fxp.choOn = P ("cho_on") > 0.5f; fxp.choRate = P ("cho_rate"); fxp.choDepth = P ("cho_depth"); fxp.choMix = P ("cho_mix");
    fxp.dlyOn = P ("dly_on") > 0.5f; fxp.dlyBeats = divBeats ((int) P ("dly_div")); fxp.dlyFb = P ("dly_fb"); fxp.dlyTone = P ("dly_tone");
    fxp.dlyMix = P ("dly_mix"); fxp.dlyPing = P ("dly_ping") > 0.5f;
    fxp.revOn = P ("rev_on") > 0.5f; fxp.revSize = P ("rev_size"); fxp.revDecay = P ("rev_decay"); fxp.revDamp = P ("rev_damp"); fxp.revMix = P ("rev_mix");
    static const double pumpBeats[] = { 4, 2, 1, 0.5, 0.25 };
    fxp.pumpOn = P ("pump_on") > 0.5f; fxp.pumpBeats = pumpBeats[juce::jlimit (0, 4, (int) P ("pump_div"))];
    fxp.pumpDepth = P ("pump_depth"); fxp.pumpCurve = P ("pump_curve");
    fxp.volDb = P ("vol"); fxp.clip = P ("clip") > 0.5f;
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

void LumenSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0) return;

    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    fillPatch();
    engine.setPatch (patch);

    double bpm = 120.0, ppq = 0.0; bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }
    engine.setTransport (bpm, playing, ppq);
    hostBpm.store ((float) bpm, std::memory_order_relaxed);
    engine.beginBlock (n);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    juce::HeapBlock<float> tmpR;
    if (R == nullptr) { tmpR.calloc ((size_t) n); R = tmpR.get(); }

    int pos = 0;
    for (const auto meta : midi)
    {
        const int s = juce::jlimit (0, n, meta.samplePosition);
        if (s > pos) { engine.render (L + pos, R + pos, s - pos); pos = s; }
        handleMidi (meta.getMessage());
    }
    if (pos < n) engine.render (L + pos, R + pos, n - pos);

    fx.process (L, R, n, fxp, bpm, playing, ppq);

    if (buffer.getNumChannels() == 1)
        for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + R[i]);

    // ---- meters / scope ----
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
    midi.clear();
}

//==============================================================================
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
        if (! found && ! resetOthers) continue;
        if (auto* prm = apvts.getParameter (s.id))
        {
            v = juce::jlimit (s.lo, s.hi, v);
            const float norm = prm->convertTo0to1 (v);
            if (std::abs (prm->getValue() - norm) > 1.0e-6f)
            {
                prm->beginChangeGesture(); prm->setValueNotifyingHost (norm); prm->endChangeGesture();
            }
        }
    }
}

void LumenSynthProcessor::loadPreset (int index)
{
    const auto& p = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) p.size())) return;
    pushUndo();
    currentPreset = index;
    applyValues (p[(size_t) index].v, true);
    presetName = p[(size_t) index].name;
    presetCategory = p[(size_t) index].category;
    ++presetVersion;
}

void LumenSynthProcessor::pushUndo()
{
    std::vector<float> snap;
    for (auto& s : specs()) snap.push_back (apvts.getParameter (s.id)->getValue());
    undoStack.push_back (std::move (snap));
    undoNames.push_back ({ presetName, presetCategory });
    if (undoStack.size() > 32) { undoStack.erase (undoStack.begin()); undoNames.erase (undoNames.begin()); }
}

bool LumenSynthProcessor::undo()
{
    if (undoStack.empty()) return false;
    const auto snap = undoStack.back(); undoStack.pop_back();
    const auto nm = undoNames.back(); undoNames.pop_back();
    size_t i = 0;
    for (auto& s : specs())
    {
        auto* prm = apvts.getParameter (s.id);
        if (std::abs (prm->getValue() - snap[i]) > 1.0e-6f) { prm->beginChangeGesture(); prm->setValueNotifyingHost (snap[i]); prm->endChangeGesture(); }
        ++i;
    }
    presetName = nm.name; presetCategory = nm.cat;
    ++presetVersion;
    return true;
}

void LumenSynthProcessor::generate (int category, juce::Random& rnd)
{
    pushUndo();
    const auto vals = lumen::synth::generatePatch (category, rnd);
    applyValues (vals, true);
    presetName = lumen::synth::generatedName (category, rnd);
    presetCategory = categories()[category];
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
                                                           [prm] (float n) { return prm->convertFrom0to1 (n); }) });
    }
    applyValues (vals, false);
    if (! presetName.endsWith ("*")) presetName << " *";
    ++presetVersion;
}

//==============================================================================
void LumenSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", presetName, nullptr);
    state.setProperty ("presetCategory", presetCategory, nullptr);
    state.setProperty ("presetIndex", currentPreset, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenSynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            presetName = vt.getProperty ("presetName", "Init Saw").toString();
            presetCategory = vt.getProperty ("presetCategory", "Lead").toString();
            currentPreset = (int) vt.getProperty ("presetIndex", 0);
            apvts.replaceState (vt);
            // Force every parameter to the stored value (APVTS skips values that look unchanged after snapping)
            for (auto& sp : specs())
            {
                auto child = vt.getChildWithProperty ("id", sp.id);
                if (! child.isValid() || ! child.hasProperty ("value")) continue;
                if (auto* prm = apvts.getParameter (sp.id))
                    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) child.getProperty ("value")));
            }
            ++presetVersion;
        }
}

juce::AudioProcessorEditor* LumenSynthProcessor::createEditor() { return new LumenSynthEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenSynthProcessor(); }
