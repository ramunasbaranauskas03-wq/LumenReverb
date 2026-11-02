#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Hub.h"

namespace lumen::spec::ids
{
inline constexpr const char* role = "role";
inline constexpr const char* colour = "colour";
inline constexpr const char* view = "view";
inline constexpr const char* barRes = "barRes";
inline constexpr const char* barColour = "barColour";
inline constexpr const char* fft = "fft";
inline constexpr const char* avg = "avg";
inline constexpr const char* chan = "chan";
inline constexpr const char* slope = "slope";
inline constexpr const char* range = "range";
inline constexpr const char* peak = "peak";
inline constexpr const char* others = "others";
inline constexpr const char* target = "target";
inline constexpr const char* smooth = "smooth";
}

class LumenSpectrumProcessor : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::AsyncUpdater
{
public:
    LumenSpectrumProcessor();
    ~LumenSpectrumProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lumen Spectrum"; }
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
    void updateTrackProperties (const TrackProperties& properties) override;

    // identity
    int getSlotIndex() const { return slotIndex; }
    juce::String getTrackName() const { return trackName; }
    void setTrackName (const juce::String& n, bool fromUser);
    void setRoleFromUser (int r);
    void refreshIdentity();

    // reference snapshot (stored with the project)
    void captureSnapshot();
    void clearSnapshot() { hasSnap.store (false); }
    bool hasSnapshot() const { return hasSnap.load(); }
    const std::array<float, lumen::spec::kPoints>& getSnapshot() const { return snap; }

    juce::SharedResourcePointer<lumen::spec::Hub> hub;
    juce::AudioProcessorValueTreeState apvts;
    int editorW = 1220, editorH = 740;

private:
    void parameterChanged (const juce::String& id, float) override;
    void pushGlobals();
    void handleAsyncUpdate() override { refreshIdentity(); }

    int slotIndex = -1;
    juce::String trackName;
    bool nameFromUser = false, roleFromUser = false, autoRoleChange = false;
    juce::Colour hostColour { 0 };
    std::array<float, lumen::spec::kPoints> snap {};
    std::atomic<bool> hasSnap { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenSpectrumProcessor)
};
