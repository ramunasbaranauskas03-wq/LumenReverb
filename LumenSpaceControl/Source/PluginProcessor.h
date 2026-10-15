#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/SpaceEngine.h"

class LumenSpaceControlProcessor : public juce::AudioProcessor, private juce::Thread
{
public:
    LumenSpaceControlProcessor();
    ~LumenSpaceControlProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Space Control"; }
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

    // Smart Assist: listens for a few seconds, measures each band's width, then sets the bands for a goal
    enum Goal { Natural, Wide, ClubSafe, Huge };
    void startAssist (Goal g);
    bool isAssisting() const { return assisting.load(); }
    float assistProgress() const { return (float) assistPos.load() / (float) juce::jmax (1, assistLen); }
    juce::String getAssistResult() const { return assistResult; }
    void finishAssistIfReady();          // message thread

    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }

    lumen::sc::Params readParams() const;
    const lumen::sc::SpaceEngine& getEngine() const { return engine; }
    bool isFirReady() const { return firReady.load(); }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inPeak[2] {}, outPeak[2] {};
    std::atomic<int> programVersion { 0 };

private:
    void run() override;                 // linear-phase FIR designer
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    static juce::uint64 hashParams (const lumen::sc::Params& p, double fs);

    lumen::sc::SpaceEngine engine;
    juce::CriticalSection engineLock;
    std::atomic<double> curFs { 0.0 };
    std::atomic<juce::uint64> loadedHash { 0 };
    std::atomic<bool> firReady { false };
    double fs = 48000.0;
    int currentProgram = 0;

    std::vector<float> asL, asR;
    std::atomic<int> assistPos { 0 };
    int assistLen = 1;
    std::atomic<bool> assisting { false };
    Goal assistGoal = Natural;
    juce::String assistResult;
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenSpaceControlProcessor)
};
