#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

// lanes x steps grid: click / drag paints steps, drag the chance bar in the lane header, double-click a lane name to clear it
class StepGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StepGrid (LumenGlitchProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    static constexpr int kLabelW = 150;
private:
    int steps() const { return proc.apvts.getRawParameterValue (IDs::steps)->load() > 0.5f ? 32 : 16; }
    float rowH() const { return (float) getHeight() / (float) lumen::glitch::kLanes; }
    float cellW() const { return (float) (getWidth() - kLabelW) / (float) steps(); }
    void paintAt (juce::Point<float> p);
    LumenGlitchProcessor& proc;
    int dragLane = -1, chanceLane = -1; bool paintOn = true; int lastCell = -1;
};

class LumenGlitchEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenGlitchEditor (LumenGlitchProcessor&);
    ~LumenGlitchEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    void timerCallback() override;
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    LumenGlitchProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<StepGrid> grid;
    juce::TextButton slotB[8], clearB { "CLEAR" }, genB { "GENERATE" };
    juce::ComboBox copyBox, styleBox, presetBox;
    juce::TextButton aB { "A" }, bB { "B" }, copyB { "A>B" }, prevB { "<" }, nextB { ">" };
    lumen::led::DotMatrix logo, status;
    float inPk = 0, outPk = 0;
    int lastProgram = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenGlitchEditor)
};
