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
class EchoDisplay : public juce::Component
{
public:
    explicit EchoDisplay (LumenDelayProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    LumenDelayProcessor& proc;
};

//==============================================================================
class SidechainPanel : public juce::Component
{
public:
    explicit SidechainPanel (LumenDelayProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    LumenDelayProcessor& proc;
    juce::ComboBox source;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

//==============================================================================
class LumenDelayEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenDelayEditor (LumenDelayProcessor&);
    ~LumenDelayEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void learnClicked();
    void applySuggestion (const lumen::Features&);
    void undoLearn();
    void setParam (const char* id, float value);
    void updateTimeKnob();

    LumenDelayProcessor& proc;
    LumenLookAndFeel lnf;

    std::unique_ptr<Knob> timeKnob, divKnob;
    std::vector<std::unique_ptr<Knob>> knobs;   // laid out in grid after the time knob
    EchoDisplay display;
    SidechainPanel scPanel;

    juce::TextButton learnButton { "LEARN" }, undoButton { "UNDO" }, freezeButton { "FREEZE" }, syncButton { "SYNC" };
    juce::ComboBox modeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment, syncAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;

    juce::Rectangle<int> smartArea;
    juce::String materialText { "Waiting for audio" }, statusText { "Play audio and press LEARN" };
    float trans = 0, lowEnd = 0, bright = 0, learnProgress = 0;
    bool wasLearning = false, lastSync = true;
    std::vector<std::pair<juce::String, float>> undoValues;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenDelayEditor)
};
