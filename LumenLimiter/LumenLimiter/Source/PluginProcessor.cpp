#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
enum PIdx
{
    kCompOn, kRatio, kThr, kAtk, kRel, kAutoRel, kScHp, kMS, kCompMix,
    kLoPunch, kLoFreq, kDetail, kDetFreq, kTransient,
    kColor, kDrive, kClip, kClipShape, kQuality,
    kGain, kCeiling, kMode, kLimRel, kLook, kBalance, kLink, kTP, kDither,
    kMonitor, kCgm, kTarget, kCustom, kNumP
};
}

LumenLimiterProcessor::LumenLimiterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenLimiter2", createParameterLayout())
{
    const char* ids[] { IDs::compOn, IDs::ratio, IDs::thr, IDs::attack, IDs::release, IDs::autoRel, IDs::scHp, IDs::midSide, IDs::compMix,
                        IDs::loPunch, IDs::loFreq, IDs::detail, IDs::detFreq, IDs::transient,
                        IDs::color, IDs::drive, IDs::clip, IDs::clipShape, IDs::quality,
                        IDs::gain, IDs::ceiling, IDs::mode, IDs::limRel, IDs::look, IDs::balance, IDs::link, IDs::truePeak, IDs::dither,
                        IDs::monitor, IDs::cgm, IDs::target, IDs::custom };
    static_assert (sizeof (ids) / sizeof (ids[0]) == kNumP, "id list");
    for (int k = 0; k < kNumP; ++k) { pr[(size_t) k] = apvts.getRawParameterValue (ids[k]); jassert (pr[(size_t) k] != nullptr); }
    outMeter.withTruePeak = true; inMeter.withTruePeak = false;
}

bool LumenLimiterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == out;
}

void LumenLimiterProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate; maxBlock = kHistChunk;           // we work in fixed 256-sample chunks (one history point each)
    for (int k = 0; k < 3; ++k)
    {
        os[k] = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) (k + 1), juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os[k]->initProcessing ((size_t) maxBlock);
    }
    work.setSize (2, maxBlock);
    comp.prepare (fs); trans.prepare (fs); lim.prepare (fs);
    inMeter.prepare (fs); outMeter.prepare (fs);
    const int maxLat = lim.latency() + 2048;
    for (auto& d : dryDelay) { d.prepare (maxLat); d.clear(); }
    gainSm = juce::Decibels::decibelsToGain (pr[kGain]->load()); cgmSm = 1;
    quality = -1; latencyNow = -1;
    applyQuality ((int) pr[kQuality]->load());
}

void LumenLimiterProcessor::applyQuality (int q)
{
    q = juce::jlimit (0, 3, q);
    quality = q; osFactor = 1 << q;
    if (q > 0) os[q - 1]->reset();
    osLatency = q > 0 ? juce::roundToInt (os[q - 1]->getLatencyInSamples()) : 0;
    color.prepare (fs * osFactor);
    const int lat = osLatency + lim.latency();
    for (auto& d : dryDelay) d.setDelay (lat);
    if (lat != latencyNow) { latencyNow = lat; setLatencySamples (lat); }
}

float LumenLimiterProcessor::targetLufs() const
{
    const int t = juce::jlimit (0, (int) targets().size() - 1, (int) pr[kTarget]->load());
    return t == (int) targets().size() - 1 ? pr[kCustom]->load() : targets()[(size_t) t].lufs;
}
float LumenLimiterProcessor::targetTp() const
{
    const int t = juce::jlimit (0, (int) targets().size() - 1, (int) pr[kTarget]->load());
    return targets()[(size_t) t].tp;
}

void LumenLimiterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, false, 0);
    const int nCh = main.getNumChannels(), n = buffer.getNumSamples();
    if (n == 0 || nCh == 0) return;
    auto v = [this] (int i) { return pr[(size_t) i]->load(); };

    const int q = (int) v (kQuality);
    if (q != quality) applyQuality (q);
    if (resetRequest.exchange (false)) { inMeter.reset(); outMeter.reset(); }

    lumen::master::CompParams cp;
    cp.on = v (kCompOn) > 0.5f; cp.ratio = v (kRatio); cp.thr = v (kThr); cp.attack = v (kAtk); cp.release = v (kRel);
    cp.autoRel = v (kAutoRel) > 0.5f; cp.scHp = v (kScHp); cp.midSide = v (kMS) > 0.5f; cp.mix = v (kCompMix) * 0.01f;
    lumen::master::TransParams tr;
    tr.loPunch = v (kLoPunch) * 0.01f; tr.loFreq = v (kLoFreq); tr.detail = v (kDetail) * 0.01f; tr.detFreq = v (kDetFreq); tr.transient = v (kTransient) * 0.01f;
    lumen::master::LimitParams lp;
    lp.ceiling = v (kCeiling); lp.release = v (kLimRel); lp.lookahead = v (kLook); lp.balance = v (kBalance) * 0.01f;
    lp.link = v (kLink) * 0.01f; lp.mode = (int) v (kMode); lp.truePeak = v (kTP) > 0.5f;
    const int colorType = (int) v (kColor); const float drive = v (kDrive) * 0.01f, clip = v (kClip) * 0.01f, shape = v (kClipShape) * 0.01f;
    const float ceilLin = juce::Decibels::decibelsToGain (lp.ceiling);
    const float gainT = juce::Decibels::decibelsToGain (v (kGain));
    const float gC = 1.0f - std::exp (-1.0f / (0.03f * (float) fs));
    const int monitor = (int) v (kMonitor); const bool cgm = v (kCgm) > 0.5f; const int dith = (int) v (kDither);

    float* L = main.getWritePointer (0);
    float* R = nCh > 1 ? main.getWritePointer (1) : nullptr;
    float ipAll = 0, opL = 0, opR = 0, clipMax = 0, limW = 0, limLoW = 0, limHiW = 0, compW = 0;

    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - start);
        float* wl = work.getWritePointer (0); float* wr = work.getWritePointer (1);
        float ip = 0;
        for (int i = 0; i < len; ++i) { wl[i] = L[start + i]; wr[i] = R != nullptr ? R[start + i] : L[start + i]; ip = std::max (ip, std::max (std::abs (wl[i]), std::abs (wr[i]))); }
        inMeter.process (wl, wr, len);
        // dry copy (for Diff), delayed by the full latency
        float dryL[kHistChunk], dryR[kHistChunk];
        for (int i = 0; i < len; ++i) { dryL[i] = dryDelay[0].tick (wl[i]); dryR[i] = dryDelay[1].tick (wr[i]); }

        comp.process (wl, wr, len, cp);
        trans.process (wl, wr, len, tr);
        float gNow = gainSm;
        for (int i = 0; i < len; ++i) { gainSm += (gainT - gainSm) * gC; wl[i] *= gainSm; wr[i] *= gainSm; }

        if (osFactor == 1)
            for (int i = 0; i < len; ++i) color.process (wl[i], wr[i], colorType, drive, clip, shape, ceilLin);
        else
        {
            auto& o = *os[quality - 1];
            juce::dsp::AudioBlock<float> blk (work);
            auto sub = blk.getSubBlock (0, (size_t) len);
            auto up = o.processSamplesUp (sub);
            float* ul = up.getChannelPointer (0); float* ur = up.getChannelPointer (1);
            for (int j = 0; j < (int) up.getNumSamples(); ++j) color.process (ul[j], ur[j], colorType, drive, clip, shape, ceilLin);
            o.processSamplesDown (sub);
        }
        const float clipNow = color.takeReduction();

        lim.process (wl, wr, len, lp);
        const float clipTot = clipNow + lim.preClipDb.load();
        clipMax = std::max (clipMax, clipTot);
        dither.process (wl, wr, len, dith);
        outMeter.process (wl, wr, len);

        // ---- history point
        float op = 0; for (int i = 0; i < len; ++i) { opL = std::max (opL, std::abs (wl[i])); opR = std::max (opR, std::abs (wr[i])); op = std::max (op, std::max (std::abs (wl[i]), std::abs (wr[i]))); }
        {
            const int hp = histPos.load (std::memory_order_relaxed);
            hist[hp] = { lumen::master::gdb (op), lumen::master::gdb (ip * gNow), lim.grDb.load(), comp.grDb.load(), outMeter.shortTerm(), -clipTot };
            histPos.store ((hp + 1) & (kHist - 1), std::memory_order_release);
        }
        ipAll = std::max (ipAll, ip);
        limW = std::min (limW, lim.grDb.load()); limLoW = std::min (limLoW, lim.grLo.load()); limHiW = std::min (limHiW, lim.grHi.load()); compW = std::min (compW, comp.grDb.load());

        // ---- monitoring (after the meters, so the meters always show the real output)
        float cgmT = 1.0f;
        if (cgm && inMeter.shortTerm() > -60.0f && outMeter.shortTerm() > -60.0f)
            cgmT = juce::Decibels::decibelsToGain (juce::jlimit (-30.0f, 6.0f, inMeter.shortTerm() - outMeter.shortTerm()));
        for (int i = 0; i < len; ++i)
        {
            cgmSm += (cgmT - cgmSm) * gC * 0.2f;
            float l = wl[i], r = wr[i];
            switch (monitor)
            {
                case 1: { const float m = 0.5f * (l + r); l = r = m; break; }
                case 2: { const float s = 0.5f * (l - r); l = r = s; break; }
                case 3: r = l; break;
                case 4: l = r; break;
                case 5: l = l - dryL[i] * gNow; r = r - dryR[i] * gNow; break;
                default: break;
            }
            if (cgm && monitor != 5) { l *= cgmSm; r *= cgmSm; }
            if (R != nullptr) { L[start + i] = l; R[start + i] = r; } else L[start + i] = 0.5f * (l + r);
        }
    }

    lufsM.store (outMeter.momentary()); lufsS.store (outMeter.shortTerm()); lufsI.store (outMeter.integrated());
    lra.store (outMeter.range()); tpMax.store (outMeter.truePeakMax()); tpNow.store (outMeter.truePeakNow());
    inLufsS.store (inMeter.shortTerm()); cgmDb.store (lumen::master::gdb (cgmSm));
    limGr.store (limW); limGrLo.store (limLoW); limGrHi.store (limHiW); compGr.store (compW);
    clipDb.store (clipMax); inPk.store (ipAll); outPkL.store (opL); outPkR.store (opR);
    punchDb.store (trans.punchDb()); detailDb.store (trans.detailDb());
}

