#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Parameters.h"

// =====================================================================================
// LED interface kit for Lumen Synth: LED-ring knobs, glowing toggles, LCD combos,
// dot-matrix text, segmented meters and panel painting.
// =====================================================================================
namespace lumen::led
{
namespace C
{
    const juce::Colour bg       { 0xff04060a };
    const juce::Colour panelTop { 0xff0d121a };
    const juce::Colour panelBot { 0xff080b10 };
    const juce::Colour border   { 0xff1b2433 };
    const juce::Colour text     { 0xffc3cfdf };
    const juce::Colour dim      { 0xff5a687c };
    const juce::Colour cyan     { 0xff22e3ff };
    const juce::Colour magenta  { 0xffff3dbe };
    const juce::Colour amber    { 0xffffb020 };
    const juce::Colour green    { 0xff3dff8a };
    const juce::Colour blue     { 0xff4d8dff };
    const juce::Colour violet   { 0xffa47bff };
    const juce::Colour red      { 0xffff4a4a };
    const juce::Colour orange   { 0xffff7a2e };
}

inline juce::Font font (float h, bool bold = true)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}
inline juce::Font mono (float h)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::bold));
}

// A single LED: glow halo + bright core. level 0..1
inline void drawLed (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, float level)
{
    level = juce::jlimit (0.0f, 1.0f, level);
    if (level > 0.02f)
    {
        g.setColour (col.withAlpha (0.10f * level)); g.fillEllipse (c.x - r * 3.2f, c.y - r * 3.2f, r * 6.4f, r * 6.4f);
        g.setColour (col.withAlpha (0.22f * level)); g.fillEllipse (c.x - r * 1.9f, c.y - r * 1.9f, r * 3.8f, r * 3.8f);
    }
    g.setColour (col.withAlpha (0.14f + 0.86f * level).interpolatedWith (juce::Colours::white, 0.35f * level * level));
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
}

// Glowing stroke for paths (wave displays)
inline void glowPath (juce::Graphics& g, const juce::Path& p, juce::Colour col, float w = 1.6f, float intensity = 1.0f)
{
    g.setColour (col.withAlpha (0.07f * intensity)); g.strokePath (p, juce::PathStrokeType (w * 5.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col.withAlpha (0.16f * intensity)); g.strokePath (p, juce::PathStrokeType (w * 2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col.withAlpha (0.95f * intensity).interpolatedWith (juce::Colours::white, 0.25f));
    g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// Background grid of faint LEDs (the "LED panel" texture)
inline void dotGrid (juce::Graphics& g, juce::Rectangle<float> r, float pitch, juce::Colour col, float alpha)
{
    g.setColour (col.withAlpha (alpha));
    for (float y = r.getY() + pitch * 0.5f; y < r.getBottom(); y += pitch)
        for (float x = r.getX() + pitch * 0.5f; x < r.getRight(); x += pitch)
            g.fillRect (x - 0.6f, y - 0.6f, 1.2f, 1.2f);
}

// LCD display well (recessed black glass)
inline void lcd (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tint)
{
    g.setColour (juce::Colour (0xff020305)); g.fillRoundedRectangle (r, 5.0f);
    juce::ColourGradient gl (tint.withAlpha (0.05f), r.getCentreX(), r.getY(), juce::Colours::transparentBlack, r.getCentreX(), r.getBottom(), false);
    g.setGradientFill (gl); g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colours::black); g.drawRoundedRectangle (r, 5.0f, 1.5f);
    g.setColour (tint.withAlpha (0.18f)); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 0.8f);
}

// Panel with title strip + status LED
inline void panel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour col, bool on = true)
{
    juce::ColourGradient gr (C::panelTop, r.getX(), r.getY(), C::panelBot, r.getX(), r.getBottom(), false);
    g.setGradientFill (gr); g.fillRoundedRectangle (r, 8.0f);
    dotGrid (g, r.reduced (4.0f).withTrimmedTop (24.0f), 7.0f, juce::Colours::white, 0.025f);
    g.setColour (C::border); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    // top accent line
    juce::ColourGradient acc (col.withAlpha (on ? 0.9f : 0.25f), r.getX() + 10, r.getY(), col.withAlpha (0.0f), r.getX() + r.getWidth() * 0.7f, r.getY(), false);
    g.setGradientFill (acc); g.fillRect (r.getX() + 10.0f, r.getY() + 0.5f, r.getWidth() * 0.6f, 1.5f);
    drawLed (g, { r.getX() + 16.0f, r.getY() + 13.0f }, 3.0f, col, on ? 0.55f : 0.0f);
    if (title.isNotEmpty())
    {
        g.setColour (on ? col.interpolatedWith (juce::Colours::white, 0.3f) : C::dim);
        g.setFont (font (11.5f));
        g.drawText (title, r.getX() + 30.0f, r.getY() + 5.0f, 200.0f, 16.0f, juce::Justification::centredLeft);
    }
}

