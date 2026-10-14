#include "PluginEditor.h"
#include "dsp/CompSuggest.h"

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
CompDisplay::CompDisplay (LumenCompressorProcessor& p) : proc (p)
{
    curve.prepare (48000.0);
}

void CompDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (C::panel);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (C::panelEdge);
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    auto inner = b.reduced (14.0f, 10.0f);
    auto curveArea = inner.removeFromLeft (inner.getHeight());
    inner.removeFromLeft (14.0f);
    auto meterArea = inner.removeFromRight (54.0f);
    inner.removeFromRight (10.0f);

    paintCurve (g, curveArea);
    paintHistory (g, inner);
    paintMeters (g, meterArea);
}

void CompDisplay::paintCurve (Graphics& g, Rectangle<float> r)
{
    auto params = proc.makeParams();
    params.autoGain = false;
    curve.setParams (params);
    curve.beginBlock();

    g.setFont (uiFont (11.0f, true));
    g.setColour (C::dim);
    g.drawText ("TRANSFER", r.removeFromTop (16.0f), Justification::centredLeft);
    r.removeFromTop (4.0f);

    const float lo = -60.0f, hi = 0.0f;
    auto xOf = [&] (float db) { return r.getX() + r.getWidth()  * (db - lo) / (hi - lo); };
    auto yOf = [&] (float db) { return r.getBottom() - r.getHeight() * (jlimit (lo, hi, db) - lo) / (hi - lo); };

    g.setColour (C::bg.withAlpha (0.5f));
    g.fillRect (r);
    g.setColour (C::track.withAlpha (0.6f));
    for (float d = -48.0f; d < 0.0f; d += 12.0f)
    {
        g.drawVerticalLine ((int) xOf (d), r.getY(), r.getBottom());
        g.drawHorizontalLine ((int) yOf (d), r.getX(), r.getRight());
    }
    g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 1.0f); // 1:1 reference

    // Threshold marker
    const float thr = params.thresholdDb;
    g.setColour (C::amber.withAlpha (0.5f));
    g.drawVerticalLine ((int) xOf (thr), r.getY(), r.getBottom());

    Path p;
    for (int k = 0; k <= 120; ++k)
    {
        const float x = lo + (hi - lo) * (float) k / 120.0f;
        const float y = curve.staticCurve (x);
        if (k == 0) p.startNewSubPath (xOf (x), yOf (y));
        else        p.lineTo (xOf (x), yOf (y));
    }
    g.setGradientFill (ColourGradient (C::cyan, r.getX(), r.getBottom(), C::violet, r.getRight(), r.getY(), false));
    g.strokePath (p, PathStrokeType (2.2f));

    // Live input dot
    const float inDb = Decibels::gainToDecibels (proc.inLevel.load(), -100.0f);
    if (inDb > lo)
    {
        const float outDb = inDb - proc.grDb.load();
        g.setColour (C::text);
        g.fillEllipse (xOf (inDb) - 4.0f, yOf (outDb) - 4.0f, 8.0f, 8.0f);
    }
}

