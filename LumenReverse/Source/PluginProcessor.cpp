#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

const juce::StringArray LumenReverseProcessor::lengthNames { "1/16", "1/8", "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars" };
const juce::StringArray LumenReverseProcessor::rateNames   { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2" };

static const double rateBeats[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 2.0 };

// Render-affecting parameter ids (anything else is applied live)
static const char* renderIds[] = { "length", "sync", "lengthMs", "algo", "size", "decay", "predelay", "diffusion",
                                   "damping", "mod", "shimmer", "mode", "rise", "tailFade", "sourceFade",
                                   "blend", "sweep", "lowCut", "highCut", "normalize" };

APVTS::ParameterLayout LumenReverseProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String ((int) v) + " Hz"; };
    auto ms = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (v, v < 10 ? 1 : 0) + " ms"; };
    auto pct = [] (float v, int) { return String ((int) std::round (v * 100.0f)) + " %"; };
    auto sec = [] (float v, int) { return String (v, 2) + " s"; };
    auto db = [] (float v, int) { return String (v, 1) + " dB"; };

    auto skewed = [] (float lo, float hi, float centre) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (centre); return r; };

    // Length / capture / trigger
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "length", 1 }, "Length", lengthNames, 4));
    p.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "sync", 1 }, "Tempo Sync", true));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "lengthMs", 1 }, "Free Length", skewed (100.0f, 16000.0f, 2000.0f), 2000.0f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "capMode", 1 }, "Capture Mode", StringArray { "Manual", "Auto (Threshold)", "Host Bar" }, 1));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "threshold", 1 }, "Threshold", NormalisableRange<float> (-60.0f, -6.0f), -36.0f, AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "capBars", 1 }, "Capture Length", StringArray { "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars" }, 2));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "trigger", 1 }, "Trigger", StringArray { "MIDI", "Host Sync", "MIDI + Host" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "cycle", 1 }, "Sync Cycle", StringArray { "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars" }, 2));
    p.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "pitchTrack", 1 }, "Pitch Track", false));

    // Space
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "algo", 1 }, "Character", StringArray { "Room", "Hall", "Plate", "Cathedral", "Shimmer", "Bloom" }, 1));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "size", 1 }, "Size", NormalisableRange<float> (0.0f, 1.0f), 0.6f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "decay", 1 }, "Decay", skewed (0.2f, 30.0f, 4.0f), 4.0f, AudioParameterFloatAttributes().withStringFromValueFunction (sec)));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "predelay", 1 }, "Pre-Delay", skewed (0.0f, 250.0f, 40.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "diffusion", 1 }, "Diffusion", NormalisableRange<float> (0.0f, 1.0f), 0.75f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "damping", 1 }, "Tone", skewed (800.0f, 20000.0f, 6000.0f), 9000.0f, AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "mod", 1 }, "Modulation", NormalisableRange<float> (0.0f, 1.0f), 0.35f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "shimmer", 1 }, "Shimmer", NormalisableRange<float> (0.0f, 1.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    // Swell
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Swell Mode", StringArray { "Tail", "Pre-Verb" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "rise", 1 }, "Rise", skewed (0.0f, 4.0f, 1.0f), 1.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); })));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "tailFade", 1 }, "Tail Fade", skewed (0.0f, 1000.0f, 80.0f), 5.0f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "sourceFade", 1 }, "Source Fade", skewed (0.0f, 500.0f, 40.0f), 5.0f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "blend", 1 }, "Source Blend", NormalisableRange<float> (0.0f, 1.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "sweep", 1 }, "Filter Sweep", NormalisableRange<float> (0.0f, 1.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    // Motion
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "tremRate", 1 }, "Motion Rate", rateNames, 2));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "tremShape", 1 }, "Motion Shape", StringArray { "Sine", "Triangle", "Square", "Saw Up", "Saw Down", "Gate 75%" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "tremDepth", 1 }, "Motion Depth", NormalisableRange<float> (0.0f, 1.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "tremPhase", 1 }, "Stereo Phase", NormalisableRange<float> (0.0f, 180.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String ((int) v) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")); })));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "tremFollow", 1 }, "Motion Follow", NormalisableRange<float> (0.0f, 1.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    // Output
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "lowCut", 1 }, "Low Cut", skewed (20.0f, 2000.0f, 200.0f), 20.0f, AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "highCut", 1 }, "High Cut", skewed (1000.0f, 20000.0f, 6000.0f), 20000.0f, AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "width", 1 }, "Width", NormalisableRange<float> (0.0f, 2.0f), 1.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "gain", 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "dry", 1 }, "Dry Thru", NormalisableRange<float> (0.0f, 1.0f), 1.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "normalize", 1 }, "Normalize", true));

    return { p.begin(), p.end() };
}

