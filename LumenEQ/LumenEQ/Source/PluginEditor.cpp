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
static const Colour bandCol[6] = { Colour (0xff5ce1e6), Colour (0xff6fb6ff), Colour (0xff9b7bff),
                                   Colour (0xffd77bff), Colour (0xffff7bb0), Colour (0xffffb35c) };

EqDisplay::EqDisplay (LumenEQProcessor& p) : proc (p)
{
    window.resize (kN); work.assign (2 * kN, 0.0f);
    ringT.assign (kN, 0.0f); ringS.assign (kN, 0.0f);
    specT.assign (kN / 2 + 1, -120.0f); specS.assign (kN / 2 + 1, -120.0f);
    for (int i = 0; i < kN; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * MathConstants<float>::pi * i / (kN - 1));
}

Rectangle<float> EqDisplay::plot() const { return getLocalBounds().toFloat().reduced (14.0f, 10.0f).withTrimmedTop (22.0f).withTrimmedBottom (14.0f); }
float EqDisplay::xOf (float f) const { auto r = plot(); return r.getX() + r.getWidth() * std::log (jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f); }
float EqDisplay::fOf (float x) const { auto r = plot(); return 20.0f * std::pow (1000.0f, jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth())); }
float EqDisplay::yOfEq (float db) const { auto r = plot(); return r.getCentreY() - db / 24.0f * r.getHeight() * 0.5f; }
float EqDisplay::dbOfY (float y) const { auto r = plot(); return jlimit (-24.0f, 24.0f, (r.getCentreY() - y) / (r.getHeight() * 0.5f) * 24.0f); }
float EqDisplay::yOfSpec (float db) const { auto r = plot(); return r.getBottom() - r.getHeight() * jlimit (0.0f, 1.0f, (db + 96.0f) / 96.0f); }

void EqDisplay::setParam (const String& id, float v)
{
    if (auto* q = proc.apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (v));
}

void EqDisplay::readFifo (AbstractFifo& f, std::vector<float>& src, std::vector<float>& ring, int& pos)
{
    const int n = f.getNumReady();
    if (n <= 0) return;
    int s1, n1, s2, n2;
    f.prepareToRead (n, s1, n1, s2, n2);
    auto put = [&] (int start, int len) { for (int i = 0; i < len; ++i) { ring[(size_t) pos] = src[(size_t) (start + i)]; pos = (pos + 1) % kN; } };
    put (s1, n1); put (s2, n2);
    f.finishedRead (n1 + n2);
}

void EqDisplay::analyse (const std::vector<float>& ring, int pos, std::vector<float>& outDb)
{
    std::fill (work.begin(), work.end(), 0.0f);
    for (int i = 0; i < kN; ++i) work[(size_t) i] = ring[(size_t) ((pos + i) % kN)] * window[(size_t) i];
    fft.performFrequencyOnlyForwardTransform (work.data());
    for (int i = 0; i <= kN / 2; ++i)
    {
        const float db = Decibels::gainToDecibels (work[(size_t) i] / (float) (kN / 4), -120.0f);
        outDb[(size_t) i] = db > outDb[(size_t) i] ? db : outDb[(size_t) i] - 1.2f;   // fast rise, slow fall
    }
}

void EqDisplay::pull()
{
    readFifo (proc.fifoTrack, proc.bufTrack, ringT, posT);
    readFifo (proc.fifoSc, proc.bufSc, ringS, posS);
    analyse (ringT, posT, specT);
    if (proc.scConnected.load()) analyse (ringS, posS, specS);
    repaint();
}

void EqDisplay::mouseDown (const MouseEvent& e)
{
    int best = -1; float bd = 14.0f;
    for (int b = 0; b < 6; ++b)
    {
        const auto band = proc.bandFromParams (b);
        const float y = lumen::isGainType (band.type) ? yOfEq (band.gainDb) : plot().getCentreY();
        const float d = e.position.getDistanceFrom ({ xOf (band.freq), y });
        if (d < bd) { bd = d; best = b; }
    }
    if (best >= 0 && onSelect) onSelect (best);
}

void EqDisplay::mouseDrag (const MouseEvent& e)
{
    const auto band = proc.bandFromParams (selected);
    setParam (IDs::freq (selected), fOf (e.position.x));
    if (lumen::isGainType (band.type)) setParam (IDs::gain (selected), dbOfY (e.position.y));
}

