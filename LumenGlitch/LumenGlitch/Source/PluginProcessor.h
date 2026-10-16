#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"

class LumenGlitchProcessor : public juce::AudioProcessor
{
public:
    LumenGlitchProcessor();
    ~LumenGlitchProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Glitch"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return juce::isPositiveAndBelow (index, getNumPrograms()) ? factoryPresets()[(size_t) index].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // patterns: 8 slots x 10 lanes x 32 steps (bit masks)
    int slot() const { return juce::jlimit (0, 7, (int) P (IDs::slot)); }
    uint32_t getPattern (int s, int lane) const { return pat[s][lane].load(); }
    void setStep (int lane, int step, bool on);
    void setLanePattern (int lane, uint32_t bits) { pat[slot()][lane].store (bits); ++programVersion; }
    void clearSlot();
    void copySlotTo (int dst);
    enum Style { BuildUp, DropFill, StutterRoll, Dubstep, Trap, HalfTime, Random };
    void generate (Style s);
    void setParam (const juce::String& id, float v);
    void syncMidiSlot();                       // message thread: applies a slot picked by MIDI

    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }
    const lumen::glitch::GlitchEngine& getEngine() const { return engine; }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> programVersion { 0 }, midiSlot { -1 };
    std::atomic<uint32_t> heldLanes { 0 };
    std::atomic<float> inPeak { 0 }, outPeak { 0 };
    std::atomic<bool> playing { false };

private:
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    lumen::glitch::GlitchEngine engine;
    std::atomic<uint32_t> pat[8][lumen::glitch::kLanes] {};
    double fs = 48000.0, freeBeat = 0.0;
    int currentProgram = 0;
    juce::Random rng;
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenGlitchProcessor)
};
