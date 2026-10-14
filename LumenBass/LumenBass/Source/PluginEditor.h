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
// Kick <-> bass phase alignment controls: ON, ALIGN (auto analysis) and status.
class PhasePanel : public juce::Component
{
public:
    explicit PhasePanel (LumenBassProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void poll();   // called from the editor timer

private:
    void setParam (const char* id, float value);

    LumenBassProcessor& proc;
    juce::TextButton onButton { "ON" }, alignButton { "ALIGN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    juce::String status { "Kick to sidechain, bass on this track" };
    bool wasCapturing = false;
};

//==============================================================================
class CorrelationMeter : public juce::Component
{
public:
    explicit CorrelationMeter (LumenBassProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    LumenBassProcessor& proc;
    float before = 0.0f, after = 0.0f;
};

//==============================================================================
class BassDisplay : public juce::Component
{
public:
    explicit BassDisplay (LumenBassProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    LumenBassProcessor& proc;
    float cIn = 0, cOut = 0, pIn = 0, pOut = 0;
};

//==============================================================================
class LumenBassEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenBassEditor (LumenBassProcessor&);
    ~LumenBassEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void learnClicked();
    void applyInfo (const lumen::BassInfo&);
    void undoLearn();
    void setParam (const char* id, float v);
    float getParam (const char* id) const;

    LumenBassProcessor& proc;
    LumenLookAndFeel lnf;
    std::vector<std::unique_ptr<Knob>> knobs;
    BassDisplay display;
    PhasePanel phasePanel;
    CorrelationMeter corrMeter;
    juce::TextButton learnButton { "LEARN BASS" }, undoButton { "UNDO" }, monoButton { "MONO BASS" }, listenButton { "LISTEN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoAtt, listenAtt;
    juce::Rectangle<int> smartArea, monoCell, listenCell;
    juce::String headline { "Ready" }, statusText { "Play the bass and press LEARN BASS" };
    bool wasCapturing = false;
    std::vector<std::pair<juce::String, float>> undoValues;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenBassEditor)
};
