#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Parameters.h"

// =====================================================================================
// Lumen Synth interface kit: studio-style arc knobs, flat switches, display fields, meters and panels.
// =====================================================================================
namespace lumen::led
{
namespace C
{
    // studio palette: neutral graphite, one cool primary accent, muted secondaries (no neon)
    const juce::Colour bg       { 0xff16181c };
    const juce::Colour panelTop { 0xff1f2227 };
    const juce::Colour panelBot { 0xff1c1f23 };
    const juce::Colour border   { 0xff2b3037 };
    const juce::Colour text     { 0xffdde2e8 };
    const juce::Colour dim      { 0xff7b8490 };
    const juce::Colour cyan     { 0xff4fa3ff };     // primary accent
    const juce::Colour magenta  { 0xff8f84ff };
    const juce::Colour amber    { 0xffe6a440 };
    const juce::Colour green    { 0xff52c78c };
    const juce::Colour blue     { 0xff5a8cff };
    const juce::Colour violet   { 0xffa293ff };
    const juce::Colour red      { 0xffe05a55 };
    const juce::Colour orange   { 0xffe6873f };
    const juce::Colour knobTop  { 0xff363c45 };
    const juce::Colour knobBot  { 0xff22262c };
    const juce::Colour track    { 0xff2d3239 };
    const juce::Colour field    { 0xff111317 };
}

inline juce::Font font (float h, bool bold = true)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}
inline juce::Font mono (float h)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::bold));
}

// Small status dot (flat, no glow). level 0..1
inline void drawLed (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, float level)
{
    level = juce::jlimit (0.0f, 1.0f, level);
    r = juce::jmin (r, 3.2f);
    g.setColour (level > 0.02f ? col.withAlpha (0.35f + 0.65f * level) : C::track);
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
}

// Crisp stroke for wave / curve displays, with a faint under-glow
inline void glowPath (juce::Graphics& g, const juce::Path& p, juce::Colour col, float w = 1.6f, float intensity = 1.0f)
{
    g.setColour (col.withAlpha (0.10f * intensity)); g.strokePath (p, juce::PathStrokeType (w * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col.withAlpha (0.95f * intensity));
    g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// (the old LED-panel texture is gone: flat surfaces read as a studio tool, not a toy)
inline void dotGrid (juce::Graphics&, juce::Rectangle<float>, float, juce::Colour, float) {}

// Display well
inline void lcd (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour)
{
    g.setColour (C::field); g.fillRoundedRectangle (r, 4.0f);
    g.setColour (C::border); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
}

// Panel: flat card with a small title and a thin accent tick
inline void panel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour col, bool on = true)
{
    g.setColour (C::panelTop); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (C::border); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (on ? col : C::track);
        g.fillRoundedRectangle (r.getX() + 14.0f, r.getY() + 8.0f, 4.0f, 11.0f, 2.0f);
        g.setColour (on ? C::text.withAlpha (0.9f) : C::dim);
        g.setFont (font (11.0f));
        g.drawText (title.toUpperCase(), r.getX() + 30.0f, r.getY() + 5.0f, 220.0f, 16.0f, juce::Justification::centredLeft);
    }
}

//==============================================================================
// Display text (kept the old DotMatrix interface so every caller stays the same; now clean type)
class DotMatrix
{
public:
    void draw (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, juce::Colour col, float pitch,
               juce::Justification just = juce::Justification::centred, bool = true, float fontH = 11.4f)
    {
        const float h = juce::jlimit (10.0f, 20.0f, juce::jmax (fontH * pitch * 0.62f, area.getHeight() * 0.5f));
        g.setColour (col.interpolatedWith (juce::Colours::white, 0.15f));
        g.setFont (juce::Font (juce::FontOptions (h, juce::Font::bold)).withExtraKerningFactor (0.02f));
        g.drawFittedText (text, area.toNearestInt(), just, 1, 0.8f);
    }
};

