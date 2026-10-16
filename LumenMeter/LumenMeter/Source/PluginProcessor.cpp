#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenMeterProcessor::LumenMeterProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenMeter", createLayout())
{
    formats.registerBasicFormats();
}

bool LumenMeterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void LumenMeterProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    meter.prepare (fs);
    histPos.store (0); histCount.store (0); histAcc = 0;
    cLL = cRR = cLR = lLL = lRR = lLR = dcAcc = 0;
}

LumenMeterProcessor::Snapshot LumenMeterProcessor::snapshot() const
{
    Snapshot s;
    s.m = meter.momentary(); s.s = meter.shortTerm(); s.i = meter.integrated(); s.lra = meter.range();
    s.mMax = meter.momentaryMax(); s.sMax = meter.shortTermMax();
    s.tpMax = juce::Decibels::gainToDecibels (meter.truePeakMax(), -100.0f); s.tpNow = juce::Decibels::gainToDecibels (meter.truePeakNow(), -100.0f);
    s.spMax = juce::Decibels::gainToDecibels (meter.samplePeakMax(), -100.0f);
    s.corr = aCorr.load(); s.corrLow = aCorrLow.load(); s.dc = aDc.load(); s.clips = meter.clips();
    s.valid = meter.hasIntegrated();
    return s;
}

void LumenMeterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    const float* L = buffer.getReadPointer (0);
    const float* R = buffer.getReadPointer (buffer.getNumChannels() > 1 ? 1 : 0);

    // transport: auto reset when playback starts; reference follows the host time
    bool playing = false; double timeSec = -1.0;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition())
    {
        playing = pos->getIsPlaying();
        if (auto t = pos->getTimeInSeconds()) timeSec = *t;
    }
    if (playing && ! wasPlaying && P (IDs::autoReset) > 0.5f) resetPending.store (true);
    wasPlaying = playing;
    if (resetPending.exchange (false)) { meter.reset(); histPos.store (0); histCount.store (0); histAcc = 0; cLL = cRR = cLR = lLL = lRR = lLR = dcAcc = 0; }

    // ---- analysis of the INPUT (your mix) ----
    meter.process (L, R, n);
    const bool hold = P (IDs::hold) > 0.5f;
    histAcc += n;
    while (histAcc >= (int) (fs * 0.1))
    {
        histAcc -= (int) (fs * 0.1);
        if (! hold)
        {
            const int hp = histPos.load();
            histM[hp] = meter.momentary(); histS[hp] = meter.shortTerm();
            histPos.store ((hp + 1) % kHist); histCount.store (std::min (kHist, histCount.load() + 1));
        }
    }
    const int rp = ringPos.load (std::memory_order_relaxed);
    const float lpA = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 150.0f / (float) fs);
    const float dec = std::exp (-1.0f / (0.5f * (float) fs));
    const double dcA = 1.0 / (0.4 * fs);
    // 3-band width (one-pole splits at 200 Hz / 3 kHz)
    const float a200 = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 200.0f / (float) fs), a3k = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 3000.0f / (float) fs);
    for (int i = 0; i < n; ++i)
    {
        const float l = L[i], r = R[i];
        ring[0][(rp + i) & (kRing - 1)] = l; ring[1][(rp + i) & (kRing - 1)] = r;
        cLL = cLL * dec + (double) l * l; cRR = cRR * dec + (double) r * r; cLR = cLR * dec + (double) l * r;
        lpL[0] += (l - lpL[0]) * lpA; lpL[1] += (lpL[0] - lpL[1]) * lpA; lpR[0] += (r - lpR[0]) * lpA; lpR[1] += (lpR[0] - lpR[1]) * lpA;
        lLL = lLL * dec + (double) lpL[1] * lpL[1]; lRR = lRR * dec + (double) lpR[1] * lpR[1]; lLR = lLR * dec + (double) lpL[1] * lpR[1];
        dcAcc += (0.5 * (l + r) - dcAcc) * dcA;
        const float m = 0.5f * (l + r), s = 0.5f * (l - r);
        const float sig[2] { m, s };
        for (int c = 0; c < 2; ++c)
        {
            float& low = bandState[c][0][0]; float& lo3 = bandState[c][1][0];
            low += (sig[c] - low) * a200; lo3 += (sig[c] - lo3) * a3k;
            const float bands[3] { low, lo3 - low, sig[c] - lo3 };
            for (int b = 0; b < 3; ++b) (c == 0 ? bm[b] : bs[b]) = (c == 0 ? bm[b] : bs[b]) * dec + (double) bands[b] * bands[b];
        }
    }
    ringPos.store ((rp + n) & (kRing - 1), std::memory_order_release);
    auto corr = [] (double a, double b, double ab) { const double d = std::sqrt (a * b); return d < 1e-12 ? 1.0f : (float) juce::jlimit (-1.0, 1.0, ab / d); };
    aCorr.store (corr (cLL, cRR, cLR)); aCorrLow.store (corr (lLL, lRR, lLR)); aDc.store ((float) dcAcc);
    for (int b = 0; b < 3; ++b) bandWidth[b].store (bm[b] > 1e-12 ? (float) std::sqrt (bs[b] / bm[b]) : 0.0f);

    // ---- output: pass-through, or platform preview, or the level-matched reference ----
    const auto rf = getReference();
    const bool refOn = P (IDs::refAB) > 0.5f && rf != nullptr && rf->buf.getNumSamples() > 0;
    float gDb = 0.0f;
    if (refOn)
    {
        if (P (IDs::refMatch) > 0.5f && meter.hasIntegrated() && rf->integrated > -69.0f) gDb = meter.integrated() - rf->integrated;
        else if (P (IDs::refMatch) > 0.5f && rf->integrated > -69.0f) gDb = meter.shortTerm() > -69.0f ? meter.shortTerm() - rf->integrated : 0.0f;
    }
    else if (P (IDs::preview) > 0.5f)
    {
        const auto& pl = platforms()[(size_t) juce::jlimit (0, (int) platforms().size() - 1, (int) P (IDs::target))];
        gDb = platformGain (pl, meter.integrated(), juce::Decibels::gainToDecibels (meter.truePeakMax(), -100.0f));
    }
    appliedGainDb.store (gDb);
    const float gT = juce::Decibels::decibelsToGain (gDb);
    const float sm = 1.0f - std::exp (-1.0f / (0.05f * (float) fs));
    if (refOn)
    {
        const auto& b = rf->buf;
        const double ratio = rf->rate / fs, len = (double) b.getNumSamples();
        if (playing && timeSec >= 0.0) refPos = std::fmod (timeSec * rf->rate, len);
        for (int i = 0; i < n; ++i)
        {
            gainSm += (gT - gainSm) * sm;
            const int i0 = (int) refPos; const float fr = (float) (refPos - i0); const int i1 = (i0 + 1) % b.getNumSamples();
            for (int c = 0; c < buffer.getNumChannels(); ++c)
            {
                const float* d = b.getReadPointer (std::min (c, b.getNumChannels() - 1));
                buffer.setSample (c, i, (d[i0] + (d[i1] - d[i0]) * fr) * gainSm);
            }
            refPos += ratio; if (refPos >= len) refPos -= len;
        }
    }
    else if (std::abs (gDb) > 1e-4f || std::abs (gainSm - 1.0f) > 1e-5f)
    {
        for (int i = 0; i < n; ++i)
        {
            gainSm += (gT - gainSm) * sm;
            for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.setSample (c, i, buffer.getSample (c, i) * gainSm);
        }
    }
}

