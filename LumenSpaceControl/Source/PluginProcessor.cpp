#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenSpaceControlProcessor::LumenSpaceControlProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("Lumen Space FIR"),
      apvts (*this, nullptr, "LumenSpaceControl", createLayout())
{
    startThread (juce::Thread::Priority::low);
}

LumenSpaceControlProcessor::~LumenSpaceControlProcessor()
{
    signalThreadShouldExit();
    notify();
    stopThread (4000);
}

bool LumenSpaceControlProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // stereo imager: stereo in / stereo out (mono in -> stereo out is allowed for SPREAD on mono sources)
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

lumen::sc::Params LumenSpaceControlProcessor::readParams() const
{
    lumen::sc::Params p;
    p.bands = (int) P (IDs::bands) + 1;
    p.linear = P (IDs::mode) > 0.5f;
    for (int k = 0; k < 3; ++k) p.x[k] = P (IDs::x (k));
    for (int b = 0; b < 4; ++b)
    {
        auto& bp = p.band[b];
        bp.width = P (IDs::w (b)); bp.spread = P (IDs::spread (b)); bp.gainDb = P (IDs::gain (b));
        bp.solo = P (IDs::solo (b)) > 0.5f; bp.mute = P (IDs::mute (b)) > 0.5f;
    }
    p.globalWidth = P (IDs::width); p.monoBelow = P (IDs::monoBelow); p.safe = P (IDs::safe);
    p.mix = P (IDs::mix); p.outDb = P (IDs::out);
    p.monoCheck = P (IDs::monoCheck) > 0.5f; p.sideOnly = P (IDs::sideOnly) > 0.5f;
    return p;
}

juce::uint64 LumenSpaceControlProcessor::hashParams (const lumen::sc::Params& p, double fs)
{
    // only what goes into the FIRs
    juce::uint64 h = 1469598103934665603ull;
    auto mixIn = [&h] (float v) { juce::uint32 u; std::memcpy (&u, &v, 4); h = (h ^ u) * 1099511628211ull; };
    mixIn ((float) p.bands); mixIn ((float) fs);
    for (float v : p.x) mixIn (v);
    for (auto& b : p.band) { mixIn (b.width); mixIn (b.spread); mixIn (b.gainDb); mixIn (b.solo ? 1.0f : 0.0f); mixIn (b.mute ? 1.0f : 0.0f); }
    mixIn (p.globalWidth); mixIn (p.monoBelow);
    return h | 1ull;
}

void LumenSpaceControlProcessor::run()
{
    while (! threadShouldExit())
    {
        wait (15);
        const double sr = curFs.load();
        if (sr <= 0.0) continue;
        const auto p = readParams();
        if (! p.linear) continue;
        const auto h = hashParams (p, sr);
        if (h == loadedHash.load()) continue;
        const int N = lumen::sc::firLength (sr);
        lumen::sc::FirSet f;
        lumen::sc::designFirs (p, sr, N, f);
        {
            const juce::ScopedLock sl (engineLock);
            if (std::abs (curFs.load() - sr) > 0.1) continue;
            engine.loadFirs (std::move (f));
        }
        loadedHash.store (h);
        firReady.store (true);
    }
}

void LumenSpaceControlProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    {
        const juce::ScopedLock sl (engineLock);
        fs = sampleRate;
        engine.prepare (fs, juce::jlimit (64, 2048, samplesPerBlock));
        loadedHash.store (0); firReady.store (false);
        curFs.store (fs);
    }
    asL.assign ((size_t) (fs * 6.0), 0.0f); asR.assign ((size_t) (fs * 6.0), 0.0f);
    const auto p = readParams();
    engine.setParams (p);
    setLatencySamples (engine.latency());
    notify();
}

void LumenSpaceControlProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            float v = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : factoryPresets()[(size_t) index].v) if (rp->paramID == kv.first) v = kv.second;
            rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture();
        }
    ++programVersion;
}

void LumenSpaceControlProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);

    const auto p = readParams();
    engine.setParams (p);
    if (engine.latency() != getLatencySamples()) setLatencySamples (engine.latency());

    if (assisting.load())
    {
        int pos = assistPos.load();
        for (int i = 0; i < n && pos < assistLen; ++i, ++pos) { asL[(size_t) pos] = L[i]; asR[(size_t) pos] = R[i]; }
        assistPos.store (pos);
    }
    inPeak[0].store (buffer.getMagnitude (0, 0, n)); inPeak[1].store (buffer.getMagnitude (1, 0, n));
    engine.process (L, R, n);
    outPeak[0].store (buffer.getMagnitude (0, 0, n)); outPeak[1].store (buffer.getMagnitude (1, 0, n));
}

//==============================================================================
void LumenSpaceControlProcessor::startAssist (Goal g)
{
    assistGoal = g;
    assistLen = (int) std::min (asL.size(), (size_t) (fs * 5.0));
    assistPos.store (0);
    assistResult.clear();
    assisting.store (true);
}

void LumenSpaceControlProcessor::finishAssistIfReady()
{
    if (! assisting.load() || assistPos.load() < assistLen) return;
    assisting.store (false);

    auto p = readParams();
    const int nb = p.bands;
    float xs[3]; lumen::sc::crossovers (p, (float) fs, xs);
    lumen::sc::Splitter sm, ss; sm.set (xs, (float) fs); ss.set (xs, (float) fs);
    double em[4] {}, es[4] {};
    for (int i = 0; i < assistLen; ++i)
    {
        float bm[4] {}, bs[4] {};
        sm.split (0.5f * (asL[(size_t) i] + asR[(size_t) i]), nb, bm);
        ss.split (0.5f * (asL[(size_t) i] - asR[(size_t) i]), nb, bs);
        for (int b = 0; b < nb; ++b) { em[b] += (double) bm[b] * bm[b]; es[b] += (double) bs[b] * bs[b]; }
    }
    double eTot = 1e-12; for (int b = 0; b < nb; ++b) eTot += em[b] + es[b];
    // target side/mid ratio per band (index by position: lowest .. highest)
    static const float tg[4][4] = { { 0.0f, 0.40f, 0.65f, 0.80f },    // Natural
                                    { 0.0f, 0.55f, 0.90f, 1.05f },    // Wide
                                    { 0.0f, 0.30f, 0.55f, 0.70f },    // Club Safe
                                    { 0.05f, 0.70f, 1.05f, 1.25f } }; // Huge
    auto set = [this] (const juce::String& id, float v)
    {
        if (auto* prm = apvts.getParameter (id)) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture(); }
    };
    juce::StringArray msg;
    for (int b = 0; b < nb; ++b)
    {
        const int slot = nb == 1 ? 2 : nb == 2 ? (b == 0 ? 0 : 3) : nb == 3 ? (b == 0 ? 0 : b == 1 ? 2 : 3) : b;
        const float target = tg[(int) assistGoal][slot];
        const float r = (float) std::sqrt (es[b] / std::max (1e-12, em[b]));
        const float share = (float) ((em[b] + es[b]) / eTot);
        float w = 1.0f, sp = 0.0f;
        if (slot == 0 && target < 0.1f) w = 0.0f;                                   // lows: mono
        else if (r < 0.08f) { w = 1.0f; sp = juce::jlimit (0.0f, 0.8f, target * 0.6f); }  // mono source: only SPREAD can widen it
        else w = juce::jlimit (0.3f, 1.8f, std::pow (target / r, 0.7f));               // gentle move towards the target
        if (share < 0.01f) { w = juce::jmin (w, 1.0f); sp = 0.0f; }
        set (IDs::w (b), std::round (w * 100.0f) / 100.0f); set (IDs::spread (b), std::round (sp * 100.0f) / 100.0f);
        msg.add (juce::String (bandName (b, nb)) + " " + juce::String ((int) std::round (r * 100.0f)) + "%");
    }
    const float mono[] = { 110.0f, 100.0f, 140.0f, 120.0f }, safe[] = { 0.0f, -0.1f, 0.2f, 0.0f };
    set (IDs::monoBelow, mono[(int) assistGoal]); set (IDs::safe, safe[(int) assistGoal]);
    assistResult = "input width  " + msg.joinIntoString ("  ");
    ++programVersion;
}

//==============================================================================
void LumenSpaceControlProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenSpaceControlProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenSpaceControlProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenSpaceControlProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* LumenSpaceControlProcessor::createEditor() { return new LumenSpaceControlEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenSpaceControlProcessor(); }
