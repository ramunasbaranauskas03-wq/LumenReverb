#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/EqEngine.h"

class LumenEQProcessor : public juce::AudioProcessor
{
public:
    LumenEQProcessor();
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
    lumen::EqBand bandFromParams (int b) const;

    // live
    std::atomic<float> dynGain[lumen::EqEngine::kBands] {};
    std::atomic<bool>  scConnected { false };
    double getSampleRateSafe() const { return sr; }

    // spectrum FIFOs (track output, sidechain)
    static constexpr int kFifo = 1 << 15;
    juce::AbstractFifo fifoTrack { kFifo }, fifoSc { kFifo };
    std::vector<float> bufTrack, bufSc;

    // unmask capture (track input + sidechain, 4 s)
    void requestCapture() { captureRequested.store (true); }
    void cancelCapture()  { capturing.store (false); }
    bool isCapturing() const { return capturing.load(); }
    float captureProgress() const { return capProgress.load(); }
    bool takeCapture (std::vector<float>& track, std::vector<float>& sc);

private:
    void push (juce::AbstractFifo& f, std::vector<float>& b, const float* d, int n);

    std::atomic<float>* p (const juce::String& id) const { return apvts.getRawParameterValue (id); }
    std::atomic<float> *pOn[6], *pType[6], *pFreq[6], *pGain[6], *pQ[6], *pDyn[6], *pThr[6], *pSc[6], *pAtk, *pRel, *pOut;

    lumen::EqEngine engine;
    juce::SmoothedValue<float> outSm;
    double sr = 48000.0;
    std::vector<float> monoTmp, scTmp;

    std::vector<float> capT, capS; int capPos = 0;
    std::atomic<bool> captureRequested { false }, capturing { false }, capReady { false };
    std::atomic<float> capProgress { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenEQProcessor)
};
