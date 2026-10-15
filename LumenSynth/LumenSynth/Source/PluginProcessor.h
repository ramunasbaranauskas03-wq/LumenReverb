#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <unordered_map>
#include <string>
#include "Parameters.h"
#include "Presets.h"
#include "dsp/SynthEngine.h"
#include "dsp/SynthFx.h"

class LumenSynthProcessor : public juce::AudioProcessor
{
public:
    LumenSynthProcessor();
    ~LumenSynthProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Lumen Synth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- Smart / preset API (message thread) ----
    void loadPreset (int index);
    void generate (int category, juce::Random& rnd);
    void mutate (float amount, juce::Random& rnd);
    bool undo();
    bool canUndo() const { return ! undoStack.empty(); }
    juce::String getPresetName() const { return presetName; }
    juce::String getPresetCategory() const { return presetCategory; }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    // ---- meters for the LED UI ----
    std::atomic<float> peakL { 0 }, peakR { 0 }, pumpGain { 1 };
    std::atomic<float> voiceLevel[16];
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> hostBpm { 120.0f };
    static constexpr int kScope = 2048;
    float scope[kScope] {};
    std::atomic<int> scopePos { 0 };
    std::atomic<int> presetVersion { 0 };   // bumps whenever the patch name changes (UI refresh)

private:
    void fillPatch();
    void handleMidi (const juce::MidiMessage& m);
    void pushUndo();
    void applyValues (const std::vector<std::pair<juce::String, float>>& values, bool resetOthers);

    std::unordered_map<std::string, std::atomic<float>*> raw;
    inline float P (const char* id) const { return raw.at (id)->load (std::memory_order_relaxed); }

    lumen::synth::Engine engine;
    lumen::synth::FxChain fx;
    lumen::synth::Patch patch;
    lumen::synth::FxP fxp;

    int currentPreset = 0;
    juce::String presetName { "Init Saw" }, presetCategory { "Lead" };
    std::vector<std::vector<float>> undoStack;
    struct UndoNames { juce::String name, cat; };
    std::vector<UndoNames> undoNames;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenSynthProcessor)
};
