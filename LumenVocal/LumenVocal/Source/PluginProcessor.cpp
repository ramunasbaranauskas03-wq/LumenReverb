#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lumen::vox;

LumenVocalProcessor::LumenVocalProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenVocal", createLayout())
{
}

bool LumenVocalProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void LumenVocalProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    engine.prepare (fs);
    tmp.setSize (2, kFrame);
    asBuf.assign ((size_t) (fs * 6.5), 0.0f);
    setLatencySamples (engine.latency());
}

void LumenVocalProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (rp->paramID == IDs::key || rp->paramID == IDs::scale || rp->paramID == IDs::in) continue;   // keep the song's key and the input trim
            float v = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : factoryPresets()[(size_t) index].v) if (rp->paramID == kv.first) v = kv.second;
            rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture();
        }
    ++programVersion;
}

void LumenVocalProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);

    Params p;
    p.inDb = P (IDs::in); p.lowCut = P (IDs::lowCut);
    p.tune = P (IDs::tune) > 0.5f; p.key = (int) P (IDs::key); p.scale = (int) P (IDs::scale);
    p.retuneMs = P (IDs::retune); p.amount = P (IDs::amount); p.transpose = P (IDs::transpose); p.formant = P (IDs::formant);
    p.deess = P (IDs::deess); p.deessFreq = P (IDs::deessFreq); p.comp = P (IDs::comp);
    p.body = P (IDs::body); p.presence = P (IDs::presence); p.air = P (IDs::air); p.warmth = P (IDs::warmth);
    p.dbl = P (IDs::dbl); p.width = P (IDs::width);
    p.h1 = (int) P (IDs::h1); p.h2 = (int) P (IDs::h2); p.h1Lvl = P (IDs::h1Lvl); p.h2Lvl = P (IDs::h2Lvl);
    p.dly = P (IDs::dly); p.dlyDiv = (int) P (IDs::dlyDiv); p.dlyFb = P (IDs::dlyFb);
    p.verb = P (IDs::verb); p.verbSize = P (IDs::verbSize); p.duck = P (IDs::duck); p.outDb = P (IDs::out);
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) p.bpm = *b;
    engine.setParams (p);

    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);
    if (assisting.load())
    {
        int pos = asPos.load();
        for (int i = 0; i < n && pos < asLen; ++i) asBuf[(size_t) pos++] = 0.5f * (L[i] + R[i]);
        asPos.store (pos);
    }
    for (int start = 0; start < n; start += kFrame)
    {
        const int len = juce::jmin (kFrame, n - start);
        tmp.copyFrom (0, 0, L + start, len); tmp.copyFrom (1, 0, R + start, len);
        engine.process (L + start, R + start, tmp.getReadPointer (0), tmp.getReadPointer (1), len);
        lumen::vox::VocalEngine::Frame f;
        engine.takeFrame (f);
        const int hp = histPos.load (std::memory_order_relaxed);
        hist[hp].det = f.detMidi; hist[hp].out = f.outMidi; hist[hp].target = f.target;
        histPos.store ((hp + 1) % kHist, std::memory_order_release);
        deessGr.store (f.deessGr); compGr.store (f.compGr);
    }
    inPk.store (engine.inPk); outPk.store (engine.outPk); engine.inPk = engine.outPk = 0.0f;
    curMidi.store (engine.currentMidi()); curOut.store (engine.currentMidi() > 0 ? engine.currentMidi() + engine.currentShift() : -1.0f);
    curTarget.store (engine.currentTarget());
}

//==============================================================================
void LumenVocalProcessor::startAssist (Style s)
{
    asStyle = s;
    asLen = (int) std::min (asBuf.size(), (size_t) (fs * 6.0));
    asPos.store (0);
    assistResult.clear();
    assisting.store (true);
}

