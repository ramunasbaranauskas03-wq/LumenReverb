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
void LimiterDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (C::panel); g.fillRoundedRectangle (b, 10.0f);
    g.setColour (C::panelEdge); g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    auto inner = b.reduced (14.0f, 10.0f);
    auto head = inner.removeFromTop (18.0f);
    g.setFont (uiFont (11.5f, true)); g.setColour (C::dim);
    g.drawText ("OUTPUT & GAIN REDUCTION", head, Justification::centredLeft);
    g.setColour (C::amber);
    g.drawText ("GR -" + String (proc.grDb.load(), 1) + " dB", head, Justification::centredRight);
    inner.removeFromTop (6.0f);

    auto meters = inner.removeFromRight (64.0f);
    inner.removeFromRight (10.0f);
    auto r = inner;
    g.setColour (C::bg.withAlpha (0.55f)); g.fillRect (r);

    auto yOf = [&] (float db) { return r.getY() + r.getHeight() * jlimit (0.0f, 1.0f, -db / 30.0f); };
    g.setColour (C::track.withAlpha (0.6f));
    for (float d : { -6.0f, -12.0f, -18.0f, -24.0f }) g.drawHorizontalLine ((int) yOf (d), r.getX(), r.getRight());

    const int N = LumenLimiterProcessor::kHistory;
    const int w = proc.histWrite.load (std::memory_order_acquire);
    auto xOf = [&] (int k) { return r.getX() + r.getWidth() * (float) k / (float) (N - 1); };

    Path outPath;
    outPath.startNewSubPath (r.getX(), r.getBottom());
    for (int k = 0; k < N; ++k)
        outPath.lineTo (xOf (k), yOf (proc.histOut[(size_t) ((w + k) % N)].load (std::memory_order_relaxed)));
    outPath.lineTo (r.getRight(), r.getBottom());
    outPath.closeSubPath();
    g.setGradientFill (ColourGradient (C::cyan.withAlpha (0.45f), 0, r.getY(), C::violet.withAlpha (0.08f), 0, r.getBottom(), false));
    g.fillPath (outPath);

    const float ceil = proc.apvts.getRawParameterValue (IDs::ceiling)->load();
    g.setColour (C::text.withAlpha (0.6f));
    g.drawHorizontalLine ((int) yOf (ceil), r.getX(), r.getRight());
    g.setFont (uiFont (10.0f));
    g.drawText ("ceiling " + String (ceil, 1), Rectangle<float> (r.getRight() - 124.0f, yOf (ceil) + 2.0f, 120.0f, 12.0f), Justification::topRight);

    Path gr;
    for (int k = 0; k < N; ++k)
    {
        const float d = proc.histGr[(size_t) ((w + k) % N)].load (std::memory_order_relaxed);
        const float y = r.getY() + r.getHeight() * jlimit (0.0f, 1.0f, d / 12.0f);
        if (k == 0) gr.startNewSubPath (xOf (k), y); else gr.lineTo (xOf (k), y);
    }
    g.setColour (C::amber);
    g.strokePath (gr, PathStrokeType (1.8f));

    g.setFont (uiFont (10.0f, true)); g.setColour (C::dim);
    g.drawText ("IN  GR  OUT", meters.removeFromTop (14.0f), Justification::centred);
    meters.removeFromTop (4.0f);
    const float bw = 12.0f, gap = (meters.getWidth() - 3 * bw) / 2.0f;
    auto bar = [&] (int i, float v, Colour c, bool top)
    {
        auto t = Rectangle<float> (meters.getX() + i * (bw + gap), meters.getY(), bw, meters.getHeight());
        g.setColour (C::track); g.fillRoundedRectangle (t, 3.0f);
        const float h = t.getHeight() * jlimit (0.0f, 1.0f, v);
        g.setColour (c);
        g.fillRoundedRectangle (top ? t.withHeight (h) : t.withTop (t.getBottom() - h), 3.0f);
    };
    auto lvl = [] (float x) { return (Decibels::gainToDecibels (x, -60.0f) + 60.0f) / 60.0f; };
    bar (0, lvl (proc.inLevel.load()), C::cyan, false);
    bar (1, proc.grDb.load() / 12.0f, C::amber, true);
    bar (2, lvl (proc.outLevel.load()), C::violet, false);
}

