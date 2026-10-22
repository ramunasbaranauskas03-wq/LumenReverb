#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/ImagerEngine.h"

// result of the AUTO analysis of the low end
struct AutoResult
{
    float monoFreq = 120; bool align = false; int source = 0; float lowWidth = 0;
    float corrBand[24] {}; float sideRatioDb[24] {}; float energyDb[24] {};   // 1/3 octaves from 25 Hz
    float lagMs = 0, lagGain = 0;
    juce::String text;
    static float bandCentre (int b) { return 25.0f * std::pow (2.0f, (float) b / 3.0f); }
};
AutoResult analyseLowEnd (const float* L, const float* R, int n, double fs);

class LumenMonoFilterProcessor : public juce::AudioProcessor
{
public:
    LumenMonoFilterProcessor();
    ~LumenMonoFilterProcessor() override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen MonoFilter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int i) override { return juce::isPositiveAndBelow (i, getNumPrograms()) ? factoryPresets()[(size_t) i].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float v);
    lumen::mf::Params readParams() const;
    void switchAB (int to); void copyAB(); int getABSlot() const { return abSlot; }

    // AUTO: listen for a few seconds, then set the mono frequency / phase align from the material
    void startLearn();
    bool isLearning() const { return learning.load(); }
    float learnProgress() const { return (float) learnPos.load() / (float) juce::jmax (1, learnLen); }
    bool finishLearnIfReady();                       // message thread
    juce::String lastAutoText() const { return autoText; }
    const AutoResult& lastAuto() const { return autoRes; }

    void rebuildFirNow();                            // tests / offline
    double sampleRateNow() const { return fs; }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> programVersion { 0 };
    // meters
    std::atomic<float> inPk { 0 }, outPk { 0 }, corrIn { 1 }, corrOut { 1 }, corrLow { 1 }, widthIn { 0 }, widthOut { 0 }, monoLoss { 0 }, balanceOut { 0 };
    std::atomic<float> alignMs { 0 }, alignPol { 1 }, alignConf { 0 }, autoGainDb { 0 };
    // ring buffers for the scope / spectrum (input and output, L/R)
    static constexpr int kRing = 8192;
    float ring[4][kRing] {};
    std::atomic<int> ringPos { 0 };

private:
    class FirWorker;
    struct Corr { double lr = 0, ll = 1e-12, rr = 1e-12; };
    static void accum (Corr& c, double a, double b, double k) { c.lr += (a * b - c.lr) * k; c.ll += (a * a - c.ll) * k; c.rr += (b * b - c.rr) * k; }
    static float corrOf (const Corr& c) { return (float) (c.lr / std::sqrt (c.ll * c.rr + 1e-30)); }
    void requestFir (const lumen::mf::Params& p);

    double fs = 48000; int maxBlock = 512;
    lumen::mf::ImagerEngine engine;
    std::unique_ptr<FirWorker> worker;
    juce::SpinLock firLock; lumen::mf::Params firWant; std::atomic<int> firWantVersion { 0 }; lumen::mf::Params firLast; bool firLastValid = false;
    Corr cIn, cOut, cLow; double eM = 0, eS = 0, eMi = 0, eSi = 0, eL = 0, eR = 0;
    lumen::mf::LR4 lowMeter[2];
    std::atomic<bool> learning { false }; std::atomic<int> learnPos { 0 }; int learnLen = 1;
    std::vector<float> learnL, learnR;
    AutoResult autoRes; juce::String autoText;
    juce::MemoryBlock ab[2]; int abSlot = 0, currentProgram = 0;
    friend class FirWorker;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenMonoFilterProcessor)
};
