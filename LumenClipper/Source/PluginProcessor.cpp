#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenClipperProcessor::LumenClipperProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenClipper", createLayout())
{
}

bool LumenClipperProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    if (out != mono && out != stereo) return false;
    if (in != mono && in != stereo) return false;
    return ! (in == stereo && out == mono);
}

void LumenClipperProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = juce::jlimit (64, 2048, samplesPerBlock);
    for (int k = 0; k < 6; ++k)
    {
        // custom stages: a steep -100 dB first stage, then wide-transition -100 dB stages (the audio band is far
        // from their edges). Measured null vs. the input: about -98 dB at every factor (JUCE's presets: -44 dB at 32x)
        using OS = juce::dsp::Oversampling<float>;
        os[k] = std::make_unique<OS> (2);
        for (int s = 0; s < k; ++s)
        {
            const float tw = s == 0 ? 0.05f : 0.25f;
            os[k]->addOversamplingStage (OS::filterHalfBandFIREquiripple, tw, -100.0f, tw, -100.0f);
        }
        os[k]->initProcessing ((size_t) maxBlock);
    }
    tpW = juce::jmax (8, (int) std::round (0.0008 * fs));
    tpPath.prepare (tpW + 8);
    for (int c = 0; c < 2; ++c) { tpDet[c].reset(); tpDelay[c].prepare (tpW + lumen::TruePeak::kDelay + 8); }
    loud.prepare (fs);
    smartBuf.assign ((size_t) (fs * 6.0), 0.0f);
    inPow = outPow = 1e-9; matchG = 1.0f;
    histN = 0; frPre = frOut = 0; for (auto& v : frBand) v = 0;
    curOs = -1;
    configure();
}

void LumenClipperProcessor::configure()
{
    const int want = juce::jlimit (0, 5, (int) P (IDs::os));
    const bool wantTp = P (IDs::tp) > 0.5f;
    if (want != curOs)
    {
        curOs = want;
        os[curOs]->reset();
        engine.prepare (fs * (1 << curOs));
    }
    {
        // exact integer latency: the oversampling filters' fractional delay + ADAA's linear delay are padded
        // inside the oversampled domain up to the next whole host sample (no interpolation = perfect null)
        const int f = 1 << curOs;
        const bool adaa = P (IDs::adaa) > 0.5f && curOs > 0;
        const double raw = curOs > 0 ? os[curOs]->getLatencyInSamples() : 0.0;
        const double total = raw * f + (adaa ? 2.0 : 0.0);
        osLat = (int) std::ceil (total / f - 1.0e-6);
        engine.setPad ((int) std::round (osLat * (double) f - total));
    }
    if (wantTp != curTp || getLatencySamples() < 0)
    {
        curTp = wantTp;
        tpPath.setWindow (tpW);
        for (int c = 0; c < 2; ++c) { tpDelay[c].setDelay (tpW + lumen::TruePeak::kDelay - 1); tpDet[c].reset(); }
    }
    const int lat = osLat + (curTp ? tpW + lumen::TruePeak::kDelay - 1 : 0);
    if (lat != getLatencySamples()) setLatencySamples (lat);
}

void LumenClipperProcessor::setCurrentProgram (int index)
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

void LumenClipperProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const int n = buffer.getNumSamples();
    if (n == 0 || main.getNumChannels() == 0) return;
    if (getMainBusNumInputChannels() == 1 && main.getNumChannels() > 1) main.copyFrom (1, 0, main, 0, 0, n);
    const int nch = juce::jmin (2, main.getNumChannels());
    if (resetReq.exchange (false)) { tpMax.store (0.0f); loud.reset(); }

    configure();
    lumen::clip::Params p;
    p.mode = (int) P (IDs::mode); p.shape = P (IDs::shape); p.asym = P (IDs::asym);
    p.driveDb = P (IDs::drive); p.ceilingDb = P (IDs::ceiling);
    p.bands = (int) P (IDs::bands) + 1;
    for (int k = 0; k < 3; ++k) p.x[k] = P (IDs::x (k));
    for (int b = 0; b < 4; ++b)
    {
        p.band[b].mode = (int) P (IDs::bmode (b)) - 1; p.band[b].trimDb = P (IDs::trim (b));
        p.band[b].bypass = P (IDs::byp (b)) > 0.5f; p.band[b].solo = P (IDs::solo (b)) > 0.5f;
    }
    p.ms = P (IDs::stereo) > 0.5f; p.sideDb = P (IDs::side);
    p.adaa = P (IDs::adaa) > 0.5f && curOs > 0;     // ADAA needs >= 2x (its droop would dull the top octave at 1x)
    p.mix = P (IDs::mix); p.delta = P (IDs::delta) > 0.5f;
    engine.setParams (p);

    const float ceil = juce::Decibels::decibelsToGain (p.ceilingDb);
    const float outG = juce::Decibels::decibelsToGain (P (IDs::out));
    const bool match = P (IDs::match) > 0.5f;
    const float tpRel = std::exp (-1.0f / (0.04f * (float) fs));
    tpPath.setRelease (tpRel, tpRel, tpRel, false);
    const float mC = 1.0f - std::exp (-1.0f / (2.0f * (float) fs));

    if (smartOn.load())
    {
        int pos = smartPos.load();
        const float* l = main.getReadPointer (0); const float* r = main.getReadPointer (nch - 1);
        for (int i = 0; i < n && pos < smartLen; ++i) smartBuf[(size_t) pos++] = std::max (std::abs (l[i]), std::abs (r[i]));
        smartPos.store (pos);
    }
    for (int c = 0; c < 2; ++c) inPk[c].store (main.getMagnitude (juce::jmin (c, nch - 1), 0, n));

    float gr = 0.0f, oPk[2] {};
    for (int start = 0; start < n; start += kFrame)
    {
        const int len = juce::jmin (kFrame, n - start);
        float* chans[2] { main.getWritePointer (0) + start, main.getWritePointer (nch - 1) + start };
        // input power for gain match (before anything)
        double ip = 0.0;
        for (int i = 0; i < len; ++i) ip += 0.5 * ((double) chans[0][i] * chans[0][i] + (double) chans[1][i] * chans[1][i]);

        juce::dsp::AudioBlock<float> block (chans, (size_t) nch, (size_t) len);
        auto up = os[curOs]->processSamplesUp (block);
        float* L = up.getChannelPointer (0);
        float* R = up.getNumChannels() > 1 ? up.getChannelPointer (1) : L;
        engine.process (L, R, (int) up.getNumSamples());
        os[curOs]->processSamplesDown (block);

        for (int i = 0; i < len; ++i)
        {
            float l = chans[0][i], r = chans[1][i];
            // gain match: hear the tone, not the loudness
            inPow += (ip / len - inPow) * mC;
            outPow += (0.5 * ((double) l * l + (double) r * r) - outPow) * mC;
            const float want = match && ! p.delta ? (float) juce::jlimit (0.125, 2.0, std::sqrt (inPow / std::max (1e-12, outPow))) : 1.0f;
            matchG += (want - matchG) * mC * 4.0f;
            l *= outG * matchG; r *= outG * matchG;
            if (curTp)
            {
                const float tp = std::max (tpDet[0].push (l), tpDet[1].push (r));
                const float req = tp > ceil * 0.9966f ? ceil * 0.9966f / tp : 1.0f;
                const float g = tpPath.process (req);
                l = tpDelay[0].process (l) * g; r = tpDelay[1].process (r) * g;
                gr = std::min (gr, juce::Decibels::gainToDecibels (g, -60.0f));
            }
            if (curTp && ! p.delta) { l = juce::jlimit (-ceil, ceil, l); r = juce::jlimit (-ceil, ceil, r); }   // final safety after the true-peak stage
            chans[0][i] = l; if (nch > 1) chans[1][i] = r;
            oPk[0] = std::max (oPk[0], std::abs (l)); oPk[1] = std::max (oPk[1], std::abs (r));
            loud.process (l, r);
        }
        // display frame (pre-clip level relative to the ceiling, output level, per-band clip amount)
        frPre = std::max (frPre, engine.prePk * ceil);
        float bo = 0.0f; for (int i = 0; i < len; ++i) bo = std::max ({ bo, std::abs (chans[0][i]), std::abs (chans[1][i]) });
        frOut = std::max (frOut, bo);
        for (int b = 0; b < 4; ++b) frBand[b] = std::max (frBand[b], engine.bandPk[b]);
        prePeak.store (engine.prePk);
        engine.resetMeters();
        histN += len;
        if (histN >= kFrame)
        {
            const int hp = histPos.load (std::memory_order_relaxed);
            hist[hp].pre = frPre; hist[hp].out = frOut; for (int b = 0; b < 4; ++b) hist[hp].band[b] = frBand[b];
            histPos.store ((hp + 1) % kHist, std::memory_order_release);
            histN = 0; frPre = frOut = 0; for (auto& v : frBand) v = 0;
        }
    }
    outPk[0].store (oPk[0]); outPk[1].store (oPk[1]);
    tpGr.store (gr);
    {
        float t = tpMax.load();
        for (int c = 0; c < 2; ++c) t = std::max (t, oPk[c]);
        tpMax.store (t);
    }
    lufsS.store (loud.shortTerm); lufsI.store (loud.integrated);
    matchDb.store (juce::Decibels::gainToDecibels (matchG));
    const int hp = (histPos.load() + kHist - 1) % kHist;
    grDb.store (hist[hp].pre > 1e-9f ? juce::jmin (0.0f, juce::Decibels::gainToDecibels (juce::jmin (1.0f, ceil / hist[hp].pre))) : 0.0f);
}

