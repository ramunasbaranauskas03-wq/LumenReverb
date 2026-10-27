#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "Parameters.h"
#include "dsp/Limit2.h"
#include "dsp/Loudness.h"

class LumenLimiterProcessor : public juce::AudioProcessor
{
public:
    LumenLimiterProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Limiter"; }
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
    float targetLufs() const;
    float targetTp() const;
    // MATCH TARGET (message thread, call ~2x per second while active). Returns status text; empty when finished.
    juce::String matchStep();
    void startMatch() { matchActive = true; matchSteps = 0; matchGood = 0; }
    void stopMatch() { matchActive = false; }
    bool matching() const { return matchActive; }
    void resetMeters() { resetRequest.store (true); }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> programVersion { 0 };

    // meters
    std::atomic<float> lufsM { -80 }, lufsS { -80 }, lufsI { -80 }, lra { 0 }, tpMax { 0 }, tpNow { 0 }, inLufsS { -80 }, cgmDb { 0 };
    std::atomic<float> limGr { 0 }, limGrLo { 0 }, limGrHi { 0 }, compGr { 0 }, clipDb { 0 }, inPk { 0 }, outPkL { 0 }, outPkR { 0 }, punchDb { 0 }, detailDb { 0 };
    struct HistPoint { float outPk = -100, inPk = -100, limGr = 0, compGr = 0, lufsS = -80, clip = 0; };
    static constexpr int kHist = 2048, kHistChunk = 256;
    HistPoint hist[kHist];
    std::atomic<int> histPos { 0 };

private:
    void applyQuality (int q);
    std::array<std::atomic<float>*, 40> pr {};
    double fs = 48000; int maxBlock = 256, quality = -1, osFactor = 1, osLatency = 0, latencyNow = -1;
    std::unique_ptr<juce::dsp::Oversampling<float>> os[3];
    juce::AudioBuffer<float> work;
    lumen::master::GlueComp comp;
    lumen::master::Transients trans;
    lumen::master::ColorClip color;
    lumen::master::Limiter2 lim;
    lumen::master::Dither dither;
    lumen::meter::LoudnessMeter inMeter, outMeter;
    lumen::master::DelayLine dryDelay[2];
    float gainSm = 1, cgmSm = 1;
    std::atomic<bool> resetRequest { false };
    bool matchActive = false; int matchSteps = 0, matchGood = 0;
    juce::MemoryBlock ab[2]; int abSlot = 0, currentProgram = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenLimiterProcessor)
};
