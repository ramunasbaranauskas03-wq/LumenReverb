#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/ResonanceEngine.h"

class LumenResonanceProcessor : public juce::AudioProcessor
{
public:
    LumenResonanceProcessor();
    ~LumenResonanceProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Resonance"; }
    bool acceptsMidi() const override { return false; }
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

    // SMART LEARN: listens 5 s, finds where the resonances live and how sharp they are, then sets everything for the source
    enum Source { Vocal, Instrument, Drums, Bass, MixMaster };
    void startLearn (Source s);
    bool isLearning() const { return learning.load(); }
    float learnProgress() const { return (float) lPos.load() / (float) juce::jmax (1, lLen); }
    juce::String getLearnResult() const { return learnResult; }
    void finishLearnIfReady();

    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }
    void setParam (const juce::String& id, float v);
    lumen::res::Params readParams() const;
    const lumen::res::ResonanceEngine& getEngine() const { return engine; }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inPeak { 0 }, outPeak { 0 };
    std::atomic<bool> scConnected { false };
    std::atomic<int> programVersion { 0 };

private:
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    lumen::res::ResonanceEngine engine;
    double fs = 48000.0;
    int currentProgram = 0;
    std::vector<float> lBuf;
    std::atomic<int> lPos { 0 };
    int lLen = 1;
    std::atomic<bool> learning { false };
    Source lSource = Vocal;
    juce::String learnResult;
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenResonanceProcessor)
};
