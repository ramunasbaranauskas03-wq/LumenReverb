#pragma once
#include "../Common.h"

namespace lxl::ui
{
namespace col
{
inline const juce::Colour bg        { 0xff0c0e12 };
inline const juce::Colour bg2       { 0xff11141a };
inline const juce::Colour panel     { 0xff161a21 };
inline const juce::Colour panelHi   { 0xff1c212a };
inline const juce::Colour border    { 0xff262c37 };
inline const juce::Colour well      { 0xff0a0c10 };
inline const juce::Colour text      { 0xffd3dae4 };
inline const juce::Colour textDim   { 0xff7a8595 };
inline const juce::Colour textFaint { 0xff4a5361 };
inline const juce::Colour accent    { 0xff36d6ff };   // values / signal
inline const juce::Colour accentDim { 0xff1a6c85 };
inline const juce::Colour mod       { 0xffffae42 };   // modulation
inline const juce::Colour modDim    { 0xff7a5420 };
inline const juce::Colour good      { 0xff59e39a };
inline const juce::Colour warn      { 0xffff5d6c };
} // namespace col

juce::Font font (float size, bool bold = false);
juce::Font labelFont (float size = 10.0f);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override { return font (13.0f); }
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (12.0f, true); }
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
};
} // namespace lxl::ui
