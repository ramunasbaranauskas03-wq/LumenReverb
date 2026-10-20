#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Parameters.h"

class LumenDrumsProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kAux = 4;
    LumenDrumsProcessor();
    ~LumenDrumsProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Drums"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return juce::isPositiveAndBelow (index, getNumPrograms()) ? factoryPresets()[(size_t) index].name : juce::String(); }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- patterns (message thread)
    lumen::drums::Pattern getPattern (int i) const { const juce::SpinLock::ScopedLockType l (lock); return patterns[(size_t) juce::jlimit (0, 7, i)]; }
    void setPattern (int i, const lumen::drums::Pattern& p);
    lumen::drums::Step getStep (int pat, int pad, int step) const { const juce::SpinLock::ScopedLockType l (lock); return patterns[(size_t) pat].tr[(size_t) pad].s[(size_t) step]; }
    void setStep (int pat, int pad, int step, lumen::drums::Step s);
    int getLength (int pat, int pad) const { const juce::SpinLock::ScopedLockType l (lock); return patterns[(size_t) pat].tr[(size_t) pad].length; }
    void setLength (int pat, int pad, int len);
    int currentPattern() const { return (int) P (IDs::pattern); }
    void selectPattern (int i);

    void generate();                 // current pattern from the chosen genre
    void generateTrack (int pad);    // one track only
    void generateSong();             // A..H song sections + kit
    void applyGenreKit (int genre);
    void fill();
    void variation();
    void clearPattern();
    void copyPatternTo (int dest);

    // ---- samples
    bool loadSample (int pad, const juce::File& f, juce::String& error);
    void clearSample (int pad);
    juce::String sampleName (int pad) const;
    std::shared_ptr<lumen::drums::SampleData> sampleFor (int pad) const;

    // ---- audition / exports
    void audition (int pad, float vel = 0.9f) { auditionPad.store (pad); auditionVel.store (vel); }
    juce::MidiFile renderMidi (int bars) const;
    juce::AudioBuffer<float> renderAudio (int bars, double rate, double bpm);
    juce::File exportMidi (int bars);
    juce::File exportWav (int bars);
    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }
    lumen::drums::PadP padParams (int p) const;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> programVersion { 0 }, patternVersion { 0 };
    std::atomic<int64_t> playStep { -1 };
    std::array<std::atomic<float>, lumen::drums::kPads> padFlash {};
    std::atomic<float> outPeak { 0 }, glueGr { 1 };
    std::atomic<float> hostBpm { 120 };
    uint32_t genSeed = 1;

private:
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void triggerPad (int pad, float vel, int offset, bool fromSeq, juce::MidiBuffer& midiOut, int blockLen);
    void renderSegment (int from, int to);
    void bumpPatterns() { ++patternVersion; dirty.store (true); }

    double fs = 48000.0, freeBeat = 0.0;
    std::array<lumen::drums::Pad, lumen::drums::kPads> pads;
    std::array<lumen::drums::PadP, lumen::drums::kPads> pp {};
    lumen::drums::Player player;
    lumen::drums::Master master;
    std::vector<lumen::drums::Hit> hits;
    struct Ev { int offset; int pad; float vel; bool seq; };
    std::vector<Ev> events;
    struct Off { int64_t at; int note; };
    std::vector<Off> midiOffs;
    int64_t sampleClock = 0;
    juce::AudioBuffer<float> bus[1 + kAux];
    int currentProgram = 0;
    mutable juce::SpinLock lock;
    std::array<lumen::drums::Pattern, lumen::drums::kPatterns> patterns {};
    lumen::drums::Pattern audioPattern {};
    int audioPatternIndex = -1;
    std::atomic<bool> dirty { true };
    std::atomic<int> auditionPad { -1 };
    std::atomic<float> auditionVel { 0.9f };
    // samples: message thread owns them, the audio thread reads raw pointers
    std::array<std::shared_ptr<lumen::drums::SampleData>, lumen::drums::kPads> samples {};
    std::array<std::atomic<lumen::drums::SampleData*>, lumen::drums::kPads> samplePtr {};
    std::vector<std::shared_ptr<lumen::drums::SampleData>> retired;
    juce::CriticalSection sampleLock;
    juce::MemoryBlock abState[2];
    int abSlot = 0;
    uint32_t humRng = 777;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenDrumsProcessor)
};