//==============================================================================
Rectangle<float> TargetStrip::cardBounds (int i) const
{
    const int n = (int) targets().size() - 1;   // all except Custom
    auto r = getLocalBounds().toFloat();
    r.removeFromTop (20.0f);
    const float gap = 8.0f, w = (r.getWidth() - gap * (float) (n - 1)) / (float) n;
    return { r.getX() + (float) i * (w + gap), r.getY(), w, r.getHeight() };
}

void TargetStrip::mouseUp (const MouseEvent& e)
{
    for (int i = 0; i < (int) targets().size() - 1; ++i)
        if (cardBounds (i).contains (e.position) && onSelect) onSelect (i);
}

void TargetStrip::paint (Graphics& g)
{
    g.setFont (uiFont (11.0f, true));
    g.setColour (C::dim.withAlpha (0.8f));
    g.drawText ("TARGETS  -  how far you are from each platform (click to select)", getLocalBounds().removeFromTop (16), Justification::centredLeft);

    const float L = currentLoudness (proc);
    const float tp = proc.tpMax.load();
    const int sel = (int) proc.apvts.getRawParameterValue (IDs::target)->load();

    for (int i = 0; i < (int) targets().size() - 1; ++i)
    {
        const auto& t = targets()[(size_t) i];
        auto r = cardBounds (i);
        const bool selected = (i == sel);
        g.setColour (C::panel); g.fillRoundedRectangle (r, 8.0f);
        g.setColour (selected ? C::cyan : C::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, selected ? 1.6f : 1.0f);

        auto in = r.reduced (8.0f, 6.0f);
        g.setFont (uiFont (11.0f, true)); g.setColour (C::text);
        g.drawText (t.name, in.removeFromTop (15.0f), Justification::centredLeft);
        g.setFont (uiFont (10.0f)); g.setColour (C::dim);
        g.drawText (String (t.lufs, 0) + " LUFS / " + String (t.tp, 1) + " TP", in.removeFromTop (13.0f), Justification::centredLeft);

        String line; Colour c = C::dim; String sub;
        if (L < -99.0f) { line = "--"; sub = "play audio"; }
        else
        {
            const float d = t.lufs - L;   // + = missing loudness
            if (std::abs (d) <= 0.5f) { line = String (CharPointer_UTF8 ("ON TARGET \xe2\x9c\x93")); c = C::cyan; sub = String (L, 1) + " LUFS"; }
            else if (d > 0.0f)        { line = "+" + String (d, 1) + " dB to go"; c = C::amber; sub = "needs more gain"; }
            else
            {
                line = String (-d, 1) + " dB over"; c = C::violet;
                sub = t.normalises ? "turned down " + String (-d, 1) + " dB" : "louder than target";
            }
        }
        g.setFont (uiFont (13.0f, true)); g.setColour (c);
        g.drawText (line, in.removeFromTop (18.0f), Justification::centredLeft);
        auto last = in.removeFromTop (13.0f);
        g.setFont (uiFont (9.5f)); g.setColour (C::dim);
        g.drawText (sub, last, Justification::centredLeft);
        if (tp > t.tp + 0.05f && tp > -99.0f)
        {
            g.setFont (uiFont (10.0f, true)); g.setColour (Colour (0xffff5c6c));
            g.drawText ("TP!", last, Justification::centredRight);
        }
    }
}