//==============================================================================
void LumenClipperProcessor::startSmart (Goal g)
{
    smartGoal = g;
    smartLen = (int) std::min (smartBuf.size(), (size_t) (fs * 5.0));
    smartPos.store (0);
    smartResult.clear();
    smartOn.store (true);
}

void LumenClipperProcessor::finishSmartIfReady()
{
    if (! smartOn.load() || smartPos.load() < smartLen) return;
    smartOn.store (false);
    // peaks of 10 ms windows; the loud ones decide how much drive gives the wanted clipping
    const int win = juce::jmax (1, (int) (fs * 0.01));
    std::vector<float> pk;
    for (int s = 0; s + win <= smartLen; s += win)
    {
        float m = 0.0f; for (int i = 0; i < win; ++i) m = std::max (m, smartBuf[(size_t) (s + i)]);
        if (m > 1e-4f) pk.push_back (m);
    }
    if (pk.size() < 10) { smartResult = "too quiet - play louder material"; ++programVersion; return; }
    std::sort (pk.begin(), pk.end());
    const float p90 = pk[(size_t) (pk.size() * 0.95)], pMax = pk.back();
    const float p90Db = juce::Decibels::gainToDecibels (p90), pMaxDb = juce::Decibels::gainToDecibels (pMax);
    const float ceilDb = P (IDs::ceiling);
    // wanted clipping on the loud hits (dB) and on the very top peak
    const float want90[] = { 0.0f, 1.0f, 2.5f, 4.5f }, wantTop[] = { 1.5f, 3.0f, 4.5f, 7.0f };
    const float d1 = ceilDb - p90Db + want90[(int) smartGoal], d2 = ceilDb - pMaxDb + wantTop[(int) smartGoal];
    const float drive = juce::jlimit (0.0f, 24.0f, std::round (juce::jmin (d1, d2) * 10.0f) / 10.0f);
    auto set = [this] (const juce::String& id, float v)
    {
        if (auto* prm = apvts.getParameter (id)) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture(); }
    };
    set (IDs::drive, drive);
    if (smartGoal == Smash) set (IDs::mode, 0.0f);
    smartResult = "peaks " + juce::String (pMaxDb, 1) + " dB, hits " + juce::String (p90Db, 1) + " dB  ->  drive +" + juce::String (drive, 1) + " dB";
    ++programVersion;
}

//==============================================================================
void LumenClipperProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenClipperProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenClipperProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenClipperProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* LumenClipperProcessor::createEditor() { return new LumenClipperEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenClipperProcessor(); }
