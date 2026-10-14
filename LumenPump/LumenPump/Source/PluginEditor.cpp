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
CurveEditor::CurveEditor (LumenPumpProcessor& p) : proc (p)
{
    pts = proc.getPoints();
    setMouseCursor (MouseCursor::CrosshairCursor);
}

Rectangle<float> CurveEditor::plotArea() const
{
    auto r = getLocalBounds().toFloat().reduced (14.0f, 10.0f);
    r.removeFromTop (24.0f);
    r.removeFromBottom (16.0f);
    return r;
}

Point<float> CurveEditor::toScreen (lumen::CurvePoint c) const
{
    auto r = plotArea();
    return { r.getX() + c.x * r.getWidth(), r.getBottom() - c.y * r.getHeight() };
}

lumen::CurvePoint CurveEditor::fromScreen (Point<float> s) const
{
    auto r = plotArea();
    return { jlimit (0.0f, 1.0f, (s.x - r.getX()) / r.getWidth()),
             jlimit (0.0f, 1.0f, (r.getBottom() - s.y) / r.getHeight()) };
}

int CurveEditor::findPoint (Point<float> s) const
{
    for (int i = 0; i < (int) pts.size(); ++i)
        if (toScreen (pts[(size_t) i]).getDistanceFrom (s) < 9.0f)
            return i;
    return -1;
}

void CurveEditor::mouseMove (const MouseEvent& e)
{
    const int h = findPoint (e.position);
    if (h != hoverIndex) { hoverIndex = h; repaint(); }
}

void CurveEditor::mouseDown (const MouseEvent& e)
{
    pts = proc.getPoints();
    dragIndex = findPoint (e.position);
    if (e.mods.isPopupMenu() && dragIndex > 0 && dragIndex < (int) pts.size() - 1)
    {
        pts.erase (pts.begin() + dragIndex);
        dragIndex = -1;
        proc.setPoints (pts);
        pts = proc.getPoints();
    }
}

void CurveEditor::mouseDrag (const MouseEvent& e)
{
    if (dragIndex < 0 || dragIndex >= (int) pts.size()) return;
    auto c = fromScreen (e.position);
    const int last = (int) pts.size() - 1;
    if (dragIndex == 0) c.x = 0.0f;
    else if (dragIndex == last) c.x = 1.0f;
    else c.x = jlimit (pts[(size_t) dragIndex - 1].x + 0.004f, pts[(size_t) dragIndex + 1].x - 0.004f, c.x);
    pts[(size_t) dragIndex] = c;
    proc.setPoints (pts);
    repaint();
}

void CurveEditor::mouseUp (const MouseEvent&) { dragIndex = -1; pts = proc.getPoints(); }

void CurveEditor::mouseDoubleClick (const MouseEvent& e)
{
    pts = proc.getPoints();
    const int h = findPoint (e.position);
    if (h > 0 && h < (int) pts.size() - 1)
        pts.erase (pts.begin() + h);                       // remove point
    else if (h < 0 && pts.size() < 32)
        pts.push_back (fromScreen (e.position));          // add point
    proc.setPoints (pts);
    pts = proc.getPoints();
    repaint();
}

