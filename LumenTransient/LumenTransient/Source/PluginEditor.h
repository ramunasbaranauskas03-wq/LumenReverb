#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

class LumenTransientEditor;

//==============================================================================
// Horizontal LED amount bar bound to a parameter (0..200%)
class AmountBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    AmountBar (juce::RangedAudioParameter& p, juce::Colour c);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::Colour colour;
private:
    void setFromX (float x);
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment att;
    float value = 1.0f;
};

//==============================================================================
// Crossover strip: log frequency axis with draggable split points
class XoverView : public juce::Component
{
public:
    explicit XoverView (LumenTransientProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    int selected = 0;
    std::function<void (int)> onSelect;
private:
    float xToF (float x) const;
    float fToX (float f) const;
    int handleAt (float x) const;
    LumenTransientProcessor& proc;
    std::unique_ptr<juce::ParameterAttachment> att[3];
    int drag = -1, hover = -1;
};

//==============================================================================
// One band lane: live waveform (in / out) with the gain curve, solo + bypass, amount bar
class BandLane : public juce::Component
{
public:
    BandLane (LumenTransientProcessor& p, int band);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override { if (onSelect) onSelect (band); }
    bool selected = false, active = true;
    int numBands = 4;
    std::function<void (int)> onSelect;
private:
    LumenTransientProcessor& proc;
    int band;
    std::unique_ptr<lumen::led::Toggle> solo, byp;
    std::unique_ptr<AmountBar> amount;
};

//==============================================================================
class LumenTransientEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenTransientEditor (LumenTransientProcessor&);
    ~LumenTransientEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void select (int b);
    void updateVisibility();
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    LumenTransientProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<XoverView> xover;
    std::unique_ptr<BandLane> lanes[4];
    std::vector<juce::Component*> shapeKnobs[4], charKnobs[4];
    juce::TextButton tabShape { "SHAPE" }, tabChar { "CHARACTER" };
    juce::TextButton aB { "A" }, bB { "B" }, copyB { "A>B" };
    juce::ComboBox presetBox, goalBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, learnB { "LEARN" };
    lumen::led::DotMatrix logo, status, bandTitle;
    int sel = 0, tab = 0, lastProgram = -1, lastBands = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenTransientEditor)
};
