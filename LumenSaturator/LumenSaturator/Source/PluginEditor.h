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
class SatDisplay : public juce::Component
{
public:
    explicit SatDisplay (LumenSaturatorProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    LumenSaturatorProcessor& proc;
};

//==============================================================================
class LumenSaturatorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenSaturatorEditor (LumenSaturatorProcessor&);
    ~LumenSaturatorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void learnClicked();
    void applySuggestion (const lumen::Features&);
    void undoLearn();
    void setParam (const juce::String& id, float v);
    float getParam (const juce::String& id) const;

    LumenSaturatorProcessor& proc;
    LumenLookAndFeel lnf;
    std::vector<std::unique_ptr<Knob>> knobs;   // [band*3 + {drive,mix,level}], then xLow, xHigh, output
    SatDisplay display;
    juce::ComboBox modeBox[3], osBox;
    juce::TextButton soloButton[3], autoGainButton { "AUTO GAIN" }, learnButton { "LEARN" }, undoButton { "UNDO" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt[3], osAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAtt[3], autoAtt;
    juce::Rectangle<int> smartArea, modeCell[3];
    juce::String headline { "Ready" }, statusText { "Play audio and press LEARN" };
    bool wasLearning = false;
    std::vector<std::pair<juce::String, float>> undoValues;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenSaturatorEditor)
};
