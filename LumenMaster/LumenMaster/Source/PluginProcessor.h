#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/Loudness.h"

class LumenMasterProcessor : public juce::AudioProcessor
{
public:
    LumenMasterProcessor();
    ~LumenMasterProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Master"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    using Getter = std::function<float (const juce::String&)>;
    static lumen::master::Settings readSettings (const Getter& g);
    lumen::master::Settings currentSettings() const;
    void setParam (const juce::String& id, float v);
    float param (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    // loudness destination
    float targetLufs() const;
    float targetCeiling() const;

    // ---- Master Assistant
    enum AssistState { Idle = 0, Listening, Analysing, Done, Failed };
    void startAssistant();
    void cancelAssistant();
    int assistState() const { return aState.load(); }
    float assistProgress() const;
    juce::String assistMessage() const { const juce::ScopedLock l (msgLock); return aMsg; }
    bool loadReference (const juce::File& f, juce::String& msg);
    juce::String referenceName() const { const juce::ScopedLock l (msgLock); return refName; }
    // analysis curves for the display (61 points on the target grid, dB)
    bool getAnalysedCurve (lumen::master::Curve& c) const { const juce::ScopedLock l (msgLock); c = analysed; return hasAnalysed; }

    // ---- display data
    int copyScope (bool output, float* dst, int n) const;      // last n mono samples
    struct Meters
    {
        std::atomic<float> inM { -100 }, inS { -100 }, inI { -100 }, outM { -100 }, outS { -100 }, outI { -100 }, lra { 0 }, tp { -100 }, tpNow { -100 },
                           inPk { 0 }, outPkL { 0 }, outPkR { 0 }, corr { 1 }, matchDb { 0 };
    } meters;
    void resetMeters() { wantReset.store (true); }
    void getBalanceCurve (float* dst) const { chain.shaper.getCurve (dst); }
    float moduleGr (int m) const;
    float multibandGr (int band) const { return chain.multi.gr[band].load(); }
    double eqResponseDb (double f, int role) const;
    int latencySamples() const { return getLatencySamples(); }
    double sampleRate() const { return fs; }

    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }

    juce::AudioProcessorValueTreeState apvts;

private:
    class Worker;
    friend class Worker;
    void runAssistant();
    void applyLatency (bool lowLat);
    static lumen::master::Curve spectrumCurve (const juce::AudioBuffer<float>& b, int len, double sr);

    double fs = 48000; int maxBlock = 512;
    lumen::master::MasterChain chain;
    lumen::meter::LoudnessMeter inMeter, outMeter;
    lumen::master::TruePeak8x tpL, tpR; float tpMax = 0, tpHold = 0;
    lumen::master::DelayLine dryDelay[2];
    juce::AudioBuffer<float> dryBuf;
    bool curLowLat = false; bool prepared = false;
    float matchG = 1.0f;
    std::atomic<bool> wantReset { false };
    double corrNum = 0, corrDen1 = 0, corrDen2 = 0;

    // scopes
    static constexpr int kScope = 8192;
    std::vector<float> scope[2]; std::atomic<int> scopePos { 0 };

    // assistant
    juce::AudioBuffer<float> capture; std::atomic<int> capLen { 0 }; int capMax = 0;
    std::atomic<int> aState { Idle };
    mutable juce::CriticalSection msgLock; juce::String aMsg, refName;
    lumen::master::Curve analysed {}; bool hasAnalysed = false;
    std::unique_ptr<Worker> worker;
    std::shared_ptr<std::atomic<bool>> alive { std::make_shared<std::atomic<bool>> (true) };

    juce::MemoryBlock abState[2]; int abSlot = 0; int currentProgram = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenMasterProcessor)
};