void EqDisplay::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    const float q = proc.bandFromParams (selected).q;
    setParam (IDs::q (selected), jlimit (0.1f, 18.0f, q * (w.deltaY > 0 ? 1.12f : 1.0f / 1.12f)));
}

void EqDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (C::panel); g.fillRoundedRectangle (b, 10.0f);
    g.setColour (C::panelEdge); g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);
    auto head = b.reduced (14.0f, 10.0f).removeFromTop (18.0f);
    g.setFont (uiFont (11.5f, true)); g.setColour (C::dim);
    g.drawText ("SPECTRUM & EQ", head, Justification::centredLeft);
    g.setFont (uiFont (10.5f));
    g.setColour (C::cyan); g.drawText ("track", head.removeFromRight (60.0f), Justification::centredRight);
    if (proc.scConnected.load()) { g.setColour (C::amber); g.drawText ("sidechain", head.removeFromRight (80.0f), Justification::centredRight); }

    auto r = plot();
    g.setColour (C::bg.withAlpha (0.55f)); g.fillRect (r);
    g.setFont (uiFont (9.5f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        g.setColour (C::track.withAlpha (0.6f)); g.drawVerticalLine ((int) xOf (f), r.getY(), r.getBottom());
        g.setColour (C::dim);
        g.drawText (f < 1000.0f ? String ((int) f) : String ((int) (f / 1000.0f)) + "k", Rectangle<float> (xOf (f) - 16.0f, r.getBottom() + 1.0f, 32.0f, 12.0f), Justification::centred);
    }
    for (float d : { -12.0f, 0.0f, 12.0f }) { g.setColour (C::track.withAlpha (std::abs (d) < 0.01f ? 0.9f : 0.5f)); g.drawHorizontalLine ((int) yOfEq (d), r.getX(), r.getRight()); }

    const double fs = proc.getSampleRateSafe();
    auto specPath = [&] (const std::vector<float>& s, bool fill)
    {
        Path p; bool started = false;
        for (int px = 0; px <= (int) r.getWidth(); px += 2)
        {
            const float f = fOf (r.getX() + (float) px);
            const int bin = jlimit (1, kN / 2, (int) std::round (f * kN / fs));
            const float y = yOfSpec (s[(size_t) bin]);
            if (! started) { p.startNewSubPath (r.getX() + (float) px, fill ? r.getBottom() : y); if (fill) p.lineTo (r.getX() + (float) px, y); started = true; }
            else p.lineTo (r.getX() + (float) px, y);
        }
        if (fill) { p.lineTo (r.getRight(), r.getBottom()); p.closeSubPath(); }
        return p;
    };
    g.setColour (C::cyan.withAlpha (0.18f)); g.fillPath (specPath (specT, true));
    if (proc.scConnected.load()) { g.setColour (C::amber.withAlpha (0.7f)); g.strokePath (specPath (specS, false), PathStrokeType (1.2f)); }

    // masking zones from the last UNMASK
    for (auto& z : zones)
    {
        const float w = 1.0f / z.q;   // ~octaves
        const float x0 = xOf (z.freq / std::pow (2.0f, w * 0.5f)), x1 = xOf (z.freq * std::pow (2.0f, w * 0.5f));
        g.setColour (C::amber.withAlpha (0.12f)); g.fillRect (Rectangle<float> (x0, r.getY(), x1 - x0, r.getHeight()));
    }

    // EQ curve (static + live dynamic gain)
    lumen::BiquadCoefs c[6]; bool on[6];
    for (int k = 0; k < 6; ++k)
    {
        const auto band = proc.bandFromParams (k);
        on[k] = band.on;
        c[k] = lumen::design (band.type, band.freq, band.gainDb + proc.dynGain[k].load(), band.q, fs);
    }
    Path eq, sel;
    for (int px = 0; px <= (int) r.getWidth(); px += 2)
    {
        const float f = fOf (r.getX() + (float) px);
        double total = 0.0;
        for (int k = 0; k < 6; ++k) if (on[k]) total += lumen::magnitudeDb (c[k], f, fs);
        const float ys = yOfEq ((float) lumen::magnitudeDb (c[selected], f, fs));
        const float y = yOfEq ((float) jlimit (-30.0, 30.0, total));
        if (px == 0) { eq.startNewSubPath (r.getX(), y); sel.startNewSubPath (r.getX(), ys); }
        else { eq.lineTo (r.getX() + (float) px, y); sel.lineTo (r.getX() + (float) px, ys); }
    }
    g.setColour (bandCol[selected].withAlpha (0.35f)); g.strokePath (sel, PathStrokeType (1.2f));
    g.setGradientFill (ColourGradient (C::cyan, r.getX(), 0, C::violet, r.getRight(), 0, false));
    g.strokePath (eq, PathStrokeType (2.2f));

    // nodes
    for (int k = 0; k < 6; ++k)
    {
        const auto band = proc.bandFromParams (k);
        const float x = xOf (band.freq);
        const float yStatic = lumen::isGainType (band.type) ? yOfEq (band.gainDb) : r.getCentreY();
        const float dyn = proc.dynGain[k].load();
        if (std::abs (dyn) > 0.05f)
        {
            g.setColour (C::amber); g.drawLine (x, yStatic, x, yOfEq (band.gainDb + dyn), 2.0f);
        }
        if (lumen::isDynamic (band.dynDb) && lumen::isGainType (band.type))
        {
            g.setColour (bandCol[k].withAlpha (0.4f));
            g.drawLine (x - 6.0f, yOfEq (band.gainDb + band.dynDb), x + 6.0f, yOfEq (band.gainDb + band.dynDb), 1.5f);
        }
        const float rad = k == selected ? 8.0f : 6.5f;
        g.setColour (band.on ? bandCol[k] : C::dim);
        g.fillEllipse (x - rad, yStatic - rad, rad * 2.0f, rad * 2.0f);
        g.setColour (C::bg); g.setFont (uiFont (9.5f, true));
        g.drawText (String (k + 1), Rectangle<float> (x - rad, yStatic - rad, rad * 2.0f, rad * 2.0f), Justification::centred);
        if (k == selected) { g.setColour (C::text); g.drawEllipse (x - rad - 2.0f, yStatic - rad - 2.0f, rad * 2.0f + 4.0f, rad * 2.0f + 4.0f, 1.2f); }
    }
}

