#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenOTTProcessor::LumenOTTProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenOTT", createLayout())
{
}

bool LumenOTTProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    if (out != mono && out != stereo) return false;
    if (in != mono && in != stereo) return false;
    return ! (in == stereo && out == mono);
}

void LumenOTTProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = juce::jlimit (64, 4096, samplesPerBlock);
    for (int k = 0; k < 3; ++k)
    {
        using OS = juce::dsp::Oversampling<float>;
        os[k] = std::make_unique<OS> (2);
        for (int s = 0; s < k; ++s) { const float tw = s == 0 ? 0.05f : 0.25f; os[k]->addOversamplingStage (OS::filterHalfBandFIREquiripple, tw, -100.0f, tw, -100.0f); }
        os[k]->initProcessing ((size_t) maxBlock);
    }
    curOs = -1;
    asL.assign ((size_t) (fs * 5.5), 0.0f); asR.assign ((size_t) (fs * 5.5), 0.0f);
}

lumen::ott::Params LumenOTTProcessor::readParams() const
{
    lumen::ott::Params p;
    p.x[0] = P (IDs::x1); p.x[1] = P (IDs::x2);
    for (int b = 0; b < 3; ++b)
    {
        auto& bp = p.band[b];
        bp.downThr = P (IDs::dthr (b)); bp.upThr = P (IDs::uthr (b)); bp.inDb = P (IDs::bin (b)); bp.outDb = P (IDs::bout (b));
        bp.downRatio = bandDefaults (b).ratio; bp.upRatio = 4.0f;
        bp.solo = P (IDs::solo (b)) > 0.5f; bp.bypass = P (IDs::byp (b)) > 0.5f;
    }
    p.depth = P (IDs::depth); p.time = P (IDs::time); p.upward = P (IDs::upward); p.downward = P (IDs::downward);
    p.inDb = P (IDs::in); p.outDb = P (IDs::out);
    p.style = (int) P (IDs::style); p.rms = P (IDs::detect) > 0.5f; p.knee = P (IDs::knee); p.link = P (IDs::link);
    p.maxBoost = P (IDs::maxBoost); p.floorDb = P (IDs::floor) <= -99.5f ? -400.0f : P (IDs::floor);
    p.levelLock = P (IDs::lock) > 0.5f; p.autoGain = P (IDs::autoGain) > 0.5f;
    return p;
}

void LumenOTTProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (rp->paramID == IDs::os) continue;
            float v = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : factoryPresets()[(size_t) index].v) if (rp->paramID == kv.first) v = kv.second;
            rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture();
        }
    ++programVersion;
}

void LumenOTTProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const int n = buffer.getNumSamples();
    if (n == 0 || main.getNumChannels() == 0) return;
    if (getMainBusNumInputChannels() == 1 && main.getNumChannels() > 1) main.copyFrom (1, 0, main, 0, 0, n);
    const int nch = juce::jmin (2, main.getNumChannels());

    const int want = juce::jlimit (0, 2, (int) P (IDs::os));
    if (want != curOs)
    {
        curOs = want;
        os[curOs]->reset();
        engine.prepare (fs * (1 << curOs));
        // exact integer latency: pad the fractional part of the filters' delay inside the oversampled domain
        const int f = 1 << curOs;
        const double raw = curOs > 0 ? os[curOs]->getLatencyInSamples() : 0.0;
        const int lat = (int) std::ceil (raw - 1.0e-6);
        pad = (int) std::round ((lat - raw) * f);
        for (auto& b : padBuf) std::fill (std::begin (b), std::end (b), 0.0f);
        padPos = 0;
        if (lat != getLatencySamples()) setLatencySamples (lat);
    }
    engine.setParams (readParams());

    if (assisting.load())
    {
        int pos = asPos.load();
        for (int i = 0; i < n && pos < asLen; ++i, ++pos) { asL[(size_t) pos] = main.getSample (0, i); asR[(size_t) pos] = main.getSample (nch - 1, i); }
        asPos.store (pos);
    }
    float ip = 0; for (int c = 0; c < nch; ++c) ip = juce::jmax (ip, main.getMagnitude (c, 0, n));
    inPk.store (ip);

    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - start);
        float* chans[2] { main.getWritePointer (0) + start, main.getWritePointer (nch - 1) + start };
        juce::dsp::AudioBlock<float> block (chans, (size_t) nch, (size_t) len);
        if (curOs == 0)
            engine.process (chans[0], chans[1], len);
        else
        {
            auto up = os[curOs]->processSamplesUp (block);
            float* L = up.getChannelPointer (0);
            float* R = up.getNumChannels() > 1 ? up.getChannelPointer (1) : L;
            const int un = (int) up.getNumSamples();
            if (pad > 0)
                for (int i = 0; i < un; ++i, padPos = (padPos + 1) & 255)
                {
                    padBuf[0][padPos] = L[i]; padBuf[1][padPos] = R[i];
                    L[i] = padBuf[0][(padPos - pad) & 255]; if (R != L) R[i] = padBuf[1][(padPos - pad) & 255];
                }
            engine.process (L, R, un);
            os[curOs]->processSamplesDown (block);
        }
    }
    if (P (IDs::clip) > 0.5f)
        for (int c = 0; c < nch; ++c)
        {
            float* d = main.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                const float a = std::abs (d[i]);
                if (a > 0.7f) { const float y = 0.7f + 0.3f * std::tanh ((a - 0.7f) / 0.3f); d[i] = d[i] < 0 ? -y : y; }
            }
        }
    float op = 0; for (int c = 0; c < nch; ++c) op = juce::jmax (op, main.getMagnitude (c, 0, n));
    outPk.store (op);
    for (int b = 0; b < 3; ++b)
    {
        bandLvl[b].store (engine.meterLvl[b]); bandUp[b].store (engine.meterUp[b]); bandDown[b].store (engine.meterDown[b]);
    }
    engine.resetMeters();
    agDb.store (engine.autoGainDb()); lockDb.store (engine.lockOffsetDb());
}