LumenReverseProcessor::LumenReverseProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undo, "LumenReverse", createLayout())
{
    formats.registerBasicFormats();
    for (auto& c : captureDisplay) c.store (0.0f);
    renderThread.startThread (juce::Thread::Priority::low);
    lastSnapshot = snapshotRenderParams();
    startTimerHz (30);
}

LumenReverseProcessor::~LumenReverseProcessor()
{
    stopTimer();
    renderThread.signalThreadShouldExit();
    renderThread.notify();
    renderThread.stopThread (3000);
}

bool LumenReverseProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void LumenReverseProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool rateChanged = std::abs (sampleRate - currentSR) > 1.0;
    currentSR = sampleRate;
    captureMax = (int) (sampleRate * 20.0);
    captureBuf.setSize (2, captureMax, false, true, false);
    wetScratch.setSize (2, juce::jmax (512, samplesPerBlock) * 2, false, true, false);
    if (rateChanged && hasSource()) requestRender();
}

// ============================================================================
//  Audio thread
// ============================================================================

float LumenReverseProcessor::tremGain (double beatPos, float frac, int ch) const
{
    const float depth = paramValue ("tremDepth");
    if (depth <= 0.0001f) return 1.0f;
    const int rateIdx = juce::jlimit (0, 7, (int) paramValue ("tremRate"));
    const int shape = (int) paramValue ("tremShape");
    const float follow = paramValue ("tremFollow");
    double ph = beatPos / rateBeats[rateIdx] + (ch == 1 ? paramValue ("tremPhase") / 360.0 : 0.0);
    ph -= std::floor (ph);
    const float p = (float) ph;
    float lfo = 0.0f; // 0..1, 1 = full level
    switch (shape)
    {
        case 0: lfo = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::twoPi * p); break;
        case 1: lfo = p < 0.5f ? 1.0f - 2.0f * p : 2.0f * p - 1.0f; break;
        case 2: lfo = p < 0.5f ? 1.0f : 0.0f; break;
        case 3: lfo = p; break;
        case 4: lfo = 1.0f - p; break;
        default: lfo = p < 0.75f ? 1.0f : 0.0f; break;
    }
    if (shape == 2 || shape == 5) // de-click hard edges
    {
        const float edge = 0.02f;
        const float on = shape == 2 ? 0.5f : 0.75f;
        if (p < edge) lfo = p / edge;
        else if (p > on - edge && p < on) lfo = (on - p) / edge;
    }
    const float d = depth * (1.0f - follow + follow * juce::jlimit (0.0f, 1.0f, frac));
    return 1.0f - d * (1.0f - lfo);
}

void LumenReverseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (2, buffer.getNumChannels());

    // ---- host info ----
    double bpm = hostBpm.load(), ppq = 0.0, bpb = 4.0;
    bool playing = false, hasPpq = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto p = pos->getPpqPosition()) { ppq = *p; hasPpq = true; }
            if (auto ts = pos->getTimeSignature()) bpb = ts->numerator * 4.0 / juce::jmax (1, ts->denominator);
            playing = pos->getIsPlaying();
        }
    }
    bpm = juce::jlimit (20.0, 400.0, bpm);
    hostBpm.store (bpm); hostBpb.store (bpb);
    const double samplesPerBeat = currentSR * 60.0 / bpm;

    // ---- capture ----
    int cs = captureState.load();
    if (cs == Armed || cs == Recording)
    {
        const int mode = (int) paramValue ("capMode");
        const float thr = juce::Decibels::decibelsToGain (paramValue ("threshold"));
        int len = captureLen.load();
        const int colSamples = juce::jmax (1, captureMax / captureCols);

        for (int i = 0; i < numSamples; ++i)
        {
            const float l = buffer.getSample (0, i);
            const float r = numCh > 1 ? buffer.getSample (1, i) : l;
            const float lvl = juce::jmax (std::abs (l), std::abs (r));

            if (cs == Armed)
            {
                if (mode == 1 && lvl > thr) { cs = Recording; len = 0; silenceCount = 0; }
                else if (mode == 2 && playing && hasPpq)
                {
                    const double pq = ppq + i / samplesPerBeat;
                    if (lastCapturePpq >= 0.0 && std::floor (pq / bpb) > std::floor (lastCapturePpq / bpb))
                    {
                        static const double capBeats[] = { 1.0, 2.0, -1.0, -2.0, -4.0 };
                        const double cb = capBeats[juce::jlimit (0, 4, (int) paramValue ("capBars"))];
                        hostCaptureTarget = (int) ((cb > 0 ? cb : -cb * bpb) * samplesPerBeat);
                        cs = Recording; len = 0;
                    }
                    lastCapturePpq = pq;
                }
                if (cs != Recording) continue;
            }

            if (len < captureMax)
            {
                captureBuf.setSample (0, len, l);
                captureBuf.setSample (1, len, r);
                auto& col = captureDisplay[(size_t) juce::jmin (captureCols - 1, len / colSamples)];
                if (lvl > col.load (std::memory_order_relaxed)) col.store (lvl, std::memory_order_relaxed);
                ++len;
            }

            bool finished = len >= captureMax || stopRequest.load();
            if (mode == 1)
            {
                silenceCount = lvl < thr * 0.5f ? silenceCount + 1 : 0;
                if (silenceCount > (int) (currentSR * 0.6)) finished = true;
            }
            if (mode == 2 && hostCaptureTarget > 0 && len >= hostCaptureTarget) finished = true;
            if (finished) { cs = Done; break; }
        }
        if (cs == Armed && mode == 2 && ! playing) lastCapturePpq = -1.0;
        captureLen.store (len);
        if (stopRequest.load() && cs == Armed) cs = Idle;
        stopRequest.store (false);
        captureState.store (cs);
    }

    // ---- grab rendered result ----
    const juce::SpinLock::ScopedTryLockType lock (resultLock);
    const lumen::RenderResult* r = (lock.isLocked() && result != nullptr && result->audio.getNumSamples() > 0) ? result.get() : nullptr;

    const float dry = paramValue ("dry");
    const float outGain = juce::Decibels::decibelsToGain (paramValue ("gain"));
    const float width = paramValue ("width");
    const int trigger = (int) paramValue ("trigger");
    const bool pitchTrack = paramValue ("pitchTrack") > 0.5f;

    // ---- MIDI / preview triggers ----
    std::vector<std::pair<int, double>> noteStarts; // sample offset, rate
    std::vector<float> noteVel;
    if (trigger != 1)
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
            {
                noteStarts.push_back ({ meta.samplePosition, pitchTrack ? std::pow (2.0, (m.getNoteNumber() - 60) / 12.0) : 1.0 });
                noteVel.push_back (0.25f + 0.75f * m.getFloatVelocity());
            }
        }
    if (previewRequest.exchange (false)) { noteStarts.insert (noteStarts.begin(), { 0, 1.0 }); noteVel.insert (noteVel.begin(), 1.0f); voiceIsPreview = true; }
    if (previewStop.exchange (false)) { voiceActive.store (false); voiceIsPreview = false; }

    if (wetScratch.getNumSamples() < numSamples) wetScratch.setSize (2, numSamples, false, false, true);
    float* wL = wetScratch.getWritePointer (0);
    float* wR = wetScratch.getWritePointer (1);
    juce::FloatVectorOperations::clear (wL, numSamples);
    juce::FloatVectorOperations::clear (wR, numSamples);

    float lastFrac = -1.0f;
    if (r != nullptr)
    {
        const int total = r->audio.getNumSamples();
        const int swell = r->swellLen;
        const float* aL = r->audio.getReadPointer (0);
        const float* aR = r->audio.getReadPointer (1);

        // Host sync: swell ends exactly on every N-bar boundary
        const bool hostMode = (trigger == 1 || trigger == 2) && playing && hasPpq;
        static const double cycleBars[] = { 1, 2, 4, 8, 16 };
        const double cycleBeats = cycleBars[juce::jlimit (0, 4, (int) paramValue ("cycle"))] * bpb;
        const double swellBeats = swell / samplesPerBeat;

        size_t nextNote = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            const double beatPos = hasPpq ? ppq + i / samplesPerBeat : freeBeat + i / samplesPerBeat;

            while (nextNote < noteStarts.size() && noteStarts[nextNote].first <= i)
            {
                voicePos = 0.0; voiceRate = noteStarts[nextNote].second;
                voiceGain = noteVel[nextNote]; voiceActive.store (true);
                ++nextNote;
            }

            if (hostMode)
            {
                double ph = std::fmod (beatPos - (cycleBeats - swellBeats), cycleBeats);
                if (ph < 0) ph += cycleBeats;
                const int idx = (int) (ph * samplesPerBeat);
                if (idx >= 0 && idx < total)
                {
                    const float frac = idx < swell ? (float) idx / (float) swell : 1.0f;
                    const float gL = idx < swell ? tremGain (beatPos, frac, 0) : 1.0f;
                    const float gR = idx < swell ? tremGain (beatPos, frac, 1) : 1.0f;
                    wL[i] += aL[idx] * gL; wR[i] += aR[idx] * gR;
                    lastFrac = (float) idx / (float) total;
                }
            }

            if (voiceActive.load (std::memory_order_relaxed))
            {
                const int i0 = (int) voicePos;
                if (i0 + 1 >= total)
                {
                    if (voiceIsPreview && loopPreview.load()) voicePos = 0.0;
                    else { voiceActive.store (false); voiceIsPreview = false; }
                }
                else
                {
                    const float f = (float) (voicePos - i0);
                    const float frac = i0 < swell ? (float) i0 / (float) swell : 1.0f;
                    const float gL = i0 < swell ? tremGain (beatPos, frac, 0) : 1.0f;
                    const float gR = i0 < swell ? tremGain (beatPos, frac, 1) : 1.0f;
                    // 3 ms fade-in on retrigger is implicit in the rendered tail fade
                    wL[i] += (aL[i0] + (aL[i0 + 1] - aL[i0]) * f) * voiceGain * gL;
                    wR[i] += (aR[i0] + (aR[i0 + 1] - aR[i0]) * f) * voiceGain * gR;
                    lastFrac = (float) (voicePos / total);
                    voicePos += voiceRate;
                }
            }
        }
    }
    else
    {
        voiceActive.store (false);
    }

    if (! hasPpq) freeBeat += numSamples / samplesPerBeat;
    playheadFrac.store (lastFrac);

    // ---- width + output ----
    for (int i = 0; i < numSamples; ++i)
    {
        const float m = 0.5f * (wL[i] + wR[i]);
        const float s = 0.5f * (wL[i] - wR[i]) * width;
        wL[i] = (m + s) * outGain;
        wR[i] = (m - s) * outGain;
    }

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* o = buffer.getWritePointer (ch);
        const float* w = ch == 0 ? wL : wR;
        for (int i = 0; i < numSamples; ++i) o[i] = o[i] * dry + (numCh == 1 ? 0.5f * (wL[i] + wR[i]) : w[i]);
    }
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, numSamples);
}