//==============================================================================
// Dot-matrix text: renders the string into a low-res bitmap, then draws each pixel as an LED.
class DotMatrix
{
public:
    void draw (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, juce::Colour col, float pitch,
               juce::Justification just = juce::Justification::centred, bool showOff = true, float fontH = 11.4f)
    {
        const int rows = 9;
        const int cols = juce::jmax (8, (int) (area.getWidth() / pitch));
        if (text != cachedText || cols != cachedCols || just.getFlags() != cachedJust || std::abs (fontH - cachedFont) > 0.001f)
        {
            cachedText = text; cachedCols = cols; cachedJust = just.getFlags(); cachedFont = fontH;
            img = juce::Image (juce::Image::ARGB, cols, rows, true);
            juce::Graphics ig (img);
            ig.setColour (juce::Colours::white);
            ig.setFont (juce::Font (juce::FontOptions (fontH, juce::Font::bold)).withExtraKerningFactor (0.04f));
            ig.drawText (text.toUpperCase(), 0, 0, cols, rows, just, false);
        }
        const float y0 = area.getCentreY() - rows * pitch * 0.5f;
        const float x0 = area.getX() + (area.getWidth() - cols * pitch) * 0.5f;
        const float r = pitch * 0.36f;
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < cols; ++x)
            {
                const float a = img.getPixelAt (x, y).getFloatAlpha();
                const float cx = x0 + (x + 0.5f) * pitch, cy = y0 + (y + 0.5f) * pitch;
                if (a > 0.32f)
                {
                    const float lv = juce::jlimit (0.0f, 1.0f, (a - 0.2f) * 1.4f);
                    g.setColour (col.withAlpha (0.16f * lv)); g.fillEllipse (cx - r * 2.3f, cy - r * 2.3f, r * 4.6f, r * 4.6f);
                    g.setColour (col.withAlpha (0.55f + 0.45f * lv).interpolatedWith (juce::Colours::white, 0.25f * lv));
                    g.fillEllipse (cx - r, cy - r, 2 * r, 2 * r);
                }
                else if (showOff)
                {
                    g.setColour (col.withAlpha (0.06f));
                    g.fillEllipse (cx - r * 0.8f, cy - r * 0.8f, r * 1.6f, r * 1.6f);
                }
            }
    }
private:
    juce::String cachedText { "\x01" }; int cachedCols = -1, cachedJust = -1; float cachedFont = 0.0f;
    juce::Image img;
};

