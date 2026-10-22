#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/OttEngine.h"

class LumenOTTProcessor : public juce::AudioProcessor
{
public:
    LumenOTTProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen OTT"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int i) override { return juce::isPositiveAndBelow (i, getNumPrograms()) ? factoryPresets()[(size_t) i].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float v);
    lumen::ott::Params readParams() const;
    void switchAB (int to); void copyAB(); int getABSlot() const { return abSlot; }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inPk { 0 }, outPk { 0 }, agDb { 0 };
    std::atomic<float> bandLvl[3] {}, bandGain[3] {}, bandOff[3] {};
    std::atomic<int> programVersion { 0 };

private:
    double fs = 48000; int maxBlock = 512, curOs = -1;
    std::unique_ptr<juce::dsp::Oversampling<float>> os[3];
    lumen::ott::OttEngine engine;
    juce::MemoryBlock ab[2]; int abSlot = 0, currentProgram = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenOTTProcessor)
};