void LumenVocalProcessor::finishAssistIfReady()
{
    if (! assisting.load() || asPos.load() < asLen) return;
    assisting.store (false);

    // --- pitch: key + scale ---
    Yin yin; yin.prepare (fs);
    double pcs[12] {}; int voiced = 0, frames = 0;
    for (int i = 0; i < asLen; ++i)
        if (yin.push (asBuf[(size_t) i]))
        {
            ++frames;
            if (yin.f0 > 0.0f && yin.conf > 0.7f)
            {
                const float m = midiFromHz (yin.f0);
                const int pc = (((int) std::round (m)) % 12 + 12) % 12;
                const float dev = std::abs (m - std::round (m));
                pcs[pc] += yin.level * (1.0 - dev);
                ++voiced;
            }
        }
    // --- level, dynamics, sibilance, tone (10 ms frames) ---
    Biquad hpS1, hpS2, bLo, bMid, bAir;
    hpS1.highPass (fs, 6000, 0.54); hpS2.highPass (fs, 6000, 1.31);
    bLo.lowPass (fs, 500, 0.7);
    Biquad bLoHp; bLoHp.highPass (fs, 180, 0.7);
    bMid.highPass (fs, 1000, 0.7); Biquad bMidLp; bMidLp.lowPass (fs, 4000, 0.7);
    bAir.highPass (fs, 8000, 0.7);
    const int win = (int) (fs * 0.01);
    std::vector<float> lv, sib; double eLo = 0, eMid = 0, eAir = 0;
    double sFull = 0, sHf = 0, sLo = 0, sMid = 0, sAir = 0; int k = 0;
    for (int i = 0; i < asLen; ++i)
    {
        const float x = asBuf[(size_t) i];
        const float h = hpS2.process (hpS1.process (x));
        const float lo = bLo.process (bLoHp.process (x)), mi = bMidLp.process (bMid.process (x)), ai = bAir.process (x);
        sFull += (double) x * x; sHf += (double) h * h; sLo += (double) lo * lo; sMid += (double) mi * mi; sAir += (double) ai * ai;
        if (++k == win)
        {
            const double rms = std::sqrt (sFull / win);
            if (rms > 0.003)
            {
                lv.push_back ((float) (20 * std::log10 (rms)));
                sib.push_back ((float) (10 * std::log10 ((sHf + 1e-12) / (sFull + 1e-12))));
                eLo += sLo; eMid += sMid; eAir += sAir;
            }
            sFull = sHf = sLo = sMid = sAir = 0; k = 0;
        }
    }
    auto set = [this] (const juce::String& id, float v)
    {
        if (auto* prm = apvts.getParameter (id)) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture(); }
    };
    if (lv.size() < 50) { assistResult = "too quiet - play / sing louder material"; ++programVersion; return; }
    auto pct = [] (std::vector<float> v, double q) { std::sort (v.begin(), v.end()); return v[(size_t) std::clamp ((int) (q * (v.size() - 1)), 0, (int) v.size() - 1)]; };
    const float loud = pct (lv, 0.75), spread = pct (lv, 0.9) - pct (lv, 0.2), sibP = pct (sib, 0.97);
    const float rLo = (float) (10 * std::log10 ((eLo + 1e-12) / (eMid + 1e-12))), rAir = (float) (10 * std::log10 ((eAir + 1e-12) / (eMid + 1e-12)));

    // style table
    struct S { float retune, amount, comp, pres, warmth, dbl, dly, verb, size, duck; int div; bool tune; };
    static const S st[] {
        { 55, 0.85f, 0.45f, 1.5f, 0.10f, 0.00f, 0.08f, 0.20f, 0.45f, 0.5f, 3, true },     // Natural Pop
        { 12, 1.00f, 0.65f, 2.5f, 0.25f, 0.45f, 0.22f, 0.30f, 0.70f, 0.7f, 2, true },     // EDM Lead
        { 0,  1.00f, 0.60f, 2.0f, 0.10f, 0.15f, 0.15f, 0.15f, 0.50f, 0.6f, 1, true },     // Hard Tune
        { 40, 0.00f, 0.75f, 3.0f, 0.30f, 0.20f, 0.05f, 0.08f, 0.30f, 0.6f, 1, false },    // Rap
        { 120, 0.70f, 0.35f, 1.0f, 0.15f, 0.00f, 0.12f, 0.40f, 0.80f, 0.5f, 3, true } };  // Ballad
    const S& s = st[(int) asStyle];

    int key = 0; bool minor = false;
    const bool haveKey = voiced > frames / 6;
    if (haveKey) Scale::detectKey (pcs, key, minor);
    set (IDs::in, std::clamp (std::round ((-17.0f - loud) * 2.0f) / 2.0f, -24.0f, 24.0f));
    if (haveKey) { set (IDs::key, (float) key); set (IDs::scale, minor ? 2.0f : 1.0f); }
    set (IDs::tune, s.tune ? 1.0f : 0.0f); set (IDs::retune, s.retune); set (IDs::amount, s.amount);
    set (IDs::deess, std::clamp ((sibP + 20.0f) / 16.0f, 0.15f, 0.85f));
    set (IDs::comp, std::clamp (0.5f * s.comp + spread / 40.0f, 0.2f, 0.9f));
    set (IDs::body, std::clamp (std::round (-(rLo - 4.0f) * 0.5f * 2.0f) / 2.0f, -5.0f, 3.0f));
    set (IDs::presence, s.pres);
    set (IDs::air, std::clamp (std::round (-(rAir + 16.0f) * 0.5f * 2.0f) / 2.0f, -2.0f, 6.0f));
    set (IDs::warmth, s.warmth); set (IDs::dbl, s.dbl); set (IDs::dly, s.dly); set (IDs::dlyDiv, (float) s.div);
    set (IDs::verb, s.verb); set (IDs::verbSize, s.size); set (IDs::duck, s.duck);
    assistResult = (haveKey ? "key " + keyNames()[key] + (minor ? " minor" : " major") : juce::String ("no clear key (spoken?)"))
                 + "  |  level " + juce::String (juce::roundToInt (loud)) + " dBFS  |  sibilance " + juce::String (juce::roundToInt (sibP)) + " dB";
    ++programVersion;
}

//==============================================================================
void LumenVocalProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenVocalProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenVocalProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenVocalProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* LumenVocalProcessor::createEditor() { return new LumenVocalEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenVocalProcessor(); }
