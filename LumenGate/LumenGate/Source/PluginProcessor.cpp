#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenGateProcessor::LumenGateProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "LumenGate", createLayout())
{
}

bool LumenGateProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    if (out != mono && out != stereo) return false;
    if (in != mono && in != stereo) return false;
    if (in == stereo && out == mono) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != mono && sc != stereo) return false;
    }
    return true;
}

void LumenGateProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    engine.prepare (fs, (int) std::ceil (0.0101 * fs));
    heldNotes = 0; histN = 0; acc = {};
    setLatencySamples ((int) std::round (P (IDs::lookahead) * 0.001 * fs));
}

void LumenGateProcessor::setParam (const juce::String& id, float v)
{
    if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
    { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture(); }
}

void LumenGateProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    const auto& pr = factoryPresets()[(size_t) index];
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (rp->paramID.startsWith ("s") && rp->paramID.substring (1).containsOnly ("0123456789")) continue;
            float v = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : pr.v) if (rp->paramID == kv.first) v = kv.second;
            if (pr.pattern != nullptr && rp->paramID == IDs::steps)
            {
                const int len = (int) std::strlen (pr.pattern);
                v = len <= 8 ? 0.0f : len <= 16 ? 1.0f : 2.0f;
            }
            rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture();
        }
    // steps: from the pattern string (tiled to 32), or the default alternating pattern
    const juce::String pat = pr.pattern != nullptr ? juce::String (pr.pattern) : juce::String ("X-X-X-X-X-X-X-X-");
    for (int i = 0; i < 32; ++i)
    {
        const auto ch = pat[i % pat.length()];
        const float v = ch == 'X' ? 1.0f : ch == 'x' ? 0.7f : ch == 'o' ? 0.45f : ch == '.' ? 0.25f : 0.0f;
        setParam (IDs::step (i), v);
    }
    ++programVersion;
}

void LumenGateProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const int n = buffer.getNumSamples();
    if (n == 0 || main.getNumChannels() == 0) return;
    if (getMainBusNumInputChannels() == 1 && main.getNumChannels() > 1) main.copyFrom (1, 0, main, 0, 0, n);
    const int nch = juce::jmin (2, main.getNumChannels());

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) ++heldNotes;
        else if (m.isNoteOff()) heldNotes = juce::jmax (0, heldNotes - 1);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) heldNotes = 0;
    }

    lumen::gate::Params p;
    p.mode = (int) P (IDs::mode); p.style = (int) P (IDs::style);
    p.thrDb = P (IDs::thr); p.rangeDb = P (IDs::range); p.ratio = P (IDs::ratio); p.attMs = P (IDs::attack); p.holdMs = P (IDs::hold);
    p.relMs = P (IDs::release); p.kneeDb = P (IDs::knee); p.hystDb = P (IDs::hyst);
    const int la = (int) std::round (P (IDs::lookahead) * 0.001 * fs);
    p.lookahead = la;
    if (la != getLatencySamples()) setLatencySamples (la);
    p.scHp = P (IDs::scHp); p.scLp = P (IDs::scLp); p.listen = P (IDs::listen) > 0.5f; p.link = P (IDs::link); p.midiTrig = P (IDs::midi) > 0.5f;
    p.steps = stepsFor ((int) P (IDs::steps)); p.stepBeats = rateBeats ((int) P (IDs::rate)); p.swing = P (IDs::swing); p.gateLen = P (IDs::gateLen);
    p.pAttMs = P (IDs::pAttack); p.pRelMs = P (IDs::pRelease); p.depth = P (IDs::depth); p.stereoOffset = P (IDs::stereo);
    for (int i = 0; i < 32; ++i) p.level[i] = P (IDs::step (i));
    p.mix = P (IDs::mix); p.outDb = P (IDs::out);

    const float* scP[2] { nullptr, nullptr };
    bool scOk = false;
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            if (sc.getNumChannels() > 0) { scP[0] = sc.getReadPointer (0); scP[1] = sc.getReadPointer (sc.getNumChannels() > 1 ? 1 : 0); scOk = true; }
        }
    scConnected.store (scOk);
    const bool useExt = scOk && P (IDs::sc) > 0.5f;
    engine.setParams (p);

    // transport: host position while playing, otherwise a free-running clock at the host tempo
    double bpm = 120.0, ppq = freePpq; bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition())
    {
        if (auto b = pos->getBpm()) bpm = *b;
        playing = pos->getIsPlaying();
        if (playing) if (auto q = pos->getPpqPosition()) ppq = *q;
    }
    const double bps = bpm / 60.0 / fs;

    float* L = main.getWritePointer (0); float* R = main.getWritePointer (nch - 1);
    for (int start = 0; start < n; start += kFrame)
    {
        const int len = juce::jmin (kFrame, n - start);
        engine.process (L + start, R + start, useExt ? scP[0] + start : nullptr, useExt ? scP[1] + start : nullptr, len, ppq + bps * start, bps, heldNotes > 0);
        acc.in = juce::jmax (acc.in, engine.meterIn); acc.out = juce::jmax (acc.out, engine.meterOut);
        acc.key = juce::jmax (acc.key, engine.meterKey);
        acc.gr = p.style == lumen::gate::Upward ? juce::jmax (acc.gr, engine.meterUp) : juce::jmin (acc.gr, engine.meterGr);
        if (learning.load())
        {
            const int lp = lrPos.load();
            if (lp < kLearnFrames) { lrBuf[lp] = engine.meterKey; lrPos.store (lp + 1); }
        }
        engine.resetMeters();
        histN += len;
        if (histN >= kFrame)
        {
            const int hp = histPos.load (std::memory_order_relaxed);
            hist[hp] = acc; acc = {};
            histPos.store ((hp + 1) % kHist, std::memory_order_release);
            histN = 0;
        }
    }
    freePpq = playing ? ppq + bps * n : freePpq + bps * n;
    curStep.store (engine.currentStep (ppq + bps * n));
    gateOpen.store (engine.isOpen());
}

