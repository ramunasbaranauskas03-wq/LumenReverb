#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Parameters.h"
#include "dsp/Analysis.h"
#include "dsp/LimiterEngine.h"
#include "dsp/Biquad.h"

// Immutable decoded reference (buffers never change after construction)
struct RefData
{
    juce::String name, path;
    juce::AudioBuffer<float> original;  double originalRate = 44100.0;
    juce::AudioBuffer<float> play;      double playRate = 48000.0;
    std::vector<float> ovMin, ovMax;    // waveform overview
    double lengthSeconds() const { return play.getNumSamples() / playRate; }
};

class LumenReferenceProcessor : public juce::AudioProcessor
{
public:
    LumenReferenceProcessor();
    ~LumenReferenceProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Reference"; }
    bool acceptsMidi() const override { return false; }
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

    // ---------- references (message thread) ----------
    void loadReference (int slot, const juce::File& f);
    void clearReference (int slot);
    void requestAnalysis (int slot);
    std::shared_ptr<const RefData> getRef (int slot) const;
    lumen::ref::Profile getRefProfile (int slot) const;
    bool isBusy (int slot) const { return busy[slot].load() > 0; }
    juce::String getError (int slot) const;
    int activeSlot() const { return (int) apvts.getRawParameterValue (IDs::slot)->load(); }

    // ---------- mix analysis (UI thread pulls the FIFO) ----------
    int pullMix (float* L, float* R, int maxSamples);
    void resetMixMeters() { resetMix.store (true); }
    void captureMatchEq (const lumen::ref::Profile& mix, const lumen::ref::Profile& ref);

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> mixM { -100 }, mixS { -100 }, mixI { -100 }, mixTp { -100 }, refGainDb { 0 }, outPeakL { 0 }, outPeakR { 0 };
    std::atomic<double> refPosSec { 0.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<int> refVersion { 0 };
    std::atomic<double> seekRequest { -1.0 };     // 0..1 position request from the UI

private:
    struct Slot
    {
        std::shared_ptr<const RefData> data;
        lumen::ref::Profile profile;
        juce::String error;
    };
    void installData (int slot, std::shared_ptr<const RefData> d);
    static std::shared_ptr<RefData> resampled (const juce::AudioBuffer<float>& src, double srcRate, double dstRate, juce::String name, juce::String path);

    mutable juce::CriticalSection lock;
    Slot slots[kSlots];
    std::atomic<const RefData*> slotPtr[kSlots];
    std::atomic<float> activeRefLufs { -100.0f };
    std::vector<std::shared_ptr<const RefData>> retired;
    std::atomic<int> busy[kSlots];
    juce::ThreadPool pool { 2 };

    double fs = 48000.0;
    double refPos = 0.0;
    float abSm = 0.0f, gainSm = 1.0f;
    lumen::LoudnessMeter mixMeter;
    lumen::TruePeak tpL, tpR;
    float tpHold = 0.0f;
    std::atomic<bool> resetMix { false };
    lumen::Biquad meq[kMeqBands];
    float meqLast[kMeqBands + 1] {};
    juce::AbstractFifo fifo { 1 << 16 };
    std::vector<float> fifoL, fifoR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenReferenceProcessor)
};
