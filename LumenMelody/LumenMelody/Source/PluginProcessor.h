#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/PreviewSynth.h"

class LumenMelodyProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    LumenMelodyProcessor();
    ~LumenMelodyProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Melody Bass"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return juce::isPositiveAndBelow (index, getNumPrograms()) ? factoryPresets()[(size_t) index].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // patterns (message thread)
    lumen::melody::Pattern getPattern (int part) const { const juce::SpinLock::ScopedLockType l (lock); return part == 0 ? lead : bass; }
    void setPattern (int part, const lumen::melody::Pattern& p);
    void generate (int part, bool newSeed);           // part 0 lead, 1 bass, 2 both
    uint32_t getSeed (int part) const { return part == 0 ? leadSeed : bassSeed; }
    lumen::melody::PlayParams readPlay() const;
    lumen::melody::LeadSettings leadSettings() const;
    lumen::melody::BassSettings bassSettings() const;
    enum ExportKind { ExportLead, ExportBass, ExportBoth };
    juce::MidiFile renderMidi (ExportKind k) const;
    juce::File exportMidi (ExportKind k);
    void setPreviewPlaying (bool b) { previewOn.store (b); }
    bool isPreviewPlaying() const { return previewOn.load(); }
    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }
    int chordAtStep (int64_t s) const;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> programVersion { 0 }, patternVersion { 0 };
    std::atomic<int64_t> playStep { -1 };
    std::atomic<int> inputDegree { 0 };
    std::array<std::atomic<bool>, 128> leadOn {}, bassOnN {};
    std::atomic<float> outPeak { 0 };

private:
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    lumen::melody::Player player;
    juce::Synthesiser leadSynth, bassSynth;
    int leadType = 0, bassType = 1;
    mutable juce::SpinLock lock;
    lumen::melody::Pattern lead, bass, leadA, bassA;   // A = audio-thread copies
    std::atomic<bool> dirty { true };
    std::atomic<bool> regenLead { false }, regenBass { false };
    std::atomic<bool> previewOn { false };
    uint32_t leadSeed = 1, bassSeed = 1;
    double fs = 48000.0, freeBeat = 0.0;
    int currentProgram = 0;
    bool loading = false;
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenMelodyProcessor)
};
