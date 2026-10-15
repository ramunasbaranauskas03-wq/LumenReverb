#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenTransientProcessor::LumenTransientProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "LumenTransient", createLayout())
{
}

bool LumenTransientProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void LumenTransientProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    for (int k = 0; k < 3; ++k)
    {
        os[k] = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) k, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os[k]->initProcessing ((size_t) kFrame);
    }
    scBuf.setSize (2, kFrame);
    scOs.setSize (2, kFrame * 4);
    curOs = (int) P (IDs::os);
    engine.prepare (fs * (1 << curOs), (int) std::ceil (0.0051 * fs) * 4);
    learnBuf.assign ((size_t) (fs * 6.0), 0.0f);
    histN = 0;
    for (int b = 0; b < 4; ++b) frIn[b] = frOut[b] = frG[b] = 0.0f;
    updateLatency();
}

void LumenTransientProcessor::updateLatency()
{
    laBase = (int) std::round (juce::jlimit (0.0f, 5.0f, P (IDs::lookahead)) * 0.001 * fs);
    const int lat = laBase + (os[curOs] != nullptr ? (int) std::round (os[curOs]->getLatencyInSamples()) : 0);
    if (lat != getLatencySamples()) setLatencySamples (lat);
}

void LumenTransientProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (rp->paramID == IDs::os || rp->paramID == IDs::sc) continue;
            float v = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : factoryPresets()[(size_t) index].v) if (rp->paramID == kv.first) v = kv.second;
            rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture();
        }
    ++programVersion;
}

void LumenTransientProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const int n = buffer.getNumSamples();
    const int numIn = getMainBusNumInputChannels(), numOut = getMainBusNumOutputChannels();
    if (n == 0 || main.getNumChannels() == 0) return;
    if (numIn == 1 && numOut > 1) main.copyFrom (1, 0, main, 0, 0, n);
    const int nch = juce::jmin (2, main.getNumChannels());

    const int want = juce::jlimit (0, 2, (int) P (IDs::os));
    if (want != curOs)
    {
        curOs = want;
        os[curOs]->reset();
        engine.prepare (fs * (1 << curOs), (int) std::ceil (0.0051 * fs) * 4);
    }
    updateLatency();
    const int f = 1 << curOs;

    lumen::tr::Params p;
    p.bands = (int) P (IDs::bands) + 1;
    for (int k = 0; k < 3; ++k) p.x[k] = P (IDs::x (k));
    for (int b = 0; b < 4; ++b)
    {
        auto& bp = p.band[b];
        bp.punch = P (IDs::punch (b)); bp.body = P (IDs::body (b)); bp.sustain = P (IDs::sus (b)); bp.gate = P (IDs::gate (b));
        bp.drive = P (IDs::drive (b)); bp.time = P (IDs::time (b)); bp.gainDb = P (IDs::gain (b)); bp.amount = P (IDs::amount (b));
        bp.solo = P (IDs::solo (b)) > 0.5f; bp.bypass = P (IDs::byp (b)) > 0.5f;
    }
    p.lookahead = laBase * f;
    p.mix = P (IDs::mix); p.outDb = P (IDs::out); p.link = P (IDs::link);
    p.clip = false; p.autoGain = P (IDs::autoGain) > 0.5f; p.delta = P (IDs::delta) > 0.5f;

    // sidechain
    const float* scP[2] { nullptr, nullptr };
    bool scOk = false;
    if (getBusCount (true) > 1)
        if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            if (sc.getNumChannels() > 0) { scP[0] = sc.getReadPointer (0); scP[1] = sc.getReadPointer (sc.getNumChannels() > 1 ? 1 : 0); scOk = true; }
        }
    scConnected.store (scOk);
    p.sidechain = scOk && P (IDs::sc) > 0.5f;
    engine.setParams (p);

    // learn capture (pre-processing, mono sum)
    if (learning.load())
    {
        int pos = learnPos.load();
        const float* l = main.getReadPointer (0); const float* r = main.getReadPointer (nch - 1);
        for (int i = 0; i < n && pos < learnLen; ++i) learnBuf[(size_t) pos++] = 0.5f * (l[i] + r[i]);
        learnPos.store (pos);
    }

    float ip = 0.0f; for (int c = 0; c < nch; ++c) ip = juce::jmax (ip, main.getMagnitude (c, 0, n));
    inPeak.store (ip);

    for (int start = 0; start < n; start += kFrame)
    {
        const int len = juce::jmin (kFrame, n - start);
        float* chans[2] { main.getWritePointer (0) + start, main.getWritePointer (nch - 1) + start };
        juce::dsp::AudioBlock<float> block (chans, (size_t) nch, (size_t) len);
        auto up = os[curOs]->processSamplesUp (block);
        const int un = (int) up.getNumSamples();
        const float* sl = nullptr; const float* sr = nullptr;
        if (p.sidechain)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float* src = scP[c] + start;
                float* dst = scOs.getWritePointer (c);
                for (int i = 0; i < un; ++i) dst[i] = src[juce::jmin (len - 1, i / f)];
            }
            sl = scOs.getReadPointer (0); sr = scOs.getReadPointer (1);
        }
        float* L = up.getChannelPointer (0);
        float* R = up.getNumChannels() > 1 ? up.getChannelPointer (1) : L;
        engine.process (L, R, sl, sr, un);
        os[curOs]->processSamplesDown (block);

        // display frames
        for (int b = 0; b < 4; ++b)
        {
            frIn[b] = juce::jmax (frIn[b], engine.inPk[b]); frOut[b] = juce::jmax (frOut[b], engine.outPk[b]);
            if (std::abs (engine.gExt[b]) > std::abs (frG[b])) frG[b] = engine.gExt[b];
        }
        engine.resetMeters();
        histN += len;
        if (histN >= kFrame)
        {
            const int hp = histPos.load (std::memory_order_relaxed);
            for (int b = 0; b < 4; ++b) { hist[hp].in[b] = frIn[b]; hist[hp].out[b] = frOut[b]; hist[hp].g[b] = frG[b]; frIn[b] = frOut[b] = frG[b] = 0.0f; }
            histPos.store ((hp + 1) % kHist, std::memory_order_release);
            histN = 0;
        }
    }

    // soft clip at the host rate (after the downsampling filter, so its overshoot is caught too)
    if (P (IDs::clip) > 0.5f)
        for (int c = 0; c < nch; ++c)
        {
            float* d = main.getWritePointer (c);
            for (int i = 0; i < n; ++i) d[i] = lumen::tr::TransientEngine::softClip (d[i]);
        }
    float op = 0.0f; for (int c = 0; c < nch; ++c) op = juce::jmax (op, main.getMagnitude (c, 0, n));
    outPeak.store (op);
    agDb.store (engine.autoGainDb());
}

//==============================================================================
void LumenTransientProcessor::startLearn (Goal g)
{
    learnGoal = g;
    learnLen = (int) std::min (learnBuf.size(), (size_t) (fs * 5.0));
    learnPos.store (0);
    learnResult.clear();
    learning.store (true);
}