//==============================================================================
class LedLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LedLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff0a0e14));
        setColour (juce::PopupMenu::textColourId, C::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, C::cyan.withAlpha (0.18f));
        setColour (juce::PopupMenu::highlightedTextColourId, C::cyan);
        setColour (juce::ComboBox::textColourId, C::cyan);
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff030508));
        setColour (juce::ComboBox::outlineColourId, C::border);
        setColour (juce::ComboBox::arrowColourId, C::cyan);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff0a0e14));
        setColour (juce::TooltipWindow::textColourId, C::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const auto col = s.findColour (juce::Slider::rotarySliderFillColourId);
        const bool bip = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const float size = (float) juce::jmin (w, h);
        const auto c = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
        const float R = size * 0.5f - 2.0f;

        // LED ring
        const int n = 25;
        const float dotR = juce::jmax (1.2f, R * 0.075f);
        const float ringR = R - dotR * 1.4f;
        const float centrePos = bip ? 0.5f : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) (n - 1);
            const float ang = a0 + t * (a1 - a0);
            const juce::Point<float> p (c.x + ringR * std::sin (ang), c.y - ringR * std::cos (ang));
            const float lo = juce::jmin (centrePos, pos), hi = juce::jmax (centrePos, pos);
            const float half = 0.5f / (float) (n - 1);
            const bool lit = (t >= lo - half && t <= hi + half) && ! (bip && std::abs (pos - 0.5f) < 0.004f && std::abs (t - 0.5f) > half);
            const bool tip = std::abs (t - pos) <= half;
            drawLed (g, p, dotR, col, lit ? (tip ? 1.0f : 0.72f) : 0.0f);
        }

        // knob body
        const float kr = R * 0.60f;
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillEllipse (c.x - kr - 1, c.y - kr + 1.5f, 2 * kr + 2, 2 * kr + 2);
        juce::ColourGradient body (juce::Colour (0xff2c3542), c.x - kr * 0.6f, c.y - kr * 0.8f, juce::Colour (0xff0b0e13), c.x + kr * 0.5f, c.y + kr, true);
        g.setGradientFill (body); g.fillEllipse (c.x - kr, c.y - kr, 2 * kr, 2 * kr);
        g.setColour (juce::Colour (0xff3a4556)); g.drawEllipse (c.x - kr, c.y - kr, 2 * kr, 2 * kr, 1.0f);
        const float ir = kr * 0.78f;
        juce::ColourGradient cap (juce::Colour (0xff1c232d), c.x, c.y - ir, juce::Colour (0xff0e1218), c.x, c.y + ir, false);
        g.setGradientFill (cap); g.fillEllipse (c.x - ir, c.y - ir, 2 * ir, 2 * ir);

        // pointer (glowing)
        const float ang = a0 + pos * (a1 - a0);
        juce::Path ptr;
        ptr.startNewSubPath (c.x + kr * 0.25f * std::sin (ang), c.y - kr * 0.25f * std::cos (ang));
        ptr.lineTo (c.x + kr * 0.88f * std::sin (ang), c.y - kr * 0.88f * std::cos (ang));
        glowPath (g, ptr, col, juce::jmax (1.6f, kr * 0.12f), s.isEnabled() ? 1.0f : 0.3f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
    {
        // horizontal bipolar LED bar (mod matrix amount)
        const auto col = s.findColour (juce::Slider::trackColourId);
        const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f, (float) h * 0.25f);
        g.setColour (juce::Colour (0xff030508)); g.fillRoundedRectangle (r, 3.0f);
        g.setColour (C::border); g.drawRoundedRectangle (r, 3.0f, 1.0f);
        const double norm = s.valueToProportionOfLength (s.getValue());
        const int segs = juce::jmax (8, (int) (r.getWidth() / 6.0f));
        const float sw = r.getWidth() / (float) segs;
        for (int i = 0; i < segs; ++i)
        {
            const float t0 = (float) i / segs, t1 = (float) (i + 1) / segs;
            const bool lit = norm >= 0.5 ? (t1 > 0.5f && t0 < (float) norm) : (t0 < 0.5f && t1 > (float) norm);
            auto seg = juce::Rectangle<float> (r.getX() + i * sw + 1.0f, r.getY() + 2.0f, sw - 2.0f, r.getHeight() - 4.0f);
            if (lit)
            {
                g.setColour (col.withAlpha (0.25f)); g.fillRoundedRectangle (seg.expanded (1.5f), 2.0f);
                g.setColour (col.interpolatedWith (juce::Colours::white, 0.2f)); g.fillRoundedRectangle (seg, 1.5f);
            }
            else { g.setColour (col.withAlpha (0.07f)); g.fillRoundedRectangle (seg, 1.5f); }
        }
        g.setColour (C::dim); g.fillRect (r.getCentreX() - 0.5f, r.getY() - 2.0f, 1.0f, r.getHeight() + 4.0f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto col = box.findColour (juce::ComboBox::textColourId);
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (juce::Colour (0xff030508)); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (box.isMouseOver (true) ? col.withAlpha (0.55f) : C::border); g.drawRoundedRectangle (r, 4.0f, 1.0f);
        juce::Path tri; const float ax = (float) w - 11.0f, ay = (float) h * 0.5f;
        tri.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 2.5f);
        g.setColour (col.withAlpha (0.8f)); g.fillPath (tri);
    }
    juce::Font getComboBoxFont (juce::ComboBox& b) override { return font (juce::jmin (13.0f, (float) b.getHeight() * 0.6f)); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 20, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
    juce::Font getPopupMenuFont() override { return font (13.0f, false); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto col = b.findColour (juce::TextButton::buttonColourId);
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const float glow = (down ? 1.0f : over ? 0.7f : 0.4f) * (b.isEnabled() ? 1.0f : 0.3f);
        g.setColour (col.withAlpha (0.08f * glow)); g.fillRoundedRectangle (r.expanded (1.0f), 6.0f);
        juce::ColourGradient gr (juce::Colour (0xff18202a), r.getX(), r.getY(), juce::Colour (0xff0a0d12), r.getX(), r.getBottom(), false);
        g.setGradientFill (gr); g.fillRoundedRectangle (r, 5.0f);
        g.setColour (col.withAlpha (0.12f * glow)); g.fillRoundedRectangle (r, 5.0f);
        g.setColour (col.withAlpha (0.35f + 0.5f * glow)); g.drawRoundedRectangle (r, 5.0f, 1.2f);
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const auto col = b.findColour (juce::TextButton::buttonColourId);
        g.setColour (b.isEnabled() ? col.interpolatedWith (juce::Colours::white, 0.35f) : C::dim);
        g.setFont (font (juce::jmin (14.0f, b.getHeight() * 0.48f)));
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
    }
};

