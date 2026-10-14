#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "Parameters.h"
#include "dsp/PumpEngine.h"

class LumenPumpProcessor : public juce::AudioProcessor
{
public:
    LumenPumpProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
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

    // ---- Curve (message thread) ---------------------------------------------
    std::vector<lumen::CurvePoint> getPoints() const { return points; }
    void setPoints (std::vector<lumen::CurvePoint> pts);   // sorts, stores in state, publishes LUT
    static std::vector<lumen::CurvePoint> presetPoints (int index, float& smoothOut);
    static const juce::StringArray& presetNames();

    // ---- Meters / playhead (read by UI) -------------------------------------
    std::atomic<float> phase { 0.0f }, level { 1.0f }, scLevel { 0.0f };
    std::atomic<bool>  scConnected { false };
    std::atomic<int>   triggerFlash { 0 };
    std::atomic<double> hostBpm { 120.0 };

    // ---- Smart Assist kick capture ------------------------------------------
    void requestCapture() { captureRequested.store (true); }
    void cancelCapture()  { capturing.store (false); }
    bool isCapturing() const { return capturing.load(); }
    float captureProgress() const { return capProgress.load(); }
    bool takeCapture (std::vector<float>& out);       // UI thread
    double getSampleRateSafe() const { return sr; }

private:
    void publishCurve();
    void loadPointsFromState();

    std::atomic<float>* p (const char* id) { return apvts.getRawParameterValue (id); }
    std::atomic<float> *pTrig, *pRate, *pLow, *pHigh, *pDepth, *pOut, *pSmooth, *pOffset, *pScTh, *pSolo, *pOutput;

    std::vector<lumen::CurvePoint> points;
    std::array<std::atomic<float>, lumen::PumpEngine::kLutSize + 1> lutLin {}, lutSm {};
    std::array<float, lumen::PumpEngine::kLutSize + 1> lutLocal {};

    lumen::PumpEngine engine;
    double sr = 48000.0;

    std::vector<float> capBuf;
    int capPos = 0;
    std::atomic<bool> captureRequested { false }, capturing { false }, capReady { false };
    std::atomic<float> capProgress { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenPumpProcessor)
};
