#pragma once
#include <JuceHeader.h>
#include "Pattern.h"
#include "BassSynth.h"

class LumenBasslineProcessor : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::AsyncUpdater
{
public:
    LumenBasslineProcessor();
    ~LumenBasslineProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Lumen Bassline"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    // Message-thread API for the editor
    void generateNew();                       // new seed, pushes history
    void regenerateSameSeed();                // knobs changed: reshape with same seed
    void mutatePattern();
    void historyBack();
    void historyForward();
    bool canGoBack() const    { return historyIndex > 0; }
    bool canGoForward() const { return historyIndex < (int) history.size() - 1; }
    void clearLocks();

    lb::Pattern getPattern() const;
    void setPatternFromEditor (const lb::Pattern&);     // manual edit
    uint32_t getSeed() const { return seed; }
    int getPatternVersion() const { return patternVersion.load(); }

    int   pitchForNote (const lb::Note&, bool includeFollow) const;
    float getPlayheadStep() const { return playheadStep.load(); }
    bool  isPlayingNow() const { return playingNow.load(); }
    double getBpm() const { return lastBpm.load(); }
    int   getFollowShift() const { return followShift.load(); }

    juce::File writeMidiFile (const juce::File& target) const;
    juce::File writeTempMidi() const;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;
    lb::GenParams currentGenParams() const;
    void publishPattern (const lb::Pattern&, bool pushHistory);
    BassSynth::Settings currentSynthSettings() const;

    // pattern shared with the audio thread
    mutable juce::SpinLock patternLock;
    lb::Pattern pattern;          // message-thread master (guarded)
    lb::Pattern audioPattern;     // audio-thread copy
    std::atomic<int> patternVersion { 0 };
    int audioPatternVersion = -1;

    std::vector<lb::Pattern> history;
    int historyIndex = -1;
    uint32_t seed = 1;
    juce::Random seedRandom;

    // playback state (audio thread)
    BassSynth synth;
    double internalPpq = 0.0;
    double lastPpqEnd = -1.0;
    int noteOnPitch[lb::maxNotes];
    bool noteSounding[lb::maxNotes];
    int heldInput[128] {};
    std::atomic<float> playheadStep { -1.0f };
    std::atomic<bool> playingNow { false };
    std::atomic<double> lastBpm { 124.0 };
    std::atomic<int> followShift { 0 };
    bool wasPlaying = false;

    std::atomic<bool> pendingRegen { false };
    std::atomic<bool> loadingState { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenBasslineProcessor)
};
