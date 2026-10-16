#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

// one band row: level meter with draggable upward (left) and downward (right) thresholds
class BandMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    BandMeter (LumenOTTProcessor& p, int band, juce::Colour c);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    float level = 0, up = 0, down = 0;     // smoothed by the editor
private:
    float xOf (float db) const { return 4.0f + (db + 80.0f) / 80.0f * ((float) getWidth() - 8.0f); }
    float dbOf (float x) const { return (x - 4.0f) / ((float) getWidth() - 8.0f) * 80.0f - 80.0f; }
    LumenOTTProcessor& proc;
    int band;
    juce::Colour colour;
    std::unique_ptr<juce::ParameterAttachment> aDown, aUp;
    int drag = -1;
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
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    LumenOTTProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<BandMeter> meters[3];
    juce::TextButton aB { "A" }, bB { "B" }, copyB { "A>B" };
    juce::ComboBox presetBox, srcBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, assistB { "ANALYZE" };
    lumen::led::DotMatrix logo, status;
    int lastProgram = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenOTTEditor)
};