void CurveEditor::paint (Graphics& g)
{
    pts = dragIndex >= 0 ? pts : proc.getPoints();
    auto b = getLocalBounds().toFloat();
    g.setColour (C::panel);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (C::panelEdge);
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    auto head = b.reduced (14.0f, 10.0f).removeFromTop (18.0f);
    g.setFont (uiFont (11.5f, true));
    g.setColour (C::dim);
    g.drawText ("PUMP CURVE", head, Justification::centredLeft);
    const int trig = (int) proc.apvts.getRawParameterValue (IDs::trigger)->load();
    const int rate = (int) proc.apvts.getRawParameterValue (IDs::rate)->load();
    g.setColour (C::text);
    g.drawText (rateChoices()[rate] + "   " + triggerChoices()[trig].toUpperCase(), head, Justification::centredRight);

    auto r = plotArea();
    g.setColour (C::bg.withAlpha (0.55f));
    g.fillRect (r);

    // Grid: quarters strong, sixteenths faint
    for (int i = 1; i < 16; ++i)
    {
        g.setColour (C::track.withAlpha (i % 4 == 0 ? 0.9f : 0.35f));
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / 16.0f), r.getY(), r.getBottom());
    }
    g.setColour (C::track.withAlpha (0.5f));
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    // Rendered curve (with current smooth)
    const float sm = proc.apvts.getRawParameterValue (IDs::smooth)->load() * 0.01f;
    std::vector<float> table (257);
    lumen::renderCurve (pts, sm, table.data(), 256);
    Path line, fill;
    for (int i = 0; i <= 256; ++i)
    {
        const auto s = toScreen ({ (float) i / 256.0f, table[(size_t) i] });
        if (i == 0) { line.startNewSubPath (s); fill.startNewSubPath (r.getX(), r.getBottom()); }
        else line.lineTo (s);
        fill.lineTo (s);
    }
    fill.lineTo (r.getRight(), r.getBottom());
    fill.closeSubPath();
    g.setGradientFill (ColourGradient (C::violet.withAlpha (0.45f), r.getX(), r.getY(), C::cyan.withAlpha (0.04f), r.getX(), r.getBottom(), false));
    g.fillPath (fill);
    g.setGradientFill (ColourGradient (C::cyan, r.getX(), 0, C::violet, r.getRight(), 0, false));
    g.strokePath (line, PathStrokeType (2.2f));

    // Playhead
    const float ph = proc.phase.load();
    const float x = r.getX() + ph * r.getWidth();
    g.setColour (C::text.withAlpha (0.45f));
    g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
    const float lv = proc.level.load();
    g.setColour (C::text);
    g.fillEllipse (x - 4.0f, r.getBottom() - lv * r.getHeight() - 4.0f, 8.0f, 8.0f);

    // Points
    for (int i = 0; i < (int) pts.size(); ++i)
    {
        const auto s = toScreen (pts[(size_t) i]);
        const bool hot = (i == hoverIndex || i == dragIndex);
        g.setColour (C::bg);
        g.fillEllipse (s.x - 6.0f, s.y - 6.0f, 12.0f, 12.0f);
        g.setColour (hot ? C::amber : C::cyan);
        g.drawEllipse (s.x - 5.0f, s.y - 5.0f, 10.0f, 10.0f, 2.0f);
    }

    // Footer help + trigger flash
    auto foot = getLocalBounds().toFloat().reduced (14.0f, 6.0f).removeFromBottom (14.0f);
    g.setFont (uiFont (10.0f));
    g.setColour (C::dim);
    g.drawText ("drag points  |  double-click: add / remove  |  right-click: remove", foot, Justification::centredLeft);
    if (proc.triggerFlash.load() > 0)
    {
        g.setColour (C::amber);
        g.drawText ("TRIGGER", foot, Justification::centredRight);
    }
}

//==============================================================================
BandView::BandView (LumenPumpProcessor& p) : proc (p)
{
    solo.setClickingTogglesState (true);
    solo.setTooltip ("Hear only the pumping band while you set Band Low / Band High");
    addAndMakeVisible (solo);
    soloAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::solo, solo);
}

void BandView::resized()
{
    solo.setBounds (getLocalBounds().reduced (10, 0).removeFromBottom (28).removeFromRight (110));
}

