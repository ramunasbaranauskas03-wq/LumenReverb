#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/EqEngine2.h"

class LumenEQProcessor : public juce::AudioProcessor
{
public:
    LumenEQProcessor();
    ~LumenEQProcessor() override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int i) override { return juce::isPositiveAndBelow (i, getNumPrograms()) ? factoryPresets()[(size_t) i].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float v);
    lumen::eq2::BandParams band (int b) const;
    std::array<lumen::eq2::BandParams, lumen::eq2::kBands> bands() const;
    double sampleRateNow() const { return fs; }

    // band management (message thread)
    int addBand (int type, float freq, float gain, float q = 1.0f, int slope = 3, int chan = 0);
    void removeBand (int b);
    void clearBands();
    std::atomic<int> soloBand { -1 };

    // EQ match / unmask (message thread)
    bool loadMatchReference (const juce::File& f, juce::String& err);
    juce::String eqMatch (bool useSidechain, float amount, int maxBands);
    juce::String unmask();
    bool hasSidechainSignal() const { return scActive.load(); }
    bool hasReference() const { return ! refSpectrum.empty(); }

    void switchAB (int to); void copyAB(); int getABSlot() const { return abSlot; }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> programVersion { 0 };
    std::atomic<float> inPk { 0 }, outPk { 0 }, autoGainNow { 0 };
    std::atomic<float> dynGain[lumen::eq2::kBands] {};
    // spectra rings (mono): 0 pre, 1 post, 2 side-chain
    static constexpr int kRing = 8192;
    float ring[3][kRing] {};
    std::atomic<int> ringPos { 0 };
    std::atomic<bool> scActive { false };

private:
    class Worker;
    bool requestFir (const std::array<lumen::eq2::BandParams, lumen::eq2::kBands>& b, float scale, int quality);
    std::vector<double> averageSpectrum (const std::vector<float>& x) const;   // 1/6-oct bands, dB
    std::vector<float> captureCopy (int which) const;

    double fs = 48000; int maxBlock = 512;
    lumen::eq2::IirEq iir;
    lumen::eq2::LinearEq lin[3];
    std::unique_ptr<Worker> worker;
    juce::SpinLock firLock; std::array<lumen::eq2::BandParams, lumen::eq2::kBands> firBands {}; float firScale = 1; int firQuality = 1;
    std::atomic<int> firWant { 0 };
    std::array<lumen::eq2::BandParams, lumen::eq2::kBands> lastFirBands {}; float lastFirScale = -99; int lastQuality = -1, lastMode = -1;
    juce::AudioBuffer<float> scBuf;
    float outSm = 1, agSm = 0, agTarget = 0; int agCounter = 0;
    lumen::eq2::Section solo[2][2]; int soloLast = -2; float soloF = 0, soloQ = 0; int soloT = -1;
    // capture for EQ match / unmask (8 s, mono pre + side-chain)
    std::vector<float> cap[2]; int capPos = 0; std::atomic<int> capFilled { 0 };
    std::vector<double> refSpectrum;
    juce::MemoryBlock ab[2]; int abSlot = 0, currentProgram = 0;
    friend class Worker;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenEQProcessor)
};