//==============================================================================
LumenEQEditor::LumenEQEditor (LumenEQProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (display);
    display.onSelect = [this] (int b) { selectBand (b); };

    const char* names[5] = { "Freq", "Gain", "Q", "Dyn Range", "Threshold" };
    for (int b = 0; b < 6; ++b)
    {
        tab[b].setButtonText (String (b + 1));
        tab[b].setClickingTogglesState (true);
        tab[b].setRadioGroupId (101);
        tab[b].onClick = [this, b] { if (tab[b].getToggleState()) selectBand (b); };
        addAndMakeVisible (tab[b]);

        onBtn[b].setButtonText ("ON");
        onBtn[b].setClickingTogglesState (true);
        addChildComponent (onBtn[b]);
        onAtt[b] = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::on (b), onBtn[b]);

        typeBox[b].addItemList (typeChoices(), 1);
        addChildComponent (typeBox[b]);
        typeAtt[b] = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::type (b), typeBox[b]);

        scBox[b].addItemList ({ "Detector: Internal", "Detector: Sidechain" }, 1);
        scBox[b].setTooltip ("What drives the dynamic part: this track, or the sidechain (e.g. the kick)");
        addChildComponent (scBox[b]);
        scAtt[b] = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::sc (b), scBox[b]);

        const String ids[5] = { IDs::freq (b), IDs::gain (b), IDs::q (b), IDs::dyn (b), IDs::thr (b) };
        for (int k = 0; k < 5; ++k)
        {
            bandKnob[b][k] = std::make_unique<Knob> (proc.apvts, ids[k], names[k]);
            addChildComponent (*bandKnob[b][k]);
        }
    }
    for (auto& [id, t] : { std::pair<const char*, const char*> { IDs::attack, "Attack" }, { IDs::release, "Release" }, { IDs::output, "Output" } })
    {
        globalKnobs.push_back (std::make_unique<Knob> (proc.apvts, id, t));
        addAndMakeVisible (*globalKnobs.back());
    }

    unmaskButton.setTooltip ("Play kick (sidechain) and bass together: finds where they clash and adds dynamic cuts that only work when the kick hits");
    unmaskButton.onClick = [this] { unmaskClicked(); };
    addAndMakeVisible (unmaskButton);
    undoButton.onClick = [this] { undo(); };
    undoButton.setEnabled (false);
    addAndMakeVisible (undoButton);

    setSize (900, 600);
    selectBand (1);
    startTimerHz (30);
}