void CompDisplay::paintHistory (Graphics& g, Rectangle<float> r)
{
    auto head = r.removeFromTop (16.0f);
    g.setFont (uiFont (11.0f, true));
    g.setColour (C::dim);
    g.drawText ("GAIN REDUCTION", head, Justification::centredLeft);

    const float gr = proc.grDb.load();
    g.setColour (C::amber);
    g.drawText ("-" + String (gr, 1) + " dB", head, Justification::centredRight);
    r.removeFromTop (4.0f);

    g.setColour (C::bg.withAlpha (0.5f));
    g.fillRect (r);

    const int N = LumenCompressorProcessor::kHistory;
    const int w = proc.histWrite.load (std::memory_order_acquire);
    auto xOf = [&] (int k) { return r.getX() + r.getWidth() * (float) k / (float) (N - 1); };

    // Input level (filled, bottom-up, -60..0 dB)
    Path inPath;
    inPath.startNewSubPath (r.getX(), r.getBottom());
    for (int k = 0; k < N; ++k)
    {
        const float db = proc.histIn[(size_t) ((w + k) % N)].load (std::memory_order_relaxed);
        const float y = r.getBottom() - r.getHeight() * jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        inPath.lineTo (xOf (k), y);
    }
    inPath.lineTo (r.getRight(), r.getBottom());
    inPath.closeSubPath();
    g.setGradientFill (ColourGradient (C::cyan.withAlpha (0.35f), 0, r.getY(), C::violet.withAlpha (0.08f), 0, r.getBottom(), false));
    g.fillPath (inPath);

    // Threshold line
    const float thr = proc.apvts.getRawParameterValue (IDs::threshold)->load();
    g.setColour (C::amber.withAlpha (0.35f));
    g.drawHorizontalLine ((int) (r.getBottom() - r.getHeight() * (thr + 60.0f) / 60.0f), r.getX(), r.getRight());

    // Gain reduction (top-down, 0..24 dB)
    Path grPath;
    for (int k = 0; k < N; ++k)
    {
        const float d = proc.histGr[(size_t) ((w + k) % N)].load (std::memory_order_relaxed);
        const float y = r.getY() + r.getHeight() * jlimit (0.0f, 1.0f, d / 24.0f);
        if (k == 0) grPath.startNewSubPath (xOf (k), y);
        else        grPath.lineTo (xOf (k), y);
    }
    g.setColour (C::amber);
    g.strokePath (grPath, PathStrokeType (1.8f));

    g.setFont (uiFont (10.0f));
    g.setColour (C::dim);
    g.drawText ("0", r.withHeight (12.0f), Justification::topRight);
    g.drawText ("-24", r.withTop (r.getBottom() - 12.0f), Justification::bottomRight);
}

void CompDisplay::paintMeters (Graphics& g, Rectangle<float> r)
{
    auto head = r.removeFromTop (16.0f);
    g.setFont (uiFont (10.5f, true));
    g.setColour (C::dim);
    g.drawText ("IN  GR  OUT", head, Justification::centred);
    r.removeFromTop (4.0f);
    auto labels = r.removeFromBottom (0.0f);
    ignoreUnused (labels);

    const float bw = 10.0f, gap = (r.getWidth() - 3.0f * bw) / 2.0f;
    auto bar = [&] (int idx, float v, Colour c, bool fromTop)
    {
        auto t = Rectangle<float> (r.getX() + idx * (bw + gap), r.getY(), bw, r.getHeight());
        g.setColour (C::track);
        g.fillRoundedRectangle (t, 3.0f);
        const float h = t.getHeight() * jlimit (0.0f, 1.0f, v);
        g.setColour (c);
        g.fillRoundedRectangle (fromTop ? t.withHeight (h) : t.withTop (t.getBottom() - h), 3.0f);
    };
    auto lvl = [] (float g_) { return (Decibels::gainToDecibels (g_, -60.0f) + 60.0f) / 60.0f; };
    bar (0, lvl (proc.inLevel.load()), C::cyan, false);
    bar (1, proc.grDb.load() / 24.0f, C::amber, true);
    bar (2, lvl (proc.outLevel.load()), C::violet, false);
}

//==============================================================================
SidechainPanel::SidechainPanel (LumenCompressorProcessor& p) : proc (p)
{
    source.addItemList ({ "Internal", "External" }, 1);
    addAndMakeVisible (source);
    sourceAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::scSource, source);

    listen.setClickingTogglesState (true);
    listen.setTooltip ("Hear what the detector hears (sidechain after filters)");
    addAndMakeVisible (listen);
    listenAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::scListen, listen);
}

void SidechainPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromTop (20);
    auto row = r.removeFromTop (28);
    listen.setBounds (row.removeFromRight (70));
    row.removeFromRight (6);
    source.setBounds (row);
}

