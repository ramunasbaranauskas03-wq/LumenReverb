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
class DecayDisplay : public juce::Component
{
public:
    explicit DecayDisplay (LumenAudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    LumenAudioProcessor& proc;
};

//==============================================================================
class LumenAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenAudioProcessorEditor (LumenAudioProcessor&);
    ~LumenAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void learnClicked();
    void applySuggestion (const lumen::Features&);
    void undoLearn();
    void setParam (const char* id, float value);

    LumenAudioProcessor& proc;
    LumenLookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    DecayDisplay display;

    juce::TextButton learnButton { "LEARN" }, undoButton { "UNDO" }, freezeButton { "FREEZE" };
    juce::ComboBox syncBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> syncAttachment;

    juce::Rectangle<int> smartArea;
    juce::String materialText { "Waiting for audio" }, statusText { "Play audio and press LEARN" };
    float trans = 0, lowEnd = 0, bright = 0, learnProgress = 0;
    bool wasLearning = false;
    std::vector<std::pair<juce::String, float>> undoValues;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenAudioProcessorEditor)
};
