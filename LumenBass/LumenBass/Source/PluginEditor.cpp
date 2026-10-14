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
PhasePanel::PhasePanel (LumenBassProcessor& p) : proc (p)
{
    onButton.setClickingTogglesState (true);
    onButton.setTooltip ("Apply the phase rotation and delay to this track (the bass)");
    addAndMakeVisible (onButton);
    onAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::phaseOn, onButton);

    alignButton.setTooltip ("Play kick and bass together, then click: finds the rotation and delay that lock their low end");
    alignButton.onClick = [this]
    {
        if (proc.phase.isCapturing())
        {
            proc.phase.cancelCapture();
            status = "Cancelled";
            return;
        }
        if (! proc.scConnected.load())
        {
            status = "Route the kick to the sidechain input first";
            return;
        }
        proc.phase.requestCapture();
        status = "Listening to kick + bass...";
    };
    addAndMakeVisible (alignButton);
}

void PhasePanel::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromTop (20);
    auto row = r.removeFromTop (28);
    onButton.setBounds (row.removeFromLeft (56));
    row.removeFromLeft (6);
    alignButton.setBounds (row);
}

void PhasePanel::setParam (const char* id, float value)
{
    if (auto* param = proc.apvts.getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    }
}

void PhasePanel::poll()
{
    const bool capturing = proc.phase.isCapturing();
    if (capturing)
        alignButton.setButtonText ("LISTENING " + String (roundToInt (proc.phase.captureProgress() * 100.0f)) + "%");
    else if (wasCapturing)
        alignButton.setButtonText ("ALIGN");
    wasCapturing = capturing;

    lumen::PhaseAligner::Result res;
    if (proc.phase.takeResult (res))
    {
        if (! res.valid)
        {
            status = "Not enough low end in kick or bass";
        }
        else
        {
            setParam (IDs::phaseRot, res.rotationDeg);
            setParam (IDs::phaseDly, res.delayMs);
            setParam (IDs::phaseOn, 1.0f);
            status = "Aligned: " + String (roundToInt (res.rotationDeg)) + String (CharPointer_UTF8 ("\xc2\xb0")) + ", "
                   + String (res.delayMs, 2) + " ms  (" + String (res.corrBefore, 2) + " -> " + String (res.corrAfter, 2) + ")";
        }
    }
    repaint();
}

void PhasePanel::paint (Graphics& g)
{
    auto r = getLocalBounds().reduced (8, 0);
    g.setFont (uiFont (12.5f));
    g.setColour (C::dim);
    g.drawText ("KICK " + String (CharPointer_UTF8 ("\xe2\x86\x94")) + " BASS", r.removeFromTop (16), Justification::centred);
    r.removeFromTop (4 + 28 + 8);

    if (proc.phase.isCapturing())
    {
        auto pr = r.removeFromTop (3).toFloat();
        g.setColour (C::track);
        g.fillRect (pr);
        g.setColour (C::cyan);
        g.fillRect (pr.withWidth (pr.getWidth() * proc.phase.captureProgress()));
        r.removeFromTop (4);
    }

    g.setFont (uiFont (10.5f));
    g.setColour (status.startsWith ("Aligned") ? C::cyan : (status.startsWith ("Route") || status.startsWith ("Not") ? C::amber : C::dim));
    g.drawFittedText (status, r.removeFromTop (30), Justification::centredTop, 2);
}

//==============================================================================
void CorrelationMeter::paint (Graphics& g)
{
    auto r = getLocalBounds().reduced (8, 0);
    g.setFont (uiFont (12.5f));
    g.setColour (C::dim);
    g.drawText ("LOW-END PHASE", r.removeFromTop (16), Justification::centred);
    r.removeFromTop (8);

    const bool live = proc.lowEndPresent.load();
    const float k = 0.15f;
    before += ((live ? proc.corrBefore.load() : 0.0f) - before) * k;
    after  += ((live ? proc.corrAfter.load()  : 0.0f) - after)  * k;
    const bool on = proc.apvts.getRawParameterValue (IDs::phaseOn)->load() > 0.5f;

    auto meter = [&] (const String& name, float v, bool active)
    {
        auto row = r.removeFromTop (22);
        g.setFont (uiFont (10.5f));
        g.setColour (C::dim);
        g.drawText (name, row.removeFromLeft (44), Justification::centredLeft);
        auto t = row.reduced (0, 6).toFloat();
        g.setColour (C::track);
        g.fillRoundedRectangle (t, 3.0f);
        const float cx = t.getCentreX();
        const float x = cx + t.getWidth() * 0.5f * jlimit (-1.0f, 1.0f, v);
        const Colour c = v >= 0.0f ? C::cyan : C::amber;
        g.setColour (active ? c : c.withAlpha (0.35f));
        g.fillRoundedRectangle (Rectangle<float> (jmin (cx, x), t.getY(), std::abs (x - cx), t.getHeight()), 3.0f);
        g.setColour (C::text.withAlpha (0.5f));
        g.drawVerticalLine ((int) cx, t.getY() - 2.0f, t.getBottom() + 2.0f);
    };
    meter ("BEFORE", before, ! on);
    meter ("AFTER", on ? after : before, on);

    g.setFont (uiFont (10.0f));
    g.setColour (C::dim);
    auto lbl = r.removeFromTop (14);
    lbl.removeFromLeft (44);
    g.drawText (CharPointer_UTF8 ("\xe2\x88\x92 cancel"), lbl, Justification::centredLeft);
    g.drawText ("+ in phase", lbl, Justification::centredRight);
    if (! live)
    {
        g.setColour (C::dim.withAlpha (0.8f));
        g.drawText ("needs kick on sidechain", r.removeFromTop (14), Justification::centred);
    }
}

