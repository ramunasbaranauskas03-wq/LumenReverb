#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lumen::eq2;

// ================================================================================================ FIR worker
class LumenEQProcessor::Worker : public juce::Thread
{
public:
    explicit Worker (LumenEQProcessor& p) : juce::Thread ("Lumen EQ FIR"), proc (p) {}
    void run() override
    {
        int built = -1;
        while (! threadShouldExit())
        {
            const int want = proc.firWant.load();
            for (auto& l : proc.lin) { l.convA().collectGarbage(); l.convB().collectGarbage(); }
            if (want != built)
            {
                std::array<BandParams, kBands> b; float sc; int qual;
                { const juce::SpinLock::ScopedLockType l (proc.firLock); b = proc.firBands; sc = proc.firScale; qual = proc.firQuality; }
                auto& L = proc.lin[qual];
                if (L.convA().canAccept() && L.convB().canAccept()) { L.build (b, sc, false); built = want; }
            }
            wait (10);
        }
    }
private:
    LumenEQProcessor& proc;
};

// ================================================================================================ processor
LumenEQProcessor::LumenEQProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "LumenEQ", createLayout())
{
    worker = std::make_unique<Worker> (*this);
}
LumenEQProcessor::~LumenEQProcessor() { if (worker) worker->stopThread (3000); }

bool LumenEQProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo()) return false;
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    if (l.inputBuses.size() > 1)
    {
        const auto sc = l.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono()) return false;
    }
    return true;
}

BandParams LumenEQProcessor::band (int b) const
{
    BandParams p;
    p.used = P (IDs::used (b)) > 0.5f; p.on = P (IDs::on (b)) > 0.5f;
    p.type = (int) P (IDs::type (b)); p.slope = (int) P (IDs::slope (b)); p.chan = (int) P (IDs::chan (b));
    p.freq = P (IDs::freq (b)); p.gain = P (IDs::gain (b)); p.q = P (IDs::q (b));
    p.dynRange = P (IDs::dyn (b)); p.thr = P (IDs::thr (b)); p.sc = P (IDs::sc (b)) > 0.5f;
    return p;
}
std::array<BandParams, kBands> LumenEQProcessor::bands() const { std::array<BandParams, kBands> a; for (int b = 0; b < kBands; ++b) a[(size_t) b] = band (b); return a; }

void LumenEQProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (worker) worker->stopThread (3000);
    fs = sampleRate; maxBlock = juce::jlimit (32, 16384, samplesPerBlock);
    iir.prepare (fs);
    const auto b = bands(); const float sc = P (IDs::scale);
    for (int q = 0; q < 3; ++q) { lin[q].prepare (fs, q); lin[q].build (b, sc, true); }
    lastFirBands = b; lastFirScale = sc; lastQuality = (int) P (IDs::quality); lastMode = -1;
    { const juce::SpinLock::ScopedLockType l (firLock); firBands = b; firScale = sc; firQuality = lastQuality; }
    scBuf.setSize (2, maxBlock);
    for (auto& c : cap) c.assign ((size_t) (fs * 8), 0.0f);
    capPos = 0; capFilled.store (0);
    outSm = juce::Decibels::decibelsToGain (P (IDs::out)); agSm = agTarget = 0; agCounter = 0;
    soloLast = -2;
    setLatencySamples ((int) P (IDs::mode) == 1 ? lin[lastQuality].latency() : 0);
    worker->startThread (juce::Thread::Priority::low);
}

static bool sameBands (const std::array<BandParams, kBands>& a, const std::array<BandParams, kBands>& b)
{
    for (int i = 0; i < kBands; ++i)
    {
        const auto& x = a[(size_t) i]; const auto& y = b[(size_t) i];
        if (x.used != y.used || x.on != y.on || x.type != y.type || x.slope != y.slope || x.chan != y.chan) return false;
        if (! juce::exactlyEqual (x.freq, y.freq) || ! juce::exactlyEqual (x.gain, y.gain) || ! juce::exactlyEqual (x.q, y.q)) return false;
        if (x.isDynamic() != y.isDynamic()) return false;
    }
    return true;
}