//==============================================================================
LumenLimiterEditor::LumenLimiterEditor (LumenLimiterProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p), strip (p)
{
    setLookAndFeel (&lnf);

    const std::pair<const char*, const char*> defs[] = {
        { IDs::gain, "Gain" }, { IDs::ceiling, "Ceiling" }, { IDs::release, "Release" }, { IDs::lookahead, "Lookahead" },
        { IDs::link, "Stereo Link" }, { IDs::clipLevel, "Clip Level" }, { IDs::clipShape, "Clip Shape" },
        { IDs::customLufs, "Custom Target" } };
    for (auto& [id, t] : defs) { knobs.push_back (std::make_unique<Knob> (proc.apvts, id, t)); addAndMakeVisible (*knobs.back()); }

    addAndMakeVisible (display);
    addAndMakeVisible (strip);
    strip.onSelect = [this] (int i) { selectTarget (i); };

    styleBox.addItemList (styleChoices(), 1);
    addAndMakeVisible (styleBox);
    styleAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::style, styleBox);

    targetBox.addItemList (targetNames(), 1);
    addAndMakeVisible (targetBox);
    targetAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::target, targetBox);
    targetBox.onChange = [this]
    {
        const int t = targetBox.getSelectedItemIndex();
        if (t >= 0 && t < (int) targets().size() - 1)
            setParam (IDs::ceiling, targets()[(size_t) t].tp);   // each platform comes with its own ceiling
    };

    ditherBox.addItemList ({ "Dither off", "Dither 24-bit", "Dither 16-bit" }, 1);
    addAndMakeVisible (ditherBox);
    ditherAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::dither, ditherBox);

    osBox.addItemList ({ "Oversampling 1x", "Oversampling 2x", "Oversampling 4x", "Oversampling 8x", "Oversampling 16x" }, 1);
    osBox.setTooltip ("Clipper oversampling. Higher = cleaner clipping (less aliasing), more CPU and latency");
    addAndMakeVisible (osBox);
    osAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::oversample, osBox);

    for (auto* b : { &autoRelButton, &clipButton, &deltaButton, &gainMatchButton })
    { b->setClickingTogglesState (true); addAndMakeVisible (*b); }
    autoRelAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::autoRel, autoRelButton);
    clipAtt    = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::clipOn, clipButton);
    deltaAtt   = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::delta, deltaButton);
    gmAtt      = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::gainMatch, gainMatchButton);
    deltaButton.setTooltip ("Hear only what the clipper and limiter take away");
    gainMatchButton.setTooltip ("Turns the output down by the Gain amount so you can compare at equal loudness");
    clipButton.setTooltip ("Clipper before the limiter: shaves the sharpest peaks so the limiter works less");

    matchButton.setTooltip ("While the track plays, Gain is adjusted until the loudness hits the selected target");
    matchButton.onClick = [this] { if (matching) { matching = false; matchButton.setButtonText ("MATCH TARGET"); statusText = "Stopped"; } else startMatch(); };
    addAndMakeVisible (matchButton);
    resetButton.setTooltip ("Reset the loudness measurement");
    resetButton.onClick = [this] { proc.resetMeters.store (true); };
    addAndMakeVisible (resetButton);

    setSize (900, 670);
    startTimerHz (30);
}

LumenLimiterEditor::~LumenLimiterEditor() { stopTimer(); setLookAndFeel (nullptr); }

float LumenLimiterEditor::getParam (const char* id) const
{
    auto* prm = proc.apvts.getParameter (id);
    return prm->convertFrom0to1 (prm->getValue());
}