// ============================================================================
//  Message thread
// ============================================================================

void LumenReverseProcessor::armCapture()
{
    for (auto& c : captureDisplay) c.store (0.0f);
    captureLen.store (0);
    stopRequest.store (false);
    silenceCount = 0; hostCaptureTarget = 0; lastCapturePpq = -1.0;
    const int mode = (int) paramValue ("capMode");
    captureState.store (mode == 0 ? Recording : Armed);
}

void LumenReverseProcessor::stopCapture()
{
    if (captureState.load() == Recording || captureState.load() == Armed) stopRequest.store (true);
}

void LumenReverseProcessor::cancelCapture()
{
    captureState.store (Idle);
    captureLen.store (0);
}

float LumenReverseProcessor::getCaptureProgress() const
{
    return captureMax > 0 ? (float) captureLen.load() / (float) captureMax : 0.0f;
}

void LumenReverseProcessor::finishCapture()
{
    const int len = captureLen.load();
    // trim leading / trailing silence (-60 dB)
    const float floorG = 0.001f;
    int a = 0, b = len;
    while (a < len && std::abs (captureBuf.getSample (0, a)) < floorG && std::abs (captureBuf.getSample (1, a)) < floorG) ++a;
    while (b > a && std::abs (captureBuf.getSample (0, b - 1)) < floorG && std::abs (captureBuf.getSample (1, b - 1)) < floorG) --b;
    a = juce::jmax (0, a - (int) (currentSR * 0.002));
    const int n = b - a;
    captureState.store (Idle);
    if (n < (int) (currentSR * 0.02)) return; // nothing useful captured

    {
        const juce::ScopedLock sl (sourceLock);
        source.setSize (2, n);
        source.copyFrom (0, 0, captureBuf, 0, a, n);
        source.copyFrom (1, 0, captureBuf, 1, a, n);
        sourceRate = currentSR;
    }
    sourceAvailable.store (true);
    requestRender();
}