void BandView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (10.0f, 0.0f);
    g.setFont (uiFont (12.5f));
    g.setColour (C::dim);
    g.drawText ("PUMP BAND", r.removeFromTop (16.0f), Justification::centredLeft);
    r.removeFromTop (6.0f);
    auto bottom = r.removeFromBottom (28.0f);
    r.removeFromBottom (14.0f);
    auto axis = r.removeFromBottom (12.0f);

    const float lo = proc.apvts.getRawParameterValue (IDs::lowHz)->load();
    const float hi = jmax (lo * 1.06f, proc.apvts.getRawParameterValue (IDs::highHz)->load());
    const float depth = proc.apvts.getRawParameterValue (IDs::depth)->load() * 0.01f;
    const float outside = proc.apvts.getRawParameterValue (IDs::outside)->load() * 0.01f;

    auto xOf = [&] (float f) { return r.getX() + r.getWidth() * std::log (jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f); };

    g.setColour (C::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 4.0f);

    // Outside regions (height = how much they pump)
    const float oh = r.getHeight() * jmax (0.06f, outside);
    g.setColour (C::dim.withAlpha (0.35f));
    g.fillRect (Rectangle<float> (r.getX(), r.getBottom() - oh, xOf (lo) - r.getX(), oh));
    g.fillRect (Rectangle<float> (xOf (hi), r.getBottom() - oh, r.getRight() - xOf (hi), oh));

    // Pump band
    const float bh = r.getHeight() * jmax (0.06f, depth);
    auto bandRect = Rectangle<float> (xOf (lo), r.getBottom() - bh, xOf (hi) - xOf (lo), bh);
    g.setGradientFill (ColourGradient (C::cyan.withAlpha (0.85f), bandRect.getX(), 0, C::violet.withAlpha (0.85f), bandRect.getRight(), 0, false));
    g.fillRect (bandRect);

    // Live pump level inside the band
    const float lv = proc.level.load();
    const float gIn = 1.0f - depth * (1.0f - lv);
    g.setColour (C::text.withAlpha (0.9f));
    g.drawHorizontalLine ((int) (r.getBottom() - r.getHeight() * gIn), bandRect.getX(), bandRect.getRight());

    g.setFont (uiFont (9.5f));
    g.setColour (C::dim);
    for (float f : { 20.0f, 100.0f, 1000.0f, 10000.0f })
        g.drawText (f < 1000.0f ? String ((int) f) : String ((int) (f / 1000.0f)) + "k",
                    Rectangle<float> (xOf (f) - 16.0f, axis.getY(), 32.0f, axis.getHeight()), Justification::centred);

    g.setFont (uiFont (11.5f, true));
    g.setColour (C::text);
    auto fmt = [] (float f) { return f < 1000.0f ? String (roundToInt (f)) + " Hz" : String (f / 1000.0f, 1) + " kHz"; };
    g.drawText (fmt (lo) + "  -  " + fmt (hi), bottom, Justification::centredLeft);
}

//==============================================================================
LumenPumpEditor::LumenPumpEditor (LumenPumpProcessor& p)
    : AudioProcessorEditor (&p), proc (p), curve (p), band (p)
{
    setLookAndFeel (&lnf);

    const std::pair<const char*, const char*> knobDefs[] = {
        { IDs::lowHz, "Band Low" },  { IDs::highHz, "Band High" }, { IDs::outside, "Outside" },
        { IDs::depth, "Depth" },     { IDs::smooth, "Smooth" },    { IDs::offset, "Offset" },
        { IDs::scThresh, "SC Threshold" }, { IDs::output, "Output" } };
    for (auto& [id, title] : knobDefs)
    {
        knobs.push_back (std::make_unique<Knob> (proc.apvts, id, title));
        addAndMakeVisible (*knobs.back());
    }

    addAndMakeVisible (curve);
    addAndMakeVisible (band);

    triggerBox.addItemList (triggerChoices(), 1);
    triggerBox.setTooltip ("Sync: DAW grid  |  Sidechain: fires on the kick routed to the sidechain  |  MIDI: fires on notes");
    addAndMakeVisible (triggerBox);
    triggerAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::trigger, triggerBox);

    rateBox.addItemList (rateChoices(), 1);
    addAndMakeVisible (rateBox);
    rateAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::rate, rateBox);

    presetBox.setTextWhenNothingSelected ("Curve presets");
    presetBox.addItemList (LumenPumpProcessor::presetNames(), 1);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx < 0) return;
        float sm = 0.5f;
        proc.setPoints (LumenPumpProcessor::presetPoints (idx, sm));
        setParam (IDs::smooth, sm * 100.0f);
        presetBox.setSelectedId (0, dontSendNotification);
        curve.repaint();
    };
    addAndMakeVisible (presetBox);

    learnButton.setTooltip ("Listens to the kick (sidechain, or this track if none) and fits the curve and band to it");
    learnButton.onClick = [this] { learnClicked(); };
    addAndMakeVisible (learnButton);
    undoButton.onClick = [this] { undoLearn(); };
    undoButton.setEnabled (false);
    addAndMakeVisible (undoButton);

    setSize (900, 560);
    startTimerHz (30);
}

