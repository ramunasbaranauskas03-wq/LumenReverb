#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/Kick2.h"

class LumenKickProcessor : public juce::AudioProcessor
{
public:
    LumenKickProcessor();
    ~LumenKickProcessor() override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Kick Designer"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int i) override { return juce::isPositiveAndBelow (i, getNumPrograms()) ? juce::String (factoryPresets()[(size_t) i].name) : juce::String(); }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float v);
    lumen::kick2::Params readParams() const;
    void switchAB (int to); void copyAB(); int getABSlot() const { return abSlot; }

    // ---- samples (message thread)
    juce::String loadKick (const juce::File& f);                 // load + split into all three layers
    juce::String loadLayer (int layer, const juce::File& f);     // swap one layer
    juce::String stepLayer (int layer, int dir);                 // next / previous file in the same folder
    void clearLayer (int layer);
    void resplitIfNeeded();                                      // after the crossovers moved
    std::shared_ptr<const lumen::kick2::SampleData> layerSample (int k) const { return owned[(size_t) k]; }
    std::array<const lumen::kick2::SampleData*, 3> samplePointers() const { return { owned[0].get(), owned[1].get(), owned[2].get() }; }
    void collectGarbage();

    // ---- preview / export
    void audition() { auditionReq.store (true); }
    juce::String exportWav (juce::File& written);
    juce::AudioFormatManager formats;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> programVersion { 0 }, sampleVersion { 0 };
    std::atomic<float> outPk { 0 };
    std::atomic<int> lastNote { -1 };
    std::atomic<juce::uint32> lastHitMs { 0 };

private:
    std::shared_ptr<lumen::kick2::SampleData> readFile (const juce::File& f, juce::String& err);
    void publish();
    double fs = 48000; int maxBlock = 512;
    lumen::kick2::Engine engine;
    std::array<std::shared_ptr<lumen::kick2::SampleData>, 3> owned;           // message thread
    std::array<std::atomic<const lumen::kick2::SampleData*>, 3> live {};      // audio thread view
    struct Retired { std::shared_ptr<lumen::kick2::SampleData> p; juce::int64 block; juce::uint32 ms; };
    struct Ptrs; std::unique_ptr<Ptrs> ptr;
    std::vector<Retired> graveyard;
    std::atomic<juce::int64> blockCounter { 0 };
    std::atomic<bool> auditionReq { false };
    juce::MemoryBlock ab[2]; int abSlot = 0, currentProgram = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenKickProcessor)
};
