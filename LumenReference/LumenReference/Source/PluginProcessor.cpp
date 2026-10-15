#include "PluginProcessor.h"
#include "PluginEditor.h"

LumenReferenceProcessor::LumenReferenceProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenReference", createLayout())
{
    for (auto& b : busy) b.store (0);
    for (auto& p : slotPtr) p.store (nullptr);
    fifoL.assign ((size_t) fifo.getTotalSize(), 0.0f);
    fifoR.assign ((size_t) fifo.getTotalSize(), 0.0f);
}

LumenReferenceProcessor::~LumenReferenceProcessor()
{
    pool.removeAllJobs (true, 20000);
}

bool LumenReferenceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void LumenReferenceProcessor::prepareToPlay (double sampleRate, int)
{
    const bool changed = std::abs (sampleRate - fs) > 0.5;
    fs = sampleRate;
    mixMeter.prepare (fs);
    tpL.reset(); tpR.reset(); tpHold = 0.0f;
    for (auto& b : meq) b.reset();
    for (auto& v : meqLast) v = 1.0e9f;
    abSm = apvts.getRawParameterValue (IDs::ab)->load() > 0.5f ? 1.0f : 0.0f;
    if (changed)
    {
        // re-resample loaded references for the new rate (background)
        for (int s = 0; s < kSlots; ++s)
        {
            auto d = getRef (s);
            if (d == nullptr) continue;
            ++busy[s];
            pool.addJob ([this, s, d, rate = fs]
            {
                auto nd = resampled (d->original, d->originalRate, rate, d->name, d->path);
                installData (s, nd);
                --busy[s];
                requestAnalysis (s);
            });
        }
    }
}

//==============================================================================
std::shared_ptr<RefData> LumenReferenceProcessor::resampled (const juce::AudioBuffer<float>& src, double srcRate, double dstRate, juce::String name, juce::String path)
{
    auto d = std::make_shared<RefData>();
    d->name = std::move (name); d->path = std::move (path);
    d->original.makeCopyOf (src); d->originalRate = srcRate;
    d->playRate = dstRate;
    const int inN = src.getNumSamples();
    if (std::abs (srcRate - dstRate) < 0.5) d->play.makeCopyOf (src);
    else
    {
        const double ratio = srcRate / dstRate;
        const int outN = (int) std::floor ((double) inN / ratio) - 8;
        d->play.setSize (2, juce::jmax (1, outN));
        for (int c = 0; c < 2; ++c)
        {
            juce::WindowedSincInterpolator interp;
            interp.process (ratio, src.getReadPointer (juce::jmin (c, src.getNumChannels() - 1)), d->play.getWritePointer (c), d->play.getNumSamples());
        }
    }
    const int cols = 1200, N = d->play.getNumSamples();
    d->ovMin.assign (cols, 0.0f); d->ovMax.assign (cols, 0.0f);
    for (int c = 0; c < cols; ++c)
    {
        const int a = (int) ((long long) c * N / cols), b = juce::jmax (a + 1, (int) ((long long) (c + 1) * N / cols));
        float mn = 0.0f, mx = 0.0f;
        for (int i = a; i < b && i < N; ++i)
        {
            const float v = 0.5f * (d->play.getSample (0, i) + d->play.getSample (1, i));
            mn = juce::jmin (mn, v); mx = juce::jmax (mx, v);
        }
        d->ovMin[(size_t) c] = mn; d->ovMax[(size_t) c] = mx;
    }
    return d;
}

void LumenReferenceProcessor::installData (int slot, std::shared_ptr<const RefData> d)
{
    const juce::ScopedLock sl (lock);
    if (slots[slot].data != nullptr) retired.push_back (slots[slot].data);
    while (retired.size() > 8) retired.erase (retired.begin());
    slots[slot].data = d;
    slots[slot].error.clear();
    slotPtr[slot].store (d.get(), std::memory_order_release);
    ++refVersion;
}

