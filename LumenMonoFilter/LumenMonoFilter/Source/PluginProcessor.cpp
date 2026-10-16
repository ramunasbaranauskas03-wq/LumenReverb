#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenMonoFilterProcessor::LumenMonoFilterProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("Lumen MonoFilter FIR"),
      apvts (*this, nullptr, "LumenMonoFilter", createLayout())
{
    startThread (juce::Thread::Priority::low);
}

LumenMonoFilterProcessor::~LumenMonoFilterProcessor()
{
    signalThreadShouldExit();
    notify();
    stopThread (4000);
}

bool LumenMonoFilterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

lumen::mono::Params LumenMonoFilterProcessor::readParams() const
{
    lumen::mono::Params p;
    p.source = (int) P (IDs::source);
    p.lowF = P (IDs::lowF); p.lowW = P (IDs::lowW); p.highF = P (IDs::highF); p.highW = P (IDs::highW);
    p.hpf = P (IDs::hpf);
    p.linear = P (IDs::mode) > 0.5f; p.hq = P (IDs::hq) > 0.5f;
    p.align = P (IDs::align) > 0.5f; p.autoAlign = P (IDs::autoAlign) > 0.5f; p.alignMs = P (IDs::alignMs); p.flip = P (IDs::flip) > 0.5f;
    p.midDb = P (IDs::midTrim); p.sideDb = P (IDs::sideTrim); p.autoTrim = P (IDs::autoTrim) > 0.5f;
    p.monitor = (int) P (IDs::monitor); p.outDb = P (IDs::out);
    return p;
}

juce::uint64 LumenMonoFilterProcessor::hashParams (const lumen::mono::Params& p, double sr)
{
    juce::uint64 h = 1469598103934665603ull;
    auto mixIn = [&h] (float v) { juce::uint32 u; std::memcpy (&u, &v, 4); h = (h ^ u) * 1099511628211ull; };
    mixIn ((float) sr); mixIn (p.lowF); mixIn (p.lowW); mixIn (p.highF); mixIn (p.highW); mixIn (p.hpf);
    mixIn (p.midDb); mixIn (p.sideDb); mixIn (p.hq ? 1.0f : 0.0f);
    return h | 1ull;
}

void LumenMonoFilterProcessor::run()
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
        const int N = lumen::mono::firLength (sr, p.hq);
        lumen::mono::FirSet f;
        lumen::mono::designFirs (p, sr, N, f);
        {
            const juce::ScopedLock sl (engineLock);
            if (std::abs (curFs.load() - sr) > 0.1) continue;
            engine.loadFirs (std::move (f), N);
        }
        loadedHash.store (h);
        firReady.store (true);
    }
}

void LumenMonoFilterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    {
        const juce::ScopedLock sl (engineLock);
        fs = sampleRate;
        engine.setParams (readParams());
        engine.prepare (fs, juce::jlimit (64, 2048, samplesPerBlock));
        loadedHash.store (0); firReady.store (false);
        curFs.store (fs);
    }
    fL.assign ((size_t) (fs * 5.0), 0.0f); fR.assign ((size_t) (fs * 5.0), 0.0f);
    engine.setParams (readParams());
    setLatencySamples (engine.latency());
    for (auto& v : cin) v = 0;
    for (auto& v : cout_) v = 0;
    notify();
}

void LumenMonoFilterProcessor::setParam (const juce::String& id, float v)
{
    if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
    { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture(); }
}

void LumenMonoFilterProcessor::setCurrentProgram (int index)
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

static void accumCorr (double* c, const float* L, const float* R, int n, double decay)
{
    double ll = 0, rr = 0, lr = 0;
    for (int i = 0; i < n; ++i) { ll += (double) L[i] * L[i]; rr += (double) R[i] * R[i]; lr += (double) L[i] * R[i]; }
    c[0] = c[0] * decay + ll; c[1] = c[1] * decay + rr; c[2] = c[2] * decay + lr;
}
static float corrOf (const double* c) { const double d = std::sqrt (c[0] * c[1]); return d < 1e-9 ? 1.0f : (float) juce::jlimit (-1.0, 1.0, c[2] / d); }

void LumenMonoFilterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);

    const auto p = readParams();
    engine.setParams (p);
    if (engine.latency() != getLatencySamples()) setLatencySamples (engine.latency());

    if (focusing.load())
    {
        int pos = fPos.load();
        for (int i = 0; i < n && pos < fLen; ++i, ++pos) { fL[(size_t) pos] = L[i]; fR[(size_t) pos] = R[i]; }
        fPos.store (pos);
    }
    const double decay = std::exp (-(double) n / (0.3 * fs));
    accumCorr (cin, L, R, n, decay);
    const int rp = ringPos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) { const int k = (rp + i) & (kRing - 1); ring[0][k] = L[i]; ring[1][k] = R[i]; }

    engine.process (L, R, n);

    accumCorr (cout_, L, R, n, decay);
    for (int i = 0; i < n; ++i) { const int k = (rp + i) & (kRing - 1); ring[2][k] = L[i]; ring[3][k] = R[i]; }
    ringPos.store ((rp + n) & (kRing - 1), std::memory_order_release);
    corrIn.store (corrOf (cin)); corrOut.store (corrOf (cout_));
    alignNow.store (engine.alignMs()); polNow.store (engine.polarity()); trimNow.store (engine.trim()); confNow.store (engine.alignConfidence);
}

//==============================================================================
void LumenMonoFilterProcessor::startFocus (Goal g)
{
    goal = g;
    fLen = (int) std::min (fL.size(), (size_t) (fs * 5.0));
    fPos.store (0);
    focusResult.clear();
    focusing.store (true);
}