void SidechainPanel::paint (Graphics& g)
{
    auto r = getLocalBounds().reduced (8, 0);
    g.setFont (uiFont (12.5f));
    g.setColour (C::dim);
    g.drawText ("SIDECHAIN", r.removeFromTop (16), Justification::centred);
    r.removeFromTop (4 + 28 + 10);

    const bool ext = proc.apvts.getRawParameterValue (IDs::scSource)->load() > 0.5f;
    const bool connected = proc.scConnected.load();

    auto row = r.removeFromTop (14);
    g.setFont (uiFont (10.5f));
    g.drawText ("SC", row.removeFromLeft (22), Justification::centredLeft);
    auto tr = row.reduced (0, 4).toFloat();
    g.setColour (C::track);
    g.fillRoundedRectangle (tr, 2.5f);
    const float scDb = Decibels::gainToDecibels (proc.scLevel.load(), -60.0f);
    g.setColour (C::cyan);
    g.fillRoundedRectangle (tr.withWidth (tr.getWidth() * jlimit (0.0f, 1.0f, ext ? (scDb + 60.0f) / 60.0f : 0.0f)), 2.5f);

    r.removeFromTop (6);
    g.setFont (uiFont (10.5f));
    if (ext && ! connected)
    {
        g.setColour (C::amber);
        g.drawFittedText ("Route a track to the sidechain input", r.removeFromTop (28), Justification::centredTop, 2);
    }
    else
    {
        g.setColour (ext ? C::cyan : C::dim);
        g.drawText (ext ? "External key active" : "Keyed from the input", r.removeFromTop (14), Justification::centred);
    }
}

//==============================================================================
PhasePanel::PhasePanel (LumenCompressorProcessor& p) : proc (p)
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
LumenCompressorEditor::LumenCompressorEditor (LumenCompressorProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p), scPanel (p), phasePanel (p), corrMeter (p)
{
    setLookAndFeel (&lnf);

    const std::pair<const char*, const char*> knobDefs[] = {
        { IDs::threshold, "Threshold" }, { IDs::ratio, "Ratio" },       { IDs::knee, "Knee" },
        { IDs::attack, "Attack" },       { IDs::release, "Release" },
        { IDs::scHp, "SC High-Pass" },   { IDs::scLp, "SC Low-Pass" },  { IDs::lookahead, "Lookahead" },
        { IDs::link, "Stereo Link" },
        { IDs::range, "Range" },         { IDs::drive, "Drive" },       { IDs::makeup, "Makeup" },
        { IDs::mix, "Mix" },             { IDs::output, "Output" },
        { IDs::phaseRot, "Rotation" },   { IDs::phaseDly, "Delay" },    { IDs::phaseFoc, "Focus" } };

    for (auto& [id, title] : knobDefs)
    {
        knobs.push_back (std::make_unique<Knob> (proc.apvts, id, title));
        addAndMakeVisible (*knobs.back());
    }

    addAndMakeVisible (display);
    addAndMakeVisible (scPanel);
    addAndMakeVisible (phasePanel);
    addAndMakeVisible (corrMeter);

    learnButton.setTooltip ("Listens to ~6 seconds of audio, then sets style, threshold and timing for the material");
    learnButton.onClick = [this] { learnClicked(); };
    addAndMakeVisible (learnButton);
    undoButton.onClick = [this] { undoLearn(); };
    undoButton.setEnabled (false);
    addAndMakeVisible (undoButton);

    styleBox.addItemList (styleChoices(), 1);
    addAndMakeVisible (styleBox);
    styleAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::style, styleBox);

    channelBox.addItemList (channelChoices(), 1);
    addAndMakeVisible (channelBox);
    channelAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::channel, channelBox);

    for (auto* b : { &autoRelButton, &autoGainButton })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    autoRelButton.setTooltip ("Program-dependent release: fast on transients, slower on sustained compression");
    autoGainButton.setTooltip ("Loudness-matched make-up gain: output stays as loud as the input");
    autoRelAtt  = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::autoRel, autoRelButton);
    autoGainAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::autoGain, autoGainButton);

    setSize (900, 770);
    startTimerHz (30);
}

