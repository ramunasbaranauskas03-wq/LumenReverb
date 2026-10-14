#include "PluginEditor.h"

using namespace juce;
namespace C = Colours_;

static Font uiFont (float size, bool bold = false)
{
    return Font (FontOptions (size, bold ? Font::bold : Font::plain));
}

//==============================================================================
LumenLookAndFeel::LumenLookAndFeel()
{
    setColour (Label::textColourId, C::dim);
    setColour (TextButton::textColourOffId, C::text);
    setColour (TextButton::textColourOnId, C::bg);
    setColour (ComboBox::backgroundColourId, C::panel);
    setColour (ComboBox::outlineColourId, C::panelEdge);
    setColour (ComboBox::textColourId, C::text);
    setColour (ComboBox::arrowColourId, C::cyan);
    setColour (PopupMenu::backgroundColourId, C::panel);
    setColour (PopupMenu::textColourId, C::text);
    setColour (PopupMenu::highlightedBackgroundColourId, C::violet.withAlpha (0.4f));
}

void LumenLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                         float start, float end, Slider& s)
{
    const auto bounds = Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float r  = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 4.0f;
    const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
    const float angle = start + pos * (end - start);

    Path bgArc;
    bgArc.addCentredArc (cx, cy, r, r, 0.0f, start, end, true);
    g.setColour (C::track);
    g.strokePath (bgArc, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

    Path valArc;
    valArc.addCentredArc (cx, cy, r, r, 0.0f, start, angle, true);
    g.setGradientFill (ColourGradient (C::cyan, cx - r, cy + r, C::violet, cx + r, cy - r, false));
    g.strokePath (valArc, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

    const float kr = r * 0.70f;
    g.setGradientFill (ColourGradient (Colour (0xff2b3446), cx, cy - kr, Colour (0xff161b26), cx, cy + kr, false));
    g.fillEllipse (cx - kr, cy - kr, kr * 2.0f, kr * 2.0f);
    g.setColour (C::panelEdge);
    g.drawEllipse (cx - kr, cy - kr, kr * 2.0f, kr * 2.0f, 1.0f);

    g.setColour (s.isMouseOverOrDragging() ? C::cyan : C::text);
    g.drawLine (cx + std::sin (angle) * kr * 0.35f, cy - std::cos (angle) * kr * 0.35f,
                cx + std::sin (angle) * kr * 0.78f, cy - std::cos (angle) * kr * 0.78f, 2.5f);
}

void LumenLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    if (b.getToggleState())
        g.setGradientFill (ColourGradient (C::cyan, r.getX(), r.getY(), C::violet, r.getRight(), r.getBottom(), false));
    else
        g.setColour (down ? C::track : (highlighted ? C::panelEdge : C::panel));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (highlighted ? C::cyan.withAlpha (0.7f) : C::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

Font LumenLookAndFeel::getTextButtonFont (TextButton&, int) { return uiFont (13.0f, true); }
Font LumenLookAndFeel::getLabelFont (Label&)                { return uiFont (12.5f); }
Font LumenLookAndFeel::getComboBoxFont (ComboBox&)          { return uiFont (13.0f); }

//==============================================================================
Knob::Knob (AudioProcessorValueTreeState& state, const String& paramId, const String& title)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    slider.setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    slider.setColour (Slider::textBoxTextColourId, C::text);
    slider.setColour (Slider::textBoxHighlightColourId, C::violet.withAlpha (0.4f));
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 90, 18);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    auto* param = state.getParameter (paramId);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    addAndMakeVisible (slider);

    label.setText (title.toUpperCase(), dontSendNotification);
    label.setJustificationType (Justification::centred);
    addAndMakeVisible (label);

    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    slider.setBounds (r);
}

//==============================================================================
static const Colour bandColour[3] = { Colour (0xff5ce1e6), Colour (0xff9b7bff), Colour (0xffffb35c) };

void SatDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (C::panel); g.fillRoundedRectangle (b, 10.0f);
    g.setColour (C::panelEdge); g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);
    auto in = b.reduced (14.0f, 10.0f);
    g.setFont (uiFont (11.5f, true)); g.setColour (C::dim);
    g.drawText ("BANDS & CURVES", in.removeFromTop (18.0f), Justification::centredLeft);
    in.removeFromTop (6.0f);

    auto meters = in.removeFromRight (40.0f);
    in.removeFromRight (10.0f);

    // ---- band strip (log frequency) ----
    auto strip = in.removeFromTop (54.0f);
    auto axis = in.removeFromTop (14.0f);
    in.removeFromTop (8.0f);
    const float xl = proc.apvts.getRawParameterValue (IDs::xLow)->load();
    const float xh = jmax (xl * 1.5f, proc.apvts.getRawParameterValue (IDs::xHigh)->load());
    auto xOf = [&] (float f) { return strip.getX() + strip.getWidth() * std::log (jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f); };
    const float edges[4] = { strip.getX(), xOf (xl), xOf (xh), strip.getRight() };
    for (int k = 0; k < 3; ++k)
    {
        const float drive = proc.apvts.getRawParameterValue (IDs::drive (k))->load();
        const bool solo = proc.apvts.getRawParameterValue (IDs::solo (k))->load() > 0.5f;
        auto r = Rectangle<float> (edges[k], strip.getY(), edges[k + 1] - edges[k], strip.getHeight()).reduced (1.0f, 0.0f);
        g.setColour (bandColour[k].withAlpha (0.12f + 0.6f * drive / 36.0f));
        g.fillRoundedRectangle (r, 4.0f);
        if (solo) { g.setColour (C::text); g.drawRoundedRectangle (r, 4.0f, 1.5f); }
        g.setFont (uiFont (11.0f, true)); g.setColour (C::text);
        g.drawText (bandNames()[k], r.removeFromTop (18.0f), Justification::centred);
        g.setFont (uiFont (10.5f));
        const int mode = (int) proc.apvts.getRawParameterValue (IDs::mode (k))->load();
        g.drawText (modeChoices()[mode] + "  +" + String (drive, 1) + " dB", r, Justification::centred);
    }
    g.setFont (uiFont (9.5f)); g.setColour (C::dim);
    for (float f : { 20.0f, 100.0f, 1000.0f, 10000.0f })
        g.drawText (f < 1000.0f ? String ((int) f) : String ((int) (f / 1000.0f)) + "k",
                    Rectangle<float> (xOf (f) - 16.0f, axis.getY(), 32.0f, axis.getHeight()), Justification::centred);

    // ---- transfer curve per band ----
    const float w = (in.getWidth() - 20.0f) / 3.0f;
    for (int k = 0; k < 3; ++k)
    {
        auto r = Rectangle<float> (in.getX() + k * (w + 10.0f), in.getY(), w, in.getHeight());
        g.setColour (C::bg.withAlpha (0.55f)); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (C::track.withAlpha (0.6f));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        g.drawVerticalLine ((int) r.getCentreX(), r.getY(), r.getBottom());
        const int mode = (int) proc.apvts.getRawParameterValue (IDs::mode (k))->load();
        const float drive = Decibels::decibelsToGain (proc.apvts.getRawParameterValue (IDs::drive (k))->load());
        Path p;
        for (int i = 0; i <= 80; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / 80.0f;
            const float y = jlimit (-1.1f, 1.1f, lumen::shape (mode, x * drive));
            const float px = r.getX() + (x + 1.0f) * 0.5f * r.getWidth();
            const float py = r.getCentreY() - y * 0.45f * r.getHeight();
            if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        g.setColour (bandColour[k]);
        g.strokePath (p, PathStrokeType (2.0f));
    }

    // ---- meters ----
    g.setFont (uiFont (9.5f, true)); g.setColour (C::dim);
    g.drawText ("IN OUT", meters.removeFromTop (12.0f), Justification::centred);
    auto lvl = [] (float x) { return (Decibels::gainToDecibels (x, -60.0f) + 60.0f) / 60.0f; };
    const float bw = 12.0f;
    auto bar = [&] (int i, float v, Colour c)
    {
        auto t = Rectangle<float> (meters.getX() + 4.0f + i * (bw + 8.0f), meters.getY() + 4.0f, bw, meters.getHeight() - 4.0f);
        g.setColour (C::track); g.fillRoundedRectangle (t, 3.0f);
        g.setColour (c); g.fillRoundedRectangle (t.withTop (t.getBottom() - t.getHeight() * jlimit (0.0f, 1.0f, v)), 3.0f);
    };
    bar (0, lvl (proc.inLevel.load()), C::cyan);
    bar (1, lvl (proc.outLevel.load()), C::violet);
}

