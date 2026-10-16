#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

// scrolling piano-roll: scale lanes, detected pitch (grey), tuned pitch (colour), target notes
class PitchView : public juce::Component
{
public:
    explicit PitchView (LumenVocalProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    LumenVocalProcessor& proc;
    float centre = 60.0f;
};

class LumenVocalEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenVocalEditor (LumenVocalProcessor&);
    ~LumenVocalEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    void timerCallback() override;
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    LumenVocalProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<PitchView> pitch;
    juce::TextButton aB { "A" }, bB { "B" }, copyB { "A>B" };
    juce::ComboBox presetBox, styleBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, assistB { "ANALYZE" };
    lumen::led::DotMatrix logo, noteText, status;
    int lastProgram = -1;
    float deessSm = 0, compSm = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenVocalEditor)
};