bool LumenReverseProcessor::loadAudioFile (const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr) return false;
    const int n = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 20.0));
    juce::AudioBuffer<float> tmp (std::max (1, (int) reader->numChannels), n);
    reader->read (&tmp, 0, n, 0, true, true);

    juce::AudioBuffer<float> st (2, n);
    st.copyFrom (0, 0, tmp, 0, 0, n);
    st.copyFrom (1, 0, tmp, juce::jmin (1, tmp.getNumChannels() - 1), 0, n);
    {
        const juce::ScopedLock sl (sourceLock);
        source = std::move (st);
        sourceRate = reader->sampleRate;
    }
    sourceAvailable.store (true);
    requestRender();
    return true;
}

void LumenReverseProcessor::clearSource()
{
    {
        const juce::ScopedLock sl (sourceLock);
        source.setSize (2, 0);
    }
    sourceAvailable.store (false);
    std::unique_ptr<lumen::RenderResult> old;
    {
        const juce::SpinLock::ScopedLockType sl (resultLock);
        std::swap (old, result);
    }
    ++renderVersion;
}

void LumenReverseProcessor::triggerPreview() { previewRequest.store (true); }
void LumenReverseProcessor::stopPreview()    { previewStop.store (true); }

int LumenReverseProcessor::swellSamplesFor (double sr, double bpm, double bpb) const
{
    if (paramValue ("sync") > 0.5f)
    {
        const double beats[] = { 0.25, 0.5, 1.0, 2.0, bpb, bpb * 2.0, bpb * 4.0 };
        const int idx = juce::jlimit (0, 6, (int) paramValue ("length"));
        return (int) std::round (beats[idx] * 60.0 / bpm * sr);
    }
    return (int) (paramValue ("lengthMs") * 0.001 * sr);
}

juce::String LumenReverseProcessor::getLengthDescription() const
{
    const int n = swellSamplesFor (currentSR, hostBpm.load(), hostBpb.load());
    const double ms = n * 1000.0 / currentSR;
    return ms >= 1000.0 ? juce::String (ms / 1000.0, 2) + " s" : juce::String ((int) ms) + " ms";
}

lumen::RenderSettings LumenReverseProcessor::makeSettings() const
{
    lumen::RenderSettings s;
    s.sampleRate = currentSR;
    s.swellSamples = juce::jmin ((int) (currentSR * 32.0), swellSamplesFor (currentSR, hostBpm.load(), hostBpb.load()));
    s.mode = (int) paramValue ("mode");
    s.reverb.algo = (int) paramValue ("algo");
    s.reverb.size = paramValue ("size");
    s.reverb.decay = paramValue ("decay");
    s.reverb.predelayMs = paramValue ("predelay");
    s.reverb.diffusion = paramValue ("diffusion");
    s.reverb.dampHz = paramValue ("damping");
    s.reverb.mod = paramValue ("mod");
    s.reverb.shimmer = paramValue ("shimmer");
    s.rise = paramValue ("rise");
    s.tailFadeMs = paramValue ("tailFade");
    s.sourceFadeMs = paramValue ("sourceFade");
    s.sourceBlend = paramValue ("blend");
    s.sweep = paramValue ("sweep");
    s.lowCut = paramValue ("lowCut");
    s.highCut = paramValue ("highCut");
    s.normalize = paramValue ("normalize") > 0.5f;
    return s;
}

