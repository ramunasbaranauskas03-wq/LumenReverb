#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

class LumenModEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenModEditor (LumenModProcessor&);
    ~LumenModEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    void timerCallback() override;
    void drawDisplay (juce::Graphics&, juce::Rectangle<float>);
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    LumenModProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    juce::TextButton modeB[7], varB[4];
    juce::TextButton aB { "A" }, bB { "B" }, copyB { "A>B" };
    juce::ComboBox presetBox;
    juce::TextButton prevB { "<" }, nextB { ">" };
    juce::Component* voicesKnob = nullptr;
    lumen::led::DotMatrix logo, modeText;
    int lastProgram = -1, lastMode = -1;
    float trail[8][96] {};
    int trailPos = 0;
    float corrSm = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenModEditor)
};
