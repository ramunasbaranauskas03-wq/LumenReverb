#include "PluginProcessor.h"
#include "PluginEditor.h"

// ================================================================================================ FIR worker
class LumenMonoFilterProcessor::FirWorker : public juce::Thread
{
public:
    explicit FirWorker (LumenMonoFilterProcessor& p) : juce::Thread ("Lumen MonoFilter FIR"), proc (p) {}
    void run() override
    {
        int built = -1;
        while (! threadShouldExit())
        {
            auto& conv = proc.engine.convolver();
            conv.collectGarbage();
            const int want = proc.firWantVersion.load();
            if (want != built && conv.canAccept())
            {
                lumen::mf::Params q;
                { const juce::SpinLock::ScopedLockType l (proc.firLock); q = proc.firWant; }
                conv.submit (conv.makeIR (lumen::mf::designSideFir (q, proc.engine.sampleRate(), proc.engine.firLength())));
                built = want;
            }
            wait (12);
        }
    }
private:
    LumenMonoFilterProcessor& proc;
};

// ================================================================================================ processor
LumenMonoFilterProcessor::LumenMonoFilterProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenMonoFilter", createLayout())
{
    worker = std::make_unique<FirWorker> (*this);
}

LumenMonoFilterProcessor::~LumenMonoFilterProcessor() { if (worker) worker->stopThread (2000); }

bool LumenMonoFilterProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void LumenMonoFilterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (worker) worker->stopThread (2000);
    fs = sampleRate; maxBlock = juce::jlimit (32, 8192, samplesPerBlock);
    engine.prepare (fs, maxBlock);
    const auto p = readParams();
    engine.setParams (p);
    engine.buildNow (p);
    { const juce::SpinLock::ScopedLockType l (firLock); firWant = p; }
    firLast = p; firLastValid = true;
    setLatencySamples (engine.latency());
    for (auto& f : lowMeter) f.reset();
    learnLen = (int) (fs * 6.0);
    learnL.assign ((size_t) learnLen, 0.0f); learnR.assign ((size_t) learnLen, 0.0f);
    cIn = cOut = cLow = Corr {};
    worker->startThread (juce::Thread::Priority::low);
}

lumen::mf::Params LumenMonoFilterProcessor::readParams() const
{
    lumen::mf::Params p;
    p.source = (int) P (IDs::source); p.polarity = (int) P (IDs::polarity); p.linear = P (IDs::mode) > 0.5f;
    p.monoOn = P (IDs::monoOn) > 0.5f; p.monoFreq = P (IDs::monoFreq); p.slope = (int) P (IDs::monoSlope); p.lowW = P (IDs::lowWidth);
    p.align = P (IDs::align) > 0.5f; p.alignAuto = P (IDs::alignAuto) > 0.5f; p.alignMs = P (IDs::alignMs);
    p.stAmt = P (IDs::stAmt); p.stMode = (int) P (IDs::stMode); p.stFreq = P (IDs::stFreq); p.stTime = P (IDs::stTime);
    p.x[0] = P (IDs::x1); p.x[1] = P (IDs::x2); p.x[2] = P (IDs::x3);
    for (int b = 0; b < 4; ++b) p.w[b] = P (IDs::w (b));
    p.width = P (IDs::width); p.balance = P (IDs::balance); p.mix = P (IDs::mix); p.outDb = P (IDs::out);
    p.autoGain = P (IDs::autoGain) > 0.5f; p.monitor = (int) P (IDs::monitor);
    return p;
}

