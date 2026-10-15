#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

// scrolling clip view: output level, with the part that was cut drawn in red above the ceiling
class ClipView : public juce::Component
{
public:
    explicit ClipView (LumenClipperProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    LumenClipperProcessor& proc;
};

// transfer curve of the current mode / shape / asymmetry with the live peak
class CurveView : public juce::Component
{
public:
    explicit CurveView (LumenClipperProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    LumenClipperProcessor& proc;
    float dotSm = 0;
};

// multiband strip with draggable split points and per-band clip LEDs
class ClipXover : public juce::Component
{
public:
    explicit ClipXover (LumenClipperProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
private:
    float xToF (float x) const;
    float fToX (float f) const;
    int handleAt (float x) const;
    int numBands() const;
    LumenClipperProcessor& proc;
    std::unique_ptr<juce::ParameterAttachment> att[3];
    int drag = -1, hover = -1;
};

class LumenClipperEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenClipperEditor (LumenClipperProcessor&);
    ~LumenClipperEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    LumenClipperProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<juce::Component*> bandComps[4];
    std::unique_ptr<ClipView> view;
    std::unique_ptr<CurveView> curve;
    std::unique_ptr<ClipXover> xover;
    juce::TextButton aB { "A" }, bB { "B" }, copyB { "A>B" };
    juce::ComboBox presetBox, goalBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, smartB { "ANALYZE" };
    lumen::led::DotMatrix logo, status, grText;
    int lastProgram = -1, lastBands = -1;
    float grSm = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenClipperEditor)
};
