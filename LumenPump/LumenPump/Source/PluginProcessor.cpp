#include "PluginProcessor.h"
#include "PluginEditor.h"

using lumen::CurvePoint;

LumenPumpProcessor::LumenPumpProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "LumenPump", createParameterLayout())
{
    pTrig = p (IDs::trigger);  pRate = p (IDs::rate);       pLow = p (IDs::lowHz);
    pHigh = p (IDs::highHz);   pDepth = p (IDs::depth);     pOut = p (IDs::outside);
    pSmooth = p (IDs::smooth); pOffset = p (IDs::offset);   pScTh = p (IDs::scThresh);
    pSolo = p (IDs::solo);     pOutput = p (IDs::output);

    float sm = 0.5f;
    setPoints (presetPoints (0, sm));
}

//==============================================================================
struct PumpPreset { const char* name; float smooth; std::vector<CurvePoint> pts; };

static const std::vector<PumpPreset>& pumpPresets()
{
    static const std::vector<PumpPreset> p {
        { "Classic Pump",      0.5f, { {0,0}, {0.08f,0.15f}, {0.5f,1}, {1,1} } },
        { "Hard Cut",          0.0f, { {0,0}, {0.22f,0}, {0.26f,1}, {1,1} } },
        { "Short Duck",        0.4f, { {0,0}, {0.04f,0.1f}, {0.18f,1}, {1,1} } },
        { "Snappy",            0.3f, { {0,0}, {0.1f,1}, {1,1} } },
        { "Soft Pump",         0.6f, { {0,0.35f}, {0.5f,1}, {1,1} } },
        { "Big Room",          0.6f, { {0,0}, {0.3f,0.05f}, {0.75f,1}, {1,1} } },
        { "Deep House Bounce", 0.7f, { {0,0.1f}, {0.12f,0.5f}, {0.35f,1}, {0.62f,0.85f}, {1,1} } },
        { "Long Swell",        0.7f, { {0,0}, {0.85f,1}, {1,1} } },
        { "Future Bass Swell", 0.5f, { {0,0}, {0.6f,0.6f}, {0.95f,1}, {1,1} } },
        { "Lo-fi Breathe",     0.8f, { {0,0.3f}, {0.25f,0.55f}, {0.7f,1}, {1,1} } },
        { "Double Pump",       0.5f, { {0,0}, {0.22f,1}, {0.5f,1}, {0.52f,0}, {0.72f,1}, {1,1} } },
        { "Kick + Snare",      0.5f, { {0,0}, {0.2f,1}, {0.48f,1}, {0.5f,0.4f}, {0.7f,1}, {1,1} } },
        { "Off-Beat Lift",     0.4f, { {0,0.2f}, {0.4f,0.3f}, {0.5f,1}, {0.9f,1}, {1,0.2f} } },
        { "Trance Gate",       0.0f, { {0,1}, {0.42f,1}, {0.47f,0}, {0.95f,0}, {1,1} } },
        { "Triplet Chop",      0.0f, { {0,0}, {0.05f,1}, {0.3f,1}, {0.333f,0}, {0.383f,1}, {0.633f,1}, {0.666f,0}, {0.716f,1}, {0.966f,1}, {1,0} } },
        { "1/16 Stutter",      0.0f, { {0,1}, {0.2f,1}, {0.25f,0}, {0.27f,1}, {0.45f,1}, {0.5f,0}, {0.52f,1}, {0.7f,1}, {0.75f,0}, {0.77f,1}, {0.95f,1}, {1,0} } },
        { "Pre-Duck Ramp",     0.5f, { {0,1}, {0.7f,1}, {1,0} } },
        { "Saw Fall",          0.0f, { {0,1}, {1,0} } },
        { "Sine Wave",         1.0f, { {0,0}, {0.5f,1}, {1,0} } },
        { "Flat (no pump)",    0.0f, { {0,1}, {1,1} } },
    };
    return p;
}

const juce::StringArray& LumenPumpProcessor::presetNames()
{
    static const juce::StringArray n = []
    {
        juce::StringArray a;
        for (auto& pr : pumpPresets()) a.add (pr.name);
        return a;
    }();
    return n;
}

std::vector<CurvePoint> LumenPumpProcessor::presetPoints (int index, float& smoothOut)
{
    const auto& all = pumpPresets();
    const auto& pr = all[(size_t) juce::jlimit (0, (int) all.size() - 1, index)];
    smoothOut = pr.smooth;
    return pr.pts;
}

void LumenPumpProcessor::setPoints (std::vector<CurvePoint> pts)
{
    std::sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.x < b.x; });
    if (pts.size() < 2) pts = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
    pts.front().x = 0.0f;
    pts.back().x  = 1.0f;
    for (auto& q : pts) { q.x = juce::jlimit (0.0f, 1.0f, q.x); q.y = juce::jlimit (0.0f, 1.0f, q.y); }
    points = std::move (pts);

    juce::String s;
    for (auto& q : points)
        s << juce::String (q.x, 4) << "," << juce::String (q.y, 4) << ";";
    apvts.state.getOrCreateChildWithName ("CURVE", nullptr).setProperty ("points", s, nullptr);

    publishCurve();
}

void LumenPumpProcessor::publishCurve()
{
    std::array<float, lumen::PumpEngine::kLutSize + 1> a {}, b {};
    lumen::renderCurve (points, 0.0f, a.data(), lumen::PumpEngine::kLutSize);
    lumen::renderCurve (points, 1.0f, b.data(), lumen::PumpEngine::kLutSize);
    for (size_t i = 0; i < a.size(); ++i)
    {
        lutLin[i].store (a[i], std::memory_order_relaxed);
        lutSm[i].store (b[i], std::memory_order_relaxed);
    }
}