void LumenMonoFilterProcessor::requestFir (const lumen::mf::Params& p)
{
    auto same = [] (const lumen::mf::Params& a, const lumen::mf::Params& b)
    {
        using lumen::mf::same;
        if (a.monoOn != b.monoOn || ! same (a.monoFreq, b.monoFreq) || a.slope != b.slope || ! same (a.lowW, b.lowW)) return false;
        for (int k = 0; k < 3; ++k) if (! same (a.x[k], b.x[k])) return false;
        for (int k = 0; k < 4; ++k) if (! same (a.w[k], b.w[k])) return false;
        return true;
    };
    if (firLastValid && same (p, firLast)) return;
    const juce::SpinLock::ScopedTryLockType l (firLock);
    if (! l.isLocked()) return;                       // try again next block
    firWant = p; firLast = p; firLastValid = true;
    ++firWantVersion;
    if (worker) worker->notify();
}

void LumenMonoFilterProcessor::rebuildFirNow()
{
    const auto p = readParams();
    engine.buildNow (p);
    const juce::SpinLock::ScopedLockType l (firLock);
    firWant = p; firLast = p; firLastValid = true;
}

void LumenMonoFilterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);

    const auto p = readParams();
    engine.setParams (p);
    requestFir (p);
    if (engine.latency() != getLatencySamples()) setLatencySamples (engine.latency());

    if (learning.load())
    {
        int pos = learnPos.load();
        for (int i = 0; i < n && pos < learnLen; ++i, ++pos) { learnL[(size_t) pos] = L[i]; learnR[(size_t) pos] = R[i]; }
        learnPos.store (pos);
    }

    // input meters + ring
    const double k = 1.0 - std::exp (-1.0 / (0.3 * fs));
    float ip = 0;
    const int rp = ringPos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        const float l = L[i], r = R[i];
        ip = std::max (ip, std::max (std::abs (l), std::abs (r)));
        accum (cIn, l, r, k);
        const double m = 0.5 * (l + r), s = 0.5 * (l - r);
        eMi += (m * m - eMi) * k; eSi += (s * s - eSi) * k;
        const int q = (rp + i) & (kRing - 1); ring[0][q] = l; ring[1][q] = r;
    }

    engine.process (L, R, n);

    float op = 0;
    for (int f = 0; f < 2; ++f) lowMeter[f].set (fs, std::clamp ((double) p.monoFreq, 30.0, 1000.0));
    for (int i = 0; i < n; ++i)
    {
        const float l = L[i], r = R[i];
        op = std::max (op, std::max (std::abs (l), std::abs (r)));
        accum (cOut, l, r, k);
        accum (cLow, lowMeter[0].lowOnly (l), lowMeter[1].lowOnly (r), k);
        const double m = 0.5 * (l + r), s = 0.5 * (l - r);
        eM += (m * m - eM) * k; eS += (s * s - eS) * k; eL += ((double) l * l - eL) * k; eR += ((double) r * r - eR) * k;
        const int q = (rp + i) & (kRing - 1); ring[2][q] = l; ring[3][q] = r;
    }
    ringPos.store ((rp + n) & (kRing - 1), std::memory_order_release);
    inPk.store (ip); outPk.store (op);
    corrIn.store (eMi + eSi > 1e-12 ? corrOf (cIn) : 1.0f);
    corrOut.store (eM + eS > 1e-12 ? corrOf (cOut) : 1.0f);
    corrLow.store (cLow.ll + cLow.rr > 1e-11 ? corrOf (cLow) : 1.0f);
    widthIn.store ((float) std::sqrt (eSi / (eMi + 1e-20)));
    widthOut.store ((float) std::sqrt (eS / (eM + 1e-20)));
    monoLoss.store (eL + eR > 1e-12 ? (float) (10.0 * std::log10 ((eM + 1e-30) / (0.5 * (eL + eR)))) : 0.0f);
    balanceOut.store (eL + eR > 1e-12 ? (float) ((eR - eL) / (eR + eL)) : 0.0f);
    alignMs.store (engine.alignMsNow()); alignPol.store (engine.polarityNow()); alignConf.store (engine.alignConfidence());
    autoGainDb.store (engine.autoGainDb());
}

