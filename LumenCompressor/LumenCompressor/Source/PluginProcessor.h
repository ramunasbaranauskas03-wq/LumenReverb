#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "Parameters.h"
#include "dsp/CompressorEngine.h"
#include "dsp/SmartAnalyzer.h"
#include "dsp/PhaseAlign.h"

class LumenCompressorProcessor : public juce::AudioProcessor
{
public:
    LumenCompressorProcessor();

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
    lumen::SmartAnalyzer analyzer;
    lumen::PhaseAligner phase;
    std::atomic<float> corrBefore { 0.0f }, corrAfter { 0.0f };
    std::atomic<bool>  lowEndPresent { false };
    lumen::CompressorEngine::Params makeParams() const;

    // Meters (read by the editor)
    std::atomic<float> inLevel { 0.0f }, outLevel { 0.0f }, grDb { 0.0f }, scLevel { 0.0f }, autoMakeupDb { 0.0f };
    std::atomic<bool>  scConnected { false };

    // History for the scrolling display: input peak (dB) and gain reduction (dB)
    static constexpr int kHistory = 400;
    std::array<std::atomic<float>, kHistory> histIn {}, histGr {};
    std::atomic<int> histWrite { 0 };

private:
    std::atomic<float>* p (const char* id) { return apvts.getRawParameterValue (id); }

    std::atomic<float> *pThr, *pRatio, *pKnee, *pAtk, *pRel, *pAutoRel, *pRange, *pLa, *pLink, *pScHp, *pScLp,
                       *pScSrc, *pListen, *pStyle, *pChan, *pDrive, *pMakeup, *pAutoGain, *pMix, *pOut,
                       *pPhOn, *pPhRot, *pPhDly, *pPhFoc;

    lumen::CompressorEngine engine;
    int histCounter = 0, histInterval = 600;
    float histInAcc = 0.0f, histGrAcc = 0.0f;
    int currentLatency = -1;
    float lastFocus = -1.0f;
    double sr = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenCompressorProcessor)
};