void LumenPumpProcessor::loadPointsFromState()
{
    const auto s = apvts.state.getChildWithName ("CURVE").getProperty ("points").toString();
    std::vector<CurvePoint> pts;
    for (auto& tok : juce::StringArray::fromTokens (s, ";", ""))
    {
        if (tok.trim().isEmpty()) continue;
        auto xy = juce::StringArray::fromTokens (tok, ",", "");
        if (xy.size() == 2) pts.push_back ({ xy[0].getFloatValue(), xy[1].getFloatValue() });
    }
    if (pts.size() >= 2) setPoints (pts);
}

//==============================================================================
bool LumenPumpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    if (out != mono && out != stereo) return false;
    if (in != out) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != mono && sc != stereo) return false;
    }
    return true;
}

void LumenPumpProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.prepare (sampleRate);
    capBuf.assign ((size_t) (sampleRate * 4.0), 0.0f);
    capPos = 0;
}

void LumenPumpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, false, 0);
    const int numCh = main.getNumChannels();
    const int n = buffer.getNumSamples();
    if (n == 0 || numCh == 0) return;

    const float* scL = nullptr;
    const float* scR = nullptr;
    if (getBusCount (true) > 1)
        if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            if (sc.getNumChannels() > 0)
            {
                scL = sc.getReadPointer (0);
                scR = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : scL;
            }
        }
    scConnected.store (scL != nullptr);

    // Host transport
    lumen::PumpEngine::Params pp;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) pp.bpm = *bpm;
            pp.playing = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition()) pp.ppq = *ppq; else pp.playing = false;
        }
    hostBpm.store (pp.bpm);

    pp.trigger    = juce::jlimit (0, 2, (int) pTrig->load());
    pp.cycleBeats = kRateBeats[juce::jlimit (0, kNumRates - 1, (int) pRate->load())];
    pp.lowHz      = pLow->load();
    pp.highHz     = pHigh->load();
    pp.depth      = pDepth->load() * 0.01f;
    pp.outside    = pOut->load() * 0.01f;
    pp.offset     = pOffset->load() * 0.01f;
    pp.scThreshDb = pScTh->load();
    pp.solo       = pSolo->load() > 0.5f;
    pp.outputDb   = pOutput->load();

    // Curve: blend linear / smooth tables
    const float sm = juce::jlimit (0.0f, 1.0f, pSmooth->load() * 0.01f);
    for (size_t i = 0; i < lutLocal.size(); ++i)
    {
        const float a = lutLin[i].load (std::memory_order_relaxed);
        const float b = lutSm[i].load (std::memory_order_relaxed);
        lutLocal[i] = a + (b - a) * sm;
    }
    engine.setCurve (lutLocal.data());
    engine.beginBlock (pp, n);

    // MIDI note-ons (trigger mode MIDI)
    int noteTimes[64]; int numNotes = 0;
    if (pp.trigger == lumen::PumpEngine::Midi)
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn() && numNotes < 64)
                noteTimes[numNotes++] = meta.samplePosition;

    // Kick capture for Smart Assist
    if (captureRequested.exchange (false))
    {
        capPos = 0; capProgress.store (0.0f); capReady.store (false); capturing.store (true);
    }
    const bool cap = capturing.load();

    float* L = main.getWritePointer (0);
    float* R = numCh > 1 ? main.getWritePointer (1) : nullptr;
    int noteIdx = 0;
    float lv = 1.0f, scPk = 0.0f;
    bool trig = false;

    for (int i = 0; i < n; ++i)
    {
        while (noteIdx < numNotes && noteTimes[noteIdx] <= i) { engine.retrigger(); trig = true; ++noteIdx; }

        const float sc = scL != nullptr ? 0.5f * (scL[i] + scR[i]) : 0.0f;
        scPk = std::max (scPk, std::abs (sc));

        float l = L[i], r = R != nullptr ? R[i] : L[i];
        const float before = engine.getPhase();
        engine.processSample (l, r, sc, lv);
        if (pp.trigger != lumen::PumpEngine::Sync && engine.getPhase() < before) trig = true;
        L[i] = l;
        if (R != nullptr) R[i] = r;

        if (cap && capPos < (int) capBuf.size())
        {
            // Capture the sidechain if connected, otherwise the track itself
            capBuf[(size_t) capPos++] = scL != nullptr ? sc : 0.5f * (L[i] + (R != nullptr ? R[i] : L[i]));
        }
    }

    if (cap)
    {
        capProgress.store ((float) capPos / (float) capBuf.size());
        if (capPos >= (int) capBuf.size())
        {
            capturing.store (false);
            capReady.store (true, std::memory_order_release);
        }
    }

    phase.store (engine.getPhase());
    level.store (lv);
    scLevel.store (scPk);
    if (trig) triggerFlash.store (6);
}

bool LumenPumpProcessor::takeCapture (std::vector<float>& out)
{
    if (! capReady.exchange (false, std::memory_order_acquire)) return false;
    out = capBuf;
    return true;
}

juce::AudioProcessorEditor* LumenPumpProcessor::createEditor() { return new LumenPumpEditor (*this); }

void LumenPumpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LumenPumpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            loadPointsFromState();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenPumpProcessor(); }