//==============================================================================
LumenSaturatorEditor::LumenSaturatorEditor (LumenSaturatorProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p)
{
    setLookAndFeel (&lnf);
    for (int b = 0; b < 3; ++b)
    {
        knobs.push_back (std::make_unique<Knob> (proc.apvts, IDs::drive (b), "Drive"));
        knobs.push_back (std::make_unique<Knob> (proc.apvts, IDs::mix (b), "Mix"));
        knobs.push_back (std::make_unique<Knob> (proc.apvts, IDs::level (b), "Level"));
        modeBox[b].addItemList (modeChoices(), 1);
        addAndMakeVisible (modeBox[b]);
        modeAtt[b] = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::mode (b), modeBox[b]);
        soloButton[b].setButtonText ("SOLO");
        soloButton[b].setClickingTogglesState (true);
        addAndMakeVisible (soloButton[b]);
        soloAtt[b] = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::solo (b), soloButton[b]);
    }
    knobs.push_back (std::make_unique<Knob> (proc.apvts, IDs::xLow, "Crossover Low"));
    knobs.push_back (std::make_unique<Knob> (proc.apvts, IDs::xHigh, "Crossover High"));
    knobs.push_back (std::make_unique<Knob> (proc.apvts, IDs::output, "Output"));
    for (auto& k : knobs) addAndMakeVisible (*k);
    addAndMakeVisible (display);

    osBox.addItemList ({ "Oversampling 1x", "Oversampling 2x", "Oversampling 4x", "Oversampling 8x", "Oversampling 16x" }, 1);
    osBox.setTooltip ("Higher = cleaner saturation (less aliasing), more CPU");
    addAndMakeVisible (osBox);
    osAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::oversample, osBox);

    autoGainButton.setClickingTogglesState (true);
    autoGainButton.setTooltip ("Keeps each band as loud as it was, so you judge the colour, not the volume");
    addAndMakeVisible (autoGainButton);
    autoAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::autoGain, autoGainButton);

    learnButton.setTooltip ("Listens for ~6 s and sets modes, drive and crossovers for the material");
    learnButton.onClick = [this] { learnClicked(); };
    addAndMakeVisible (learnButton);
    undoButton.onClick = [this] { undoLearn(); };
    undoButton.setEnabled (false);
    addAndMakeVisible (undoButton);

    setSize (900, 690);
    startTimerHz (30);
}

