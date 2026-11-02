#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace lbui
{
namespace Pal
{
    const juce::Colour bg       { 0xff0d0f13 };
    const juce::Colour panel    { 0xff161920 };
    const juce::Colour panelHi  { 0xff1e222b };
    const juce::Colour line     { 0xff2a2f3a };
    const juce::Colour text     { 0xffe8eaf0 };
    const juce::Colour dim      { 0xff8a90a0 };
    const juce::Colour amber    { 0xffffb547 };
    const juce::Colour coral    { 0xffff6b5a };
    const juce::Colour teal     { 0xff3ddbd9 };
}

class LumenLnf : public juce::LookAndFeel_V4
{
public:
    LumenLnf();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
};

//==============================================================================
class PianoRoll : public juce::Component
{
public:
    explicit PianoRoll (LumenBasslineProcessor& p) : proc (p) {}
    void refresh (bool force = false);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> gridArea() const;
    juce::Rectangle<float> noteRect (const lb::Note&) const;
    int  hitNote (juce::Point<float>) const;
    int  stepAt (float x) const;
    int  degreeAt (float y) const;
    void commit();
    void computeRows();
    void showMenu (int index);

    LumenBasslineProcessor& proc;
    lb::Pattern pat;
    int version = -1, lo = -3, hi = 10;
    int dragIndex = -1, dragMode = 0;   // 1 move, 2 resize
    int dragStartStep = 0, dragStartDeg = 0, dragStartLen = 1;
    juce::Point<float> downPos;
    bool moved = false, createdOnDown = false;
    const float keyW = 46.0f, headerH = 18.0f;
};

//==============================================================================
class DragMidiBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit DragMidiBox (LumenBasslineProcessor& p) : proc (p) { setTooltip ("Drag the bassline straight into your DAW"); setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; repaint(); }
private:
    LumenBasslineProcessor& proc;
    bool dragging = false;
};
} // namespace lbui

//==============================================================================
class LumenBasslineEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenBasslineEditor (LumenBasslineProcessor&);
    ~LumenBasslineEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void addKnob (const char* id, const char* label, int group);
    void setupCombo (juce::ComboBox&, const char* id);
    void setupToggle (juce::ToggleButton&, const char* id, const char* text);

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<SA> att;
        int group = 0;
    };

    LumenBasslineProcessor& proc;
    lbui::LumenLnf lnf;

    juce::ComboBox styleBox, soundBox, keyBox, scaleBox, barsBox, rangeBox, progBox;
    juce::TextButton stylePrev { "<" }, styleNext { ">" };
    juce::Slider octaveSlider;
    juce::ToggleButton playBtn, drumsBtn, followBtn, liveBtn, fillBtn, synthBtn;
    juce::TextButton generateBtn { "GENERATE" }, mutateBtn { "MUTATE" }, backBtn { "<" }, fwdBtn { ">" },
                     clearLocksBtn { "UNLOCK ALL" }, saveBtn { "SAVE .MID" };
    juce::Label seedLabel, statusLabel;

    std::vector<std::unique_ptr<juce::Label>> rowLabels;
    std::vector<std::unique_ptr<CA>> comboAtts;
    std::vector<std::unique_ptr<BA>> buttonAtts;
    std::unique_ptr<SA> octaveAtt;
    std::vector<std::unique_ptr<Knob>> knobs;

    lbui::PianoRoll roll;
    lbui::DragMidiBox dragBox;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Rectangle<int> groovePanel, pitchPanel, soundPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenBasslineEditor)
};
