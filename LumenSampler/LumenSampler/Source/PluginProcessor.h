#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"

class LumenSamplerProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    LumenSamplerProcessor();
    ~LumenSamplerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Sampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- sample
    bool loadFile (const juce::File& f, juce::String& error);
    bool loadBuffer (const juce::AudioBuffer<float>& b, double rate, const juce::String& name, int bits, bool isFloat);
    void clearSample();
    std::shared_ptr<lumen::sampler::SampleSource> source() const { const juce::ScopedLock sl (srcLock); return src; }
    juce::String sampleInfo() const;
    double detectedRoot() const { return detRoot; }
    double detectedBpm() const { return detBpm; }
    void applyDetectedRoot();
    std::vector<int> getSlices() const { const juce::ScopedLock sl (srcLock); return slices; }
    void rebuildSlices();
    juce::File exportSliceMidi();
    juce::File exportRegionWav();
    float normalizeGainDb() const { return juce::Decibels::gainToDecibels (normGain.load()); }
    void setParam (const juce::String& id, float v);
    lumen::sampler::Params readParams() const;

    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    std::atomic<int> sampleVersion { 0 };
    std::atomic<float> outPeak { 0 };
    std::array<std::atomic<double>, 8> playPos {};
    std::atomic<int> playCount { 0 };
    std::atomic<float> hostBpm { 120 };

private:
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void installSource (std::shared_ptr<lumen::sampler::SampleSource> s, int bits, bool isFloat);

    void colour (float* L, float* R, int n);
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler; int maxBlock = 4096; bool drivePrimed = false;
    double vPh = 0; float holdL = 0, holdR = 0, lpL[2] {}, lpR[2] {};
    std::atomic<float> normGain { 1.0f };
    double fs = 48000.0;
    lumen::sampler::Engine engine;
    mutable juce::CriticalSection srcLock;
    std::shared_ptr<lumen::sampler::SampleSource> src;
    std::vector<std::shared_ptr<lumen::sampler::SampleSource>> retired;
    std::atomic<lumen::sampler::SampleSource*> srcPtr { nullptr };
    lumen::sampler::SampleSource* audioSrc = nullptr;
    std::vector<int> slices, audioSlices; std::atomic<bool> slicesDirty { true };
    int srcBits = 24; bool srcFloat = false;
    double detRoot = -1, detBpm = 0;
    float bendNorm = 0;
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenSamplerProcessor)
};
