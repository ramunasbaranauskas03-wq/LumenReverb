#pragma once
#include <JuceHeader.h>
#include "ReverseEngine.h"

class LumenReverseProcessor : public juce::AudioProcessor,
                              private juce::Timer
{
public:
    LumenReverseProcessor();
    ~LumenReverseProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Lumen Reverse"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---------------- public API for the editor ----------------
    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;

    enum CaptureState { Idle = 0, Armed, Recording, Done };
    void armCapture();
    void stopCapture();
    void cancelCapture();
    int  getCaptureState() const { return captureState.load(); }
    float getCaptureDisplay (int col) const { return captureDisplay[(size_t) col].load (std::memory_order_relaxed); }
    static constexpr int captureCols = 600;
    float getCaptureProgress() const;

    bool loadAudioFile (const juce::File& f);
    bool hasSource() const { return sourceAvailable.load(); }
    void clearSource();

    void triggerPreview();
    void stopPreview();
    bool isVoicePlaying() const { return voiceActive.load(); }
    std::atomic<bool> loopPreview { false };

    // display snapshot (message thread)
    struct DisplayData
    {
        std::vector<float> peaks, envelope;
        int swellLen = 0, srcLen = 0, version = -1;
        double sampleRate = 48000.0;
    };
    bool getDisplayData (DisplayData& d, int knownVersion);
    float getPlayheadFraction() const { return playheadFrac.load(); }
    bool isRendering() const { return renderPending.load() || renderBusy.load(); }

    juce::File exportAudio (bool forDrag);
    juce::String getLengthDescription() const;
    double getHostBpm() const { return hostBpm.load(); }

    // presets / A-B
    static const juce::StringArray& getPresetNames();
    void loadPreset (int index);
    int  currentPreset = 0;
    void switchAB (int slot);
    void copyAtoB();
    int  getABSlot() const { return abSlot; }

    static const juce::StringArray lengthNames, rateNames;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void requestRender();
    void finishCapture();
    lumen::RenderSettings makeSettings() const;
    std::vector<float> snapshotRenderParams() const;
    int swellSamplesFor (double sr, double bpm, double bpb) const;
    float paramValue (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    void applyTremWidth (juce::AudioBuffer<float>& b, int swellLen, double bpm) const;
    float tremGain (double beatPos, float frac, int ch) const;

    // render thread
    struct RenderThread : juce::Thread
    {
        RenderThread (LumenReverseProcessor& p) : juce::Thread ("LumenReverse Render"), owner (p) {}
        void run() override;
        LumenReverseProcessor& owner;
    };
    RenderThread renderThread { *this };
    std::atomic<bool> renderPending { false }, renderBusy { false };
    juce::CriticalSection settingsLock;
    lumen::RenderSettings pendingSettings;

    juce::CriticalSection sourceLock;
    juce::AudioBuffer<float> source;     // captured / loaded phrase
    double sourceRate = 48000.0;
    std::atomic<bool> sourceAvailable { false };

    juce::SpinLock resultLock;
    std::unique_ptr<lumen::RenderResult> result;
    int renderVersion = 0;

    // capture (audio thread writes)
    juce::AudioBuffer<float> captureBuf;
    std::atomic<int> captureState { Idle }, captureLen { 0 };
    std::atomic<bool> stopRequest { false };
    int captureMax = 0, silenceCount = 0, hostCaptureTarget = 0;
    double lastCapturePpq = -1.0;
    std::array<std::atomic<float>, captureCols> captureDisplay;

    // playback
    std::atomic<bool> voiceActive { false }, previewRequest { false }, previewStop { false };
    double voicePos = 0.0, voiceRate = 1.0;
    float voiceGain = 1.0f;
    bool voiceIsPreview = false;
    double freeBeat = 0.0;
    std::atomic<float> playheadFrac { -1.0f };
    juce::AudioBuffer<float> wetScratch;

    double currentSR = 48000.0;
    std::atomic<double> hostBpm { 120.0 }, hostBpb { 4.0 };

    std::vector<float> lastSnapshot;
    double lastChangeTime = 0.0;
    bool changeDirty = false;

    juce::ValueTree abState[2];
    int abSlot = 0;
    juce::AudioFormatManager formats;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenReverseProcessor)
};
