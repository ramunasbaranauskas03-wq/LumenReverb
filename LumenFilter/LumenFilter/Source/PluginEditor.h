#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

class LumenFilterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenFilterEditor (LumenFilterProcessor&);
    ~LumenFilterEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    LumenFilterProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    std::vector<std::unique_ptr<juce::Component>> owned;
    lumen::led::Knob* rateK = nullptr;
    lumen::led::Combo* divC = nullptr;
    juce::ComboBox presetBox;
    juce::TextButton prevB { "<" }, nextB { ">" };
    lumen::led::DotMatrix logo, noteMatrix;
    float outM = 0, inM = 0, lfoTrail[64] {};
    int trailPos = 0, lastProgram = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenFilterEditor)
};