//==============================================================================
void LumenOTTProcessor::startAssist (Source s)
{
    asSource = s;
    asLen = (int) std::min (asL.size(), (size_t) (fs * 5.0));
    asPos.store (0);
    assistResult.clear();
    assisting.store (true);
}

void LumenOTTProcessor::finishAssistIfReady()
{
    if (! assisting.load() || asPos.load() < asLen) return;
    assisting.store (false);
    // band balance of the source (same crossovers as the plugin)
    lumen::ott::Splitter3 sp; sp.set (P (IDs::x1), P (IDs::x2), (float) fs);
    double e[3] {}, tot = 0;
    for (int i = 0; i < asLen; ++i)
    {
        const float x = 0.5f * (asL[(size_t) i] + asR[(size_t) i]);
        float b[3]; sp.split (x, b);
        for (int k = 0; k < 3; ++k) e[k] += (double) b[k] * b[k];
        tot += (double) x * x;
    }
    const double rmsDb = 10 * std::log10 (tot / asLen + 1e-20);
    if (rmsDb < -60) { assistResult = "too quiet - play the part"; ++programVersion; return; }
    double bandDb[3]; for (int k = 0; k < 3; ++k) bandDb[k] = 10 * std::log10 (e[k] / asLen + 1e-20);
    // source table: depth, time, style, upward, downward, band outputs (OTT levels every band, so these set the result's balance)
    struct S { float depth, time; int style; float up, down, outL, outM, outH, clip; const char* name; };
    static const S st[] { { 0.45f, 0.45f, 0, 1.0f, 1.0f,  8.0f, 5.7f, 10.3f, 1, "synth" },
                          { 0.5f, 0.55f, 1, 0.9f, 1.0f, 11.0f, 7.0f,  3.0f, 0, "bass" },
                          { 0.35f, 0.3f, 1, 0.8f, 1.0f, 10.0f, 5.7f,  8.0f, 1, "drums" },
                          { 0.25f, 0.5f, 2, 0.8f, 0.8f,  2.0f, 5.7f,  9.0f, 0, "vocal" },
                          { 0.12f, 0.6f, 2, 0.7f, 0.7f,  9.0f, 5.7f,  9.0f, 0, "master" } };
    const S& s = st[(int) asSource];
    auto set = [this] (const juce::String& id, float v)
    {
        if (auto* prm = apvts.getParameter (id)) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture(); }
    };
    set (IDs::depth, s.depth); set (IDs::time, s.time); set (IDs::style, (float) s.style);
    set (IDs::upward, s.up); set (IDs::downward, s.down); set (IDs::clip, s.clip);
    set (IDs::lock, 1.0f);                       // consistent result whatever the source level
    set (IDs::floor, (int) asSource >= 3 ? -60.0f : -72.0f);
    // already bright / already boomy sources get a little less of that band
    const double hiRel = bandDb[2] - bandDb[1], loRel = bandDb[0] - bandDb[1];
    set (IDs::bout (0), s.outL - (loRel > 3.0 ? 2.0f : 0.0f));
    set (IDs::bout (1), s.outM);
    set (IDs::bout (2), s.outH - (hiRel > -3.0 ? 2.5f : 0.0f) + (hiRel < -20.0 ? 1.5f : 0.0f));
    assistResult = juce::String ("source ") + s.name + "  |  level " + juce::String (juce::roundToInt (rmsDb)) + " dBFS  |  balance L/M/H "
                 + juce::String (juce::roundToInt (bandDb[0])) + " / " + juce::String (juce::roundToInt (bandDb[1])) + " / " + juce::String (juce::roundToInt (bandDb[2])) + " dB";
    ++programVersion;
}

//==============================================================================
void LumenOTTProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenOTTProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenOTTProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenOTTProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* LumenOTTProcessor::createEditor() { return new LumenOTTEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenOTTProcessor(); }
