#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <random>
#include "Parameters.h"
#include "dsp/LimiterEngine.h"

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
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Target values for the current selection (custom uses the parameter)
    float targetLufs() const;
    float targetTp() const;

    // Meters
    std::atomic<float> momentary { -100.0f }, shortTerm { -100.0f }, integrated { -100.0f };
    std::atomic<float> tpMax { -100.0f }, grDb { 0.0f }, inLevel { 0.0f }, outLevel { 0.0f };
    std::atomic<bool>  resetMeters { false };

    static constexpr int kHistory = 400;
    std::array<std::atomic<float>, kHistory> histOut {}, histGr {};
    std::atomic<int> histWrite { 0 };

private:
    void configure (int lookaheadSamples);

    std::atomic<float>* p (const char* id) { return apvts.getRawParameterValue (id); }
    std::atomic<float> *pGain, *pCeil, *pRel, *pAutoRel, *pLa, *pLink, *pStyle, *pClipOn, *pClipLvl, *pClipShape,
                       *pDither, *pDelta, *pMatch, *pOs;

    std::unique_ptr<juce::dsp::Oversampling<float>> os[5];   // index = log2(factor), [0] unused (1x)
    int osIndex = -1;
    lumen::GainPath gpath[2];
    lumen::TruePeak det[2], outTp[2];
    lumen::IntDelay audioDelay[2], dryDelay[2];
    lumen::LoudnessMeter loud;
    juce::SmoothedValue<float> gainSm;
    juce::AudioBuffer<float> dryBuf;

    std::mt19937 rng { 12345 };
    std::uniform_real_distribution<float> uni { -0.5f, 0.5f };

    double sr = 48000.0;
    int maxBlock = 512, curW = -1, osLatency = 0;
    float tpMaxLin = 0.0f;
    int histCounter = 0, histInterval = 600; float histOutAcc = 0.0f, histGrAcc = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenLimiterProcessor)
};
