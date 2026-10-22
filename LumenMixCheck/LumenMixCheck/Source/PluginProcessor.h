#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/MixCheckEngine.h"

// ANALYZE ALL result: what every device does to the last seconds of the mix
struct DeviceReport
{
    static constexpr int kBands = 5;                 // sub, bass, mids, highs, air
    float bandDb[kBands] {};                         // level change vs the original (level matched)
    float streamDb = 0;                              // normalisation gain (streaming devices)
    float distortionPct = 0;                         // how much the device distorts it
    juce::String verdict; bool valid = false;
};
inline const char* reportBandName (int b) { static const char* n[] { "SUB", "BASS", "MIDS", "HIGHS", "AIR" }; return n[b]; }
inline float reportBandEdge (int k) { static const float e[] { 20, 60, 250, 2000, 8000, 20000 }; return e[k]; }

class LumenMixCheckProcessor : public juce::AudioProcessor
{
public:
    LumenMixCheckProcessor();
    ~LumenMixCheckProcessor() override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen MixCheck"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float v);
    int activeDevice() const { return shownDevice.load(); }

    // headphone correction
    bool loadHeadphoneEq (const juce::File& f, juce::String& err);
    juce::String customEqName() const { return customEqFile.getFileNameWithoutExtension(); }
    double headphoneResponseDb (double f) const { return hpEqView.responseDb (f); }
    float headphonePreampDb() const { return hpEqView.preampDb(); }
    bool headphoneEqActive() const { return hpEqView.active(); }
    void refreshHeadphoneView();                                   // message thread
    std::atomic<bool> viewDirty { true };

    // custom impulse response
    bool loadIR (const juce::File& f, juce::String& err);
    juce::String irName() const { return irFile.getFileNameWithoutExtension(); }

    // ANALYZE ALL
    void startAnalyzeAll();
    bool isAnalyzing() const { return analyzing.load(); }
    float analyzeProgress() const { return anProgress.load(); }
    std::vector<DeviceReport> reports() const { const juce::ScopedLock l (repLock); return rep; }
    int reportsVersion() const { return repVersion.load(); }
    float capturedSeconds() const { return (float) juce::jmin (capFilled.load(), capLen) / (float) fs; }
    static std::vector<DeviceReport> analyse (const juce::AudioBuffer<float>& clip, double fs, bool includeCustom, juce::dsp::Convolution* ir,
                                              std::function<void (float)> progress = {});
    void resetLoudness() { resetMeters.store (true); }

    juce::AudioProcessorValueTreeState apvts;
    // meters (UI)
    std::atomic<float> inLufsM { -100 }, inLufsS { -100 }, inLufsI { -100 }, inTp { -100 }, outLufsS { -100 }, outTp { -100 };
    std::atomic<float> streamGainDb { 0 }, levelMatchDb { 0 }, limGr { 0 }, corrOut { 1 };
    std::atomic<bool> renderBypassed { false };
    static constexpr int kRing = 8192;
    float ring[4][kRing] {};                           // in L/R, out L/R
    std::atomic<int> ringPos { 0 };

private:
    class Analyzer;
    lumen::mixcheck::Controls readControls() const;
    void updateStreamGain (lumen::mixcheck::DeviceChain& c);

    double fs = 48000; int maxBlock = 512;
    lumen::mixcheck::DeviceChain chain[2]; int active = 0;
    lumen::mixcheck::HeadphoneEq hpEq, hpEqView;                 // audio / UI copies
    std::vector<lumen::mixcheck::PeqBand> customEq; juce::File customEqFile; juce::SpinLock eqLock;
    int hpModelNow = -1; float hpAmountNow = -1; int customEqVersion = 0, customEqUsed = -1;
    void updateHeadphoneEq (bool audioThread);
    juce::dsp::Convolution conv[2];
    juce::File irFile; std::atomic<bool> irLoaded { false };
    lumen::meter::LoudnessMeter inMeter, outMeter;
    std::atomic<bool> resetMeters { false };
    juce::AudioBuffer<float> scratch;
    std::atomic<int> shownDevice { 1 };
    bool wasPlaying = false;
    double corrLR = 0, corrLL = 1e-9, corrRR = 1e-9;
    // capture for ANALYZE ALL
    juce::AudioBuffer<float> capture; int capLen = 1, capPos = 0; std::atomic<int> capFilled { 0 };
    std::atomic<bool> analyzing { false }; std::atomic<float> anProgress { 0 };
    std::unique_ptr<Analyzer> analyzer;
    juce::CriticalSection repLock; std::vector<DeviceReport> rep; std::atomic<int> repVersion { 0 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenMixCheckProcessor)
};