//==============================================================================
inline juce::String formatValue (const synth::Spec& s, float v)
{
    if (s.kind == synth::Kind::Choice) return (*s.choices)[(int) v];
    if (s.kind == synth::Kind::Bool) return v > 0.5f ? "ON" : "OFF";
    if (s.kind == synth::Kind::Int)
    {
        const int i = (int) std::lround (v);
        if (s.id.endsWith ("_oct") || s.id.endsWith ("_semi")) return (i > 0 ? "+" : "") + juce::String (i);
        if (s.id == "bend") return juce::String (i) + " st";
        return juce::String (i);
    }
    if (s.unit == "Hz") return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (v, v < 10.0f ? 2 : 0) + " Hz";
    if (s.unit == "s")  return v < 1.0f ? juce::String (v * 1000.0f, v < 0.01f ? 1 : 0) + " ms" : juce::String (v, 2) + " s";
    if (s.unit == "dB") return juce::String (v, 1) + " dB";
    if (s.unit == "ct") return (v > 0 ? "+" : "") + juce::String (v, 0) + " ct";
    if (s.lo < 0.0f) return (v > 0.0f ? "+" : "") + juce::String (v * 100.0f, 0) + "%";
    if (s.hi <= 1.0f) return juce::String (v * 100.0f, 0) + "%";
    return juce::String (v, 2);
}

