#pragma once
#include "PluginProcessor.h"
#include "StudioUi.h"

// one band: level bar with the upward (green) and downward (red) zones, draggable thresholds and the live gain arrow
class BandMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    BandMeter (LumenOTTProcessor& p, int band, juce::Colour c);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    float level = -100, gain = 0, off = 0;     // smoothed by the editor
private:
    float xOf (float db) const { return 6.0f + (db + 72.0f) / 78.0f * ((float) getWidth() - 12.0f); }
    float dbOf (float x) const { return (x - 6.0f) / ((float) getWidth() - 12.0f) * 78.0f - 72.0f; }
    LumenOTTProcessor& proc; int band; juce::Colour colour;
    int drag = -1, hover = -1;
};

class LumenOTTEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenOTTEditor (LumenOTTProcessor&);
    ~LumenOTTEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
private:
    void timerCallback() override;
    float pv (const juce::String& id) const { return proc.P (id); }
    using Knob = lumen::led::Knob; using Toggle = lumen::led::Toggle; using Combo = lumen::led::Combo;
    Knob& knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip = false);
    Toggle& toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r);
    LumenOTTProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<BandMeter> meters[3];
    juce::ComboBox presetBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, abA { "A" }, abB { "B" }, abCopy { "COPY" };
    float inM = 0, outM = 0;
    int lastProgram = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenOTTEditor)
};