void LumenMonoFilterProcessor::finishFocusIfReady()
{
    if (! focusing.load() || fPos.load() < fLen) return;
    focusing.store (false);

    // 1) cross-spectra per 1/3 octave (20 Hz .. 1.6 kHz)
    constexpr int order = 13, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> a ((size_t) N * 2), b ((size_t) N * 2), win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
    constexpr int nb = 20;
    double sll[nb] {}, srr[nb] {}, slr[nb] {}, tot = 1e-12, below25 = 0;
    auto bandOf = [] (double f) { const double x = 3.0 * std::log2 (f / 20.0); return x < 0 ? -1 : (int) x; };
    for (int start = 0; start + N <= fLen; start += N / 2)
    {
        for (int i = 0; i < N; ++i) { a[(size_t) i] = fL[(size_t) (start + i)] * win[(size_t) i]; b[(size_t) i] = fR[(size_t) (start + i)] * win[(size_t) i]; }
        std::fill (a.begin() + N, a.end(), 0.0f); std::fill (b.begin() + N, b.end(), 0.0f);
        fft.performRealOnlyForwardTransform (a.data()); fft.performRealOnlyForwardTransform (b.data());
        for (int k = 1; k < N / 2; ++k)
        {
            const double f = k * fs / N;
            const double ar = a[(size_t) (2 * k)], ai = a[(size_t) (2 * k + 1)], br = b[(size_t) (2 * k)], bi = b[(size_t) (2 * k + 1)];
            const double el = ar * ar + ai * ai, er = br * br + bi * bi;
            tot += el + er;
            if (f < 25.0) below25 += el + er;
            const int bi_ = bandOf (f);
            if (bi_ < 0 || bi_ >= nb) continue;
            sll[bi_] += el; srr[bi_] += er; slr[bi_] += ar * br + ai * bi;
        }
    }
    // 2) L/R time offset of the lows (cross-correlation of the low-passed channels, +-3 ms)
    lumen::mono::Biquad lpL[2], lpR[2];
    lpL[0].lp (fs, 250, 0.5412); lpL[1].lp (fs, 250, 1.3066); lpR[0] = lpL[0]; lpR[1] = lpL[1];
    std::vector<float> xl ((size_t) fLen), xr ((size_t) fLen);
    for (int i = 0; i < fLen; ++i) { xl[(size_t) i] = lpL[1].process (lpL[0].process (fL[(size_t) i])); xr[(size_t) i] = lpR[1].process (lpR[0].process (fR[(size_t) i])); }
    const int maxLag = (int) std::round (0.003 * fs), step = juce::jmax (1, (int) std::round (fs / 24000.0));
    double best = 0, el = 1e-12, er = 1e-12; int bestLag = 0;
    for (int i = maxLag; i < fLen - maxLag; i += step) { el += (double) xl[(size_t) i] * xl[(size_t) i]; er += (double) xr[(size_t) i] * xr[(size_t) i]; }
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double c = 0;
        for (int i = maxLag; i < fLen - maxLag; i += step) c += (double) xl[(size_t) i] * xr[(size_t) (i + lag)];
        if (std::abs (c) > std::abs (best)) { best = c; bestLag = lag; }
    }
    const double conf = std::abs (best) / std::sqrt (el * er);
    const float lagMs = (float) (bestLag / fs * 1000.0);
    if (tot < 1e-6) { focusResult = "nothing heard - play the track"; ++programVersion; return; }

    // 3) find the highest band (<= 315 Hz) whose stereo content is a problem (phasey or wide lows)
    float probTop = 0, worstCorr = 1;
    for (int k = 0; k < nb; ++k)
    {
        const double fc = 20.0 * std::pow (2.0, (k + 0.5) / 3.0);
        if (fc > 330.0) break;
        const double e = sll[k] + srr[k];
        if (e / tot < 0.002) continue;
        const double corr = slr[k] / std::sqrt (sll[k] * srr[k] + 1e-20);
        const double side = (e - 2 * slr[k]) / (e + 2 * slr[k] + 1e-20);
        worstCorr = juce::jmin (worstCorr, (float) corr);
        if (corr < 0.6 || side > 0.3) probTop = (float) fc;
    }
    const bool offset = conf > 0.4 && std::abs (lagMs) > 0.04f;
    const bool needAlign = offset || worstCorr < 0.0f;
    float lowF = 0, highF = 0, lowW = 0; bool linear = false, align = false;
    switch (goal)
    {
        case Master:    lowF = juce::jlimit (60.0f, 160.0f, probTop > 0 ? probTop * 1.15f : 80.0f); highF = lowF * 2.2f; linear = true; align = offset; break;
        case Bass:      lowF = juce::jlimit (80.0f, 300.0f, probTop > 0 ? probTop * 1.2f : 120.0f); highF = lowF * 2.5f; align = true; break;
        case Recording: lowF = juce::jlimit (80.0f, 250.0f, probTop > 0 ? probTop : 120.0f); highF = lowF * 3.0f; lowW = 0.3f; align = needAlign; break;
    }
    lowF = std::round (lowF / 5.0f) * 5.0f; highF = std::round (highF / 5.0f) * 5.0f;
    setParam (IDs::lowF, lowF); setParam (IDs::highF, highF); setParam (IDs::lowW, lowW);
    setParam (IDs::mode, linear ? 1.0f : 0.0f);
    setParam (IDs::align, align ? 1.0f : 0.0f); setParam (IDs::autoAlign, 1.0f);
    setParam (IDs::alignMs, juce::jlimit (-3.0f, 3.0f, lagMs));
    setParam (IDs::flip, best < 0 && conf > 0.4 ? 1.0f : 0.0f);
    setParam (IDs::hpf, below25 / tot > 0.01 ? 25.0f : 0.0f);
    focusResult = "lows corr " + juce::String (worstCorr, 2) + (probTop > 0 ? ", wide up to " + hzText (probTop) : juce::String (", lows already mono"))
                + (offset ? ", L/R offset " + juce::String (lagMs, 2) + " ms" : juce::String())
                + (best < 0 && conf > 0.4 ? ", POLARITY flipped" : juce::String())
                + " -> mono below " + hzText (lowF);
    ++programVersion;
}

//==============================================================================
void LumenMonoFilterProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (abState[abSlot]);
    abSlot = to;
    if (abState[to].getSize() > 0) setStateInformation (abState[to].getData(), (int) abState[to].getSize());
}
void LumenMonoFilterProcessor::copyAB() { getStateInformation (abState[1 - abSlot]); }

void LumenMonoFilterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenMonoFilterProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* LumenMonoFilterProcessor::createEditor() { return new LumenMonoFilterEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenMonoFilterProcessor(); }