bool LumenEQProcessor::requestFir (const std::array<BandParams, kBands>& b, float scale, int quality)
{
    const juce::SpinLock::ScopedTryLockType l (firLock);
    if (! l.isLocked()) return false;                     // worker is copying: try again next block
    firBands = b; firScale = scale; firQuality = quality;
    ++firWant;
    if (worker) worker->notify();
    return true;
}

void LumenEQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;
    if (getMainBusNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);

    // side-chain
    const float* scL = nullptr; const float* scR = nullptr;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled() && getBus (true, 1)->getNumberOfChannels() > 0)
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0) { scL = sc.getReadPointer (0); scR = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : scL; }
    }
    scActive.store (scL != nullptr && juce::FloatVectorOperations::findMaximum (scL, n) - juce::FloatVectorOperations::findMinimum (scL, n) > 1e-6f);

    const auto b = bands();
    const int mode = (int) P (IDs::mode), quality = juce::jlimit (0, 2, (int) P (IDs::quality));
    const float scale = P (IDs::scale);
    Globals g; g.scale = scale; g.attackMs = P (IDs::attack); g.releaseMs = P (IDs::release);

    // linear-phase filters follow the parameters (built off the audio thread)
    if (! sameBands (b, lastFirBands) || ! juce::exactlyEqual (scale, lastFirScale) || quality != lastQuality)
        if (requestFir (b, scale, quality)) { lastFirBands = b; lastFirScale = scale; lastQuality = quality; }
    if (mode != lastMode)
    {
        lastMode = mode;
        setLatencySamples (mode == 1 ? lin[quality].latency() : 0);
        iir.reset();
    }
    if (mode == 1 && getLatencySamples() != lin[quality].latency()) setLatencySamples (lin[quality].latency());

    // meters + rings (pre)
    float ip = 0;
    const int rp = ringPos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        ip = std::max (ip, std::max (std::abs (L[i]), std::abs (R[i])));
        const float m = 0.5f * (L[i] + R[i]), s = scL ? 0.5f * (scL[i] + scR[i]) : 0.0f;
        const int q = (rp + i) & (kRing - 1); ring[0][q] = m; ring[2][q] = s;
        cap[0][(size_t) capPos] = m; cap[1][(size_t) capPos] = s; if (++capPos >= (int) cap[0].size()) capPos = 0;
    }
    capFilled.store (std::min ((int) cap[0].size(), capFilled.load() + n));
    inPk.store (ip);

    if (mode == 1) lin[quality].process (L, R, n);
    iir.setBands (b, g, mode == 1);
    iir.process (L, R, scL, scR, n);
    for (int k = 0; k < kBands; ++k) dynGain[k].store (iir.dynamicGain (k));

    // band solo: listen to the region the band works on
    const int sb = soloBand.load();
    if (sb >= 0 && sb < kBands && b[(size_t) sb].used)
    {
        const auto& bp = b[(size_t) sb];
        if (sb != soloLast || ! juce::exactlyEqual (bp.freq, soloF) || ! juce::exactlyEqual (bp.q, soloQ) || bp.type != soloT)
        {
            soloLast = sb; soloF = bp.freq; soloQ = bp.q; soloT = bp.type;
            Coefs c;
            const double wc = 2 * kPi * bp.freq / fs;
            if (bp.type == LowShelf || bp.type == HighCut) c = matched ({ 1, 0, 0, 1, 1.4142, 1, 2 }, wc);
            else if (bp.type == HighShelf || bp.type == LowCut) c = matched ({ 0, 0, 1, 1, 1.4142, 1, 2 }, wc);
            else { const double q = std::max (0.5, (double) bp.q); c = matched ({ 0, 1 / q, 0, 1, 1 / q, 1, 2 }, wc); }
            for (auto& ch : solo) for (auto& s : ch) { s.c = c; s.reset(); }
        }
        for (int i = 0; i < n; ++i)
        {
            L[i] = (float) solo[0][1].process (solo[0][0].process (L[i]));
            R[i] = (float) solo[1][1].process (solo[1][0].process (R[i]));
        }
    }
    else soloLast = -2;

    // output gain + auto gain
    if (--agCounter <= 0) { agTarget = P (IDs::autoGain) > 0.5f ? juce::jlimit (-24.0f, 24.0f, autoGainDb (b, scale, fs)) : 0.0f; agCounter = (int) (fs * 0.05 / std::max (1, n)) + 1; }
    autoGainNow.store (agSm);
    const float outT = juce::Decibels::decibelsToGain (P (IDs::out));
    const float c = 1.0f - std::exp (-1.0f / (0.03f * (float) fs));
    float op = 0;
    for (int i = 0; i < n; ++i)
    {
        outSm += (outT - outSm) * c; agSm += (agTarget - agSm) * c;
        const float gg = outSm * juce::Decibels::decibelsToGain (agSm);
        L[i] *= gg; R[i] *= gg;
        op = std::max (op, std::max (std::abs (L[i]), std::abs (R[i])));
        ring[1][(rp + i) & (kRing - 1)] = 0.5f * (L[i] + R[i]);
    }
    ringPos.store ((rp + n) & (kRing - 1), std::memory_order_release);
    outPk.store (op);
    (void) main;
}