LumenSaturatorEditor::~LumenSaturatorEditor() { stopTimer(); setLookAndFeel (nullptr); }

float LumenSaturatorEditor::getParam (const String& id) const { auto* q = proc.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); }
void LumenSaturatorEditor::setParam (const String& id, float v)
{
    if (auto* q = proc.apvts.getParameter (id)) { q->beginChangeGesture(); q->setValueNotifyingHost (q->convertTo0to1 (v)); q->endChangeGesture(); }
}

void LumenSaturatorEditor::learnClicked()
{
    if (proc.analyzer.isLearning()) { proc.analyzer.cancelLearn(); learnButton.setButtonText ("LEARN"); headline = "Cancelled"; return; }
    proc.analyzer.requestLearn();
    statusText = "Listening to the material...";
}

void LumenSaturatorEditor::applySuggestion (const lumen::Features& f)
{
    using namespace lumen::SmartAssist;
    const auto m = classify (f);
    struct B { int mode; float drive, mix; };
    struct S { B b[3]; float xl, xh; const char* why; };
    S s;
    switch (m)
    {
        case Material::Percussive: s = { { { 0, 6, 100 }, { 2, 9, 100 }, { 0, 4, 80 } }, 150, 4000, "punchy low, clipped mids, airy top" }; break;
        case Material::Bass:       s = { { { 1, 6, 100 }, { 0, 10, 100 }, { 0, 2, 60 } }, 120, 2500, "warm sub, growling mids" }; break;
        case Material::Pad:        s = { { { 0, 4, 70 }, { 0, 5, 70 }, { 0, 3, 70 } }, 200, 4000, "gentle tape glue" }; break;
        case Material::FullMix:    s = { { { 0, 3, 60 }, { 0, 3, 60 }, { 0, 2, 60 } }, 150, 4000, "subtle tape on the whole mix" }; break;
        case Material::VocalLead:
        case Material::Silence:
        default:                   s = { { { 0, 0, 100 }, { 1, 8, 70 }, { 0, 4, 60 } }, 250, 5000, "clean lows, tube mids, tape presence" }; break;
    }
    undoValues.clear();
    for (int b = 0; b < 3; ++b)
        for (auto& id : { IDs::mode (b), IDs::drive (b), IDs::mix (b) }) undoValues.emplace_back (id, getParam (id));
    undoValues.emplace_back (IDs::xLow, getParam (IDs::xLow));
    undoValues.emplace_back (IDs::xHigh, getParam (IDs::xHigh));

    for (int b = 0; b < 3; ++b)
    {
        setParam (IDs::mode (b), (float) s.b[b].mode);
        setParam (IDs::drive (b), s.b[b].drive);
        setParam (IDs::mix (b), s.b[b].mix);
    }
    setParam (IDs::xLow, s.xl);
    setParam (IDs::xHigh, s.xh);
    setParam (IDs::autoGain, 1.0f);
    headline = name (m);
    statusText = String ("Set for ") + name (m) + ": " + s.why;
    undoButton.setEnabled (true);
}

