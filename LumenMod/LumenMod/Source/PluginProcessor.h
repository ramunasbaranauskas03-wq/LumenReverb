#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/ModEngine.h"

class LumenModProcessor : public juce::AudioProcessor
{
public:
    LumenModProcessor();
    ~LumenModProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Mod"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return juce::isPositiveAndBelow (index, getNumPrograms()) ? factoryPresets()[(size_t) index].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }
    void setChoice (const juce::String& id, int index);

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inPk { 0 }, outPk { 0 }, corr { 1 }, rateNow { 1 };
    std::atomic<float> voicePos[lumen::mod::kMaxVoices] {}, notch[2] {};
    std::atomic<int> voiceCount { 0 }, programVersion { 0 };

private:
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    lumen::mod::ModEngine engine;
    double fs = 48000.0;
    float cLR = 0, cLL = 1e-9f, cRR = 1e-9f;
    int currentProgram = 0;
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenModProcessor)
};