//==============================================================================
void BassDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (C::panel); g.fillRoundedRectangle (b, 10.0f);
    g.setColour (C::panelEdge); g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);
    auto in = b.reduced (14.0f, 10.0f);
    auto left = in.removeFromLeft (in.getWidth() * 0.5f - 8.0f);
    in.removeFromLeft (16.0f);
    auto right = in;

    const float k = 0.25f;
    const bool active = proc.hasBass.load();
    cIn  += ((active ? proc.lowCorrIn.load()  : 0.0f) - cIn)  * k;
    cOut += ((active ? proc.lowCorrOut.load() : 0.0f) - cOut) * k;
    pIn  += ((active ? proc.phoneIn.load()  : 0.0f) - pIn)  * k;
    pOut += ((active ? proc.phoneOut.load() : 0.0f) - pOut) * k;

    // ---- Low-end stereo ----
    g.setFont (uiFont (11.5f, true)); g.setColour (C::dim);
    g.drawText ("LOW-END STEREO (< 150 Hz)", left.removeFromTop (18.0f), Justification::centredLeft);
    left.removeFromTop (10.0f);
    auto corrBar = [&] (const String& name, float v)
    {
        auto row = left.removeFromTop (26.0f);
        g.setFont (uiFont (11.0f)); g.setColour (C::dim);
        g.drawText (name, row.removeFromLeft (54.0f), Justification::centredLeft);
        auto val = row.removeFromRight (40.0f);
        auto t = row.reduced (0.0f, 8.0f);
        g.setColour (C::track); g.fillRoundedRectangle (t, 3.0f);
        const float cx = t.getCentreX(), x = cx + t.getWidth() * 0.5f * jlimit (-1.0f, 1.0f, v);
        g.setColour (v > 0.9f ? C::cyan : v > 0.5f ? C::violet : C::amber);
        g.fillRoundedRectangle (Rectangle<float> (jmin (cx, x), t.getY(), std::abs (x - cx), t.getHeight()), 3.0f);
        g.setColour (C::text.withAlpha (0.5f)); g.drawVerticalLine ((int) cx, t.getY() - 2, t.getBottom() + 2);
        g.setColour (C::text); g.drawText (active ? String (v, 2) : String ("--"), val, Justification::centredRight);
    };
    corrBar ("BEFORE", cIn);
    corrBar ("AFTER", cOut);
    left.removeFromTop (8.0f);
    g.setFont (uiFont (11.0f));
    g.setColour (! active ? C::dim : cOut > 0.95f ? C::cyan : C::amber);
    g.drawFittedText (! active ? "Play the bass" : cOut > 0.95f ? "Mono-safe low end: solid in clubs and on mono speakers"
                                                : "Stereo low end: may lose power in mono, turn on MONO BASS",
                      left.removeFromTop (34.0f).toNearestInt(), Justification::topLeft, 2);

    // ---- Phone translation ----
    g.setFont (uiFont (11.5f, true)); g.setColour (C::dim);
    g.drawText ("PHONE TRANSLATION", right.removeFromTop (18.0f), Justification::centredLeft);
    right.removeFromTop (6.0f);
    auto text = right.removeFromBottom (34.0f);
    auto bars = right;
    const float bw = 46.0f;
    auto bar = [&] (int idx, const String& name, float v, Colour c)
    {
        auto col = Rectangle<float> (bars.getX() + 20.0f + idx * (bw + 40.0f), bars.getY(), bw, bars.getHeight());
        auto lbl = col.removeFromBottom (14.0f);
        auto val = col.removeFromTop (16.0f);
        g.setColour (C::track); g.fillRoundedRectangle (col, 4.0f);
        const float db = 10.0f * std::log10 (jmax (v, 1.0e-4f));          // audible share in dB
        const float h = col.getHeight() * jlimit (0.0f, 1.0f, (db + 36.0f) / 36.0f);
        g.setColour (c); g.fillRoundedRectangle (col.withTop (col.getBottom() - h), 4.0f);
        g.setFont (uiFont (10.0f)); g.setColour (C::dim); g.drawText (name, lbl, Justification::centred);
        g.setFont (uiFont (12.0f, true)); g.setColour (C::text);
        g.drawText (active ? String (db, 1) + " dB" : String ("--"), val, Justification::centred);
    };
    bar (0, "BEFORE", pIn, C::dim);
    bar (1, "AFTER", pOut, C::cyan);
    const float gainDb = 10.0f * std::log10 (jmax (pOut, 1.0e-4f) / jmax (pIn, 1.0e-4f));
    g.setFont (uiFont (10.5f));
    g.setColour (active && gainDb > 1.0f ? C::cyan : C::dim);
    g.drawFittedText (active && gainDb > 1.0f ? "Bass is " + String (gainDb, 1) + " dB louder on phone and laptop speakers"
                                              : "How much of the bass a phone speaker can play (above ~150 Hz). Raise HARMONICS.",
                      text.toNearestInt(), Justification::topLeft, 2);
}

