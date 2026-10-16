#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/VocalEngine.h"

class LumenVocalProcessor : public juce::AudioProcessor
{
public:
    LumenVocalProcessor();
    ~LumenVocalProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Vocal"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return juce::isPositiveAndBelow (index, getNumPrograms()) ? factoryPresets()[(size_t) index].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Smart Assist: listens 6 s, finds key / level / sibilance / dynamics / tone, then builds the chain for a style
    enum Style { NaturalPop, EdmLead, HardTune, Rap, Ballad };
    void startAssist (Style s);
    bool isAssisting() const { return assisting.load(); }
    float assistProgress() const { return (float) asPos.load() / (float) juce::jmax (1, asLen); }
    juce::String getAssistResult() const { return assistResult; }
    void finishAssistIfReady();

    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }

    juce::AudioProcessorValueTreeState apvts;

    static constexpr int kHist = 360, kFrame = 256;
    struct Frame { float det = -1, out = -1, target = -1; };
    Frame hist[kHist];
    std::atomic<int> histPos { 0 };
    std::atomic<float> inPk { 0 }, outPk { 0 }, deessGr { 0 }, compGr { 0 }, curMidi { -1 }, curOut { -1 };
    std::atomic<int> curTarget { -1 }, programVersion { 0 };

private:
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    lumen::vox::VocalEngine engine;
    juce::AudioBuffer<float> tmp;
    double fs = 48000.0;
    int currentProgram = 0;
    std::vector<float> asBuf;
    std::atomic<int> asPos { 0 };
    int asLen = 1;
    std::atomic<bool> assisting { false };
    Style asStyle = NaturalPop;
    juce::String assistResult;
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenVocalProcessor)
};