void LumenLimiterEditor::setParam (const char* id, float value)
{
    if (auto* prm = proc.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

void LumenLimiterEditor::selectTarget (int i)
{
    setParam (IDs::target, (float) i);
    setParam (IDs::ceiling, targets()[(size_t) i].tp);
}

void LumenLimiterEditor::startMatch()
{
    matching = true; matchTicks = 0; stableCount = 0; matchElapsed = 0; grAcc = 0.0f; grCount = 0;
    proc.resetMeters.store (true);
    matchButton.setButtonText ("MATCHING...");
    statusText = "Matching: keep the loudest section playing";
}

void LumenLimiterEditor::stepMatch()
{
    const float st = proc.shortTerm.load();
    grAcc += proc.grDb.load(); ++grCount;
    if (++matchTicks < 60) return;               // adjust every 2 s
    matchTicks = 0; matchElapsed += 2;

    if (st < -60.0f)
    {
        statusText = "Matching: waiting for audio...";
        if (matchElapsed > 40) { matching = false; matchButton.setButtonText ("MATCH TARGET"); }
        return;
    }

    const float target = proc.targetLufs();
    const float err = target - st;
    const float gain = getParam (IDs::gain);

    if (std::abs (err) < 0.3f)
    {
        if (++stableCount >= 2)
        {
            matching = false;
            matchButton.setButtonText ("MATCH TARGET");
            const float avgGr = grCount > 0 ? grAcc / (float) grCount : 0.0f;
            statusText = "On target (" + String (st, 1) + " LUFS), Gain +" + String (gain, 1) + " dB"
                       + (avgGr > 6.0f ? ". Heavy limiting: try CLIPPER" : "");
            return;
        }
    }
    else stableCount = 0;

    const float newGain = jlimit (0.0f, 24.0f, gain + jlimit (-4.0f, 4.0f, err * 0.8f));
    setParam (IDs::gain, newGain);
    statusText = "Matching: " + String (st, 1) + " -> " + String (target, 1) + " LUFS, Gain +" + String (newGain, 1) + " dB";

    if ((newGain >= 23.99f && err > 0.5f) || matchElapsed > 40)
    {
        matching = false;
        matchButton.setButtonText ("MATCH TARGET");
        statusText = newGain >= 23.99f ? "Max gain reached: the mix is too quiet for this target" : "Stopped after 40 s";
    }
}

void LumenLimiterEditor::timerCallback()
{
    if (matching) stepMatch();
    display.repaint();
    strip.repaint();
    repaint (loudArea);
}

void LumenLimiterEditor::paint (Graphics& g)
{
    g.setGradientFill (ColourGradient (C::bg2, 0, 0, C::bg, 0, (float) getHeight(), false));
    g.fillAll();
    g.setFont (uiFont (26.0f, true));
    g.setGradientFill (ColourGradient (C::cyan, 20, 0, C::violet, 160, 0, false));
    g.drawText ("LUMEN", 20, 14, 200, 34, Justification::centredLeft);
    g.setFont (uiFont (12.0f)); g.setColour (C::dim);
    g.drawText ("SMART LIMITER", 128, 24, 200, 18, Justification::centredLeft);

    // ---- Loudness panel ----
    auto s = loudArea.toFloat();
    g.setColour (C::panel); g.fillRoundedRectangle (s, 10.0f);
    g.setColour (C::panelEdge); g.drawRoundedRectangle (s.reduced (0.5f), 10.0f, 1.0f);
    auto in = loudArea.reduced (14, 10);
    auto head = in.removeFromTop (18);
    const float target = proc.targetLufs(), tpT = proc.targetTp();
    const int t = (int) proc.apvts.getRawParameterValue (IDs::target)->load();
    g.setFont (uiFont (11.5f, true)); g.setColour (C::dim);
    g.drawText ("LOUDNESS", head, Justification::centredLeft);
    g.setColour (C::text);
    g.drawText (String (targets()[(size_t) jlimit (0, (int) targets().size() - 1, t)].name) + "  " + String (target, 1) + " LUFS",
                head, Justification::centredRight);

    const float I = proc.integrated.load(), S = proc.shortTerm.load(), M = proc.momentary.load(), TP = proc.tpMax.load();
    const float L = currentLoudness (proc);
    auto fmt = [] (float v) { return v < -99.0f ? String ("--") : String (v, 1); };

    auto big = in.removeFromTop (40);
    g.setFont (uiFont (32.0f, true));
    g.setColour (I > -99.0f && std::abs (I - target) <= 0.5f ? C::cyan : C::text);
    g.drawText (fmt (I), big.removeFromLeft (104), Justification::centredLeft);
    // Missing / over for the selected target
    String miss; Colour mc = C::dim;
    if (L > -99.0f)
    {
        const float d = target - L;
        if (std::abs (d) <= 0.5f) { miss = String (CharPointer_UTF8 ("on target \xe2\x9c\x93")); mc = C::cyan; }
        else if (d > 0.0f)        { miss = "+" + String (d, 1) + " dB to go"; mc = C::amber; }
        else                      { miss = String (-d, 1) + " dB over"; mc = C::violet; }
    }
    auto bigR = big;
    g.setFont (uiFont (11.0f)); g.setColour (C::dim);
    g.drawText ("LUFS integrated", bigR.removeFromTop (18), Justification::centredLeft);
    g.setFont (uiFont (13.5f, true)); g.setColour (mc);
    g.drawText (miss, bigR, Justification::centredLeft);

    auto row = in.removeFromTop (18);
    g.setFont (uiFont (11.0f));
    const int cw = row.getWidth() / 3;
    auto cell = [&] (const String& name, const String& v, Colour c)
    {
        auto r = row.removeFromLeft (cw);
        g.setColour (C::dim); g.drawText (name, r.removeFromLeft (42), Justification::centredLeft);
        g.setColour (c); g.drawText (v, r, Justification::centredLeft);
    };
    cell ("Short", fmt (S), C::text);
    cell ("Mom.", fmt (M), C::text);
    cell ("TP", fmt (TP), TP > tpT + 0.05f ? Colour (0xffff5c6c) : C::text);

    // Target bar (-30..-4 LUFS): short-term fill, target marker
    auto tb = in.removeFromTop (20).reduced (0, 6).toFloat();
    auto xOf = [&] (float v) { return tb.getX() + tb.getWidth() * jlimit (0.0f, 1.0f, (v + 30.0f) / 26.0f); };
    g.setColour (C::track); g.fillRoundedRectangle (tb, 3.0f);
    if (S > -99.0f)
    {
        g.setGradientFill (ColourGradient (C::cyan, tb.getX(), 0, C::violet, tb.getRight(), 0, false));
        g.fillRoundedRectangle (tb.withRight (xOf (S)), 3.0f);
    }
    g.setColour (C::amber);
    g.fillRect (Rectangle<float> (xOf (target) - 1.5f, tb.getY() - 4.0f, 3.0f, tb.getHeight() + 8.0f));

    g.setFont (uiFont (10.5f));
    g.setColour (C::cyan.withAlpha (0.9f));
    g.drawFittedText (statusText, in.removeFromTop (30), Justification::topLeft, 2);

    const char* sections[] = { "LIMITER", "CLIPPER, TARGET & MONITOR" };
    for (int r = 0; r < 2; ++r)
    {
        const int y = 300 + r * 126;
        g.setFont (uiFont (11.0f, true)); g.setColour (C::dim.withAlpha (0.8f));
        g.drawText (sections[r], 20, y, 260, 14, Justification::centredLeft);
        g.setColour (C::panelEdge);
        g.drawHorizontalLine (y + 7, 230.0f, (float) getWidth() - 20.0f);
    }
    g.setFont (uiFont (12.5f)); g.setColour (C::dim);
    g.drawText ("CLIP STAGE", clipCell.withHeight (16), Justification::centred);
    g.drawText ("MONITOR", monitorCell.withHeight (16), Justification::centred);
}

void LumenLimiterEditor::resized()
{
    styleBox.setBounds (420, 18, 140, 28);
    targetBox.setBounds (568, 18, 186, 28);
    autoRelButton.setBounds (762, 18, 122, 28);

    display.setBounds (16, 66, 548, 222);
    loudArea = { 576, 66, 308, 222 };
    matchButton.setBounds (loudArea.getX() + 14, loudArea.getBottom() - 42, 190, 30);
    resetButton.setBounds (loudArea.getX() + 212, loudArea.getBottom() - 42, 82, 30);

    const int colW = (getWidth() - 32) / 5;
    auto cell = [&] (int row, int col) { return Rectangle<int> (16 + col * colW, 318 + row * 126, colW, 104); };
    for (int i = 0; i < 5; ++i) knobs[(size_t) i]->setBounds (cell (0, i));

    clipCell = cell (1, 0).reduced (8, 0);
    clipButton.setBounds (clipCell.getX(), clipCell.getY() + 22, clipCell.getWidth(), 26);
    osBox.setBounds (clipCell.getX(), clipCell.getY() + 54, clipCell.getWidth(), 26);
    knobs[5]->setBounds (cell (1, 1));
    knobs[6]->setBounds (cell (1, 2));
    knobs[7]->setBounds (cell (1, 3));
    monitorCell = cell (1, 4).reduced (8, 0);
    deltaButton.setBounds (monitorCell.getX(), monitorCell.getY() + 20, monitorCell.getWidth() / 2 - 3, 26);
    gainMatchButton.setBounds (monitorCell.getX() + monitorCell.getWidth() / 2 + 3, monitorCell.getY() + 20, monitorCell.getWidth() / 2 - 3, 26);
    ditherBox.setBounds (monitorCell.getX(), monitorCell.getY() + 54, monitorCell.getWidth(), 26);

    strip.setBounds (16, 560, getWidth() - 32, 98);
}