//==============================================================================
class LedLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LedLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1d2025));
        setColour (juce::PopupMenu::textColourId, C::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, C::cyan.withAlpha (0.18f));
        setColour (juce::PopupMenu::highlightedTextColourId, C::cyan);
        setColour (juce::ComboBox::textColourId, C::cyan);
        setColour (juce::ComboBox::backgroundColourId, C::field);
        setColour (juce::ComboBox::outlineColourId, C::border);
        setColour (juce::ComboBox::arrowColourId, C::cyan);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff23272d));
        setColour (juce::TooltipWindow::textColourId, C::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const auto col = s.findColour (juce::Slider::rotarySliderFillColourId);
        const bool bip = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const float size = (float) juce::jmin (w, h);
        const auto c = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
        const float R = size * 0.5f - 2.0f;
        const float tw = juce::jlimit (2.0f, 4.0f, R * 0.12f), ar = R - tw * 0.5f;
        // track + value arc
        juce::Path tr; tr.addCentredArc (c.x, c.y, ar, ar, 0.0f, a0, a1, true);
        g.setColour (C::track); g.strokePath (tr, juce::PathStrokeType (tw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float from = bip ? 0.5f : 0.0f;
        if (std::abs (pos - from) > 0.002f)
        {
            juce::Path va; va.addCentredArc (c.x, c.y, ar, ar, 0.0f, a0 + juce::jmin (from, pos) * (a1 - a0), a0 + juce::jmax (from, pos) * (a1 - a0), true);
            g.setColour (s.isEnabled() ? col : col.withAlpha (0.35f));
            g.strokePath (va, juce::PathStrokeType (tw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // body
        const float kr = R * 0.66f;
        g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillEllipse (c.x - kr, c.y - kr + 1.5f, 2 * kr, 2 * kr);
        juce::ColourGradient body (C::knobTop, c.x, c.y - kr, C::knobBot, c.x, c.y + kr, false);
        g.setGradientFill (body); g.fillEllipse (c.x - kr, c.y - kr, 2 * kr, 2 * kr);
        g.setColour (juce::Colour (0xff434a55)); g.drawEllipse (c.x - kr + 0.5f, c.y - kr + 0.5f, 2 * kr - 1, 2 * kr - 1, 1.0f);
        // pointer
        const float ang = a0 + pos * (a1 - a0);
        juce::Path ptr;
        ptr.startNewSubPath (c.x + kr * 0.30f * std::sin (ang), c.y - kr * 0.30f * std::cos (ang));
        ptr.lineTo (c.x + kr * 0.86f * std::sin (ang), c.y - kr * 0.86f * std::cos (ang));
        g.setColour (s.isEnabled() ? juce::Colour (0xffeef1f5) : C::dim);
        g.strokePath (ptr, juce::PathStrokeType (juce::jmax (1.6f, kr * 0.10f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
    {
        // horizontal bipolar bar (mod matrix amount)
        const auto col = s.findColour (juce::Slider::trackColourId);
        const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f, (float) h * 0.32f);
        g.setColour (C::field); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        const float norm = (float) s.valueToProportionOfLength (s.getValue());
        const float x0 = r.getX() + r.getWidth() * juce::jmin (0.5f, norm), x1 = r.getX() + r.getWidth() * juce::jmax (0.5f, norm);
        g.setColour (col); g.fillRoundedRectangle (juce::Rectangle<float> (x0, r.getY(), juce::jmax (1.0f, x1 - x0), r.getHeight()), r.getHeight() * 0.5f);
        g.setColour (C::dim); g.fillRect (r.getCentreX() - 0.5f, r.getY() - 2.0f, 1.0f, r.getHeight() + 4.0f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto col = box.findColour (juce::ComboBox::textColourId);
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (C::field); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (box.isMouseOver (true) ? C::dim : C::border); g.drawRoundedRectangle (r, 4.0f, 1.0f);
        juce::Path tri; const float ax = (float) w - 11.0f, ay = (float) h * 0.5f;
        tri.addTriangle (ax - 3.5f, ay - 1.8f, ax + 3.5f, ay - 1.8f, ax, ay + 2.2f);
        g.setColour (col.withAlpha (0.7f)); g.fillPath (tri);
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
        const bool on = b.getToggleState();
        if ((bool) b.getProperties().getWithDefault ("tab", false))
        {
            // page tab: no box, just a hover wash (the active underline is drawn by the editor)
            if (over || down) { g.setColour (juce::Colours::white.withAlpha (down ? 0.07f : 0.04f)); g.fillRoundedRectangle (r, 4.0f); }
            return;
        }
        g.setColour (down ? juce::Colour (0xff2c3138) : over ? juce::Colour (0xff292e35) : juce::Colour (0xff24282e));
        g.fillRoundedRectangle (r, 4.0f);
        if (on) { g.setColour (col.withAlpha (0.18f)); g.fillRoundedRectangle (r, 4.0f); }
        g.setColour (on ? col.withAlpha (0.8f) : over ? C::dim.withAlpha (0.8f) : C::border); g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const auto col = b.findColour (juce::TextButton::buttonColourId);
        const bool tab = (bool) b.getProperties().getWithDefault ("tab", false);
        g.setColour (! b.isEnabled() ? C::dim : tab ? (b.getToggleState() ? C::text : C::dim) : b.getToggleState() ? col.interpolatedWith (juce::Colours::white, 0.3f) : C::text.interpolatedWith (col, 0.25f));
        g.setFont (font (juce::jmin (tab ? 12.0f : 12.5f, b.getHeight() * 0.44f)));
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
// Modulation host: implemented by the editor so knobs can show / accept modulation.
struct ModRing { int slot; float amt; float live; juce::Colour col; };
struct ModHost
{
    virtual ~ModHost() = default;
    virtual std::vector<ModRing> ringsFor (int dst) = 0;
    virtual void dropMod (int src, int dst) = 0;
    virtual void knobMenu (int dst, juce::Component* target) = 0;
    static ModHost*& current() { static ModHost* h = nullptr; return h; }
};

inline juce::Colour sourceColour (int src)
{
    static const juce::Colour cols[] = { C::dim, C::blue, C::violet, C::cyan, C::magenta, C::green, C::amber, C::magenta,
                                         C::orange, C::text, C::text, C::text, C::cyan, C::magenta, C::amber, C::green, C::red, C::text };
    return cols[juce::jlimit (0, 17, src)];
}

class Knob : public juce::Component, public juce::DragAndDropTarget
{
public:
    struct ModSlider : public juce::Slider
    {
        std::function<void()> onMenu;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && onMenu) { onMenu(); return; }
            juce::Slider::mouseDown (e);
        }
    };

    Knob (juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& lbl, juce::Colour col, bool bipolar = false)
        : label (lbl), spec (synth::findSpec (id)), dst (synth::destForParam (id))
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
        slider.onMenu = [this] { if (dst > 0 && ModHost::current() != nullptr) ModHost::current()->knobMenu (dst, this); };
        slider.addMouseListener (this, false);
        colour = col;
        if (dst > 0) slider.setTooltip ("Drag an LFO / ENV / MACRO badge here to modulate. Right-click for modulation options.");
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
    void paintOverChildren (juce::Graphics& g) override
    {
        if (dragOver)
        {
            auto b = slider.getBounds().toFloat().reduced (2.0f);
            g.setColour (C::cyan.withAlpha (0.18f)); g.fillEllipse (b);
            g.setColour (C::cyan); g.drawEllipse (b, 1.5f);
        }
        if (rings.empty()) return;
        auto b = slider.getBounds().toFloat();
        const float size = juce::jmin (b.getWidth(), b.getHeight());
        const auto c = b.getCentre();
        const float R = size * 0.5f - 2.0f, rr = R * 0.70f;
        const auto rp = slider.getRotaryParameters();
        const float a0 = rp.startAngleRadians, a1 = rp.endAngleRadians;
        const float pos = (float) slider.valueToProportionOfLength (slider.getValue());
        float k = 0.0f;
        for (auto& m : rings)
        {
            const float r2 = rr - k * 3.0f;
            const float from = juce::jlimit (0.0f, 1.0f, pos), to = juce::jlimit (0.0f, 1.0f, pos + m.amt);
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, r2, r2, 0.0f, a0 + from * (a1 - a0), a0 + to * (a1 - a0), true);
            g.setColour (m.col.withAlpha (0.85f)); g.strokePath (arc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const float live = juce::jlimit (0.0f, 1.0f, pos + m.amt * m.live);
            const float ang = a0 + live * (a1 - a0);
            drawLed (g, { c.x + r2 * std::sin (ang), c.y - r2 * std::cos (ang) }, 2.2f, m.col, 1.0f);
            k += 1.0f;
        }
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    // drag & drop modulation
    bool isInterestedInDragSource (const SourceDetails& d) override { return dst > 0 && d.description.toString().startsWith ("mod:"); }
    void itemDragEnter (const SourceDetails&) override { dragOver = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dragOver = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dragOver = false;
        if (ModHost::current() != nullptr) ModHost::current()->dropMod (d.description.toString().fromFirstOccurrenceOf ("mod:", false, false).getIntValue(), dst);
        refreshRings(); repaint();
    }

    bool refreshRings()
    {
        if (dst <= 0 || ModHost::current() == nullptr) return false;
        rings = ModHost::current()->ringsFor (dst);
        return ! rings.empty();
    }
    bool hasRings() const { return ! rings.empty(); }
    int modDest() const { return dst; }

    ModSlider slider;
private:
    juce::String label;
public:
    void setLabel (const juce::String& s) { if (s != label) { label = s; repaint(); } }
private:
    const synth::Spec* spec;
    int dst = 0;
    juce::Colour colour;
    bool dragOver = false;
    std::vector<ModRing> rings;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

//==============================================================================
// Draggable modulation source badge ("drag me onto a knob")
class ModBadge : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModBadge (int source, juce::String txt, juce::Colour c) : src (source), text (std::move (txt)), col (c)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag onto any knob to modulate it");
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (hover ? juce::Colour (0xff2a2f36) : juce::Colour (0xff23272d)); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (col.withAlpha (hover ? 0.9f : 0.55f)); g.drawRoundedRectangle (r, 4.0f, 1.0f);
        // drag grip + live level bar
        g.setColour (col.withAlpha (0.25f + 0.75f * level)); g.fillRoundedRectangle (r.getX() + 5.0f, r.getCentreY() - 4.0f, 3.0f, 8.0f, 1.5f);
        g.setColour (C::text.interpolatedWith (col, 0.35f));
        g.setFont (font (9.5f));
        g.drawText (text, r.withTrimmedLeft (13.0f), juce::Justification::centredLeft);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() < 4) return;
        if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! dc->isDragAndDropActive()) dc->startDragging ("mod:" + juce::String (src), this);
    }
    void setLevel (float v) { if (std::abs (v - level) > 0.02f) { level = v; repaint(); } }
    int source() const { return src; }
private:
    int src; juce::String text; juce::Colour col;
    bool hover = false; float level = 0.0f;
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
            // compact power switch
            const float d = juce::jmin (r.getWidth(), r.getHeight()) * 0.72f;
            const auto c = r.getCentre();
            g.setColour (on ? colour : juce::Colour (0xff2a2f36)); g.fillEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d);
            g.setColour (on ? colour.brighter (0.3f) : (over ? C::dim : juce::Colour (0xff3a4049))); g.drawEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d, 1.0f);
            if (on) { g.setColour (juce::Colour (0xff10141a)); g.fillEllipse (c.x - d * 0.16f, c.y - d * 0.16f, d * 0.32f, d * 0.32f); }
            return;
        }
        r = r.reduced (1.0f);
        g.setColour (on ? juce::Colour (0xff23282f) : juce::Colour (0xff1f2328)); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? colour.withAlpha (0.65f) : (over ? C::dim : C::border)); g.drawRoundedRectangle (r, 4.0f, 1.0f);
        auto box = juce::Rectangle<float> (r.getX() + 7.0f, r.getCentreY() - 4.5f, 9.0f, 9.0f);
        g.setColour (on ? colour : juce::Colour (0xff353b44)); g.fillRoundedRectangle (box, 2.0f);
        g.setColour (on ? C::text : C::dim);
        g.setFont (font (10.5f));
        g.drawText (getButtonText(), r.withTrimmedLeft (22.0f), juce::Justification::centredLeft);
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
// Level meter (vertical or horizontal), smooth bar with peak hold. level is linear amplitude.
inline void ledMeter (juce::Graphics& g, juce::Rectangle<float> r, float level, float hold, bool vertical, int)
{
    const float db = juce::Decibels::gainToDecibels (level, -60.0f);
    const float holdDb = juce::Decibels::gainToDecibels (hold, -60.0f);
    const float t = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
    const float th = juce::jlimit (0.0f, 1.0f, (holdDb + 48.0f) / 48.0f);
    g.setColour (C::field); g.fillRoundedRectangle (r, 2.0f);
    juce::ColourGradient gr = vertical ? juce::ColourGradient (C::green, r.getX(), r.getBottom(), C::red, r.getX(), r.getY(), false)
                                       : juce::ColourGradient (C::green, r.getX(), r.getY(), C::red, r.getRight(), r.getY(), false);
    gr.addColour (0.78, C::green); gr.addColour (0.9, C::amber);
    g.setGradientFill (gr);
    if (vertical) g.fillRoundedRectangle (r.withTop (r.getBottom() - r.getHeight() * t), 2.0f);
    else g.fillRoundedRectangle (r.withWidth (r.getWidth() * t), 2.0f);
    if (th > 0.02f)
    {
        g.setColour (C::text.withAlpha (0.8f));
        if (vertical) g.fillRect (r.getX(), r.getBottom() - r.getHeight() * th, r.getWidth(), 1.5f);
        else g.fillRect (r.getX() + r.getWidth() * th - 1.0f, r.getY(), 1.5f, r.getHeight());
    }
}

} // namespace lumen::led
