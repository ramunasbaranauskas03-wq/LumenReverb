#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/PreviewSynth.h"

class LumenChordProcessor : public juce::AudioProcessor
{
public:
    LumenChordProcessor();
    ~LumenChordProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Chord"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return juce::isPositiveAndBelow (index, getNumPrograms()) ? factoryPresets()[(size_t) index].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // progression + step lane (message thread edits, audio thread reads a copy)
    lumen::chord::Slot getSlot (int i) const { const juce::SpinLock::ScopedLockType l (lock); return slots[i]; }
    void setSlot (int i, const lumen::chord::Slot& s) { { const juce::SpinLock::ScopedLockType l (lock); slots[i] = s; } ++programVersion; }
    float getStep (int i) const { return steps[i]; }
    void setStep (int i, float v) { const juce::SpinLock::ScopedLockType l (lock); steps[i] = v; }
    bool getRatchet (int i) const { return (ratchet >> i) & 1u; }
    void toggleRatchet (int i) { const juce::SpinLock::ScopedLockType l (lock); ratchet ^= 1u << i; }
    void parseProgression (const juce::String& s);
    juce::String slotName (int i);             // e.g. "Am9"
    juce::String slotRoman (int i);

    enum Mood { Euphoric, Emotional, Dark, Epic, Chill, Random };
    void generate (Mood m);
    void audition (int slot) { auditionSlot.store (slot); }
    juce::File exportMidi (int bars);          // progression (+ arp) as a .mid clip
    void setParam (const juce::String& id, float v);
    lumen::chord::Params readParams() const;

    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> programVersion { 0 }, currentChordSlot { -1 };
    std::array<std::atomic<bool>, 128> sounding {}, inChord {};
    std::atomic<float> outPeak { 0 };

private:
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    lumen::chord::ChordEngine engine;
    juce::Synthesiser synth;
    int soundType = 0;
    mutable juce::SpinLock lock;
    lumen::chord::Slot slots[8];
    float steps[16] { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    uint32_t ratchet = 0;
    double fs = 48000.0, freeBeat = 0.0;
    int currentProgram = 0;
    std::atomic<int> auditionSlot { -1 };
    juce::Random rng;
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenChordProcessor)
};
