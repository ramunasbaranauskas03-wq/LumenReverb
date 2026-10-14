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
class CompDisplay : public juce::Component
{
public:
    explicit CompDisplay (LumenCompressorProcessor&);
    void paint (juce::Graphics&) override;

private:
    void paintCurve (juce::Graphics&, juce::Rectangle<float>);
    void paintHistory (juce::Graphics&, juce::Rectangle<float>);
    void paintMeters (juce::Graphics&, juce::Rectangle<float>);

    LumenCompressorProcessor& proc;
    lumen::CompressorEngine curve;   // UI-side copy, used only for the static transfer curve
};

//==============================================================================
class SidechainPanel : public juce::Component
{
public:
    explicit SidechainPanel (LumenCompressorProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    LumenCompressorProcessor& proc;
    juce::ComboBox source;
    juce::TextButton listen { "LISTEN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAtt;
};

//==============================================================================
// Kick <-> bass phase alignment controls: ON, ALIGN (auto analysis) and status.
class PhasePanel : public juce::Component
{
public:
    explicit PhasePanel (LumenCompressorProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void poll();   // called from the editor timer

private:
    void setParam (const char* id, float value);

    LumenCompressorProcessor& proc;
    juce::TextButton onButton { "ON" }, alignButton { "ALIGN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    juce::String status { "Kick to sidechain, bass on this track" };
    bool wasCapturing = false;
};

//==============================================================================
class CorrelationMeter : public juce::Component
{
public:
    explicit CorrelationMeter (LumenCompressorProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    LumenCompressorProcessor& proc;
    float before = 0.0f, after = 0.0f;
};

//==============================================================================
class LumenCompressorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenCompressorEditor (LumenCompressorProcessor&);
    ~LumenCompressorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void learnClicked();
    void applySuggestion (const lumen::Features&);
    void undoLearn();
    void setParam (const char* id, float value);

    LumenCompressorProcessor& proc;
    LumenLookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    CompDisplay display;
    SidechainPanel scPanel;
    PhasePanel phasePanel;
    CorrelationMeter corrMeter;

    juce::TextButton learnButton { "LEARN" }, undoButton { "UNDO" }, autoRelButton { "AUTO REL" }, autoGainButton { "AUTO GAIN" };
    juce::ComboBox styleBox, channelBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoRelAtt, autoGainAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAtt, channelAtt;

    juce::Rectangle<int> smartArea;
    juce::String materialText { "Waiting for audio" }, statusText { "Play audio and press LEARN" };
    float trans = 0, lowEnd = 0, bright = 0, learnProgress = 0;
    bool wasLearning = false;
    std::vector<std::pair<juce::String, float>> undoValues;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenCompressorEditor)
};
