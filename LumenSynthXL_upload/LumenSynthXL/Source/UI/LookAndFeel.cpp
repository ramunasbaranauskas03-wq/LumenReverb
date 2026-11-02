#include "LookAndFeel.h"

namespace lxl::ui
{
juce::Font font (float size, bool bold)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font labelFont (float size)
{
    return juce::Font (juce::FontOptions (size, juce::Font::bold)).withExtraKerningFactor (0.08f);
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);
    setColour (juce::ComboBox::backgroundColourId, col::well);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::outlineColourId, col::border);
    setColour (juce::ComboBox::arrowColourId, col::textDim);
    setColour (juce::PopupMenu::backgroundColourId, col::panelHi);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId, col::textDim);
    setColour (juce::TextButton::buttonColourId, col::panelHi);
    setColour (juce::TextButton::buttonOnColourId, col::accentDim);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextEditor::backgroundColourId, col::well);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::outlineColourId, col::border);
    setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::AlertWindow::backgroundColourId, col::panel);
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::AlertWindow::outlineColourId, col::border);
    setColour (juce::TooltipWindow::backgroundColourId, col::panelHi);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffe9edf2));
    setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff1a1e25));
    setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff9aa3af));
    setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, col::accent.withAlpha (0.75f));
    setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, col::accent.withAlpha (0.25f));
    setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, col::panel);
    setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, col::textDim);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const float size = (float) juce::jmin (w, h);
    const auto c = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
    const float r = size * 0.5f - 4.0f;
    const float trackW = juce::jmax (2.0f, size * 0.07f);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const bool enabled = s.isEnabled();
    const float angle = start + pos * (end - start);

    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.0f, start, end, true);
    g.setColour (col::well);
    g.strokePath (track, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, r, r, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (enabled ? col::accent : col::textFaint);
        g.strokePath (val, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // body
    const float br = r - trackW * 1.6f;
    juce::ColourGradient grad (col::panelHi.brighter (0.12f), c.x, c.y - br, col::bg2, c.x, c.y + br, false);
    g.setGradientFill (grad);
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.0f);

    // pointer
    const float pr0 = br * 0.25f, pr1 = br * 0.85f;
    const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
    g.setColour (enabled ? col::text : col::textFaint);
    g.drawLine (c.x + dir.x * pr0, c.y + dir.y * pr0, c.x + dir.x * pr1, c.y + dir.y * pr1, juce::jmax (1.5f, size * 0.045f));
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                    juce::Slider::SliderStyle, juce::Slider& s)
{
    auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f, (float) h * 0.32f);
    g.setColour (col::well);
    g.fillRoundedRectangle (area, 3.0f);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const float mid = bipolar ? area.getCentreX() : area.getX();
    const float px = juce::jlimit (area.getX(), area.getRight(), pos);
    const auto colour = (bool) s.getProperties().getWithDefault ("modColour", false) ? col::mod : col::accent;
    g.setColour (colour);
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (mid, px), area.getY(), std::abs (px - mid), area.getHeight()), 3.0f);
    g.setColour (col::text);
    g.fillRect (juce::Rectangle<float> (px - 1.0f, area.getY() - 3.0f, 2.0f, area.getHeight() + 6.0f));
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.isMouseOver (true) ? col::panelHi : col::well);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (true) ? col::accentDim : col::border);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (col::textDim);
    g.fillPath (arrow);
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 0, box.getWidth() - 22, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::panelHi);
    g.setColour (col::border);
    g.drawRect (0, 0, w, h);
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                     bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                     const juce::String& shortcut, const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (col::border);
        g.fillRect (area.reduced (6, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    auto r = area.reduced (2, 0);
    if (isHighlighted && isActive)
    {
        g.setColour (col::accentDim);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
    }
    g.setColour (! isActive ? col::textFaint : (isTicked ? col::accent : (textColour != nullptr ? *textColour : col::text)));
    g.setFont (getPopupMenuFont());
    auto tr = r.reduced (10, 0);
    if (isTicked) g.fillEllipse ((float) tr.getX() - 6.0f, (float) tr.getCentreY() - 2.5f, 5.0f, 5.0f);
    g.drawFittedText (text, tr.withTrimmedLeft (4), juce::Justification::centredLeft, 1);
    if (hasSubMenu)
    {
        juce::Path p;
        const float x = (float) r.getRight() - 12.0f, y = (float) r.getCentreY();
        p.addTriangle (x, y - 4.0f, x, y + 4.0f, x + 5.0f, y);
        g.fillPath (p);
    }
    juce::ignoreUnused (shortcut, icon);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? col::accentDim : (down ? col::panel : (over ? col::panelHi.brighter (0.08f) : col::panelHi));
    g.setColour (fill);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? col::accent.withAlpha (0.8f) : col::border);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    // power-style switch: LED + optional text
    auto r = b.getLocalBounds().toFloat();
    const float d = juce::jmin (r.getHeight() - 4.0f, 12.0f);
    auto led = juce::Rectangle<float> (r.getX() + 2.0f, r.getCentreY() - d * 0.5f, d, d);
    const bool on = b.getToggleState();
    g.setColour (col::well);
    g.fillEllipse (led);
    if (on)
    {
        g.setColour (col::accent.withAlpha (0.25f));
        g.fillEllipse (led.expanded (2.5f));
        g.setColour (col::accent);
        g.fillEllipse (led.reduced (2.0f));
    }
    g.setColour (over ? col::textDim : col::border);
    g.drawEllipse (led, 1.0f);
    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (on ? col::text : col::textDim);
        g.setFont (labelFont (10.5f));
        g.drawFittedText (b.getButtonText().toUpperCase(), r.withTrimmedLeft (d + 7.0f).toNearestInt(), juce::Justification::centredLeft, 1);
    }
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (col::panelHi);
    g.setColour (col::border);
    g.drawRect (0, 0, w, h);
    g.setColour (col::text);
    g.setFont (font (12.5f));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (8, 4), juce::Justification::centredLeft, 4);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const int w = juce::jmin (320, (int) juce::GlyphArrangement::getStringWidth (font (12.5f), tipText) + 20);
    const int lines = 1 + (int) juce::GlyphArrangement::getStringWidth (font (12.5f), tipText) / 300;
    const int h = 10 + 17 * lines;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h).constrainedWithin (parent);
}
} // namespace lxl::ui
