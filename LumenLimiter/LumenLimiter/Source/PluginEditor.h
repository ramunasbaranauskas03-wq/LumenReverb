#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace Colours_
{
    const juce::Colour bg        { 0xff0d1016 };
    const juce::Colour bg2       { 0xff141a25 };
    const juce::Colour panel     { 0xff171e2b };
    const juce::Colour panelEdge { 0xff252e40 };
    const juce::Colour track     { 0xff2a3347 };
    const juce::Colour cyan      { 0xff5ce1e6 };
    const juce::Colour violet    { 0xff9b7bff };
    const juce::Colour amber     { 0xffffb35c };
    const juce::Colour text      { 0xffe6e9f0 };
    const juce::Colour dim       { 0xff7c8597 };
}

//==============================================================================
class LumenLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LumenLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
};

//==============================================================================
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& title);
    void resized() override;

private:
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
class LimiterDisplay : public juce::Component
{
public:
    explicit LimiterDisplay (LumenLimiterProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    LumenLimiterProcessor& proc;
};

//==============================================================================
// One card per platform: target, how much is missing / over, true-peak check. Click selects it.
class TargetStrip : public juce::Component
{
public:
    explicit TargetStrip (LumenLimiterProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void (int)> onSelect;
private:
    juce::Rectangle<float> cardBounds (int i) const;
    LumenLimiterProcessor& proc;
};

//==============================================================================
class LumenLimiterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenLimiterEditor (LumenLimiterProcessor&);
    ~LumenLimiterEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void startMatch();
    void stepMatch();
    void selectTarget (int index);
    void setParam (const char* id, float value);
    float getParam (const char* id) const;

    LumenLimiterProcessor& proc;
    LumenLookAndFeel lnf;
    std::vector<std::unique_ptr<Knob>> knobs;
    LimiterDisplay display;
    TargetStrip strip;

    juce::TextButton matchButton { "MATCH TARGET" }, resetButton { "RESET" }, autoRelButton { "AUTO REL" },
                     clipButton { "CLIPPER" }, deltaButton { "DELTA" }, gainMatchButton { "GAIN MATCH" };
    juce::ComboBox styleBox, targetBox, ditherBox, osBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoRelAtt, clipAtt, deltaAtt, gmAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAtt, targetAtt, ditherAtt, osAtt;

    juce::Rectangle<int> loudArea, clipCell, monitorCell;
    juce::String statusText { "Play the loudest part, then MATCH TARGET" };
    bool matching = false;
    int matchTicks = 0, stableCount = 0, matchElapsed = 0;
    float grAcc = 0.0f; int grCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenLimiterEditor)
};

// Loudness used for "how far from target": integrated once there is enough of it, else short-term.
inline float currentLoudness (const LumenLimiterProcessor& p)
{
    const float I = p.integrated.load(), S = p.shortTerm.load();
    return I > -99.0f ? I : S;
}
