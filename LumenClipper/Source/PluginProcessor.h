#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/ClipEngine.h"
#include "dsp/LimiterEngine.h"

class LumenClipperProcessor : public juce::AudioProcessor
{
public:
    LumenClipperProcessor();
    ~LumenClipperProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Clipper"; }
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

    // Smart Drive: listens 5 s, measures the peaks, sets DRIVE for a target amount of clipping
    enum Goal { Transparent, Punchy, Loud, Smash };
    void startSmart (Goal g);
    bool isSmartRunning() const { return smartOn.load(); }
    float smartProgress() const { return (float) smartPos.load() / (float) juce::jmax (1, smartLen); }
    juce::String getSmartResult() const { return smartResult; }
    void finishSmartIfReady();

    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }
    void resetPeaks() { resetReq.store (true); }

    juce::AudioProcessorValueTreeState apvts;

    // display: one frame per kFrame samples
    static constexpr int kHist = 400, kFrame = 256;
    struct Frame { float pre = 0, out = 0, band[4] {}; };
    Frame hist[kHist];
    std::atomic<int> histPos { 0 };
    std::atomic<float> inPk[2] {}, outPk[2] {}, grDb { 0 }, tpMax { 0 }, lufsS { -100 }, lufsI { -100 }, matchDb { 0 }, prePeak { 0 }, tpGr { 0 };
    std::atomic<int> programVersion { 0 };

private:
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void configure();
    lumen::clip::ClipEngine engine;
    std::unique_ptr<juce::dsp::Oversampling<float>> os[6];
    lumen::TruePeak tpDet[2];
    lumen::GainPath tpPath;
    lumen::IntDelay tpDelay[2];
    lumen::LoudnessMeter loud;
    double fs = 48000.0;
    int curOs = -1, osLat = 0, tpW = 32, maxBlock = 512;
    bool curTp = false;
    int currentProgram = 0, histN = 0;
    float frPre = 0, frOut = 0, frBand[4] {};
    double inPow = 1e-9, outPow = 1e-9;
    float matchG = 1.0f;
    std::atomic<bool> resetReq { false };

    std::vector<float> smartBuf;
    std::atomic<int> smartPos { 0 };
    int smartLen = 1;
    std::atomic<bool> smartOn { false };
    Goal smartGoal = Punchy;
    juce::String smartResult;
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenClipperProcessor)
};
