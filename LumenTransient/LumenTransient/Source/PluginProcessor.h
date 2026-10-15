#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/TransientEngine.h"

class LumenTransientProcessor : public juce::AudioProcessor
{
public:
    LumenTransientProcessor();
    ~LumenTransientProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Transient"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return juce::isPositiveAndBelow (index, getNumPrograms()) ? factoryPresets()[(size_t) index].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Smart LEARN: listens for a few seconds, then sets every band for the chosen goal
    enum Goal { Punch, Tighter, PunchTight, Softer, Clarity };
    void startLearn (Goal g);
    bool isLearning() const { return learning.load(); }
    float learnProgress() const { return (float) learnPos.load() / (float) juce::jmax (1, learnLen); }
    juce::String getLearnResult() const { return learnResult; }
    void finishLearnIfReady();        // message thread (editor timer)

    // A/B compare
    void switchAB (int to);
    void copyAB();                     // copy the active slot to the other one
    int getABSlot() const { return abSlot; }

    juce::AudioProcessorValueTreeState apvts;

    // display history: one frame per kFrame input samples (audio thread writes, UI reads)
    static constexpr int kHist = 256, kFrame = 192;
    struct Frame { float in[4] {}, out[4] {}, g[4] {}; };
    Frame hist[kHist];
    std::atomic<int> histPos { 0 };
    std::atomic<float> inPeak { 0 }, outPeak { 0 }, agDb { 0 };
    std::atomic<int> programVersion { 0 };
    std::atomic<bool> scConnected { false };

private:
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void updateLatency();
    lumen::tr::TransientEngine engine;
    std::unique_ptr<juce::dsp::Oversampling<float>> os[3];
    juce::AudioBuffer<float> scBuf, scOs;
    double fs = 48000.0;
    int curOs = 0, laBase = 0;
    int currentProgram = 0, histN = 0;
    float frIn[4] {}, frOut[4] {}, frG[4] {};

    std::vector<float> learnBuf;
    std::atomic<int> learnPos { 0 };
    int learnLen = 1;
    std::atomic<bool> learning { false };
    Goal learnGoal = Punch;
    juce::String learnResult;
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenTransientProcessor)
};
