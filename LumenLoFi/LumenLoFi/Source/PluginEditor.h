#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

class LumenLoFiEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenLoFiEditor (LumenLoFiProcessor&);
    ~LumenLoFiEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    void timerCallback() override;
    void drawCassette (juce::Graphics&, juce::Rectangle<float>);
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    LumenLoFiProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    juce::TextButton modelB[12], diceB { "DICE" };
    juce::TextButton aB { "A" }, bB { "B" }, copyB { "A>B" }, prevB { "<" }, nextB { ">" };
    juce::ComboBox presetBox;
    lumen::led::DotMatrix logo;
    float inPk = 0, outPk = 0, reelAngle = 0, dropVis = 0;
    int lastProgram = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenLoFiEditor)
};