//==============================================================================
LumenBassEditor::LumenBassEditor (LumenBassProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p), phasePanel (p), corrMeter (p)
{
    setLookAndFeel (&lnf);
    const std::pair<const char*, const char*> defs[] = {
        { IDs::monoHz, "Mono Below" }, { IDs::width, "Width" }, { IDs::sub, "Sub" }, { IDs::output, "Output" },
        { IDs::harmAmt, "Harmonics" }, { IDs::harmFocus, "Harm Focus" }, { IDs::harmChar, "Character" }, { IDs::harmTone, "Harm Tone" },
        { IDs::phaseRot, "Rotation" }, { IDs::phaseDly, "Delay" }, { IDs::phaseFoc, "Focus" } };
    for (auto& [id, t] : defs) { knobs.push_back (std::make_unique<Knob> (proc.apvts, id, t)); addAndMakeVisible (*knobs.back()); }

    addAndMakeVisible (display);
    addAndMakeVisible (phasePanel);
    addAndMakeVisible (corrMeter);

    for (auto* b : { &monoButton, &listenButton }) { b->setClickingTogglesState (true); addAndMakeVisible (*b); }
    monoButton.setTooltip ("Everything below Mono Below becomes mono; above it the stereo image is untouched");
    listenButton.setTooltip ("Hear only the harmonics that are being added");
    monoAtt   = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::monoOn, monoButton);
    listenAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::listen, listenButton);

    learnButton.setTooltip ("Listens to the bass for 4 s, then sets mono split, harmonics amount and focus");
    learnButton.onClick = [this] { learnClicked(); };
    addAndMakeVisible (learnButton);
    undoButton.onClick = [this] { undoLearn(); };
    undoButton.setEnabled (false);
    addAndMakeVisible (undoButton);

    setSize (900, 700);
    startTimerHz (30);
}

LumenBassEditor::~LumenBassEditor() { stopTimer(); setLookAndFeel (nullptr); }

float LumenBassEditor::getParam (const char* id) const { auto* q = proc.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); }
void LumenBassEditor::setParam (const char* id, float v)
{
    if (auto* q = proc.apvts.getParameter (id)) { q->beginChangeGesture(); q->setValueNotifyingHost (q->convertTo0to1 (v)); q->endChangeGesture(); }
}

void LumenBassEditor::learnClicked()
{
    if (proc.isCapturing()) { proc.cancelCapture(); learnButton.setButtonText ("LEARN BASS"); headline = "Cancelled"; return; }
    proc.requestCapture();
    statusText = "Listening to the bass (4 s)...";
}

void LumenBassEditor::applyInfo (const lumen::BassInfo& b)
{
    undoValues.clear();
    for (auto* id : { IDs::monoHz, IDs::harmAmt, IDs::harmFocus, IDs::harmChar, IDs::monoOn })
        undoValues.emplace_back (id, getParam (id));

    const float f0 = b.f0 > 20.0f ? b.f0 : 55.0f;
    const float mono  = jlimit (80.0f, 200.0f, std::round (f0 * 2.5f / 5.0f) * 5.0f);
    const float focus = jlimit (50.0f, 250.0f, std::round (f0 * 2.0f / 5.0f) * 5.0f);
    // the more energy lives below 100 Hz, the more harmonics a phone needs
    const float amt   = jlimit (10.0f, 70.0f, (b.subRatio - 0.3f) * 110.0f);
    const float chr   = f0 < 50.0f ? 30.0f : 50.0f;
    setParam (IDs::monoOn, 1.0f);
    setParam (IDs::monoHz, mono);
    setParam (IDs::harmFocus, focus);
    setParam (IDs::harmAmt, amt);
    setParam (IDs::harmChar, chr);

    headline = b.f0 > 20.0f ? "Bass " + String (b.f0, 1) + " Hz" : "Bass analysed";
    statusText = String (roundToInt (b.subRatio * 100.0f)) + "% of the energy is below 100 Hz. Mono below "
               + String (roundToInt (mono)) + " Hz, harmonics " + String (roundToInt (amt)) + "%";
    undoButton.setEnabled (true);
}