LumenEQEditor::~LumenEQEditor() { stopTimer(); setLookAndFeel (nullptr); }

float LumenEQEditor::getParam (const String& id) const { auto* q = proc.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); }
void LumenEQEditor::setParam (const String& id, float v)
{
    if (auto* q = proc.apvts.getParameter (id)) { q->beginChangeGesture(); q->setValueNotifyingHost (q->convertTo0to1 (v)); q->endChangeGesture(); }
}

void LumenEQEditor::selectBand (int b)
{
    display.selected = b;
    for (int k = 0; k < 6; ++k)
    {
        const bool v = (k == b);
        tab[k].setToggleState (v, dontSendNotification);
        onBtn[k].setVisible (v); typeBox[k].setVisible (v); scBox[k].setVisible (v);
        for (auto& kn : bandKnob[k]) kn->setVisible (v);
    }
    display.repaint();
}

void LumenEQEditor::unmaskClicked()
{
    if (proc.isCapturing()) { proc.cancelCapture(); unmaskButton.setButtonText ("UNMASK"); headline = "Cancelled"; return; }
    if (! proc.scConnected.load()) { headline = "No sidechain"; statusText = "Route the kick (or the track that should win) to the sidechain input"; return; }
    proc.requestCapture();
    statusText = "Listening to both for 4 s...";
}

void LumenEQEditor::applyZones (const std::vector<lumen::MaskZone>& zones)
{
    display.zones = zones;
    if (zones.empty()) { headline = "No clash found"; statusText = "Track and sidechain don't fight for the same frequencies"; return; }

    // free bands: no static and no dynamic gain, prefer the middle bands
    std::vector<int> freeBands;
    for (int b : { 1, 2, 3, 4, 0, 5 })
        if (std::abs (getParam (IDs::gain (b))) < 0.05f && std::abs (getParam (IDs::dyn (b))) < 0.05f) freeBands.push_back (b);

    undoValues.clear();
    String list;
    int used = 0;
    for (size_t i = 0; i < zones.size() && used < (int) freeBands.size(); ++i, ++used)
    {
        const int b = freeBands[(size_t) used];
        for (auto& id : { IDs::on (b), IDs::type (b), IDs::freq (b), IDs::gain (b), IDs::q (b), IDs::dyn (b), IDs::thr (b), IDs::sc (b) })
            undoValues.emplace_back (id, getParam (id));
        const auto& z = zones[i];
        const float depth = jlimit (3.0f, 9.0f, 3.0f + (z.severity - 6.0f) * 0.25f);
        setParam (IDs::on (b), 1.0f);
        setParam (IDs::type (b), (float) lumen::Bell);
        setParam (IDs::freq (b), z.freq);
        setParam (IDs::gain (b), 0.0f);
        setParam (IDs::q (b), z.q);
        setParam (IDs::dyn (b), -depth);
        setParam (IDs::thr (b), jlimit (-60.0f, -3.0f, z.scPeakDb - 12.0f));   // full cut at each hit
        setParam (IDs::sc (b), 1.0f);
        list << (list.isEmpty() ? "" : ", ") << String (roundToInt (z.freq)) << " Hz (-" << String (depth, 1) << " dB)";
    }
    headline = String (used) + (used == 1 ? " clash fixed" : " clashes fixed");
    statusText = "Dynamic cuts only when the sidechain hits: " + list;
    undoButton.setEnabled (true);
    if (! freeBands.empty()) selectBand (freeBands[0]);
}

void LumenEQEditor::undo()
{
    for (auto& [id, v] : undoValues) setParam (id, v);
    undoValues.clear(); display.zones.clear(); undoButton.setEnabled (false);
    headline = "Ready"; statusText = "Previous settings restored";
}

