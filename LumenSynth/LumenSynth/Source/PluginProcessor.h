#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <unordered_map>
#include <string>
#include "Parameters.h"
#include "Presets.h"
#include "Arp.h"
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
    int getCurrentProgram() override { return juce::jmax (0, currentPreset); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---------------- presets (message thread) ----------------
    struct PresetEntry { juce::String name, category; bool user = false; int factoryIndex = -1; juce::File file; };
    std::vector<PresetEntry> listPresets() const;
    void loadPreset (int factoryIndex);
    bool loadPresetEntry (const PresetEntry&);
    bool saveUserPreset (const juce::String& name, const juce::String& category);
    bool deleteUserPreset (const PresetEntry&);
    static juce::File presetFolder();
    bool isFavourite (const PresetEntry&) const;
    void toggleFavourite (const PresetEntry&);
    juce::String getPresetName() const { return presetName; }
    juce::String getPresetCategory() const { return presetCategory; }
    bool isUserPreset() const { return currentPreset < 0; }

    // ---------------- smart ----------------
    void generate (int category, juce::Random& rnd);
    void mutate (float amount, juce::Random& rnd);
    bool undo();
    bool canUndo() const { return ! undoStack.empty(); }

    // ---------------- wavetables ----------------
    bool loadWavetableFile (int osc, const juce::File& f, juce::String& error);
    juce::String userTableName (int osc) const;
    const lumen::Table* tableFor (int osc) const;      // what the oscillator currently plays (UI)

    // ---------------- LFO curves ----------------
    std::vector<lumen::CurvePoint> getLfoPoints (int lfo) const;
    void setLfoPoints (int lfo, std::vector<lumen::CurvePoint> pts);
    void setLfoPointsFromShape (int lfo, int shape);

    // ---------------- FX order ----------------
    std::array<int, lumen::synth::kNumFx> getFxOrder() const;
    void moveFx (int slot, int delta);

    // ---------------- modulation ----------------
    int assignMod (int src, int dst, float amount = 0.35f);
    void clearModSlot (int slot);
    void setModAmount (int slot, float amt);
    struct ModInfo { int slot, src; float amt; };
    std::vector<ModInfo> modsForDest (int dst) const;
    float modSourceValue (int src) const { return src >= 0 && src < lumen::synth::kNumSrc ? srcView[src].load() : 0.0f; }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    // ---------------- meters for the LED UI ----------------
    std::atomic<float> peakL { 0 }, peakR { 0 }, pumpGain { 1 }, compGr { 0 };
    std::atomic<float> voiceLevel[16];
    std::atomic<float> ottBand[3];
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> hostBpm { 120.0f };
    std::atomic<float> lfoPhase[lumen::synth::kLfo];
    std::atomic<float> srcView[lumen::synth::kNumSrc];
    std::atomic<int> arpStepFlash { 0 };
    static constexpr int kScope = 2048;
    float scope[kScope] {};
    std::atomic<int> scopePos { 0 };
    std::atomic<int> presetVersion { 0 };
    std::atomic<int> tableVersion { 0 };
    std::atomic<int> orderVersion { 0 };

private:
    void fillPatch();
    void handleMidi (const juce::MidiMessage& m);
    void pushUndo();
    void applyValues (const std::vector<std::pair<juce::String, float>>& values, bool resetOthers);
    void renderSynth (float* L, float* R, int n, juce::MidiBuffer& midi, int midiOffset, int factor);
    void resetExtras();
    void writeExtras (juce::ValueTree& vt) const;
    void readExtras (const juce::ValueTree& vt);
    void setUserTable (int osc, std::shared_ptr<lumen::Table> t);
    void rebuildLut (int lfo);
    juce::File favouritesFile() const { return presetFolder().getChildFile ("favourites.txt"); }
    juce::String favKey (const PresetEntry&) const;

    std::unordered_map<std::string, std::atomic<float>*> raw;
    inline float P (const char* id) const { return raw.at (id)->load (std::memory_order_relaxed); }

    lumen::synth::Engine engine;
    lumen::synth::FxChain fx;
    lumen::synth::Decimator2 decim;
    lumen::synth::Arp arp;
    lumen::synth::Patch patch;
    lumen::synth::FxP fxp;
    double baseRate = 48000.0;
    int engineFactor = 1;
    std::vector<float> osL, osR;
    static constexpr int kChunk = 512;

    // user wavetables
    std::shared_ptr<lumen::Table> userTables[lumen::synth::kOsc];
    std::atomic<const lumen::Table*> userTablePtr[lumen::synth::kOsc];
    std::vector<std::shared_ptr<lumen::Table>> retired;

    // LFO curves (double-buffered lookup tables)
    std::vector<lumen::CurvePoint> lfoPts[lumen::synth::kLfo];
    float lut[lumen::synth::kLfo][2][lumen::synth::kLut + 1] {};
    std::atomic<int> lutActive[lumen::synth::kLfo];
    mutable juce::CriticalSection extrasLock;

    std::atomic<int> fxOrder[lumen::synth::kNumFx];

    int currentPreset = 0;
    juce::String presetName { "Init Saw" }, presetCategory { "Lead" };
    std::vector<juce::MemoryBlock> undoStack;
    juce::StringArray favourites;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenSynthProcessor)
};
