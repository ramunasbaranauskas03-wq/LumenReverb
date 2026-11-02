#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace lumen::spec::ui
{
namespace C
{
    const juce::Colour bg     { 0xff0b0d12 };
    const juce::Colour panel  { 0xff12151c };
    const juce::Colour panel2 { 0xff191d27 };
    const juce::Colour line   { 0xff252a36 };
    const juce::Colour grid   { 0xff1d2230 };
    const juce::Colour text   { 0xffe9ebf2 };
    const juce::Colour dim    { 0xff858c9c };
    const juce::Colour faint  { 0xff4a5163 };
    const juce::Colour accent { 0xff8fe3d1 };
    const juce::Colour warn   { 0xffffc46b };
    const juce::Colour bad    { 0xffff5d6c };
    const juce::Colour good   { 0xff7be59b };
}

inline juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

class Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour (juce::ComboBox::backgroundColourId, C::panel2);
        setColour (juce::ComboBox::textColourId, C::text);
        setColour (juce::ComboBox::outlineColourId, C::line);
        setColour (juce::ComboBox::arrowColourId, C::dim);
        setColour (juce::PopupMenu::backgroundColourId, C::panel2);
        setColour (juce::PopupMenu::textColourId, C::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, C::accent.withAlpha (0.18f));
        setColour (juce::PopupMenu::highlightedTextColourId, C::text);
        setColour (juce::TextButton::buttonColourId, C::panel2);
        setColour (juce::TextButton::buttonOnColourId, C::accent);
        setColour (juce::TextButton::textColourOffId, C::dim);
        setColour (juce::TextButton::textColourOnId, C::bg);
        setColour (juce::Label::textColourId, C::text);
        setColour (juce::Label::textWhenEditingColourId, C::text);
        setColour (juce::Label::backgroundWhenEditingColourId, C::panel2);
        setColour (juce::Label::outlineWhenEditingColourId, C::accent);
        setColour (juce::TextEditor::highlightColourId, C::accent.withAlpha (0.3f));
        setColour (juce::TextEditor::textColourId, C::text);
        setColour (juce::CaretComponent::caretColourId, C::accent);
        setColour (juce::TooltipWindow::backgroundColourId, C::panel2);
        setColour (juce::TooltipWindow::textColourId, C::text);
        setColour (juce::TooltipWindow::outlineColourId, C::line);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        auto base = on ? b.findColour (juce::TextButton::buttonOnColourId) : C::panel2;
        if (down) base = base.brighter (0.1f); else if (over) base = base.brighter (0.05f);
        g.setColour (base);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (on ? base.brighter (0.3f) : C::line);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return font (juce::jmin (12.5f, (float) h * 0.5f), true); }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (box.isMouseOver (true) ? C::faint : C::line);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
        arrow.startNewSubPath (ax - 4.0f, ay - 2.0f); arrow.lineTo (ax, ay + 2.0f); arrow.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (C::dim);
        g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::Font getComboBoxFont (juce::ComboBox& b) override { return font (juce::jmin (12.5f, (float) b.getHeight() * 0.5f)); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
    juce::Font getPopupMenuFont() override { return font (13.0f); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }
};

// small caption drawn above a control
inline void caption (juce::Graphics& g, juce::Component& c, const juce::String& text)
{
    g.setColour (C::faint);
    g.setFont (font (9.5f, true));
    g.drawText (text.toUpperCase(), c.getX() + 2, c.getY() - 13, c.getWidth(), 12, juce::Justification::left);
}
} // namespace lumen::spec::ui