LumenCompressorEditor::~LumenCompressorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void LumenCompressorEditor::paint (Graphics& g)
{
    g.setGradientFill (ColourGradient (C::bg2, 0, 0, C::bg, 0, (float) getHeight(), false));
    g.fillAll();

    g.setFont (uiFont (26.0f, true));
    g.setGradientFill (ColourGradient (C::cyan, 20, 0, C::violet, 160, 0, false));
    g.drawText ("LUMEN", 20, 14, 200, 34, Justification::centredLeft);
    g.setFont (uiFont (12.0f));
    g.setColour (C::dim);
    g.drawText ("SMART COMPRESSOR", 128, 24, 200, 18, Justification::centredLeft);

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
    g.drawText (materialText, inner.removeFromTop (28), Justification::centredLeft);

    auto bar = [&] (const String& name, float v)
    {
        auto row = inner.removeFromTop (17);
        g.setFont (uiFont (11.0f));
        g.setColour (C::dim);
        g.drawText (name, row.removeFromLeft (82), Justification::centredLeft);
        auto track = row.reduced (0, 6).toFloat();
        g.setColour (C::track);
        g.fillRoundedRectangle (track, 2.5f);
        g.setGradientFill (ColourGradient (C::cyan, track.getX(), 0, C::violet, track.getRight(), 0, false));
        g.fillRoundedRectangle (track.withWidth (track.getWidth() * jlimit (0.0f, 1.0f, v)), 2.5f);
    };
    bar ("Transients", trans);
    bar ("Low end", lowEnd);
    bar ("Brightness", bright);

    inner.removeFromTop (4);
    g.setFont (uiFont (11.0f));
    g.setColour (C::cyan.withAlpha (0.9f));
    g.drawFittedText (statusText, inner.removeFromTop (30), Justification::topLeft, 2);

    if (proc.analyzer.isLearning())
    {
        auto pr = Rectangle<float> (s.getX() + 14, s.getBottom() - 50, s.getWidth() - 28, 3);
        g.setColour (C::track);
        g.fillRect (pr);
        g.setColour (C::cyan);
        g.fillRect (pr.withWidth (pr.getWidth() * learnProgress));
    }

    const char* sections[] = { "DYNAMICS", "DETECTOR & SIDECHAIN", "COLOUR & OUTPUT", "PHASE ALIGN" };
    for (int row = 0; row < 4; ++row)
    {
        const int y = 276 + row * 120;
        g.setFont (uiFont (11.0f, true));
        g.setColour (C::dim.withAlpha (0.8f));
        g.drawText (sections[row], 20, y, 200, 14, Justification::centredLeft);
        g.setColour (C::panelEdge);
        g.drawHorizontalLine (y + 7, 175.0f, (float) getWidth() - 20.0f);
    }
}

void LumenCompressorEditor::resized()
{
    styleBox.setBounds (390, 18, 120, 28);
    channelBox.setBounds (518, 18, 120, 28);
    autoRelButton.setBounds (646, 18, 112, 28);
    autoGainButton.setBounds (766, 18, 118, 28);

    display.setBounds (16, 66, 548, 196);
    smartArea = { 576, 66, 308, 196 };
    learnButton.setBounds (smartArea.getX() + 14, smartArea.getBottom() - 42, 190, 30);
    undoButton.setBounds (smartArea.getX() + 212, smartArea.getBottom() - 42, 82, 30);

    const int colW = (getWidth() - 32) / 5;
    auto cell = [&] (int row, int col) { return Rectangle<int> (16 + col * colW, 294 + row * 120, colW, 100); };

    for (int i = 0; i < 5; ++i)   knobs[(size_t) i]->setBounds (cell (0, i));
    scPanel.setBounds (cell (1, 0));
    for (int i = 5; i < 9; ++i)   knobs[(size_t) i]->setBounds (cell (1, i - 4));
    for (int i = 9; i < 14; ++i)  knobs[(size_t) i]->setBounds (cell (2, i - 9));
    phasePanel.setBounds (cell (3, 0));
    for (int i = 14; i < 17; ++i) knobs[(size_t) i]->setBounds (cell (3, i - 13));
    corrMeter.setBounds (cell (3, 4));
}

