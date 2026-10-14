#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/BassEngine.h"
#include "dsp/PhaseAlign.h"

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
    lumen::PhaseAligner phase;

    // meters
    std::atomic<float> corrBefore { 0.0f }, corrAfter { 0.0f }, lowCorrIn { 1.0f }, lowCorrOut { 1.0f };
    std::atomic<float> phoneIn { 0.0f }, phoneOut { 0.0f }, inLevel { 0.0f }, outLevel { 0.0f };
    std::atomic<bool>  lowEndPresent { false }, scConnected { false }, hasBass { false };

    // Smart Assist capture
    void requestCapture() { captureRequested.store (true); }
    void cancelCapture()  { capturing.store (false); }
    bool isCapturing() const { return capturing.load(); }
    float captureProgress() const { return capProgress.load(); }
    bool takeCapture (std::vector<float>& out);
    double getSampleRateSafe() const { return sr; }

private:
    std::atomic<float>* p (const char* id) { return apvts.getRawParameterValue (id); }
    std::atomic<float> *pMonoOn, *pMonoHz, *pWidth, *pSub, *pOut, *pHarm, *pFocus, *pChar, *pTone, *pListen,
                       *pPhOn, *pPhRot, *pPhDly, *pPhFoc;

    lumen::BassEngine engine;
    double sr = 48000.0;
    float lastFocus = -1.0f;

    std::vector<float> capBuf; int capPos = 0;
    std::atomic<bool> captureRequested { false }, capturing { false }, capReady { false };
    std::atomic<float> capProgress { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenBassProcessor)
};
