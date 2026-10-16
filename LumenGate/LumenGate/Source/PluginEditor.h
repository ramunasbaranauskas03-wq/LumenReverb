#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

// scrolling key level + gain display, threshold (and hysteresis) draggable
class GateView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit GateView (LumenGateProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    float yOf (float db) const { return 6.0f + (-db / 80.0f) * ((float) getHeight() - 12.0f); }
    float dbOf (float y) const { return -(y - 6.0f) / ((float) getHeight() - 12.0f) * 80.0f; }
    LumenGateProcessor& proc;
    std::unique_ptr<juce::ParameterAttachment> thrAtt;
};

// step sequencer editor: click toggles a step, drag sets its level
class StepGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StepGrid (LumenGateProcessor& p) : proc (p) { setTooltip ("Click a step to switch it on/off, drag up/down to set its level"); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
private:
    int stepAt (float x) const;
    LumenGateProcessor& proc;
    int dragStep = -1;
    bool dragged = false;
    float downLevel = 0;
};

class LumenGateEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenGateEditor (LumenGateProcessor&);
    ~LumenGateEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    void timerCallback() override;
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    LumenGateProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<GateView> view;
    std::unique_ptr<StepGrid> grid;
    juce::TextButton modeB[3], styleB[5];
    juce::TextButton toolB[6];
    juce::TextButton aB { "A" }, bB { "B" }, copyB { "A>B" };
    juce::ComboBox presetBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, learnB { "AUTO THRESHOLD" };
    lumen::led::DotMatrix logo, status;
    int lastProgram = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenGateEditor)
};