void LumenReferenceProcessor::loadReference (int slot, const juce::File& f)
{
    if (! juce::isPositiveAndBelow (slot, kSlots)) return;
    ++busy[slot];
    pool.addJob ([this, slot, f, rate = fs]
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
        if (reader == nullptr)
        {
            const juce::ScopedLock sl (lock);
            slots[slot].error = "Can't open " + f.getFileName();
            --busy[slot]; ++refVersion; return;
        }
        const auto maxLen = (juce::int64) (reader->sampleRate * 60.0 * 15.0);
        const int len = (int) juce::jmin (reader->lengthInSamples, maxLen);
        juce::AudioBuffer<float> buf (2, juce::jmax (1, len));
        reader->read (&buf, 0, len, 0, true, reader->numChannels > 1);
        if (reader->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, len);
        auto d = resampled (buf, reader->sampleRate, rate, f.getFileNameWithoutExtension(), f.getFullPathName());
        installData (slot, d);
        --busy[slot];
        requestAnalysis (slot);
    });
}

void LumenReferenceProcessor::clearReference (int slot)
{
    const juce::ScopedLock sl (lock);
    if (slots[slot].data != nullptr) retired.push_back (slots[slot].data);
    slots[slot].data.reset();
    slots[slot].profile = {};
    slotPtr[slot].store (nullptr);
    ++refVersion;
}

void LumenReferenceProcessor::requestAnalysis (int slot)
{
    auto d = getRef (slot);
    if (d == nullptr) return;
    const float ls = apvts.getRawParameterValue (IDs::loopS)->load(), le = apvts.getRawParameterValue (IDs::loopE)->load();
    ++busy[slot];
    pool.addJob ([this, slot, d, ls, le]
    {
        const int N = d->play.getNumSamples();
        int a = (int) (juce::jmin (ls, le) * N), b = (int) (juce::jmax (ls, le) * N);
        if (b - a < (int) (d->playRate * 2.0)) { a = 0; b = N; }
        auto prof = lumen::ref::analyseBuffer (d->play, a, b, d->playRate);
        {
            const juce::ScopedLock sl (lock);
            if (slots[slot].data == d) slots[slot].profile = prof;
        }
        --busy[slot]; ++refVersion;
    });
}

std::shared_ptr<const RefData> LumenReferenceProcessor::getRef (int slot) const
{
    const juce::ScopedLock sl (lock);
    return juce::isPositiveAndBelow (slot, kSlots) ? slots[slot].data : nullptr;
}
lumen::ref::Profile LumenReferenceProcessor::getRefProfile (int slot) const
{
    const juce::ScopedLock sl (lock);
    return juce::isPositiveAndBelow (slot, kSlots) ? slots[slot].profile : lumen::ref::Profile {};
}
juce::String LumenReferenceProcessor::getError (int slot) const
{
    const juce::ScopedLock sl (lock);
    return slots[slot].error;
}

//==============================================================================
int LumenReferenceProcessor::pullMix (float* L, float* R, int maxSamples)
{
    int s1, n1, s2, n2;
    fifo.prepareToRead (maxSamples, s1, n1, s2, n2);
    std::copy (fifoL.begin() + s1, fifoL.begin() + s1 + n1, L); std::copy (fifoR.begin() + s1, fifoR.begin() + s1 + n1, R);
    std::copy (fifoL.begin() + s2, fifoL.begin() + s2 + n2, L + n1); std::copy (fifoR.begin() + s2, fifoR.begin() + s2 + n2, R + n1);
    fifo.finishedRead (n1 + n2);
    return n1 + n2;
}

void LumenReferenceProcessor::captureMatchEq (const lumen::ref::Profile& mix, const lumen::ref::Profile& ref)
{
    if (! mix.valid || ! ref.valid) return;
    float target[lumen::ref::kPoints];
    for (int i = 0; i < lumen::ref::kPoints; ++i) target[i] = juce::jlimit (-12.0f, 12.0f, ref.spec[i] - mix.spec[i]);
    float g[kMeqBands] {};
    const double rate = 48000.0;
    for (int iter = 0; iter < 12; ++iter)
    {
        lumen::Biquad bq[kMeqBands];
        for (int b = 0; b < kMeqBands; ++b) bq[b].set (lumen::meqType (b, kMeqBands), kMeqFreq[b], g[b], 1.0, rate);
        for (int b = 0; b < kMeqBands; ++b)
        {
            double err = 0.0; int n = 0;
            for (int i = 0; i < lumen::ref::kPoints; ++i)
            {
                const float f = lumen::ref::pointFreq (i);
                const bool in = b == 0 ? f < 90.0f : b == kMeqBands - 1 ? f > 8000.0f
                                     : std::abs (std::log2 (f / kMeqFreq[b])) < 0.5f;
                if (! in) continue;
                double resp = 0.0; for (auto& q : bq) resp += q.magnitudeDb (f, rate);
                err += target[i] - resp; ++n;
            }
            if (n > 0) g[b] = juce::jlimit (-9.0f, 9.0f, g[b] + 0.6f * (float) (err / n));
        }
    }
    for (int b = 0; b < kMeqBands; ++b)
        if (auto* p = apvts.getParameter (IDs::meqGain (b)))
        { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (std::round (g[b] * 10.0f) / 10.0f)); p->endChangeGesture(); }
}