void LumenCompressorEditor::timerCallback()
{
    const auto f = proc.analyzer.getLive();
    materialText = lumen::SmartAssist::name (lumen::SmartAssist::classify (f));
    trans  = jlimit (0.0f, 1.0f, f.onsetRate / 6.0f);
    lowEnd = jlimit (0.0f, 1.0f, (f.lowRatio - 0.1f) / 0.6f);
    bright = jlimit (0.0f, 1.0f, f.highRatio / 0.25f);

    const bool learning = proc.analyzer.isLearning();
    learnProgress = proc.analyzer.getLearnProgress();

    if (learning)
    {
        learnButton.setButtonText ("LISTENING " + String (roundToInt (learnProgress * 100.0f)) + "%");
        statusText = f.levelDb < -55.0f ? "Waiting for audio... (click to cancel)" : "Measuring level and dynamics...";
    }
    else if (wasLearning)
    {
        learnButton.setButtonText ("LEARN");
    }
    wasLearning = learning;

    lumen::Features learned;
    if (proc.analyzer.consumeLearnResult (learned))
        applySuggestion (learned);

    if (! learning && f.levelDb > -55.0f && ! statusText.startsWith ("Learned"))
    {
        String s = "GR " + String (proc.grDb.load(), 1) + " dB";
        if (proc.apvts.getRawParameterValue (IDs::autoGain)->load() > 0.5f)
            s << "  |  Auto gain +" << String (proc.autoMakeupDb.load(), 1) << " dB";
        s << "  |  Level " << String (roundToInt (f.levelDb)) << " dB RMS";
        statusText = s;
    }

    display.repaint();
    scPanel.repaint();
    phasePanel.poll();
    corrMeter.repaint();
    repaint (smartArea);
}

void LumenCompressorEditor::learnClicked()
{
    if (proc.analyzer.isLearning())
    {
        proc.analyzer.cancelLearn();
        learnButton.setButtonText ("LEARN");
        statusText = "Cancelled";
        return;
    }
    proc.analyzer.requestLearn();
    statusText = "Measuring level and dynamics...";
}

void LumenCompressorEditor::setParam (const char* id, float value)
{
    if (auto* param = proc.apvts.getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    }
}

void LumenCompressorEditor::applySuggestion (const lumen::Features& f)
{
    using namespace lumen::SmartAssist;
    const auto material = classify (f);
    const auto s = suggestComp (material, f);

    const char* ids[] = { IDs::style, IDs::threshold, IDs::ratio, IDs::knee, IDs::attack, IDs::release,
                          IDs::autoRel, IDs::scHp, IDs::drive, IDs::autoGain, IDs::makeup };
    undoValues.clear();
    for (auto* id : ids)
        if (auto* param = proc.apvts.getParameter (id))
            undoValues.emplace_back (id, param->convertFrom0to1 (param->getValue()));

    setParam (IDs::style, (float) s.style);
    setParam (IDs::threshold, s.threshold);
    setParam (IDs::ratio, s.ratio);
    setParam (IDs::knee, s.knee);
    setParam (IDs::attack, s.attack);
    setParam (IDs::release, s.release);
    setParam (IDs::autoRel, s.autoRelease ? 1.0f : 0.0f);
    setParam (IDs::scHp, s.scHp);
    setParam (IDs::drive, s.drive);
    setParam (IDs::makeup, 0.0f);
    setParam (IDs::autoGain, 1.0f);

    statusText = "Learned: " + String (name (material)) + " -> " + styleChoices()[s.style]
               + ", threshold " + String (s.threshold, 1) + " dB (~" + String (s.targetGr, 0) + " dB GR)";
    undoButton.setEnabled (true);
}

void LumenCompressorEditor::undoLearn()
{
    for (auto& [id, value] : undoValues)
        setParam (id.toRawUTF8(), value);
    undoValues.clear();
    undoButton.setEnabled (false);
    statusText = "Previous settings restored";
}
