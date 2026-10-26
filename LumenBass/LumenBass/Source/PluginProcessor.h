#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/Bass2.h"

class LumenBassProcessor : public juce::AudioProcessor
{
public:
    LumenBassProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Bass"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int i) override { return juce::isPositiveAndBelow (i, getNumPrograms()) ? juce::String (factoryPresets()[(size_t) i].name) : juce::String(); }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float v);
    void switchAB (int to); void copyAB(); int getABSlot() const { return abSlot; }
    juce::String learn();                      // message thread, after a capture finished
    void startLearn() { learnRequest.store (true); }
    float learnProgress() const { return capProgress.load(); }
    bool learnRunning() const { return capturing.load() || learnRequest.load(); }
    bool learnReady() const { return capReady.load(); }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> programVersion { 0 };

    // meters
    std::atomic<float> inPk { 0 }, outPk { 0 }, subGr { 0 }, midSq { 0 }, topSq { 0 }, duckDb { 0 };
    std::atomic<float> bassHz { 0 }, kickHz { 0 }, lowCorr { 1 }, phoneDb { 0 }, x1Now { 110 }, x2Now { 2500 };
    std::atomic<bool> scActive { false };
    static constexpr int kRing = 8192;
    float ring[3][kRing] {};                    // 0 = input, 1 = output, 2 = side-chain (mono, base rate)
    float detectPitch (int which) const;        // message thread: YIN on the last 8192 samples of a ring
    std::atomic<int> ringPos { 0 };

private:
    void applyQuality (int q);
    lumen::bass2::Params readParams() const;
    std::array<std::atomic<float>*, 40> pr {};

    double fs = 48000; int maxBlock = 512, quality = -1, osFactor = 1;
    std::unique_ptr<juce::dsp::Oversampling<float>> os[3];
    lumen::bass2::Engine engine;
    lumen::bass2::KickDuck duck;
    lumen::bass2::Svf corrL, corrR, phIn[2], phOut[2];
    float eL = 0, eR = 0, eLR = 0, ePhIn = 0, ePhOut = 0, meterC = 0.001f;
    juce::AudioBuffer<float> work;
    std::vector<float> duckGain;

    std::vector<float> cap; int capPos = 0;
    std::atomic<bool> learnRequest { false }, capturing { false }, capReady { false };
    std::atomic<float> capProgress { 0 };

    juce::MemoryBlock ab[2]; int abSlot = 0, currentProgram = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenBassProcessor)
};
