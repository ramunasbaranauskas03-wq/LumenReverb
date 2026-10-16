#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenResonanceProcessor::LumenResonanceProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "LumenResonance", createLayout())
{
}

bool LumenResonanceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

lumen::res::Params LumenResonanceProcessor::readParams() const
{
    lumen::res::Params p;
    p.depth = P (IDs::depth); p.sharpness = P (IDs::sharp); p.selectivity = P (IDs::select);
    p.attackMs = P (IDs::attack); p.releaseMs = P (IDs::release);
    p.hard = P (IDs::mode) > 0.5f; p.resolution = (int) P (IDs::res); p.stereo = (int) P (IDs::stereo);
    p.midAmt = P (IDs::midAmt); p.sideAmt = P (IDs::sideAmt); p.protect = P (IDs::protect);
    p.focus = P (IDs::focus); p.toneKeep = P (IDs::toneKeep); p.recover = P (IDs::recover); p.perceptual = P (IDs::ear) > 0.5f;
    p.lowF = P (IDs::lowF); p.highF = P (IDs::highF);
    for (int i = 0; i < 6; ++i) { p.band[i].freq = P (IDs::bf (i)); p.band[i].gainDb = P (IDs::bg (i)); }
    p.sidechain = P (IDs::sc) > 0.5f; p.delta = P (IDs::delta) > 0.5f; p.autoGain = P (IDs::autoGain) > 0.5f;
    p.mix = P (IDs::mix); p.outDb = P (IDs::out);
    return p;
}

void LumenResonanceProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    const auto p = readParams();
    engine.prepare (fs, p.resolution);
    engine.setParams (p);
    setLatencySamples (engine.latency());
    lBuf.assign ((size_t) (fs * 5.0), 0.0f);
}

void LumenResonanceProcessor::setParam (const juce::String& id, float v)
{
    if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
    { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture(); }
}

void LumenResonanceProcessor::setCurrentProgram (int index)
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

void LumenResonanceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const int n = buffer.getNumSamples();
    if (n == 0 || main.getNumChannels() == 0) return;
    if (getMainBusNumInputChannels() == 1 && main.getNumChannels() > 1) main.copyFrom (1, 0, main, 0, 0, n);
    const int nch = juce::jmin (2, main.getNumChannels());

    const auto p = readParams();
    engine.setParams (p);
    if (engine.latency() != getLatencySamples()) setLatencySamples (engine.latency());

    const float* scP[2] { nullptr, nullptr };
    bool scOk = false;
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            if (sc.getNumChannels() > 0) { scP[0] = sc.getReadPointer (0); scP[1] = sc.getReadPointer (sc.getNumChannels() > 1 ? 1 : 0); scOk = true; }
        }
    scConnected.store (scOk);

    float* L = main.getWritePointer (0); float* R = main.getWritePointer (nch - 1);
    if (learning.load())
    {
        int pos = lPos.load();
        for (int i = 0; i < n && pos < lLen; ++i, ++pos) lBuf[(size_t) pos] = 0.5f * (L[i] + R[i]);
        lPos.store (pos);
    }
    inPeak.store (main.getMagnitude (0, n));
    if (nch == 1)
    {
        // mono out: run a temporary copy as the right channel
        float tmp[1024];
        for (int s = 0; s < n; s += 1024)
        {
            const int len = juce::jmin (1024, n - s);
            std::memcpy (tmp, L + s, sizeof (float) * (size_t) len);
            engine.process (L + s, tmp, scP[0] != nullptr && p.sidechain ? scP[0] + s : nullptr, scP[1] != nullptr && p.sidechain ? scP[1] + s : nullptr, len);
        }
    }
    else
        engine.process (L, R, scOk && p.sidechain ? scP[0] : nullptr, scOk && p.sidechain ? scP[1] : nullptr, n);
    outPeak.store (main.getMagnitude (0, n));
}

//==============================================================================
void LumenResonanceProcessor::startLearn (Source s)
{
    lSource = s;
    lLen = (int) std::min (lBuf.size(), (size_t) (fs * 5.0));
    lPos.store (0);
    learnResult.clear();
    learning.store (true);
}

