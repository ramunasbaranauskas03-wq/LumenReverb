#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/Loudness.h"

struct RefTrack
{
    juce::AudioBuffer<float> buf; double rate = 48000;
    juce::String name, path;
    float integrated = -70, truePeak = -100, lra = 0;
    std::vector<float> spectrum;           // 1/6 octave, dB (average)
};

class LumenMeterProcessor : public juce::AudioProcessor
{
public:
    LumenMeterProcessor();
    ~LumenMeterProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Meter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void resetMeter() { resetPending.store (true); }
    bool loadReference (const juce::File& f, juce::String& error);
    std::shared_ptr<const RefTrack> getReference() const { return std::atomic_load (&ref); }
    static std::vector<float> octaveSpectrum (const juce::AudioBuffer<float>& b, double rate, int maxFrames = 400);   // 1/6 oct dB
    static constexpr int kSpecBands = 61;     // 20 Hz .. 20 kHz, 1/6 octave
    static float bandFreq (int i) { return 20.0f * std::pow (2.0f, (float) i / 6.0f); }

    // live values (UI)
    struct Snapshot { float m, s, i, lra, mMax, sMax, tpMax, tpNow, spMax, corr, corrLow, dc; int clips; bool valid; };
    Snapshot snapshot() const;
    static constexpr int kHist = 1200;        // 120 s at 10 Hz
    float histM[kHist] {}, histS[kHist] {};
    std::atomic<int> histPos { 0 }, histCount { 0 };
    // analysis ring (input, L/R) for the spectrum / scope
    static constexpr int kRing = 16384;
    float ring[2][kRing] {};
    std::atomic<int> ringPos { 0 };
    std::atomic<float> bandWidth[3] {};       // side/mid energy ratio low / mid / high
    std::atomic<float> appliedGainDb { 0 };   // preview / reference gain currently applied
    juce::AudioProcessorValueTreeState apvts;

private:
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    lumen::meter::LoudnessMeter meter;
    juce::AudioFormatManager formats;
    std::shared_ptr<const RefTrack> ref;
    double fs = 48000.0, refPos = 0;
    std::atomic<bool> resetPending { false };
    bool wasPlaying = false;
    int histAcc = 0;
    // correlation / dc
    double cLL = 0, cRR = 0, cLR = 0, lLL = 0, lRR = 0, lLR = 0, dcAcc = 0;
    float lpL[2] {}, lpR[2] {};
    double bm[3] {}, bs[3] {};
    float bandState[2][3][2] {};
    float gainSm = 1.0f;
    std::atomic<float> aCorr { 1 }, aCorrLow { 1 }, aDc { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenMeterProcessor)
};
