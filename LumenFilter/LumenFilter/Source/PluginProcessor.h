#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/FilterEngine.h"

class LumenFilterProcessor : public juce::AudioProcessor
{
public:
    LumenFilterProcessor();
    ~LumenFilterProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Filter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return juce::isPositiveAndBelow (index, getNumPrograms()) ? factoryPresets()[(size_t) index].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> cutL { 1000 }, cutR { 1000 }, lfoVal { 0 }, envVal { 0 }, riseHp { 20 }, riseLp { 20000 }, inPeak { 0 }, outPeak { 0 };
    std::atomic<int> programVersion { 0 };

private:
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    lumen::flt::FilterEngine engine;
    std::unique_ptr<juce::dsp::Oversampling<float>> os[3];
    int curOs = -1, currentProgram = 0;
    double fs = 48000.0;
    juce::AudioBuffer<float> envBuf, envOs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenFilterProcessor)
};