void LumenEQEditor::timerCallback()
{
    display.pull();
    const bool cap = proc.isCapturing();
    if (cap) { unmaskButton.setButtonText ("LISTENING " + String (roundToInt (proc.captureProgress() * 100.0f)) + "%"); headline = "Listening..."; }
    else if (wasCapturing) unmaskButton.setButtonText ("UNMASK");
    wasCapturing = cap;

    std::vector<float> t, s;
    if (proc.takeCapture (t, s)) applyZones (lumen::findMasking (t, s, proc.getSampleRateSafe(), 3));
    repaint (smartArea);
}

void LumenEQEditor::paint (Graphics& g)
{
    g.setGradientFill (ColourGradient (C::bg2, 0, 0, C::bg, 0, (float) getHeight(), false));
    g.fillAll();
    g.setFont (uiFont (26.0f, true));
    g.setGradientFill (ColourGradient (C::cyan, 20, 0, C::violet, 160, 0, false));
    g.drawText ("LUMEN", 20, 14, 200, 34, Justification::centredLeft);
    g.setFont (uiFont (12.0f)); g.setColour (C::dim);
    g.drawText ("SMART EQ", 128, 24, 200, 18, Justification::centredLeft);

    auto s = smartArea.toFloat();
    g.setColour (C::panel); g.fillRoundedRectangle (s, 10.0f);
    g.setColour (C::panelEdge); g.drawRoundedRectangle (s.reduced (0.5f), 10.0f, 1.0f);
    auto in = smartArea.reduced (14, 10);
    g.setFont (uiFont (11.5f, true)); g.setColour (C::dim);
    g.drawText ("SMART ASSIST - UNMASK", in.removeFromTop (18), Justification::centredLeft);
    g.setFont (uiFont (19.0f, true)); g.setColour (C::text);
    g.drawText (headline, in.removeFromTop (28), Justification::centredLeft);
    g.setFont (uiFont (11.0f));
    g.setColour (proc.scConnected.load() ? C::cyan : C::amber);
    g.drawText (proc.scConnected.load() ? "Sidechain connected" : "Sidechain: not connected", in.removeFromTop (16), Justification::centredLeft);
    g.setColour (C::cyan.withAlpha (0.9f));
    g.drawFittedText (statusText, in.removeFromTop (60), Justification::topLeft, 4);
    if (proc.isCapturing())
    {
        auto pr = Rectangle<float> (s.getX() + 14, s.getBottom() - 50, s.getWidth() - 28, 3);
        g.setColour (C::track); g.fillRect (pr);
        g.setColour (C::cyan); g.fillRect (pr.withWidth (pr.getWidth() * proc.captureProgress()));
    }

    g.setFont (uiFont (11.0f, true)); g.setColour (C::dim.withAlpha (0.8f));
    g.drawText ("BAND", 20, 304, 60, 14, Justification::centredLeft);
    g.drawText ("DYNAMICS & OUTPUT", 20, 470, 200, 14, Justification::centredLeft);
    g.setColour (C::panelEdge); g.drawHorizontalLine (477, 175.0f, (float) getWidth() - 20.0f);
}

void LumenEQEditor::resized()
{
    display.setBounds (16, 66, 548, 222);
    smartArea = { 576, 66, 308, 222 };
    unmaskButton.setBounds (smartArea.getX() + 14, smartArea.getBottom() - 42, 190, 30);
    undoButton.setBounds (smartArea.getX() + 212, smartArea.getBottom() - 42, 82, 30);

    for (int b = 0; b < 6; ++b)
    {
        tab[b].setBounds (70 + b * 44, 298, 38, 28);
        onBtn[b].setBounds (348, 298, 60, 28);
        typeBox[b].setBounds (416, 298, 150, 28);
        scBox[b].setBounds (574, 298, 200, 28);
    }
    const int colW = (getWidth() - 32) / 5;
    for (int b = 0; b < 6; ++b)
        for (int k = 0; k < 5; ++k)
            bandKnob[b][k]->setBounds (16 + k * colW, 340, colW, 104);
    for (int k = 0; k < 3; ++k) globalKnobs[(size_t) k]->setBounds (16 + k * colW, 490, colW, 104);
}