void LumenResonanceProcessor::finishLearnIfReady()
{
    if (! learning.load() || lPos.load() < lLen) return;
    learning.store (false);

    // average "excess over the 1-octave envelope" per bin
    const int N = lumen::res::ResonanceEngine::fftSizeFor (fs, 1), nb = N / 2 + 1;
    juce::dsp::FFT fft ((int) std::round (std::log2 ((double) N)));
    std::vector<float> buf ((size_t) N * 2), win ((size_t) N), lev ((size_t) nb);
    std::vector<double> ex ((size_t) nb, 0.0), pre ((size_t) nb + 1, 0.0);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
    int frames = 0; double peakSum = 0;
    for (int st = 0; st + N <= lLen; st += N / 2)
    {
        double e = 0;
        for (int i = 0; i < N; ++i) { buf[(size_t) i] = lBuf[(size_t) (st + i)] * win[(size_t) i]; e += (double) buf[(size_t) i] * buf[(size_t) i]; }
        if (e < 1e-9) continue;
        std::fill (buf.begin() + N, buf.end(), 0.0f);
        fft.performRealOnlyForwardTransform (buf.data(), true);
        for (int k = 0; k < nb; ++k) lev[(size_t) k] = 10.0f * std::log10 (buf[(size_t) (2 * k)] * buf[(size_t) (2 * k)] + buf[(size_t) (2 * k + 1)] * buf[(size_t) (2 * k + 1)] + 1e-14f);
        for (int k = 0; k < nb; ++k) pre[(size_t) k + 1] = pre[(size_t) k] + lev[(size_t) k];
        const float r = std::pow (2.0f, 0.5f);
        for (int k = 1; k < nb; ++k)
        {
            const int lo = juce::jlimit (1, nb - 1, (int) std::floor (k / r)), hi = juce::jlimit (lo, nb - 1, (int) std::ceil (k * r));
            const double env = (pre[(size_t) hi + 1] - pre[(size_t) lo]) / (hi - lo + 1);
            const double x = juce::jmax (0.0, (double) lev[(size_t) k] - env - 3.0);
            ex[(size_t) k] += x; peakSum += x;
        }
        ++frames;
    }
    if (frames < 4) { learnResult = "nothing heard - play the track"; ++programVersion; return; }

    // 1/3-octave zones (60 Hz .. 16 kHz) inside the source's range
    static const float loR[] { 150, 200, 100, 30, 60 }, hiR[] { 20000, 20000, 20000, 1500, 20000 };
    const float lo = loR[lSource], hi = hiR[lSource];
    struct Z { float f; double v; double peak; };
    std::vector<Z> zones;
    for (int z = 0; z < 25; ++z)
    {
        const float fc = 63.0f * std::pow (2.0f, z / 3.0f);
        if (fc < lo || fc > hi) continue;
        const int k0 = (int) (fc / std::pow (2.0f, 1.0f / 6.0f) * N / fs), k1 = (int) (fc * std::pow (2.0f, 1.0f / 6.0f) * N / fs);
        double s = 0, pk = 0; int c = 0;
        for (int k = juce::jmax (1, k0); k <= juce::jmin (nb - 1, k1); ++k) { s += ex[(size_t) k] / frames; pk = juce::jmax (pk, ex[(size_t) k] / frames); ++c; }
        if (c > 0) zones.push_back ({ fc, s / c, pk });
    }
    if (zones.empty()) { learnResult = "no usable range"; ++programVersion; return; }
    double mean = 0; for (auto& z : zones) mean += z.v; mean /= (double) zones.size();
    auto sorted = zones;
    std::sort (sorted.begin(), sorted.end(), [] (const Z& a, const Z& b) { return a.v > b.v; });
    // pick up to 4 hot zones at least 2/3 octave apart
    std::vector<Z> hot;
    for (auto& z : sorted)
    {
        if (z.v < mean * 1.25 || (int) hot.size() >= 4) continue;
        bool far = true; for (auto& h : hot) far &= std::abs (std::log2 (z.f / h.f)) > 0.6f;
        if (far) hot.push_back (z);
    }
    std::sort (hot.begin(), hot.end(), [] (const Z& a, const Z& b) { return a.f < b.f; });
    // narrow resonances -> sharper
    double pk = 0; for (auto& z : zones) pk += z.peak / juce::jmax (1e-6, z.v); pk /= (double) zones.size();
    const float sharp = juce::jlimit (2.5f, 8.5f, (float) (2.0 + pk * 1.6));

    static const float tgt[] { 6.0f, 5.0f, 5.0f, 5.0f, 2.5f }, atk[] { 3.0f, 5.0f, 2.0f, 10.0f, 8.0f }, rel[] { 50.0f, 60.0f, 70.0f, 140.0f, 110.0f },
                       prot[] { 0.4f, 0.6f, 1.0f, 0.6f, 0.9f };
    const float resonant = juce::jlimit (0.5f, 1.6f, (float) (mean / 1.2));
    setParam (IDs::depth, std::round (juce::jlimit (1.5f, 12.0f, tgt[lSource] * (0.7f + 0.3f * resonant)) * 10.0f) / 10.0f);
    setParam (IDs::sharp, std::round (sharp * 10.0f) / 10.0f);
    setParam (IDs::select, lSource == MixMaster ? 5.0f : 3.5f);
    setParam (IDs::attack, atk[lSource]); setParam (IDs::release, rel[lSource]); setParam (IDs::protect, prot[lSource]);
    setParam (IDs::lowF, lo); setParam (IDs::highF, hi);
    static const float keep[] { 0.5f, 0.4f, 0.0f, 0.5f, 0.4f }, foc[] { 0.3f, 0.4f, 0.7f, 0.5f, 0.3f };
    setParam (IDs::toneKeep, keep[lSource]); setParam (IDs::focus, foc[lSource]);
    setParam (IDs::res, lSource == Bass || lSource == MixMaster ? 2.0f : 1.0f);
    setParam (IDs::bf (0), lSource == Bass ? 40.0f : 200.0f); setParam (IDs::bg (0), lSource == Vocal ? -12.0f : 0.0f);
    setParam (IDs::bf (5), 12000.0f); setParam (IDs::bg (5), 0.0f);
    juce::StringArray msg;
    for (int b = 0; b < 4; ++b)
    {
        if (b < (int) hot.size())
        {
            const float g = juce::jlimit (2.0f, 9.0f, (float) (3.0 * hot[(size_t) b].v / juce::jmax (1e-6, mean)));
            setParam (IDs::bf (b + 1), hot[(size_t) b].f); setParam (IDs::bg (b + 1), std::round (g * 2.0f) / 2.0f);
            msg.add (hzText (hot[(size_t) b].f));
        }
        else { setParam (IDs::bf (b + 1), bandDefaults()[b + 1]); setParam (IDs::bg (b + 1), 0.0f); }
    }
    learnResult = (msg.isEmpty() ? juce::String ("no strong resonances - gentle setting") : "hot zones " + msg.joinIntoString (", "))
                + ", sharpness " + juce::String (sharp, 1);
    ++programVersion;
}

//==============================================================================
void LumenResonanceProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenResonanceProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenResonanceProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenResonanceProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* LumenResonanceProcessor::createEditor() { return new LumenResonanceEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenResonanceProcessor(); }
