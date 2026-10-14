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
class CurveEditor : public juce::Component
{
public:
    explicit CurveEditor (LumenPumpProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen (lumen::CurvePoint) const;
    lumen::CurvePoint fromScreen (juce::Point<float>) const;
    int findPoint (juce::Point<float>) const;

    LumenPumpProcessor& proc;
    std::vector<lumen::CurvePoint> pts;
    int dragIndex = -1, hoverIndex = -1;
};

//==============================================================================
class BandView : public juce::Component
{
public:
    explicit BandView (LumenPumpProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    LumenPumpProcessor& proc;
    juce::TextButton solo { "SOLO BAND" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAtt;
};

//==============================================================================
class LumenPumpEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenPumpEditor (LumenPumpProcessor&);
    ~LumenPumpEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void learnClicked();
    void applyKick (const lumen::KickInfo&);
    void undoLearn();
    void setParam (const char* id, float value);
    float getParam (const char* id) const;

    LumenPumpProcessor& proc;
    LumenLookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    CurveEditor curve;
    BandView band;

    juce::TextButton learnButton { "LEARN KICK" }, undoButton { "UNDO" };
    juce::ComboBox triggerBox, rateBox, presetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> triggerAtt, rateAtt;

    juce::Rectangle<int> smartArea;
    juce::String headline { "Ready" }, statusText { "Play the kick and press LEARN KICK" };
    bool wasCapturing = false;

    std::vector<lumen::CurvePoint> undoPoints;
    std::vector<std::pair<juce::String, float>> undoValues;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenPumpEditor)
};