// ================================================================================================ band management
void LumenEQProcessor::setParam (const juce::String& id, float v)
{
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id))) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture(); }
}
int LumenEQProcessor::addBand (int type, float freq, float gain, float q, int slope, int chan)
{
    for (int b = 0; b < kBands; ++b)
        if (P (IDs::used (b)) < 0.5f)
        {
            setParam (IDs::type (b), (float) type); setParam (IDs::freq (b), freq); setParam (IDs::gain (b), gain); setParam (IDs::q (b), q);
            setParam (IDs::slope (b), (float) slope); setParam (IDs::chan (b), (float) chan); setParam (IDs::on (b), 1);
            setParam (IDs::dyn (b), 0); setParam (IDs::thr (b), -30); setParam (IDs::sc (b), 0);
            setParam (IDs::used (b), 1);
            return b;
        }
    return -1;
}
void LumenEQProcessor::removeBand (int b) { if (juce::isPositiveAndBelow (b, kBands)) { setParam (IDs::used (b), 0); if (soloBand.load() == b) soloBand.store (-1); } }
void LumenEQProcessor::clearBands() { for (int b = 0; b < kBands; ++b) if (P (IDs::used (b)) > 0.5f) setParam (IDs::used (b), 0); soloBand.store (-1); }

void LumenEQProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    const auto& pr = factoryPresets()[(size_t) index];
    for (int b = 0; b < kBands; ++b)
    {
        if (b < (int) pr.bands.size())
        {
            const auto& d = pr.bands[(size_t) b];
            setParam (IDs::type (b), (float) d.type); setParam (IDs::freq (b), d.freq); setParam (IDs::gain (b), d.gain); setParam (IDs::q (b), d.q);
            setParam (IDs::slope (b), (float) d.slope); setParam (IDs::chan (b), (float) d.chan); setParam (IDs::dyn (b), d.dyn); setParam (IDs::thr (b), d.thr);
            setParam (IDs::sc (b), d.sc ? 1.0f : 0.0f); setParam (IDs::on (b), 1); setParam (IDs::used (b), 1);
        }
        else setParam (IDs::used (b), 0);
    }
    setParam (IDs::mode, (float) pr.mode); setParam (IDs::scale, 1); setParam (IDs::out, 0);
    soloBand.store (-1);
    ++programVersion;
}

// ================================================================================================ EQ match / unmask
std::vector<float> LumenEQProcessor::captureCopy (int which) const
{
    const int filled = std::min (capFilled.load(), (int) cap[0].size());
    std::vector<float> out ((size_t) filled);
    int src = capPos - filled; if (src < 0) src += (int) cap[0].size();
    for (int i = 0; i < filled; ++i) { out[(size_t) i] = cap[which][(size_t) src]; if (++src >= (int) cap[0].size()) src = 0; }
    return out;
}

static std::vector<double> matchBandFreqs() { std::vector<double> f; for (double x = 25; x <= 18000; x *= std::pow (2.0, 1.0 / 6.0)) f.push_back (x); return f; }