void LumenTransientProcessor::finishLearnIfReady()
{
    if (! learning.load() || learnPos.load() < learnLen) return;
    learning.store (false);

    const int nb = (int) P (IDs::bands) + 1;
    float xs[3]; for (int k = 0; k < 3; ++k) xs[k] = P (IDs::x (k));
    xs[1] = juce::jmax (xs[1], xs[0] * 1.25f); xs[2] = juce::jmax (xs[2], xs[1] * 1.25f);
    lumen::tr::Splitter sp; sp.set (xs, (float) fs); sp.reset();
    lumen::tr::Detector d[4];
    lumen::tr::BandP neutral;
    double sP[4] {}, sB[4] {}, sS[4] {}, e[4] {}; int cnt[4] {};
    for (int b = 0; b < 4; ++b) { d[b].prepare ((float) fs); d[b].setTime (P (IDs::time (b))); }
    for (int i = 0; i < learnLen; ++i)
    {
        float v[4] {};
        sp.split (learnBuf[(size_t) i], 0, nb, v);
        for (int b = 0; b < nb; ++b)
        {
            d[b].process (std::abs (v[b]), neutral);
            e[b] += (double) v[b] * v[b];
            if (std::abs (v[b]) > 1.0e-4f) { sP[b] += d[b].dPunch; sB[b] += d[b].dBody; sS[b] += d[b].dSus; ++cnt[b]; }
        }
    }
    double eTot = 1e-12; for (int b = 0; b < nb; ++b) eTot += e[b];
    float tP[4] {}, tS[4] {}, maxP = 1e-6f, maxS = 1e-6f;
    for (int b = 0; b < nb; ++b)
    {
        tP[b] = cnt[b] > 0 ? (float) (sP[b] / cnt[b]) : 0.0f; tS[b] = cnt[b] > 0 ? (float) (sS[b] / cnt[b]) : 0.0f;
        maxP = juce::jmax (maxP, tP[b]); maxS = juce::jmax (maxS, tS[b]);
    }
    auto set = [this] (const juce::String& id, float v)
    {
        if (auto* prm = apvts.getParameter (id)) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture(); }
    };
    auto r2 = [] (float v) { return std::round (v * 100.0f) / 100.0f; };
    juce::StringArray msg;
    for (int b = 0; b < nb; ++b)
    {
        const float share = (float) (e[b] / eTot);
        const float act = share > 0.02f ? 1.0f : 0.35f;
        const float tp = tP[b] / maxP, ts = tS[b] / maxS;
        const bool low = b == 0 && nb > 1, top = b == nb - 1 && nb > 1;
        float pk = 0, bd = 0, su = 0, gt = 0;
        switch (learnGoal)
        {
            case Punch:      pk = act * (0.2f + 0.35f * tp); bd = low ? act * 0.2f : 0.0f; break;
            case Tighter:    pk = act * 0.1f; su = -act * (0.25f + 0.4f * ts); gt = ts > 0.7f && ! low ? 0.2f : 0.0f; break;
            case PunchTight: pk = act * (0.2f + 0.3f * tp); su = -act * (0.15f + 0.3f * ts); break;
            case Softer:     pk = -act * (0.2f + 0.35f * tp) * (top ? 1.2f : 1.0f); su = act * 0.1f; break;
            case Clarity:    pk = low ? 0.1f : act * (0.15f + 0.3f * tp); su = low ? -act * (0.2f + 0.3f * ts) : -act * 0.1f * ts; break;
        }
        set (IDs::punch (b), r2 (juce::jlimit (-1.0f, 1.0f, pk))); set (IDs::body (b), r2 (bd));
        set (IDs::sus (b), r2 (juce::jlimit (-1.0f, 1.0f, su))); set (IDs::gate (b), r2 (gt));
        set (IDs::amount (b), 1.0f);
        msg.add (juce::String (bandName (b, nb)) + " " + juce::String ((int) std::round (share * 100.0f)) + "%");
    }
    if (learnGoal == Softer) set (IDs::lookahead, 2.0f);
    else if (learnGoal == Punch || learnGoal == PunchTight) set (IDs::lookahead, 0.5f);
    learnResult = msg.joinIntoString ("  ");
    ++programVersion;
}

//==============================================================================
void LumenTransientProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}

void LumenTransientProcessor::copyAB()
{
    getStateInformation (abState[1 - abSlot]);
}

void LumenTransientProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenTransientProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* LumenTransientProcessor::createEditor() { return new LumenTransientEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenTransientProcessor(); }
