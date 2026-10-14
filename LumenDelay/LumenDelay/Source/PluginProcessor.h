#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/DelayEngine.h"
#include "dsp/SmartAnalyzer.h"

class LumenDelayProcessor : public juce::AudioProcessor
{
public:
    LumenDelayProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    lumen::SmartAnalyzer analyzer;

    // Read by the editor
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<float> effTimeL { 0.375f }, effTimeR { 0.375f }, effFeedback { 0.4f };
    std::atomic<float> effLowCut { 150.0f }, effHighCut { 7000.0f }, effDuck { 0.0f };
    std::atomic<int>   effMode { 1 };
    std::atomic<float> outLevel { 0.0f }, scLevel { 0.0f }, duckReduction { 0.0f };
    std::atomic<bool>  scConnected { false };

private:
    std::atomic<float>* p (const char* id) { return apvts.getRawParameterValue (id); }

    std::atomic<float> *pSync, *pTime, *pDiv, *pOffset, *pMode, *pFb, *pDiff, *pWidth, *pLow, *pHigh,
                       *pDrive, *pWow, *pModR, *pDuck, *pAdapt, *pMix, *pOut, *pFreeze, *pDuckSrc;

    lumen::DelayEngine engine;
    juce::SmoothedValue<float> drySm, wetSm, outSm;
    float duckEnv = 0.0f, duckGain = 1.0f, duckAtk = 0.0f, duckRel = 0.0f, duckSmooth = 0.0f;
    float smTrans = 0.0f, smBright = 0.0f, smLow = 0.0f;
    double sr = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenDelayProcessor)
};