//==============================================================================
juce::String LumenLimiterProcessor::matchStep()
{
    if (! matchActive) return {};
    const float st = lufsS.load(), tgt = targetLufs();
    if (st < -45.0f) return "MATCH: play the loudest part of the track";
    const float diff = tgt - st;
    if (std::abs (diff) < 0.3f) { if (++matchGood >= 3) { matchActive = false; return "MATCH done: " + juce::String (st, 1) + " LUFS (target " + juce::String (tgt, 1) + ")"; } }
    else matchGood = 0;
    if (++matchSteps > 40) { matchActive = false; return "MATCH stopped at " + juce::String (st, 1) + " LUFS"; }
    const float g = P (IDs::gain);
    const float ng = juce::jlimit (0.0f, 24.0f, g + juce::jlimit (-2.0f, 2.0f, diff * 0.7f));
    if (std::abs (ng - g) > 0.01f) setParam (IDs::gain, ng);
    else if (ng >= 24.0f && diff > 0) { matchActive = false; return "MATCH: max gain reached - add Clip or a Loud mode"; }
    return "MATCHING... " + juce::String (st, 1) + " -> " + juce::String (tgt, 1) + " LUFS";
}

void LumenLimiterProcessor::setParam (const juce::String& id, float val)
{
    if (auto* p = apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (val)); p->endChangeGesture(); }
}

void LumenLimiterProcessor::setCurrentProgram (int i)
{
    if (! juce::isPositiveAndBelow (i, getNumPrograms())) return;
    currentProgram = i;
    const auto& pre = factoryPresets()[(size_t) i];
    const juce::StringArray keep { IDs::monitor, IDs::cgm, IDs::dither, IDs::quality, IDs::custom };
    const bool setsTarget = std::any_of (pre.v.begin(), pre.v.end(), [] (auto& kv) { return kv.first == IDs::target; });
    for (auto* ap : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ap))
        {
            const auto id = rp->getParameterID();
            if (keep.contains (id) || (id == IDs::target && ! setsTarget)) continue;
            float val = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : pre.v) if (kv.first == id) val = kv.second;
            rp->setValueNotifyingHost (rp->convertTo0to1 (val));
        }
    ++programVersion;
}

void LumenLimiterProcessor::switchAB (int to)
{
    if (to == abSlot) return;
    getStateInformation (ab[abSlot]);
    abSlot = to;
    if (ab[to].getSize() > 0) setStateInformation (ab[to].getData(), (int) ab[to].getSize());
    ++programVersion;
}
void LumenLimiterProcessor::copyAB() { getStateInformation (ab[1 - abSlot]); }

juce::AudioProcessorEditor* LumenLimiterProcessor::createEditor() { return new LumenLimiterEditor (*this); }

void LumenLimiterProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto st = apvts.copyState();
    st.setProperty ("program", currentProgram, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, d);
}

void LumenLimiterProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (tree);
    currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty ("program", 0));
    for (auto* ap : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ap))
        {
            auto child = tree.getChildWithProperty ("id", rp->getParameterID());
            if (child.isValid() && child.hasProperty ("value"))
                rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
        }
    ++programVersion;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenLimiterProcessor(); }