//==============================================================================
void LumenReferenceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;
    const bool stereo = buffer.getNumChannels() > 1;

    if (resetMix.exchange (false)) { mixMeter.reset(); tpHold = 0.0f; tpL.reset(); tpR.reset(); }

    // ---- mix meters + analysis FIFO (pre-everything) ----
    for (int i = 0; i < n; ++i)
    {
        mixMeter.process (L[i], R[i]);
        tpHold = std::max (tpHold, std::max (tpL.push (L[i]), tpR.push (R[i])));
    }
    mixM.store (mixMeter.momentary); mixS.store (mixMeter.shortTerm); mixI.store (mixMeter.integrated);
    mixTp.store (20.0f * std::log10 (std::max (tpHold, 1.0e-6f)));
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        std::copy (L, L + n1, fifoL.begin() + s1); std::copy (R, R + n1, fifoR.begin() + s1);
        std::copy (L + n1, L + n1 + n2, fifoL.begin() + s2); std::copy (R + n1, R + n1 + n2, fifoR.begin() + s2);
        fifo.finishedWrite (n1 + n2);
    }

    // ---- transport ----
    bool playing = false; juce::int64 timeSamples = 0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto t = pos->getTimeInSamples()) timeSamples = *t;
        }
    hostPlaying.store (playing);

    // ---- match EQ on the mix path ----
    const bool meqOn = apvts.getRawParameterValue (IDs::meqOn)->load() > 0.5f;
    if (meqOn)
    {
        const float amt = apvts.getRawParameterValue (IDs::meqAmt)->load();
        for (int b = 0; b < kMeqBands; ++b)
        {
            const float gdb = apvts.getRawParameterValue (IDs::meqGain (b))->load() * amt;
            if (std::abs (gdb - meqLast[b]) > 1.0e-3f) { meq[b].set (lumen::meqType (b, kMeqBands), kMeqFreq[b], gdb, 1.0, fs); meqLast[b] = gdb; }
        }
    }

    // ---- reference playback ----
    const int slot = activeSlot();
    const RefData* d = slotPtr[slot].load (std::memory_order_acquire);
    const bool wantRef = apvts.getRawParameterValue (IDs::ab)->load() > 0.5f && d != nullptr;
    const float abT = wantRef ? 1.0f : 0.0f;
    const float abCoef = 1.0f - std::exp (-1.0f / (0.015f * (float) fs));
    const bool sync = apvts.getRawParameterValue (IDs::sync)->load() > 0.5f;
    const bool preview = apvts.getRawParameterValue (IDs::preview)->load() > 0.5f;

    float targetGain = 1.0f;
    double ls = 0, le = 0, step = 0;
    int N = 0;
    if (d != nullptr)
    {
        N = d->play.getNumSamples();
        const float a = apvts.getRawParameterValue (IDs::loopS)->load(), b = apvts.getRawParameterValue (IDs::loopE)->load();
        ls = juce::jmin (a, b) * (N - 1); le = juce::jmax (a, b) * (N - 1);
        if (le - ls < d->playRate * 0.25) { ls = 0; le = N - 1; }
        step = d->playRate / fs;
        // loudness match: bring the reference to the mix's loudness
        const float refL = [&] { const juce::ScopedTryLock tl (lock); return tl.isLocked() ? slots[slot].profile.lufsI : activeRefLufs.load(); } ();
        activeRefLufs.store (refL);
        float gdb = apvts.getRawParameterValue (IDs::trim)->load();
        if (apvts.getRawParameterValue (IDs::match)->load() > 0.5f && refL > -70.0f)
        {
            const float mixRef = mixMeter.integrated > -70.0f ? mixMeter.integrated : (mixMeter.shortTerm > -70.0f ? mixMeter.shortTerm : refL);
            gdb += juce::jlimit (-24.0f, 24.0f, mixRef - refL);
        }
        refGainDb.store (gdb);
        targetGain = std::pow (10.0f, gdb / 20.0f);
        { const double sk = seekRequest.exchange (-1.0); if (sk >= 0.0) refPos = juce::jlimit (ls, le - 1.0, sk * (N - 1)); }
        if (sync && playing) refPos = ls + std::fmod ((double) timeSamples * step, std::max (1.0, le - ls));
        if (refPos < ls || refPos >= le) refPos = ls;
    }
    const bool advance = d != nullptr && (playing || preview);
    const float gCoef = 1.0f - std::exp (-(float) n / (0.05f * (float) fs));
    gainSm += (targetGain - gainSm) * gCoef;

    float pl = 0.0f, pr = 0.0f;
    const float* rl = d != nullptr ? d->play.getReadPointer (0) : nullptr;
    const float* rr = d != nullptr ? d->play.getReadPointer (1) : nullptr;
    for (int i = 0; i < n; ++i)
    {
        float ml = L[i], mr = R[i];
        if (meqOn) for (auto& b : meq) { ml = b.process (ml, 0); mr = b.process (mr, 1); }
        abSm += (abT - abSm) * abCoef;
        float refl = 0.0f, refr = 0.0f;
        if (d != nullptr && advance)
        {
            const int i0 = (int) refPos; const float fr = (float) (refPos - i0);
            const int i1 = juce::jmin (i0 + 1, N - 1);
            refl = (rl[i0] + fr * (rl[i1] - rl[i0])) * gainSm;
            refr = (rr[i0] + fr * (rr[i1] - rr[i0])) * gainSm;
            refPos += step;
            if (refPos >= le) refPos = ls + (refPos - le);
        }
        const float a = std::cos (abSm * juce::MathConstants<float>::halfPi), b = std::sin (abSm * juce::MathConstants<float>::halfPi);
        float ol = ml * a + refl * b, orr = mr * a + refr * b;
        if (apvts.getRawParameterValue (IDs::mono)->load() > 0.5f) { const float m = 0.5f * (ol + orr); ol = orr = m; }
        L[i] = ol; if (stereo) R[i] = orr;
        pl = std::max (pl, std::abs (ol)); pr = std::max (pr, std::abs (orr));
    }
    outPeakL.store (pl); outPeakR.store (pr);
    refPosSec.store (d != nullptr ? refPos / d->playRate : 0.0);
}

