#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Parameters.h"
#include "dsp/KickSynth.h"
#include "dsp/KickClone.h"
#include "dsp/DrumSplitter.h"

class LumenKickProcessor : public juce::AudioProcessor, private juce::Thread
{
public:
    LumenKickProcessor();
    ~LumenKickProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Kick Designer"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return juce::isPositiveAndBelow (index, getNumPrograms()) ? factoryPresets()[(size_t) index].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- sample layer ----
    bool loadSample (const juce::File& f, juce::String& error);
    void setSampleBuffer (const juce::AudioBuffer<float>& b, double rate, const juce::String& name, const juce::String& path);
    void clearSample();
    std::shared_ptr<const lumen::kick::SampleData> getSample() const { return synth.getSample(); }
    juce::String cloneFromSample();           // KICK CLONE

    // ---- kick render / audition / export ----
    void audition() { auditionPending.store (true); }
    const juce::AudioBuffer<float>& getRender() { renderIfNeeded(); return render; }
    int renderVersion() const { return renderVer; }
    juce::File exportKick();
    static juce::File userFolder (const juce::String& sub);
    static bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double rate);
    double getLoopRate() const { return loopRate; }

    // ---- drum split ----
    bool startSplit (const juce::File& f, juce::String& error);
    bool isSplitting() const { return splitting.load(); }
    float splitProgress() const { return splitProg.load(); }
    std::shared_ptr<const lumen::split::Result> getSplit() const { return std::atomic_load (&splitRes); }
    const juce::AudioBuffer<float>& getLoop() const { return loop; }
    juce::String getLoopName() const { return loopName; }
    juce::File stemFile (int c) const { return stemFiles[c]; }
    juce::String getSplitMessage() const { return splitMessage; }
    void previewStem (int c) { previewPos.store (0); previewCls.store (c); }
    int previewing() const { return previewCls.load(); }
    void stemToSampleLayer (int c);
    void finishSplitIfReady();               // message thread
    juce::File splitFolder() const { return stemFiles[0].getParentDirectory(); }

    void switchAB (int to);
    void copyAB();
    int getABSlot() const { return abSlot; }
    void setParam (const juce::String& id, float v);
    lumen::kick::Params readParams() const;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> outPeak { 0 };
    std::atomic<int> programVersion { 0 }, lastNote { -1 };

private:
    void run() override;                     // drum split worker
    void renderIfNeeded();
    float P (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    lumen::kick::KickSynth synth;
    juce::AudioFormatManager formats;
    double fs = 48000.0;
    int currentProgram = 0;
    std::atomic<bool> auditionPending { false };
    // render cache
    juce::AudioBuffer<float> render;
    juce::uint64 renderHash = 0; int renderVer = 0;
    const void* renderSamplePtr = nullptr;
    // split
    juce::AudioBuffer<float> loop; double loopRate = 48000; juce::String loopName;
    std::shared_ptr<const lumen::split::Result> splitRes;
    std::atomic<bool> splitting { false }, splitDone { false };
    std::atomic<float> splitProg { 0 };
    juce::File stemFiles[lumen::split::kClasses];
    juce::String splitMessage;
    std::shared_ptr<lumen::split::Result> pendingSplit;
    std::atomic<int> previewCls { -1 }, previewPos { 0 };
    juce::MemoryBlock abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenKickProcessor)
};