//==============================================================================
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& lbl, juce::Colour col, bool bipolar = false)
        : label (lbl), spec (synth::findSpec (id))
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setColour (juce::Slider::rotarySliderFillColourId, col);
        slider.getProperties().set ("bipolar", bipolar);
        slider.setMouseDragSensitivity (220);
        slider.setVelocityBasedMode (false);
        addAndMakeVisible (slider);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (st, id, slider);
        if (spec != nullptr) slider.setDoubleClickReturnValue (true, spec->def);
        slider.onValueChange = [this] { repaint(); };
        slider.addMouseListener (this, false);
        colour = col;
    }
    void resized() override { slider.setBounds (getLocalBounds().withTrimmedBottom (15)); }
    void paint (juce::Graphics& g) override
    {
        const bool hot = slider.isMouseOverOrDragging();
        auto r = getLocalBounds().removeFromBottom (15).toFloat();
        g.setFont (font (10.5f));
        if (hot && spec != nullptr)
        {
            g.setColour (colour.interpolatedWith (juce::Colours::white, 0.4f));
            g.drawText (formatValue (*spec, (float) slider.getValue()), r, juce::Justification::centred);
        }
        else { g.setColour (C::text.withAlpha (0.8f)); g.drawText (label, r, juce::Justification::centred); }
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    juce::Slider slider;
private:
    juce::String label;
    const synth::Spec* spec;
    juce::Colour colour;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

//==============================================================================
class Toggle : public juce::ToggleButton
{
public:
    Toggle (juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& txt, juce::Colour col, bool roundLed = false)
        : colour (col), round (roundLed)
    {
        setButtonText (txt);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, id, *this);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto r = getLocalBounds().toFloat();
        if (round)
        {
            const float d = juce::jmin (r.getWidth(), r.getHeight());
            const auto c = r.withSizeKeepingCentre (d, d).getCentre();
            g.setColour (juce::Colour (0xff020305)); g.fillEllipse (c.x - d * 0.42f, c.y - d * 0.42f, d * 0.84f, d * 0.84f);
            drawLed (g, c, d * 0.26f, colour, on ? 1.0f : (over ? 0.2f : 0.0f));
            return;
        }
        r = r.reduced (1.0f);
        g.setColour (juce::Colour (0xff070a0f)); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? colour.withAlpha (0.12f) : juce::Colours::transparentBlack); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? colour.withAlpha (0.6f) : (over ? C::dim : C::border)); g.drawRoundedRectangle (r, 4.0f, 1.0f);
        drawLed (g, { r.getX() + 10.0f, r.getCentreY() }, 3.2f, colour, on ? 1.0f : 0.0f);
        g.setColour (on ? colour.interpolatedWith (juce::Colours::white, 0.4f) : C::dim);
        g.setFont (font (10.5f));
        g.drawText (getButtonText(), r.withTrimmedLeft (18.0f), juce::Justification::centredLeft);
    }
private:
    juce::Colour colour; bool round;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

//==============================================================================
class Combo : public juce::ComboBox
{
public:
    Combo (juce::AudioProcessorValueTreeState& st, const juce::String& id, juce::Colour col)
    {
        if (auto* s = synth::findSpec (id)) addItemList (*s->choices, 1);
        setColour (juce::ComboBox::textColourId, col);
        setColour (juce::ComboBox::arrowColourId, col);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (st, id, *this);
    }
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
};

//==============================================================================
// Segmented LED meter (vertical or horizontal). level is linear amplitude.
inline void ledMeter (juce::Graphics& g, juce::Rectangle<float> r, float level, float hold, bool vertical, int segs)
{
    const float db = juce::Decibels::gainToDecibels (level, -60.0f);
    const float holdDb = juce::Decibels::gainToDecibels (hold, -60.0f);
    const float t = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
    const float th = juce::jlimit (0.0f, 1.0f, (holdDb + 48.0f) / 48.0f);
    const float len = vertical ? r.getHeight() : r.getWidth();
    const float sl = len / (float) segs;
    for (int i = 0; i < segs; ++i)
    {
        const float f = (i + 0.5f) / (float) segs;
        const juce::Colour col = f > 0.9f ? C::red : f > 0.75f ? C::amber : C::green;
        juce::Rectangle<float> s = vertical ? juce::Rectangle<float> (r.getX(), r.getBottom() - (i + 1) * sl + 1.0f, r.getWidth(), sl - 2.0f)
                                            : juce::Rectangle<float> (r.getX() + i * sl + 1.0f, r.getY(), sl - 2.0f, r.getHeight());
        const bool lit = f <= t;
        const bool isHold = std::abs (f - th) < 0.5f / segs && th > 0.02f;
        if (lit || isHold)
        {
            g.setColour (col.withAlpha (0.22f)); g.fillRoundedRectangle (s.expanded (1.5f), 2.0f);
            g.setColour (col.interpolatedWith (juce::Colours::white, 0.15f)); g.fillRoundedRectangle (s, 1.0f);
        }
        else { g.setColour (col.withAlpha (0.08f)); g.fillRoundedRectangle (s, 1.0f); }
    }
}

} // namespace lumen::led
