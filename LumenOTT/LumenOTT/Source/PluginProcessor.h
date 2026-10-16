#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/OttEngine.h"

class LumenOTTProcessor : public juce::AudioProcessor
{
public:
    LumenOTTProcessor();
    ~LumenOTTProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen OTT"; }
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

    // Smart Assist: listens 5 s, measures the band balance and level, sets OTT for the source type
    enum Source { Synth, Bass, Drums, Vocal, Master };
    void startAssist (Source s);
    bool isAssisting() const { return assisting.load(); }
    float assistProgress() const { return (float) asPos.load() / (float) juce::jmax (1, asLen); }
    juce::String getAssistResult() const { return assistResult; }
    void finishAssistIfReady();

    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }
    lumen::ott::Params readParams() const;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inPk { 0 }, outPk { 0 }, agDb { 0 }, lockDb { 0 };
    std::atomic<float> bandLvl[3] {}, bandUp[3] {}, bandDown[3] {};
    std::atomic<int> programVersion { 0 };

private:
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    lumen::ott::OttEngine engine;
    std::unique_ptr<juce::dsp::Oversampling<float>> os[3];
    double fs = 48000.0;
    int curOs = -1, maxBlock = 512, pad = 0, padPos = 0;
    float padBuf[2][256] {};
    int currentProgram = 0;
    float mLvl[3] {}, mUp[3] {}, mDown[3] {};
    std::vector<float> asL, asR;
    std::atomic<int> asPos { 0 };
    int asLen = 1;
    std::atomic<bool> assisting { false };
    Source asSource = Synth;
    juce::String assistResult;
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenOTTProcessor)
};