//==============================================================================
void LumenGateProcessor::startLearn() { lrPos.store (0); learnResult.clear(); learning.store (true); }

void LumenGateProcessor::finishLearnIfReady()
{
    if (! learning.load() || lrPos.load() < kLearnFrames) return;
    learning.store (false);
    std::vector<float> v (lrBuf, lrBuf + kLearnFrames);
    std::sort (v.begin(), v.end());
    const float floorDb = v[(size_t) (kLearnFrames * 0.10)], sig = v[(size_t) (kLearnFrames * 0.95)];
    if (sig < -90.0f) { learnResult = "nothing heard - play the track"; ++programVersion; return; }
    const float gap = sig - floorDb;
    if (gap < 6.0f) { learnResult = "no clear gap between signal and noise (" + juce::String (juce::roundToInt (gap)) + " dB)"; ++programVersion; return; }
    const float thr = juce::jlimit (-80.0f, -1.0f, floorDb + juce::jmax (6.0f, 0.35f * gap));
    const float hy = juce::jlimit (2.0f, 10.0f, 0.15f * gap);
    setParam (IDs::thr, std::round (thr * 2.0f) / 2.0f);
    setParam (IDs::hyst, std::round (hy * 2.0f) / 2.0f);
    learnResult = "floor " + juce::String (juce::roundToInt (floorDb)) + " dB, signal " + juce::String (juce::roundToInt (sig)) + " dB -> threshold "
                + juce::String (thr, 1) + " dB";
    ++programVersion;
}

void LumenGateProcessor::setStep (int i, float v) { setParam (IDs::step (i), juce::jlimit (0.0f, 1.0f, v)); }
void LumenGateProcessor::patternRandom()
{
    const int steps = stepsFor ((int) P (IDs::steps));
    for (int i = 0; i < steps; ++i)
    {
        const float r = rng.nextFloat();
        setStep (i, i % 4 == 0 ? (r < 0.85f ? 1.0f : 0.7f) : r < 0.45f ? 0.0f : r < 0.75f ? 1.0f : r < 0.9f ? 0.7f : 0.45f);
    }
}
void LumenGateProcessor::patternInvert()
{
    const int steps = stepsFor ((int) P (IDs::steps));
    for (int i = 0; i < steps; ++i) setStep (i, P (IDs::step (i)) > 0.01f ? 0.0f : 1.0f);
}
void LumenGateProcessor::patternShift (int dir)
{
    const int steps = stepsFor ((int) P (IDs::steps));
    std::vector<float> v ((size_t) steps);
    for (int i = 0; i < steps; ++i) v[(size_t) i] = P (IDs::step (i));
    for (int i = 0; i < steps; ++i) setStep (i, v[(size_t) (((i - dir) % steps + steps) % steps)]);
}
void LumenGateProcessor::patternClear (bool allOn)
{
    for (int i = 0; i < 32; ++i) setStep (i, allOn ? 1.0f : 0.0f);
}

void LumenGateProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenGateProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenGateProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenGateProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            currentProgram = (int) vt.getProperty ("program", 0);
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

juce::AudioProcessorEditor* LumenGateProcessor::createEditor() { return new LumenGateEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenGateProcessor(); }
