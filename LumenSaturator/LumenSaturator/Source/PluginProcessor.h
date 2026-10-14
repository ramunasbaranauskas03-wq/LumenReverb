#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/SatEngine.h"
#include "dsp/SmartAnalyzer.h"

class LumenSaturatorProcessor : public juce::AudioProcessor
{
public:
    LumenSaturatorProcessor();
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
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    lumen::SmartAnalyzer analyzer;
    std::atomic<float> inLevel { 0.0f }, outLevel { 0.0f };

private:
    void setOversampling (int index);

    std::atomic<float>* p (const juce::String& id) { return apvts.getRawParameterValue (id); }
    std::atomic<float> *pMode[3], *pDrive[3], *pMix[3], *pLevel[3], *pSolo[3], *pXLow, *pXHigh, *pOut, *pAuto, *pOs;

    std::unique_ptr<juce::dsp::Oversampling<float>> os[5];
    lumen::SatEngine engine;
    std::vector<float> monoScratch;
    int osIndex = -1, maxBlock = 512;
    double sr = 48000.0;
    float lastXLow = -1.0f, lastXHigh = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenSaturatorProcessor)
};
