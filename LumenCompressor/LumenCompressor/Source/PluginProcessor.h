#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "Parameters.h"
#include "dsp/Comp2.h"
#include "dsp/SmartAnalyzer.h"
#include "dsp/PhaseAlign.h"

class LumenCompressorProcessor : public juce::AudioProcessor
{
public:
    LumenCompressorProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Compressor"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int i) override { return juce::isPositiveAndBelow (i, getNumPrograms()) ? juce::String (factoryPresets()[(size_t) i].name) : juce::String(); }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float v);
    void switchAB (int to); void copyAB(); int getABSlot() const { return abSlot; }
    juce::String applyLearn (const lumen::Features& f);     // message thread
    lumen::comp2::Params readParams() const;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> programVersion { 0 };
    lumen::comp2::Engine engine;
    lumen::SmartAnalyzer analyzer;
    lumen::PhaseAligner phase;
    std::atomic<float> corrBefore { 0 }, corrAfter { 0 }, scLevel { 0 };
    std::atomic<bool> lowEndPresent { false }, scActive { false };

private:
    void applyQuality (int q);
    void updateLatency();
    std::array<std::atomic<float>*, 40> pr {};

    double fs = 48000; int maxBlock = 512, quality = -1, osFactor = 1, osLatency = 0, latencyNow = -1;
    std::unique_ptr<juce::dsp::Oversampling<float>> os[3];
    juce::AudioBuffer<float> work;
    float lastFocus = -1.0f;
    juce::MemoryBlock ab[2]; int abSlot = 0, currentProgram = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenCompressorProcessor)
};