std::vector<float> LumenMeterProcessor::octaveSpectrum (const juce::AudioBuffer<float>& b, double rate, int maxFrames)
{
    constexpr int order = 13, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2), win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
    std::vector<double> pw ((size_t) N / 2 + 1, 0.0);
    const int ns = b.getNumSamples();
    if (ns < N) return std::vector<float> ((size_t) kSpecBands, -120.0f);
    const int frames = std::min (maxFrames, std::max (1, (ns - N) / (N / 2)));
    const int step = std::max (N / 2, (ns - N) / std::max (1, frames));
    int cnt = 0;
    for (int s = 0; s + N <= ns && cnt < maxFrames; s += step, ++cnt)
    {
        for (int i = 0; i < N; ++i) { float v = 0; for (int c = 0; c < b.getNumChannels(); ++c) v += b.getSample (c, s + i); buf[(size_t) i] = v / (float) b.getNumChannels() * win[(size_t) i]; }
        std::fill (buf.begin() + N, buf.end(), 0.0f);
        fft.performRealOnlyForwardTransform (buf.data(), true);
        for (int k = 0; k <= N / 2; ++k) pw[(size_t) k] += (double) buf[(size_t) (2 * k)] * buf[(size_t) (2 * k)] + (double) buf[(size_t) (2 * k + 1)] * buf[(size_t) (2 * k + 1)];
    }
    std::vector<float> out ((size_t) kSpecBands);
    const double norm = 1.0 / (std::max (1, cnt) * (N / 4.0) * (N / 4.0));
    for (int bnd = 0; bnd < kSpecBands; ++bnd)
    {
        const double f0 = bandFreq (bnd) / std::pow (2.0, 1.0 / 12.0), f1 = bandFreq (bnd) * std::pow (2.0, 1.0 / 12.0);
        const int k0 = std::max (1, (int) std::floor (f0 * N / rate)), k1 = std::max (k0, std::min (N / 2, (int) std::ceil (f1 * N / rate)));
        double s = 0; for (int k = k0; k <= k1; ++k) s += pw[(size_t) k];
        out[(size_t) bnd] = (float) (10.0 * std::log10 (s * norm + 1e-14));
    }
    return out;
}

bool LumenMeterProcessor::loadReference (const juce::File& f, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    if (r == nullptr) { error = "can't read " + f.getFileName(); return false; }
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 600.0));
    auto t = std::make_shared<RefTrack>();
    t->buf.setSize ((int) std::min (2u, std::max (1u, r->numChannels)), len);
    r->read (&t->buf, 0, len, 0, true, t->buf.getNumChannels() > 1);
    t->rate = r->sampleRate; t->name = f.getFileNameWithoutExtension(); t->path = f.getFullPathName();
    lumen::meter::LoudnessMeter m; m.prepare (t->rate);
    const float* l = t->buf.getReadPointer (0); const float* rr = t->buf.getReadPointer (t->buf.getNumChannels() > 1 ? 1 : 0);
    for (int s = 0; s < len; s += 8192) m.process (l + s, rr + s, std::min (8192, len - s));
    t->integrated = m.integrated(); t->lra = m.range(); t->truePeak = juce::Decibels::gainToDecibels (m.truePeakMax(), -100.0f);
    t->spectrum = octaveSpectrum (t->buf, t->rate);
    std::atomic_store (&ref, std::shared_ptr<const RefTrack> (t));
    return true;
}

void LumenMeterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto rf = getReference()) state.setProperty ("refPath", rf->path, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenMeterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            const juce::File rf (vt.getProperty ("refPath", "").toString());
            if (rf.existsAsFile()) { juce::String err; loadReference (rf, err); }
            vt.removeProperty ("refPath", nullptr);
            apvts.replaceState (vt);
            for (auto* prm : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                {
                    auto child = vt.getChildWithProperty ("id", rp->paramID);
                    if (child.isValid() && child.hasProperty ("value")) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
                }
        }
}

juce::AudioProcessorEditor* LumenMeterProcessor::createEditor() { return new LumenMeterEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenMeterProcessor(); }
