#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/Analysis.h"
#include "synth/SynthFx.h"
#include "synth/Arp.h"

class LumenSynthSamplerProcessor : public juce::AudioProcessor
{
public:
    LumenSynthSamplerProcessor();
    ~LumenSynthSamplerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Synth Sampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- samples
    struct Slot
    {
        std::shared_ptr<lumen::sampler::SampleSource> src;
        std::shared_ptr<lumen::sampler::Resynth> rs;
        int bits = 24; bool isFloat = false; double detectedRoot = -1; bool analysing = false;
    };
    bool loadFile (int layer, const juce::File& f, juce::String& error);
    bool loadBuffer (int layer, const juce::AudioBuffer<float>& b, double rate, const juce::String& name, int bits, bool isFloat, bool setRoot);
    void clearLayer (int layer);
    Slot slot (int layer) const { const juce::ScopedLock l (slotLock); return slots[(size_t) layer]; }
    juce::String layerInfo (int layer) const;
    void waitForAnalysis();                 // tests / offline
    void setParam (const juce::String& id, float v);
    float param (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    lumen::ss::Params readParams() const;

    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    std::atomic<int> sampleVersion { 0 };
    std::atomic<float> outPeak { 0 };
    std::atomic<int> voicesActive { 0 };

    void finishAnalysis (int layer, std::shared_ptr<lumen::sampler::SampleSource> s, std::shared_ptr<lumen::sampler::Resynth> rs);

private:
    class Analyser;
    void install (int layer, std::shared_ptr<lumen::sampler::SampleSource> s, std::shared_ptr<lumen::sampler::Resynth> rs, int bits, bool isFloat, double root, bool analysing);
    void fillFx (lumen::synth::FxP& fx) const;

    double fs = 48000; int maxBlock = 512;
    lumen::ss::Engine engine;
    lumen::ss::LayerData audioLayers[lumen::ss::kLayers];
    std::atomic<lumen::sampler::SampleSource*> srcPtr[lumen::ss::kLayers] {};
    std::atomic<lumen::sampler::Resynth*> rsPtr[lumen::ss::kLayers] {};
    mutable juce::CriticalSection slotLock;
    std::array<Slot, lumen::ss::kLayers> slots;
    std::vector<std::shared_ptr<void>> retired;
    lumen::synth::FxChain fx;
    lumen::synth::Arp arp;
    float bendNorm = 0, modWheel = 0, aftertouch = 0;
    double ppqLast = 0;
    std::unique_ptr<Analyser> analyser;
    juce::MemoryBlock abState[2]; int abSlot = 0, currentProgram = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenSynthSamplerProcessor)
};
