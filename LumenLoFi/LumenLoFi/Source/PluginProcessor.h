#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/LoFiEngine.h"

class LumenLoFiProcessor : public juce::AudioProcessor
{
public:
    LumenLoFiProcessor();
    ~LumenLoFiProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen LoFi"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return juce::isPositiveAndBelow (index, getNumPrograms()) ? factoryPresets()[(size_t) index].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void randomize();                     // DICE: a new tasteful character
    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }
    void setParam (const juce::String& id, float v);
    lumen::lofi::Params readParams() const;
    const lumen::lofi::LoFiEngine& getEngine() const { return engine; }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inPeak { 0 }, outPeak { 0 };
    std::atomic<int> programVersion { 0 };

private:
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    lumen::lofi::LoFiEngine engine;
    double fs = 48000.0;
    int currentProgram = 0;
    juce::Random rng;
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenLoFiProcessor)
};