std::vector<double> LumenEQProcessor::averageSpectrum (const std::vector<float>& x) const
{
    constexpr int order = 13, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> win (N), buf (2 * N); std::vector<double> pw (N / 2 + 1, 0.0);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * (float) kPi * (float) i / (float) N);
    int frames = 0;
    for (size_t st = 0; st + N <= x.size(); st += N / 2, ++frames)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = x[st + (size_t) i] * win[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int i = 0; i <= N / 2; ++i) pw[(size_t) i] += (double) buf[(size_t) i] * buf[(size_t) i];
    }
    std::vector<double> out;
    if (frames == 0) return out;
    for (double f : matchBandFreqs())
    {
        const double lo = f / std::pow (2.0, 1.0 / 12.0), hi = f * std::pow (2.0, 1.0 / 12.0);
        double e = 0; int c = 0;
        for (int i = std::max (1, (int) std::floor (lo * N / fs)); i <= std::min (N / 2, (int) std::ceil (hi * N / fs)); ++i) { e += pw[(size_t) i]; ++c; }
        out.push_back (10.0 * std::log10 (e / std::max (1, c) / frames + 1e-20));
    }
    return out;
}

bool LumenEQProcessor::loadMatchReference (const juce::File& f, juce::String& err)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr) { err = "Not an audio file"; return false; }
    const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 180));
    juce::AudioBuffer<float> b ((int) std::max (1u, rd->numChannels), len);
    rd->read (&b, 0, len, 0, true, true);
    std::vector<float> mono ((size_t) len);
    for (int i = 0; i < len; ++i) { float s = 0; for (int c = 0; c < b.getNumChannels(); ++c) s += b.getSample (c, i); mono[(size_t) i] = s / (float) b.getNumChannels(); }
    // analyse at the file's own rate: the band edges are in Hz, so a small resample mismatch does not matter
    const double keep = fs; fs = rd->sampleRate; refSpectrum = averageSpectrum (mono); fs = keep;
    if (refSpectrum.empty()) { err = "File too short"; return false; }
    return true;
}

juce::String LumenEQProcessor::eqMatch (bool useSidechain, float amount, int maxBands)
{
    const auto cur = averageSpectrum (captureCopy (0));
    std::vector<double> ref = useSidechain ? averageSpectrum (captureCopy (1)) : refSpectrum;
    if (cur.empty()) return "Play your track for a few seconds first";
    if (ref.size() != cur.size()) return useSidechain ? "No side-chain signal captured - route the reference to the side-chain input" : "Load a reference track first";
    const auto fr = matchBandFreqs();
    const size_t nb = fr.size();
    std::vector<double> d (nb);
    double mean = 0; int mc = 0;
    for (size_t k = 0; k < nb; ++k) { d[k] = ref[k] - cur[k]; if (fr[k] > 100 && fr[k] < 8000) { mean += d[k]; ++mc; } }
    mean /= std::max (1, mc);
    std::vector<double> sm (nb);
    for (size_t k = 0; k < nb; ++k) { double s = 0; int c = 0; for (int j = -2; j <= 2; ++j) { const int i = (int) k + j; if (i >= 0 && i < (int) nb) { s += d[(size_t) i] - mean; ++c; } } sm[k] = std::clamp (s / c, -12.0, 12.0) * amount; }
    // greedy fit with real band responses
    std::vector<double> res = sm;
    std::vector<BandParams> made;
    for (int it = 0; it < maxBands; ++it)
    {
        size_t best = 0; double bv = 0;
        for (size_t k = 0; k < nb; ++k) if (fr[k] > 30 && fr[k] < 16000 && std::abs (res[k]) > bv) { bv = std::abs (res[k]); best = k; }
        if (bv < 0.7) break;
        const double g = res[best];
        size_t lo = best, hi = best;
        while (lo > 0 && std::abs (res[lo - 1]) > std::abs (g) * 0.5 && (res[lo - 1] > 0) == (g > 0)) --lo;
        while (hi + 1 < nb && std::abs (res[hi + 1]) > std::abs (g) * 0.5 && (res[hi + 1] > 0) == (g > 0)) ++hi;
        BandParams bp; bp.used = true; bp.on = true; bp.freq = (float) fr[best]; bp.gain = (float) g;
        const double bwOct = std::max (1.0 / 3.0, std::log2 (fr[hi] / fr[lo]) + 1.0 / 6.0);
        bp.q = (float) std::clamp (1.0 / (2.0 * std::sinh (std::log (2.0) / 2.0 * bwOct)), 0.3, 6.0);
        bp.type = Bell;
        if (fr[best] < 70 && lo == 0) { bp.type = LowShelf; bp.freq = (float) fr[std::min (nb - 1, hi)]; bp.q = 0.7f; bp.slope = 1; }
        if (fr[best] > 9000 && hi >= nb - 2) { bp.type = HighShelf; bp.freq = (float) fr[lo]; bp.q = 0.7f; bp.slope = 1; }
        for (size_t k = 0; k < nb; ++k) res[k] -= bandDb (bp, fs, fr[k]);
        made.push_back (bp);
    }
    clearBands();
    for (auto& bp : made) addBand (bp.type, bp.freq, bp.gain, bp.q, bp.slope, 0);
    double rmsBefore = 0, rmsAfter = 0; for (size_t k = 0; k < nb; ++k) { rmsBefore += sm[k] * sm[k]; rmsAfter += res[k] * res[k]; }
    return "EQ Match: " + juce::String ((int) made.size()) + " bands, " + juce::String (std::sqrt (rmsBefore / (double) nb), 1) + " dB -> " + juce::String (std::sqrt (rmsAfter / (double) nb), 1) + " dB difference";
}

