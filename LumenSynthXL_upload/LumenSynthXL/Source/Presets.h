#pragma once
#include "Params.h"

namespace lxl
{
struct FactoryPreset
{
    const char* name;
    const char* category;
    const char* genre;
    const char* macros;   // "Cutoff|Drive|Space|Movement"
    const char* data;     // "param=value;...;m=src:paramId:amount;"
};
extern const FactoryPreset kFactoryPresets[];
extern const int kNumFactoryPresets;

struct PresetEntry
{
    juce::String name, category, genre;
    bool factory = true;
    int factoryIndex = -1;
    juce::File file;
};

class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& s);

    static juce::File userFolder();
    static juce::StringArray categories();
    static juce::StringArray genres();

    void refresh();
    const juce::Array<PresetEntry>& entries() const { return list; }

    void load (int index);
    bool saveUser (const juce::String& name, const juce::String& category, const juce::ValueTree& fullState);
    int currentIndex() const { return current; }
    juce::String currentName() const { return current >= 0 && current < list.size() ? list[current].name : juce::String ("Init"); }
    juce::String currentInfo() const;
    void step (int delta);
    void resetToDefaults();

    /** Applies factory data to the parameters. Returns the macro names. */
    juce::StringArray applyFactory (int factoryIndex);

    std::function<void (const juce::ValueTree&)> applyFullState;
    std::function<void (const juce::StringArray& macroNames)> onFactoryLoaded;
    std::function<void()> onListChanged;

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::Array<PresetEntry> list;
    int current = 0;
};
} // namespace lxl
