#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/MonoEngine.h"

class LumenMonoFilterProcessor : public juce::AudioProcessor, private juce::Thread
{
public:
    LumenMonoFilterProcessor();
    ~LumenMonoFilterProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen MonoFilter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return juce::isPositiveAndBelow (index, getNumPrograms()) ? factoryPresets()[(size_t) index].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // AUTO FOCUS: listens 5 s, measures width + correlation per 1/3 octave and the L/R time offset of the lows,
    // then places the nodes, the HPF and phase align for the chosen goal
    enum Goal { Master, Bass, Recording };
    void startFocus (Goal g);
    bool isFocusing() const { return focusing.load(); }
    float focusProgress() const { return (float) fPos.load() / (float) juce::jmax (1, fLen); }
    juce::String getFocusResult() const { return focusResult; }
    void finishFocusIfReady();

    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }
    void setParam (const juce::String& id, float v);
    lumen::mono::Params readParams() const;
    bool isFirReady() const { return firReady.load(); }
    double getFs() const { return fs; }

    // analysis rings for the editor (input + output, L/R)
    static constexpr int kRing = 8192;
    float ring[4][kRing] {};
    std::atomic<int> ringPos { 0 };
    std::atomic<float> alignNow { 0 }, polNow { 1 }, trimNow { 0 }, confNow { 0 }, corrIn { 1 }, corrOut { 1 };
    std::atomic<int> programVersion { 0 };

    juce::AudioProcessorValueTreeState apvts;

private:
    void run() override;
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    static juce::uint64 hashParams (const lumen::mono::Params& p, double fs);

    lumen::mono::MonoEngine engine;
    juce::CriticalSection engineLock;
    std::atomic<double> curFs { 0.0 };
    std::atomic<juce::uint64> loadedHash { 0 };
    std::atomic<bool> firReady { false };
    double fs = 48000.0;
    int currentProgram = 0;
    double cin[3] {}, cout_[3] {};

    std::vector<float> fL, fR;
    std::atomic<int> fPos { 0 };
    int fLen = 1;
    std::atomic<bool> focusing { false };
    Goal goal = Master;
    juce::String focusResult;
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenMonoFilterProcessor)
};
