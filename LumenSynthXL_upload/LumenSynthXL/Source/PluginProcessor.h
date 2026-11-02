#pragma once
#include "Engine.h"
#include "Presets.h"

class LumenSynthXLProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    LumenSynthXLProcessor();
    ~LumenSynthXLProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Lumen Synth XL"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- wavetables
    juce::String importWavetable (int osc, const juce::File& file);
    const lxl::Wavetable* currentTable (int osc) const { return engine.tables[osc].load(); }
    void refreshTables();

    // ---- modulation helpers for the UI
    /** Adds (or updates) a matrix row src -> dest. Returns the row or -1 if the matrix is full. */
    int assignModulation (int src, int destIndex, float amount);
    void removeModulation (int destIndex, int src = -1);
    juce::Array<int> rowsForDest (int destIndex) const;

    // ---- presets / mutation
    juce::ValueTree fullState();
    void applyFullState (const juce::ValueTree& tree);
    void mutate (float amount);

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    lxl::Engine engine;
    lxl::PresetManager presets;
    juce::MidiKeyboardState keyboardState;

    /** Per-preset macro labels (shown under the macro knobs). */
    juce::String macroName (int m) const { const juce::ScopedLock sl (nameLock); return m < macroNames.size() && macroNames[m].isNotEmpty() ? macroNames[m] : "Macro " + juce::String (m + 1); }
    void setMacroNames (const juce::StringArray& names) { const juce::ScopedLock sl (nameLock); macroNames = names; }

private:
    void timerCallback() override;
    const lxl::Wavetable* factoryTable (int index);
    float paramValue (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    juce::CriticalSection tableLock;
    std::vector<std::unique_ptr<lxl::Wavetable>> tableStore;
    std::map<int, const lxl::Wavetable*> factoryCache;
    const lxl::Wavetable* imported[lxl::kNumOsc] { nullptr, nullptr };
    int loadedChoice[lxl::kNumOsc] { -1, -1 };
    const lxl::Wavetable* loadedPtr[lxl::kNumOsc] { nullptr, nullptr };
    juce::Random random;
    juce::StringArray macroNames;
    juce::CriticalSection nameLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenSynthXLProcessor)
};