std::vector<float> LumenReverseProcessor::snapshotRenderParams() const
{
    std::vector<float> v;
    for (auto* id : renderIds) v.push_back (paramValue (id));
    v.push_back ((float) hostBpm.load());
    v.push_back ((float) hostBpb.load());
    v.push_back ((float) currentSR);
    return v;
}

void LumenReverseProcessor::requestRender()
{
    {
        const juce::ScopedLock sl (settingsLock);
        pendingSettings = makeSettings();
    }
    renderPending.store (true);
    renderThread.notify();
}

void LumenReverseProcessor::RenderThread::run()
{
    while (! threadShouldExit())
    {
        if (! owner.renderPending.exchange (false)) { wait (-1); continue; }
        owner.renderBusy.store (true);

        lumen::RenderSettings s;
        { const juce::ScopedLock sl (owner.settingsLock); s = owner.pendingSettings; }

        juce::AudioBuffer<float> src;
        double srcRate;
        { const juce::ScopedLock sl (owner.sourceLock); src.makeCopyOf (owner.source); srcRate = owner.sourceRate; }

        if (src.getNumSamples() > 0)
        {
            if (std::abs (srcRate - s.sampleRate) > 1.0)
            {
                const double ratio = srcRate / s.sampleRate;
                const int outN = (int) (src.getNumSamples() / ratio);
                juce::AudioBuffer<float> rs (2, outN);
                for (int ch = 0; ch < 2; ++ch)
                {
                    juce::LagrangeInterpolator li;
                    li.process (ratio, src.getReadPointer (ch), rs.getWritePointer (ch), outN);
                }
                src = std::move (rs);
            }

            auto res = lumen::Renderer::render (src, s);
            if (owner.renderPending.load()) { owner.renderBusy.store (false); continue; } // stale, redo
            {
                const juce::SpinLock::ScopedLockType sl (owner.resultLock);
                std::swap (owner.result, res);
                ++owner.renderVersion;
                owner.result->version = owner.renderVersion;
            }
            res.reset(); // old one freed here, off the audio thread
        }
        owner.renderBusy.store (false);
    }
}

void LumenReverseProcessor::timerCallback()
{
    if (captureState.load() == Done) finishCapture();

    auto snap = snapshotRenderParams();
    if (snap != lastSnapshot)
    {
        lastSnapshot = std::move (snap);
        lastChangeTime = juce::Time::getMillisecondCounterHiRes();
        changeDirty = true;
    }
    if (changeDirty && juce::Time::getMillisecondCounterHiRes() - lastChangeTime > 90.0)
    {
        changeDirty = false;
        if (hasSource()) requestRender();
    }
}

bool LumenReverseProcessor::getDisplayData (DisplayData& d, int knownVersion)
{
    const juce::SpinLock::ScopedLockType sl (resultLock);
    if (result == nullptr)
    {
        if (knownVersion == -1 && d.version == -1) return false;
        d = DisplayData(); d.version = -1; return true;
    }
    if (result->version == knownVersion) return false;
    d.peaks = result->peaks;
    d.envelope = result->envelope;
    d.swellLen = result->swellLen;
    d.srcLen = result->srcLen;
    d.version = result->version;
    d.sampleRate = currentSR;
    return true;
}

// ---------------------------------------------------------------------------

void LumenReverseProcessor::applyTremWidth (juce::AudioBuffer<float>& b, int swellLen, double bpm) const
{
    const double spb = currentSR * 60.0 / bpm;
    const float width = paramValue ("width");
    const float g = juce::Decibels::decibelsToGain (paramValue ("gain"));
    // align the motion grid so that the swell's end sits on a beat
    const double beatOffset = -(double) swellLen / spb;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        float l = b.getSample (0, i), r = b.getSample (1, i);
        if (i < swellLen)
        {
            const float frac = (float) i / (float) swellLen;
            const double beat = beatOffset + i / spb;
            l *= tremGain (beat, frac, 0); r *= tremGain (beat, frac, 1);
        }
        const float m = 0.5f * (l + r), s = 0.5f * (l - r) * width;
        b.setSample (0, i, (m + s) * g);
        b.setSample (1, i, (m - s) * g);
    }
}