LumenPumpEditor::~LumenPumpEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void LumenPumpEditor::paint (Graphics& g)
{
    g.setGradientFill (ColourGradient (C::bg2, 0, 0, C::bg, 0, (float) getHeight(), false));
    g.fillAll();

    g.setFont (uiFont (26.0f, true));
    g.setGradientFill (ColourGradient (C::cyan, 20, 0, C::violet, 160, 0, false));
    g.drawText ("LUMEN", 20, 14, 200, 34, Justification::centredLeft);
    g.setFont (uiFont (12.0f));
    g.setColour (C::dim);
    g.drawText ("SMART PUMP", 128, 24, 200, 18, Justification::centredLeft);

    auto s = smartArea.toFloat();
    g.setColour (C::panel);
    g.fillRoundedRectangle (s, 10.0f);
    g.setColour (C::panelEdge);
    g.drawRoundedRectangle (s.reduced (0.5f), 10.0f, 1.0f);

    auto inner = smartArea.reduced (14, 10);
    g.setFont (uiFont (11.5f, true));
    g.setColour (C::dim);
    g.drawText ("SMART ASSIST", inner.removeFromTop (18), Justification::centredLeft);
    g.setFont (uiFont (19.0f, true));
    g.setColour (C::text);
    g.drawText (headline, inner.removeFromTop (28), Justification::centredLeft);

    // Sidechain status
    const int trig = (int) proc.apvts.getRawParameterValue (IDs::trigger)->load();
    const bool sc = proc.scConnected.load();
    auto row = inner.removeFromTop (17);
    g.setFont (uiFont (11.0f));
    g.setColour (C::dim);
    g.drawText ("Sidechain", row.removeFromLeft (82), Justification::centredLeft);
    auto tr = row.reduced (0, 5).toFloat();
    g.setColour (C::track);
    g.fillRoundedRectangle (tr, 2.5f);
    const float scDb = Decibels::gainToDecibels (proc.scLevel.load(), -60.0f);
    g.setColour (proc.triggerFlash.load() > 0 ? C::amber : C::cyan);
    g.fillRoundedRectangle (tr.withWidth (tr.getWidth() * jlimit (0.0f, 1.0f, (scDb + 60.0f) / 60.0f)), 2.5f);
    // threshold tick
    const float th = proc.apvts.getRawParameterValue (IDs::scThresh)->load();
    g.setColour (C::text.withAlpha (0.7f));
    g.drawVerticalLine ((int) (tr.getX() + tr.getWidth() * (th + 60.0f) / 60.0f), tr.getY() - 2, tr.getBottom() + 2);

    row = inner.removeFromTop (17);
    g.setColour (C::dim);
    g.drawText ("Tempo", row.removeFromLeft (82), Justification::centredLeft);
    g.setColour (C::text);
    g.drawText (String (proc.hostBpm.load(), 1) + " BPM", row, Justification::centredLeft);

    inner.removeFromTop (6);
    g.setFont (uiFont (11.0f));
    String st = statusText;
    if (trig == 1 && ! sc && ! statusText.startsWith ("Kick"))
        st = "Sidechain mode: route the kick to the sidechain input";
    g.setColour (st.startsWith ("Sidechain mode") ? C::amber : C::cyan.withAlpha (0.9f));
    g.drawFittedText (st, inner.removeFromTop (30), Justification::topLeft, 2);

    if (proc.isCapturing())
    {
        auto pr = Rectangle<float> (s.getX() + 14, s.getBottom() - 50, s.getWidth() - 28, 3);
        g.setColour (C::track);
        g.fillRect (pr);
        g.setColour (C::cyan);
        g.fillRect (pr.withWidth (pr.getWidth() * proc.captureProgress()));
    }

    const char* sections[] = { "FREQUENCY BAND", "SHAPE & TRIGGER" };
    for (int r = 0; r < 2; ++r)
    {
        const int y = 300 + r * 126;
        g.setFont (uiFont (11.0f, true));
        g.setColour (C::dim.withAlpha (0.8f));
        g.drawText (sections[r], 20, y, 200, 14, Justification::centredLeft);
        g.setColour (C::panelEdge);
        g.drawHorizontalLine (y + 7, 150.0f, (float) getWidth() - 20.0f);
    }
}

