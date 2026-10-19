#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Parameters.h"

class LumenRiserProcessor : public juce::AudioProcessor
{
public:
    LumenRiserProcessor();
    ~LumenRiserProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Riser"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return juce::isPositiveAndBelow (index, getNumPrograms()) ? factoryPresets()[(size_t) index].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    lumen::riser::Params readParams() const;
    void preview() { engine.preview(); }
    void dropNow() { engine.triggerDrop(); }
    enum RenderKind { RenderRiser, RenderImpact, RenderDown, RenderFull };
    juce::AudioBuffer<float> render (RenderKind k, double sampleRate, double bpm) const;
    juce::File exportWav (RenderKind k);
    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> programVersion { 0 };
    std::atomic<float> outPeak { 0 }, periodPhase { -1 }, hostBpm { 120 };
    std::atomic<float> progress { 0 };
    std::atomic<int> state { 0 }, stutterDiv { -1 }, drops { 0 };
    lumen::riser::RiserEngine engine;

private:
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    double fs = 48000.0;
    int currentProgram = 0;
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenRiserProcessor)
};