juce::File LumenReverseProcessor::exportAudio (bool forDrag)
{
    juce::AudioBuffer<float> out;
    int swell = 0;
    {
        const juce::SpinLock::ScopedLockType sl (resultLock);
        if (result == nullptr) return {};
        out.makeCopyOf (result->audio);
        swell = result->swellLen;
    }
    applyTremWidth (out, swell, hostBpm.load());

    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                  .getChildFile ("Lumen Reverse").getChildFile ("Exports");
    dir.createDirectory();
    const auto algo = apvts.getParameter ("algo")->getCurrentValueAsText();
    auto file = dir.getNonexistentChildFile ("LumenReverse_" + algo + "_" + juce::String ((int) std::round (hostBpm.load())) + "bpm_"
                                              + juce::Time::getCurrentTime().formatted ("%H%M%S"), ".wav");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> fos (file.createOutputStream());
    if (fos == nullptr) return {};
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (fos.get(), currentSR, 2, 24, {}, 0));
    if (w == nullptr) return {};
    fos.release();
    w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    w.reset();
    juce::ignoreUnused (forDrag);
    return file;
}

// ============================================================================
//  Presets & A/B
// ============================================================================

namespace
{
    struct PresetDef { const char* name; std::vector<std::pair<const char*, float>> v; };
    const std::vector<PresetDef>& presets()
    {
        static const std::vector<PresetDef> p =
        {
            { "Init", {} },
            { "Vocal Ghost Swell",   { { "algo", 1 }, { "size", 0.7f }, { "decay", 5.5f }, { "damping", 7000 }, { "rise", 1.6f }, { "length", 4 }, { "lowCut", 180 }, { "highCut", 12000 }, { "mod", 0.45f } } },
            { "Snare Suck",          { { "algo", 2 }, { "size", 0.45f }, { "decay", 1.8f }, { "rise", 2.4f }, { "length", 2 }, { "lowCut", 250 }, { "tailFade", 2 } } },
            { "Cathedral Rise",      { { "algo", 3 }, { "size", 0.9f }, { "decay", 12.0f }, { "damping", 5200 }, { "rise", 1.2f }, { "length", 5 }, { "lowCut", 120 }, { "mod", 0.5f } } },
            { "Shimmer Halo",        { { "algo", 4 }, { "size", 0.8f }, { "decay", 9.0f }, { "shimmer", 0.6f }, { "rise", 1.4f }, { "length", 5 }, { "lowCut", 300 }, { "damping", 11000 } } },
            { "Bloom Pad Into Drop", { { "algo", 5 }, { "size", 0.85f }, { "decay", 14.0f }, { "diffusion", 0.95f }, { "rise", 0.8f }, { "length", 6 }, { "sweep", 0.7f }, { "lowCut", 150 } } },
            { "Tight Room Pull",     { { "algo", 0 }, { "size", 0.35f }, { "decay", 0.9f }, { "rise", 2.0f }, { "length", 1 }, { "tailFade", 1 } } },
            { "Plate Whoosh",        { { "algo", 2 }, { "size", 0.7f }, { "decay", 4.0f }, { "rise", 1.8f }, { "sweep", 0.85f }, { "length", 4 }, { "lowCut", 200 } } },
            { "Trance Gate Build",   { { "algo", 1 }, { "size", 0.8f }, { "decay", 8.0f }, { "rise", 1.3f }, { "length", 5 }, { "tremDepth", 1.0f }, { "tremShape", 2 }, { "tremRate", 2 }, { "tremFollow", 0.6f } } },
            { "Tech House Stutter",  { { "algo", 0 }, { "size", 0.5f }, { "decay", 2.2f }, { "rise", 1.5f }, { "length", 4 }, { "tremDepth", 0.9f }, { "tremShape", 5 }, { "tremRate", 4 }, { "lowCut", 300 } } },
            { "Wide Stereo Ripple",  { { "algo", 1 }, { "decay", 6.0f }, { "tremDepth", 0.6f }, { "tremRate", 3 }, { "tremPhase", 180 }, { "width", 1.6f }, { "length", 4 } } },
            { "Ghost Pre-Verb",      { { "mode", 1 }, { "algo", 1 }, { "size", 0.75f }, { "decay", 6.0f }, { "rise", 1.0f }, { "length", 4 }, { "lowCut", 150 }, { "blend", 1.0f } } },
            { "Dark Techno Reverse", { { "algo", 3 }, { "size", 0.8f }, { "decay", 10.0f }, { "damping", 2500 }, { "highCut", 6000 }, { "rise", 1.1f }, { "length", 5 } } },
            { "Melodic Shimmer Lift",{ { "algo", 4 }, { "shimmer", 0.85f }, { "decay", 14.0f }, { "size", 0.9f }, { "rise", 1.0f }, { "sweep", 0.5f }, { "length", 6 }, { "lowCut", 400 } } },
            { "Short Vox Flip",      { { "algo", 2 }, { "size", 0.5f }, { "decay", 2.5f }, { "rise", 1.4f }, { "length", 3 }, { "blend", 1.0f }, { "sourceFade", 8 }, { "lowCut", 200 } } },
            { "Festival Riser Tail", { { "algo", 5 }, { "size", 1.0f }, { "decay", 20.0f }, { "rise", 0.6f }, { "sweep", 1.0f }, { "length", 6 }, { "tremDepth", 0.7f }, { "tremRate", 2 }, { "tremShape", 2 }, { "tremFollow", 1.0f }, { "width", 1.4f } } },
        };
        return p;
    }
}

