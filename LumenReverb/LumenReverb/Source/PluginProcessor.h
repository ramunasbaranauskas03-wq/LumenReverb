#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/Reverb2.h"
#include "dsp/SmartAnalyzer.h"

class LumenAudioProcessor : public juce::AudioProcessor
{
public:
    LumenAudioProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Reverb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int i) override { return juce::isPositiveAndBelow (i, getNumPrograms()) ? factoryPresets()[(size_t) i].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float v);
    void switchAB (int to); void copyAB(); int getABSlot() const { return abSlot; }

    // Smart Assist
    void applySuggestion (const lumen::SmartAssist::Suggestion&);
    void undoLearn();
    bool canUndo() const { return undoState.getSize() > 0; }

    juce::AudioProcessorValueTreeState apvts;
    lumen::SmartAnalyzer analyzer;
    std::atomic<double> hostBpm { 0 };
    std::atomic<int> programVersion { 0 };
    // meters
    std::atomic<float> inLevel { 0 }, outLevel { 0 }, wetLevel { 0 }, duckDb { 0 }, effDecay { 2.5f }, effPre { 20 };
    std::atomic<float> clarityDb[4] {};
    static constexpr int kHist = 512;                 // wet level history (UI decay display), 1 value / 10 ms
    float hist[kHist] {}; std::atomic<int> histPos { 0 };

private:
    lumen::rv2::Params readParams (double bpm, float& mixOut);
    double fs = 48000; int maxBlock = 512;
    lumen::rv2::Reverb engine;
    juce::AudioBuffer<float> wet, dry;
    // ducking + clarity
    float duckEnv = 0, duckG = 1;
    lumen::rv2::BandTree wetTree[2], dryTree;
    float envDry[4] {}, envWet[4] {}, bandG[4] { 1, 1, 1, 1 };
    float drySm = 1, wetSm = 0.3f, outSm = 1;
    float smTrans = 0, smBright = 0, smLow = 0;
    double histAcc = 0; int histCount = 0;
    juce::MemoryBlock ab[2], undoState; int abSlot = 0, currentProgram = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenAudioProcessor)
};