void LumenBassEditor::undoLearn()
{
    for (auto& [id, v] : undoValues) setParam (id.toRawUTF8(), v);
    undoValues.clear(); undoButton.setEnabled (false);
    headline = "Ready"; statusText = "Previous settings restored";
}

void LumenBassEditor::timerCallback()
{
    const bool cap = proc.isCapturing();
    if (cap) { learnButton.setButtonText ("LISTENING " + String (roundToInt (proc.captureProgress() * 100.0f)) + "%"); headline = "Listening..."; }
    else if (wasCapturing) learnButton.setButtonText ("LEARN BASS");
    wasCapturing = cap;

    std::vector<float> buf;
    if (proc.takeCapture (buf))
    {
        const auto info = lumen::analyseBass (buf, (float) proc.getSampleRateSafe());
        if (info.valid) applyInfo (info);
        else { headline = "No bass found"; statusText = "Play the bass louder or longer, then try again"; }
    }
    display.repaint();
    phasePanel.poll();
    corrMeter.repaint();
    repaint (smartArea);
}

void LumenBassEditor::paint (Graphics& g)
{
    g.setGradientFill (ColourGradient (C::bg2, 0, 0, C::bg, 0, (float) getHeight(), false));
    g.fillAll();
    g.setFont (uiFont (26.0f, true));
    g.setGradientFill (ColourGradient (C::cyan, 20, 0, C::violet, 160, 0, false));
    g.drawText ("LUMEN", 20, 14, 200, 34, Justification::centredLeft);
    g.setFont (uiFont (12.0f)); g.setColour (C::dim);
    g.drawText ("SMART BASS", 128, 24, 200, 18, Justification::centredLeft);

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
    if (proc.isCapturing())
    {
        auto pr = Rectangle<float> (s.getX() + 14, s.getBottom() - 50, s.getWidth() - 28, 3);
        g.setColour (C::track); g.fillRect (pr);
        g.setColour (C::cyan); g.fillRect (pr.withWidth (pr.getWidth() * proc.captureProgress()));
    }

    const char* sections[] = { "LOW END & STEREO", "HARMONICS - PHONE TRANSLATION", "PHASE ALIGN - KICK / BASS" };
    for (int r = 0; r < 3; ++r)
    {
        const int y = 300 + r * 126;
        g.setFont (uiFont (11.0f, true)); g.setColour (C::dim.withAlpha (0.8f));
        g.drawText (sections[r], 20, y, 300, 14, Justification::centredLeft);
        g.setColour (C::panelEdge); g.drawHorizontalLine (y + 7, 270.0f, (float) getWidth() - 20.0f);
    }
    g.setFont (uiFont (12.5f)); g.setColour (C::dim);
    g.drawText ("MONO", monoCell.withHeight (16), Justification::centred);
    g.drawText ("SOLO", listenCell.withHeight (16), Justification::centred);
}

void LumenBassEditor::resized()
{
    display.setBounds (16, 66, 548, 222);
    smartArea = { 576, 66, 308, 222 };
    learnButton.setBounds (smartArea.getX() + 14, smartArea.getBottom() - 42, 190, 30);
    undoButton.setBounds (smartArea.getX() + 212, smartArea.getBottom() - 42, 82, 30);

    const int colW = (getWidth() - 32) / 5;
    auto cell = [&] (int row, int col) { return Rectangle<int> (16 + col * colW, 318 + row * 126, colW, 104); };

    monoCell = cell (0, 0).reduced (8, 0);
    monoButton.setBounds (monoCell.getX(), monoCell.getY() + 34, monoCell.getWidth(), 30);
    for (int i = 0; i < 4; ++i) knobs[(size_t) i]->setBounds (cell (0, i + 1));
    for (int i = 4; i < 8; ++i) knobs[(size_t) i]->setBounds (cell (1, i - 4));
    listenCell = cell (1, 4).reduced (8, 0);
    listenButton.setBounds (listenCell.getX(), listenCell.getY() + 34, listenCell.getWidth(), 30);
    phasePanel.setBounds (cell (2, 0));
    for (int i = 8; i < 11; ++i) knobs[(size_t) i]->setBounds (cell (2, i - 7));
    corrMeter.setBounds (cell (2, 4));
}