juce::String LumenEQProcessor::unmask()
{
    const auto t = captureCopy (0), s = captureCopy (1);
    if (t.size() < (size_t) (fs * 2)) return "Play your track (with the side-chain) for a few seconds first";
    const auto st = averageSpectrum (t), ss = averageSpectrum (s);
    if (ss.empty() || *std::max_element (ss.begin(), ss.end()) < -110) return "No side-chain signal: route the kick (or vocal) to the side-chain input";
    const auto fr = matchBandFreqs();
    const double tMax = *std::max_element (st.begin(), st.end()), sMax = *std::max_element (ss.begin(), ss.end());
    size_t best = 0; double bv = -1e9;
    for (size_t k = 0; k < fr.size(); ++k)
    {
        if (fr[k] < 30 || fr[k] > 8000) continue;
        const double a = st[k] - tMax, b = ss[k] - sMax;              // both relative to their own peak
        if (a < -24 || b < -18) continue;
        const double score = std::min (a, b) + 0.5 * std::max (0.0, b - a);
        if (score > bv) { bv = score; best = k; }
    }
    if (bv < -1e8) return "No real masking found between the track and the side-chain";
    // threshold: loud level of the side-chain in that band
    Section bp[2]; const double q = 1.5;
    for (auto& x : bp) x.c = matched ({ 0, 1 / q, 0, 1, 1 / q, 1, 2 }, 2 * kPi * fr[best] / fs);
    std::vector<float> frames; double e = 0; int c = 0;
    for (float v : s) { const double y = bp[1].process (bp[0].process (v)); e += y * y; if (++c >= (int) (fs * 0.05)) { frames.push_back ((float) (10 * std::log10 (2 * e / c + 1e-12))); e = 0; c = 0; } }
    std::sort (frames.begin(), frames.end());
    const float loud = frames.empty() ? -30.0f : frames[(size_t) (frames.size() * 9 / 10)];
    const int b = addBand (Bell, (float) fr[best], 0.0f, (float) q, 3, 0);
    if (b < 0) return "All 24 bands are in use";
    setParam (IDs::dyn (b), -7.0f); setParam (IDs::thr (b), loud - 8.0f); setParam (IDs::sc (b), 1);
    return "UNMASK: dynamic cut at " + hzText ((float) fr[best]) + " - the track steps back only while the side-chain plays there";
}

// ================================================================================================ state
void LumenEQProcessor::switchAB (int to)
{
    if (to == abSlot || ! juce::isPositiveAndBelow (to, 2)) return;
    getStateInformation (ab[abSlot]); abSlot = to;
    if (ab[to].getSize() > 0) setStateInformation (ab[to].getData(), (int) ab[to].getSize());
}
void LumenEQProcessor::copyAB() { getStateInformation (ab[1 - abSlot]); }

void LumenEQProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto st = apvts.copyState(); st.setProperty ("program", currentProgram, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, d);
}
void LumenEQProcessor::setStateInformation (const void* data, int size)
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

juce::AudioProcessorEditor* LumenEQProcessor::createEditor() { return new LumenEQEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenEQProcessor(); }