void LumenSaturatorEditor::undoLearn()
{
    for (auto& [id, v] : undoValues) setParam (id, v);
    undoValues.clear(); undoButton.setEnabled (false);
    headline = "Ready"; statusText = "Previous settings restored";
}

void LumenSaturatorEditor::timerCallback()
{
    const bool learning = proc.analyzer.isLearning();
    if (learning) { learnButton.setButtonText ("LISTENING " + String (roundToInt (proc.analyzer.getLearnProgress() * 100.0f)) + "%"); headline = "Listening..."; }
    else if (wasLearning) learnButton.setButtonText ("LEARN");
    wasLearning = learning;
    lumen::Features f;
    if (proc.analyzer.consumeLearnResult (f)) applySuggestion (f);
    display.repaint();
    repaint (smartArea);
}

void LumenSaturatorEditor::paint (Graphics& g)
{
    g.setGradientFill (ColourGradient (C::bg2, 0, 0, C::bg, 0, (float) getHeight(), false));
    g.fillAll();
    g.setFont (uiFont (26.0f, true));
    g.setGradientFill (ColourGradient (C::cyan, 20, 0, C::violet, 160, 0, false));
    g.drawText ("LUMEN", 20, 14, 200, 34, Justification::centredLeft);
    g.setFont (uiFont (12.0f)); g.setColour (C::dim);
    g.drawText ("SMART SATURATOR", 128, 24, 220, 18, Justification::centredLeft);

    auto s = smartArea.toFloat();
    g.setColour (C::panel); g.fillRoundedRectangle (s, 10.0f);
    g.setColour (C::panelEdge); g.drawRoundedRectangle (s.reduced (0.5f), 10.0f, 1.0f);
    auto in = smartArea.reduced (14, 10);
    g.setFont (uiFont (11.5f, true)); g.setColour (C::dim);
    g.drawText ("SMART ASSIST", in.removeFromTop (18), Justification::centredLeft);
    g.setFont (uiFont (19.0f, true)); g.setColour (C::text);
    g.drawText (headline, in.removeFromTop (28), Justification::centredLeft);
    g.setFont (uiFont (11.0f)); g.setColour (C::cyan.withAlpha (0.9f));
    g.drawFittedText (statusText, in.removeFromTop (48), Justification::topLeft, 3);
    if (proc.analyzer.isLearning())
    {
        auto pr = Rectangle<float> (s.getX() + 14, s.getBottom() - 50, s.getWidth() - 28, 3);
        g.setColour (C::track); g.fillRect (pr);
        g.setColour (C::cyan); g.fillRect (pr.withWidth (pr.getWidth() * proc.analyzer.getLearnProgress()));
    }

    for (int r = 0; r < 3; ++r)
    {
        const int y = 300 + r * 126;
        g.setFont (uiFont (11.0f, true)); g.setColour (bandColour[r]);
        g.drawText (bandNames()[r] + " BAND", 20, y, 200, 14, Justification::centredLeft);
        g.setColour (C::panelEdge); g.drawHorizontalLine (y + 7, 120.0f, (float) getWidth() - 20.0f);
        g.setFont (uiFont (12.5f)); g.setColour (C::dim);
        g.drawText ("MODE", modeCell[r].withHeight (16), Justification::centred);
    }
}

void LumenSaturatorEditor::resized()
{
    osBox.setBounds (594, 18, 170, 28);
    autoGainButton.setBounds (772, 18, 112, 28);
    display.setBounds (16, 66, 548, 222);
    smartArea = { 576, 66, 308, 222 };
    learnButton.setBounds (smartArea.getX() + 14, smartArea.getBottom() - 42, 190, 30);
    undoButton.setBounds (smartArea.getX() + 212, smartArea.getBottom() - 42, 82, 30);

    const int colW = (getWidth() - 32) / 5;
    auto cell = [&] (int row, int col) { return Rectangle<int> (16 + col * colW, 318 + row * 126, colW, 104); };
    for (int b = 0; b < 3; ++b)
    {
        modeCell[b] = cell (b, 0).reduced (8, 0);
        modeBox[b].setBounds (modeCell[b].getX(), modeCell[b].getY() + 22, modeCell[b].getWidth(), 28);
        soloButton[b].setBounds (modeCell[b].getX(), modeCell[b].getY() + 58, modeCell[b].getWidth(), 28);
        for (int k = 0; k < 3; ++k) knobs[(size_t) (b * 3 + k)]->setBounds (cell (b, k + 1));
        knobs[(size_t) (9 + b)]->setBounds (cell (b, 4));
    }
}