// ================================================================================================ AUTO
AutoResult analyseLowEnd (const float* L, const float* R, int n, double fs)
{
    AutoResult res;
    using lumen::mf::Biquad;
    float emax = -200;
    for (int b = 0; b < 24; ++b)
    {
        const double fc = AutoResult::bandCentre (b);
        if (fc > fs * 0.4) { res.energyDb[b] = -200; res.corrBand[b] = 1; continue; }
        Biquad bl[2], br[2];
        for (int k = 0; k < 2; ++k)
        {
            // 1/3-octave band pass (two 2nd-order sections, Q 4.3)
            const double w = 2 * lumen::mf::kPi * fc / fs, al = std::sin (w) / (2 * 4.32), c = std::cos (w), a0 = 1 + al;
            for (auto* q : { &bl[k], &br[k] }) { q->b0 = al / a0; q->b1 = 0; q->b2 = -al / a0; q->a1 = -2 * c / a0; q->a2 = (1 - al) / a0; }
        }
        double ll = 0, rr = 0, lr = 0, mm = 0, ss = 0;
        const int skip = std::min (n / 4, (int) (fs * 0.2));
        for (int i = 0; i < n; ++i)
        {
            const double a = bl[1].process (bl[0].process (L[i])), c = br[1].process (br[0].process (R[i]));
            if (i < skip) continue;
            ll += a * a; rr += c * c; lr += a * c; const double m = 0.5 * (a + c), s = 0.5 * (a - c); mm += m * m; ss += s * s;
        }
        const int cnt = std::max (1, n - skip);
        res.energyDb[b] = (float) (10 * std::log10 (0.5 * (ll + rr) / cnt + 1e-20));
        res.corrBand[b] = (float) (lr / std::sqrt (ll * rr + 1e-30));
        res.sideRatioDb[b] = (float) (10 * std::log10 ((ss + 1e-20) / (mm + 1e-20)));
        if (fc < 700) emax = std::max (emax, res.energyDb[b]);
    }
    // wide low bands: the ones that carry real BASS energy decide how high the mono maker goes;
    // wide content that only starts higher up (pads, reverbs) is protected by staying below it
    auto wide = [&] (int b)
    {
        const float fc = AutoResult::bandCentre (b);
        if (res.energyDb[b] < emax - 40.0f || res.energyDb[b] < -90.0f) return false;
        const float lim = fc < 150.0f ? 0.92f : 0.75f;
        return res.corrBand[b] < lim || res.sideRatioDb[b] > (fc < 150.0f ? -14.0f : -8.0f);
    };
    int worst = -1, lowestWide = -1;
    for (int b = 0; b < 24; ++b)
    {
        const float fc = AutoResult::bandCentre (b);
        if (fc > 400.0f) break;
        if (! wide (b)) continue;
        if (lowestWide < 0) lowestWide = b;
        if (fc <= 210.0f && res.energyDb[b] > emax - 15.0f) worst = b;
    }
    juce::String why;
    if (worst >= 0)
    {
        res.monoFreq = std::clamp (std::round (AutoResult::bandCentre (worst) * 1.26f * 1.1f / 5.0f) * 5.0f, 60.0f, 250.0f);
        why = "the bass itself is wide up to ~" + juce::String ((int) AutoResult::bandCentre (worst)) + " Hz";
    }
    else if (lowestWide >= 0)
    {
        res.monoFreq = std::clamp (std::round (AutoResult::bandCentre (lowestWide) / 1.5f / 5.0f) * 5.0f, 60.0f, 120.0f);
        why = "bass is mono, wide sounds from ~" + juce::String ((int) AutoResult::bandCentre (lowestWide)) + " Hz stay stereo";
    }
    else { res.monoFreq = 100.0f; why = "the low end is already mono"; }
    // inter-channel lag of the low end (spaced mics, layered / detuned bass): cross-correlation within +-3 ms
    {
        Biquad fl[2], fr[2];
        for (int k = 0; k < 2; ++k) { fl[k].lp (fs, 300.0, 0.70710678); fr[k].lp (fs, 300.0, 0.70710678); }
        std::vector<float> a ((size_t) n), c ((size_t) n);
        for (int i = 0; i < n; ++i) { a[(size_t) i] = (float) fl[1].process (fl[0].process (L[i])); c[(size_t) i] = (float) fr[1].process (fr[0].process (R[i])); }
        const int K = (int) (0.003 * fs), skip = std::min (n / 4, (int) (fs * 0.2)) + K;
        double ea = 0, ec = 0; for (int i = skip; i < n - K; ++i) { ea += (double) a[(size_t) i] * a[(size_t) i]; ec += (double) c[(size_t) i] * c[(size_t) i]; }
        const double norm = std::sqrt (ea * ec) + 1e-30;
        double c0 = 0, best = 0; int bestLag = 0;
        for (int lag = -K; lag <= K; ++lag)
        {
            double s = 0; for (int i = skip; i < n - K; ++i) s += (double) a[(size_t) i] * c[(size_t) (i + lag)];
            s /= norm;
            if (lag == 0) c0 = s;
            if (std::abs (s) > std::abs (best)) { best = s; bestLag = lag; }
        }
        res.lagMs = (float) (bestLag * 1000.0 / fs);
        res.lagGain = (float) (std::abs (best) - c0);
        if (ea > 1e-9 && ec > 1e-9 && ((std::abs (best) - c0 > 0.2 && std::abs (res.lagMs) > 0.05f) || c0 < -0.3))
            res.align = true;
    }
    // reasonable bass-width: keep a little of the original space on wide acoustic material
    res.lowWidth = 0.0f;
    juce::String t;
    t << "Mono below " << (int) res.monoFreq << " Hz";
    t << " (" << why << ")";
    if (res.align) t << ",  phase align ON (" << juce::String (std::abs (res.lagMs), 2) << " ms" << (std::abs (res.lagMs) > 0.02f ? (res.lagMs > 0 ? ", R late" : ", L late") : ", polarity") << ")";
    res.text = t;
    return res;
}

