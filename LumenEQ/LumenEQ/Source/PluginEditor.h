#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
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
class EqDisplay : public juce::Component
{
public:
    explicit EqDisplay (LumenEQProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void pull();                                   // read FIFOs + FFT (timer)

    int selected = 0;
    std::function<void (int)> onSelect;
    std::vector<lumen::MaskZone> zones;

private:
    juce::Rectangle<float> plot() const;
    float xOf (float f) const;
    float fOf (float x) const;
    float yOfEq (float db) const;
    float dbOfY (float y) const;
    float yOfSpec (float db) const;
    void setParam (const juce::String& id, float v);
    void readFifo (juce::AbstractFifo&, std::vector<float>& src, std::vector<float>& ring, int& ringPos);
    void analyse (const std::vector<float>& ring, int ringPos, std::vector<float>& outDb);

    LumenEQProcessor& proc;
    static constexpr int kOrder = 12, kN = 1 << kOrder;
    juce::dsp::FFT fft { kOrder };
    std::vector<float> window, work, ringT, ringS, specT, specS;
    int posT = 0, posS = 0;
};

//==============================================================================
class LumenEQEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenEQEditor (LumenEQProcessor&);
    ~LumenEQEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void selectBand (int b);
    void unmaskClicked();
    void applyZones (const std::vector<lumen::MaskZone>&);
    void undo();
    void setParam (const juce::String& id, float v);
    float getParam (const juce::String& id) const;

    LumenEQProcessor& proc;
    LumenLookAndFeel lnf;
    EqDisplay display;
    juce::TextButton tab[6], onBtn[6], unmaskButton { "UNMASK" }, undoButton { "UNDO" };
    juce::ComboBox typeBox[6], scBox[6];
    std::unique_ptr<Knob> bandKnob[6][5];
    std::vector<std::unique_ptr<Knob>> globalKnobs;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt[6];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAtt[6], scAtt[6];
    juce::Rectangle<int> smartArea;
    juce::String headline { "Ready" }, statusText { "Kick on the sidechain, bass on this track, then UNMASK" };
    bool wasCapturing = false;
    std::vector<std::pair<juce::String, float>> undoValues;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenEQEditor)
};