void LumenPumpEditor::resized()
{
    triggerBox.setBounds (410, 18, 140, 28);
    rateBox.setBounds (558, 18, 110, 28);
    presetBox.setBounds (676, 18, 208, 28);

    curve.setBounds (16, 66, 548, 222);
    smartArea = { 576, 66, 308, 222 };
    learnButton.setBounds (smartArea.getX() + 14, smartArea.getBottom() - 42, 190, 30);
    undoButton.setBounds (smartArea.getX() + 212, smartArea.getBottom() - 42, 82, 30);

    const int colW = (getWidth() - 32) / 5;
    auto cell = [&] (int row, int col) { return Rectangle<int> (16 + col * colW, 318 + row * 126, colW, 104); };

    band.setBounds (cell (0, 0).getUnion (cell (0, 1)));
    for (int i = 0; i < 3; ++i) knobs[(size_t) i]->setBounds (cell (0, i + 2));
    for (int i = 3; i < 8; ++i) knobs[(size_t) i]->setBounds (cell (1, i - 3));
}

void LumenPumpEditor::timerCallback()
{
    const int f = proc.triggerFlash.load();
    if (f > 0) proc.triggerFlash.store (f - 1);

    const bool cap = proc.isCapturing();
    if (cap)
    {
        learnButton.setButtonText ("LISTENING " + String (roundToInt (proc.captureProgress() * 100.0f)) + "%");
        headline = "Listening...";
    }
    else if (wasCapturing)
        learnButton.setButtonText ("LEARN KICK");
    wasCapturing = cap;

    std::vector<float> buf;
    if (proc.takeCapture (buf))
    {
        const auto info = lumen::analyseKick (buf, (float) proc.getSampleRateSafe());
        if (info.valid) applyKick (info);
        else { headline = "No kick found"; statusText = "Play the kick louder or longer, then try again"; }
    }

    curve.repaint();
    band.repaint();
    repaint (smartArea);
}

float LumenPumpEditor::getParam (const char* id) const
{
    auto* param = proc.apvts.getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

void LumenPumpEditor::setParam (const char* id, float value)
{
    if (auto* param = proc.apvts.getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    }
}

void LumenPumpEditor::learnClicked()
{
    if (proc.isCapturing()) { proc.cancelCapture(); learnButton.setButtonText ("LEARN KICK"); headline = "Cancelled"; return; }
    proc.requestCapture();
    statusText = proc.scConnected.load() ? "Listening to the sidechain kick (4 s)..." : "No sidechain: listening to this track (4 s)...";
}

void LumenPumpEditor::applyKick (const lumen::KickInfo& k)
{
    undoPoints = proc.getPoints();
    undoValues.clear();
    for (auto* id : { IDs::smooth, IDs::lowHz, IDs::highHz, IDs::trigger })
        undoValues.emplace_back (id, getParam (id));

    // Fit the curve so the duck lasts as long as the kick's audible body
    const double beats = kRateBeats[jlimit (0, kNumRates - 1, (int) getParam (IDs::rate))];
    const double cycleMs = beats * 60000.0 / jmax (20.0, proc.hostBpm.load());
    const float xr = jlimit (0.12f, 0.9f, (float) (k.decayMs / cycleMs));
    proc.setPoints ({ { 0.0f, 0.0f }, { xr * 0.35f, 0.12f }, { xr, 1.0f }, { 1.0f, 1.0f } });
    setParam (IDs::smooth, 60.0f);

    // Band: from the bottom up to a few harmonics above the kick's fundamental
    float high = 400.0f;
    if (k.fundamentalHz > 25.0f)
        high = jlimit (150.0f, 500.0f, std::round (k.fundamentalHz * 5.0f / 10.0f) * 10.0f);
    setParam (IDs::lowHz, 20.0f);
    setParam (IDs::highHz, high);
    if (proc.scConnected.load()) setParam (IDs::trigger, 1.0f);

    headline = "Kick " + (k.fundamentalHz > 25.0f ? String (roundToInt (k.fundamentalHz)) + " Hz, " : String())
             + String (roundToInt (k.decayMs)) + " ms";
    statusText = "Kick: curve fitted to the decay, band 20 - " + String (roundToInt (high)) + " Hz"
               + (proc.scConnected.load() ? ", sidechain trigger on" : "");
    undoButton.setEnabled (true);
}

void LumenPumpEditor::undoLearn()
{
    if (! undoPoints.empty()) proc.setPoints (undoPoints);
    for (auto& [id, v] : undoValues) setParam (id.toRawUTF8(), v);
    undoPoints.clear(); undoValues.clear();
    undoButton.setEnabled (false);
    headline = "Ready";
    statusText = "Previous settings restored";
}