void LumenMonoFilterProcessor::startLearn() { learnPos.store (0); learning.store (true); }

bool LumenMonoFilterProcessor::finishLearnIfReady()
{
    if (! learning.load() || learnPos.load() < learnLen) return false;
    learning.store (false);
    autoRes = analyseLowEnd (learnL.data(), learnR.data(), learnLen, fs);
    setParam (IDs::monoOn, 1.0f);
    setParam (IDs::monoFreq, autoRes.monoFreq);
    setParam (IDs::align, autoRes.align ? 1.0f : 0.0f);
    if (autoRes.align) setParam (IDs::alignAuto, 1.0f);
    autoText = autoRes.text;
    return true;
}

// ================================================================================================ state / presets
void LumenMonoFilterProcessor::setParam (const juce::String& id, float v)
{
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id))) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture(); }
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

void LumenMonoFilterProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (ab[abSlot]); abSlot = to;
    if (ab[to].getSize() > 0) setStateInformation (ab[to].getData(), (int) ab[to].getSize());
}
void LumenMonoFilterProcessor::copyAB() { getStateInformation (ab[1 - abSlot]); }

void LumenMonoFilterProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto st = apvts.copyState(); st.setProperty ("program", currentProgram, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, d);
}
void LumenMonoFilterProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto vt = juce::ValueTree::fromXml (*xml);
    currentProgram = (int) vt.getProperty ("program", 0); vt.removeProperty ("program", nullptr);
    apvts.replaceState (vt);
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            auto child = vt.getChildWithProperty ("id", rp->paramID);
            if (child.isValid() && child.hasProperty ("value")) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
        }
    ++programVersion;
}

juce::AudioProcessorEditor* LumenMonoFilterProcessor::createEditor() { return new LumenMonoFilterEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenMonoFilterProcessor(); }
