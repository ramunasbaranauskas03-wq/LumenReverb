#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/GateEngine.h"

class LumenGateProcessor : public juce::AudioProcessor
{
public:
    LumenGateProcessor();
    ~LumenGateProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Gate"; }
    bool acceptsMidi() const override { return true; }
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

    // AUTO THRESHOLD: listens 4 s, finds the noise floor and the signal, puts threshold + hysteresis between them
    void startLearn();
    bool isLearning() const { return learning.load(); }
    float learnProgress() const { return (float) lrPos.load() / (float) kLearnFrames; }
    juce::String getLearnResult() const { return learnResult; }
    void finishLearnIfReady();

    // pattern tools (message thread)
    void patternRandom();
    void patternInvert();
    void patternShift (int dir);
    void patternClear (bool allOn);
    void setStep (int i, float v);

    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }
    void setParam (const juce::String& id, float v);

    juce::AudioProcessorValueTreeState apvts;
    static constexpr int kHist = 300, kFrame = 256, kLearnFrames = 750;
    struct Frame { float in = 0, out = 0, key = -120, gr = 0; };
    Frame hist[kHist];
    std::atomic<int> histPos { 0 }, curStep { 0 }, programVersion { 0 };
    std::atomic<bool> scConnected { false }, gateOpen { false };

private:
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    lumen::gate::GateEngine engine;
    double fs = 48000.0, freePpq = 0.0;
    int currentProgram = 0, histN = 0, heldNotes = 0;
    Frame acc;
    float lrBuf[kLearnFrames] {};
    std::atomic<int> lrPos { 0 };
    std::atomic<bool> learning { false };
    juce::String learnResult;
    juce::Random rng;
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenGateProcessor)
};