//==============================================================================
void LumenReferenceProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    for (int i = state.getNumChildren(); --i >= 0;) if (state.getChild (i).hasType ("Refs")) state.removeChild (i, nullptr);
    juce::ValueTree refs ("Refs");
    for (int s = 0; s < kSlots; ++s)
        if (auto d = getRef (s)) refs.setProperty ("path" + juce::String (s), d->path, nullptr);
    state.appendChild (refs, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LumenReferenceProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            const auto refs = vt.getChildWithName ("Refs");
            for (int i = vt.getNumChildren(); --i >= 0;) if (vt.getChild (i).hasType ("Refs")) vt.removeChild (i, nullptr);
            apvts.replaceState (vt);
            for (auto* p : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    auto child = vt.getChildWithProperty ("id", rp->paramID);
                    if (child.isValid() && child.hasProperty ("value")) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
                }
            for (int s = 0; s < kSlots; ++s)
            {
                const juce::String path = refs.getProperty ("path" + juce::String (s)).toString();
                auto cur = getRef (s);
                if (path.isNotEmpty() && juce::File (path).existsAsFile() && (cur == nullptr || cur->path != path)) loadReference (s, juce::File (path));
            }
        }
}

juce::AudioProcessorEditor* LumenReferenceProcessor::createEditor() { return new LumenReferenceEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenReferenceProcessor(); }
