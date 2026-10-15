#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

//==============================================================================
// Crossover strip with draggable split points; each band shows its live stereo width (in -> out)
class SpaceXover : public juce::Component
{
public:
    explicit SpaceXover (LumenSpaceControlProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    float wIn[4] {}, wOut[4] {};
private:
    float xToF (float x) const;
    float fToX (float f) const;
    int handleAt (float x) const;
    int numBands() const;
    LumenSpaceControlProcessor& proc;
    std::unique_ptr<juce::ParameterAttachment> att[3];
    int drag = -1, hover = -1;
};

//==============================================================================
// Vectorscope (goniometer) + correlation
class Scope : public juce::Component
{
public:
    explicit Scope (LumenSpaceControlProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    LumenSpaceControlProcessor& proc;
    juce::Image trail;
};

//==============================================================================
class LumenSpaceControlEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenSpaceControlEditor (LumenSpaceControlProcessor&);
    ~LumenSpaceControlEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    LumenSpaceControlProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<juce::Component*> bandComps[4];
    std::unique_ptr<SpaceXover> xover;
    std::unique_ptr<Scope> scope;
    juce::TextButton aB { "A" }, bB { "B" }, copyB { "A>B" };
    juce::ComboBox presetBox, goalBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, assistB { "ANALYZE" };
    lumen::led::DotMatrix logo, status;
    float wIn[4] {}, wOut[4] {}, corrSm = 1.0f, bandCorr[4] {};
    int lastProgram = -1, lastBands = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenSpaceControlEditor)
};