const juce::StringArray& LumenReverseProcessor::getPresetNames()
{
    static juce::StringArray names = [] { juce::StringArray s; for (auto& p : presets()) s.add (p.name); return s; }();
    return names;
}

int LumenReverseProcessor::getNumPrograms() { return (int) presets().size(); }
void LumenReverseProcessor::setCurrentProgram (int index) { loadPreset (index); }
const juce::String LumenReverseProcessor::getProgramName (int index) { return getPresetNames()[index]; }

void LumenReverseProcessor::loadPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size())) return;
    currentPreset = index;
    undo.beginNewTransaction ("Preset");
    static const juce::StringArray keep { "capMode", "threshold", "capBars", "trigger", "cycle", "pitchTrack", "sync", "dry" };
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! keep.contains (rp->paramID))
                rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& kv : presets()[(size_t) index].v)
        if (auto* rp = apvts.getParameter (kv.first))
            rp->setValueNotifyingHost (rp->convertTo0to1 (kv.second));
}

void LumenReverseProcessor::switchAB (int slot)
{
    if (slot == abSlot) return;
    abState[abSlot] = apvts.copyState();
    abSlot = slot;
    if (abState[slot].isValid()) apvts.replaceState (abState[slot].createCopy());
}

void LumenReverseProcessor::copyAtoB()
{
    abState[abSlot == 0 ? 1 : 0] = apvts.copyState();
}

// ============================================================================
//  State
// ============================================================================

void LumenReverseProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    xml->setAttribute ("preset", currentPreset);

    const juce::ScopedLock sl (sourceLock);
    if (source.getNumSamples() > 0)
    {
        juce::MemoryBlock mb;
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::MemoryOutputStream (mb, false), sourceRate, 2, 24, {}, 0));
            if (w != nullptr) w->writeFromAudioSampleBuffer (source, 0, source.getNumSamples());
        }
        auto* s = xml->createNewChildElement ("SOURCE");
        s->setAttribute ("data", mb.toBase64Encoding());
    }
    copyXmlToBinary (*xml, destData);
}

void LumenReverseProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr) return;
    currentPreset = xml->getIntAttribute ("preset", 0);

    if (auto* s = xml->getChildByName ("SOURCE"))
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (s->getStringAttribute ("data")))
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (new juce::MemoryInputStream (mb, true), true));
            if (r != nullptr)
            {
                juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
                r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
                const juce::ScopedLock sl (sourceLock);
                source = std::move (b);
                sourceRate = r->sampleRate;
                sourceAvailable.store (true);
            }
        }
        xml->removeChildElement (s, true);
    }
    if (xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
    if (hasSource()) requestRender();
}

juce::AudioProcessorEditor* LumenReverseProcessor::createEditor() { return new LumenReverseEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenReverseProcessor(); }
