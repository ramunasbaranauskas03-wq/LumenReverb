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
    setColour (Slider::textBoxTextColourId, C::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
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
    const float stroke = 4.0f;

    Path bgArc;
    bgArc.addCentredArc (cx, cy, r, r, 0.0f, start, end, true);
    g.setColour (C::track);
    g.strokePath (bgArc, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));

    Path valArc;
    valArc.addCentredArc (cx, cy, r, r, 0.0f, start, angle, true);
    g.setGradientFill (ColourGradient (C::cyan, cx - r, cy + r, C::violet, cx + r, cy - r, false));
    g.strokePath (valArc, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));

    const float kr = r * 0.70f;
    g.setGradientFill (ColourGradient (Colour (0xff2b3446), cx, cy - kr, Colour (0xff161b26), cx, cy + kr, false));
    g.fillEllipse (cx - kr, cy - kr, kr * 2.0f, kr * 2.0f);
    g.setColour (C::panelEdge);
    g.drawEllipse (cx - kr, cy - kr, kr * 2.0f, kr * 2.0f, 1.0f);

    const float px = cx + std::sin (angle) * kr * 0.78f;
    const float py = cy - std::cos (angle) * kr * 0.78f;
    g.setColour (s.isMouseOverOrDragging() ? C::cyan : C::text);
    g.drawLine (cx + std::sin (angle) * kr * 0.35f, cy - std::cos (angle) * kr * 0.35f, px, py, 2.5f);
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
    slider.setDoubleClickReturnValue (true, state.getParameter (paramId)->convertFrom0to1 (
                                                state.getParameter (paramId)->getDefaultValue()));
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
void DecayDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (C::panel);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (C::panelEdge);
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    const float decay  = proc.effDecay.load();
    const float preMs  = proc.effPreDelay.load();
    const float size   = proc.effSize.load();
    const float early  = proc.effEarly.load();
    const bool  frozen = proc.apvts.getRawParameterValue (IDs::freeze)->load() > 0.5f;

    auto header = b.reduced (14.0f, 10.0f).removeFromTop (18.0f);
    g.setFont (uiFont (11.5f, true));
    g.setColour (C::dim);
    g.drawText ("DECAY ENVELOPE", header, Justification::centredLeft);
    g.setColour (C::text);
    g.drawText (frozen ? String ("FROZEN") : "RT60 " + String (decay, 2) + " s   PRE " + String (roundToInt (preMs)) + " ms",
                header, Justification::centredRight);

    auto plot = b.reduced (14.0f, 10.0f);
    plot.removeFromTop (24.0f);
    auto axis = plot.removeFromBottom (14.0f);

    const float pre  = preMs * 0.001f;
    const float tMax = jmax (0.5f, (pre + (frozen ? 3.0f : decay)) * 1.1f);
    auto xOf = [&] (float t)  { return plot.getX() + plot.getWidth() * (t / tMax); };
    auto yOf = [&] (float db) { return plot.getY() + plot.getHeight() * (jlimit (-66.0f, 0.0f, db) / -66.0f); };

    g.setColour (C::track.withAlpha (0.6f));
    for (float db : { -20.0f, -40.0f, -60.0f })
        g.drawHorizontalLine ((int) yOf (db), plot.getX(), plot.getRight());

    // Tail curve
    Path tail;
    tail.startNewSubPath (xOf (0.0f), plot.getBottom());
    tail.lineTo (xOf (pre), plot.getBottom());
    tail.lineTo (xOf (pre), yOf (-3.0f));
    const int steps = 160;
    for (int k = 0; k <= steps; ++k)
    {
        const float t = pre + (tMax - pre) * (float) k / (float) steps;
        const float db = frozen ? -3.0f : -3.0f - 60.0f * (t - pre) / decay;
        tail.lineTo (xOf (t), yOf (db));
    }
    Path fill (tail);
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();

    g.setGradientFill (ColourGradient (C::violet.withAlpha (0.45f), plot.getX(), plot.getY(),
                                       C::cyan.withAlpha (0.03f), plot.getX(), plot.getBottom(), false));
    g.fillPath (fill);
    g.setGradientFill (ColourGradient (C::cyan, plot.getX(), 0, C::violet, plot.getRight(), 0, false));
    g.strokePath (tail, PathStrokeType (2.0f));

    // Early reflections
    static constexpr float tapMs[] = { 3.1f, 5.7f, 8.9f, 12.3f, 15.8f, 19.6f, 24.1f, 28.7f, 33.9f, 39.4f, 46.2f, 53.5f };
    const float scale = 0.4f + 1.2f * size;
    g.setColour (C::cyan.withAlpha (0.85f));
    for (int k = 0; k < 12; ++k)
    {
        const float t  = pre + tapMs[k] * 0.001f * scale;
        const float db = -6.0f - 1.5f * (float) k - (1.0f - early) * 30.0f;
        g.drawVerticalLine ((int) xOf (t), yOf (db), plot.getBottom());
    }

    // Axis labels
    g.setFont (uiFont (10.5f));
    g.setColour (C::dim);
    g.drawText ("0 s", axis, Justification::centredLeft);
    g.drawText (String (tMax * 0.5f, 1) + " s", axis, Justification::centred);
    g.drawText (String (tMax, 1) + " s", axis, Justification::centredRight);

    // Output meter
    const float lvl = jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (proc.outLevel.load(), -60.0f) + 60.0f) / 60.0f);
    auto meter = Rectangle<float> (b.getRight() - 8.0f, plot.getY(), 3.0f, plot.getHeight());
    g.setColour (C::track);
    g.fillRoundedRectangle (meter, 1.5f);
    g.setColour (C::cyan);
    g.fillRoundedRectangle (meter.withTop (meter.getBottom() - meter.getHeight() * lvl), 1.5f);
}

//==============================================================================
LumenAudioProcessorEditor::LumenAudioProcessorEditor (LumenAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p)
{
    setLookAndFeel (&lnf);

    const std::pair<const char*, const char*> knobDefs[] = {
        { IDs::size, "Size" },         { IDs::decay, "Decay" },      { IDs::preDelay, "Pre-Delay" },
        { IDs::diffusion, "Diffusion" },{ IDs::early, "Early" },
        { IDs::damping, "Damping" },   { IDs::lowCut, "Low Cut" },   { IDs::highCut, "High Cut" },
        { IDs::width, "Width" },       { IDs::modDepth, "Mod Depth" },
        { IDs::modRate, "Mod Rate" },  { IDs::duck, "Ducking" },     { IDs::adapt, "Adapt" },
        { IDs::mix, "Mix" },           { IDs::output, "Output" } };

    for (auto& [id, title] : knobDefs)
    {
        knobs.push_back (std::make_unique<Knob> (proc.apvts, id, title));
        addAndMakeVisible (*knobs.back());
    }

    addAndMakeVisible (display);

    learnButton.setTooltip ("Listens to ~6 seconds of audio and sets the reverb for the material and tempo");
    learnButton.onClick = [this] { learnClicked(); };
    addAndMakeVisible (learnButton);

    undoButton.onClick = [this] { undoLearn(); };
    undoButton.setEnabled (false);
    addAndMakeVisible (undoButton);

    freezeButton.setClickingTogglesState (true);
    addAndMakeVisible (freezeButton);
    freezeAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::freeze, freezeButton);

    syncBox.addItemList (syncChoices(), 1);
    addAndMakeVisible (syncBox);
    syncAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::sync, syncBox);

    setSize (900, 650);
    startTimerHz (30);
}

LumenAudioProcessorEditor::~LumenAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void LumenAudioProcessorEditor::paint (Graphics& g)
{
    g.setGradientFill (ColourGradient (C::bg2, 0, 0, C::bg, 0, (float) getHeight(), false));
    g.fillAll();

    // Header
    g.setFont (uiFont (26.0f, true));
    g.setGradientFill (ColourGradient (C::cyan, 20, 0, C::violet, 160, 0, false));
    g.drawText ("LUMEN", 20, 14, 200, 34, Justification::centredLeft);
    g.setFont (uiFont (12.0f));
    g.setColour (C::dim);
    g.drawText ("SMART REVERB", 128, 24, 200, 18, Justification::centredLeft);
    g.drawText ("SYNC", 640, 22, 40, 20, Justification::centredRight);

    // Smart Assist panel
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

    // Section labels
    g.setFont (uiFont (11.0f, true));
    g.setColour (C::dim.withAlpha (0.8f));
    const char* sections[] = { "SPACE", "TONE & MOTION", "DYNAMICS & OUTPUT" };
    for (int row = 0; row < 3; ++row)
    {
        const int y = 276 + row * 120;
        g.drawText (sections[row], 20, y, 200, 14, Justification::centredLeft);
        g.setColour (C::panelEdge);
        g.drawHorizontalLine (y + 7, 150.0f, (float) getWidth() - 20.0f);
        g.setColour (C::dim.withAlpha (0.8f));
    }
}

void LumenAudioProcessorEditor::resized()
{
    freezeButton.setBounds (520, 18, 100, 28);
    syncBox.setBounds (690, 18, 190, 28);

    display.setBounds (16, 66, 548, 196);
    smartArea = { 576, 66, 308, 196 };
    learnButton.setBounds (smartArea.getX() + 14, smartArea.getBottom() - 42, 190, 30);
    undoButton.setBounds (smartArea.getX() + 212, smartArea.getBottom() - 42, 82, 30);

    const int colW = (getWidth() - 32) / 5;
    for (int i = 0; i < (int) knobs.size(); ++i)
    {
        const int row = i / 5, col = i % 5;
        knobs[(size_t) i]->setBounds (16 + col * colW, 294 + row * 120, colW, 100);
    }
}

void LumenAudioProcessorEditor::timerCallback()
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
        statusText = f.levelDb < -55.0f ? "Waiting for audio... (click to cancel)" : "Analysing material and tempo...";
    }
    else if (wasLearning)
    {
        learnButton.setButtonText ("LEARN");
    }
    wasLearning = learning;

    lumen::Features learned;
    if (proc.analyzer.consumeLearnResult (learned))
        applySuggestion (learned);

    if (! learning && proc.apvts.getRawParameterValue (IDs::adapt)->load() > 0.5f && f.levelDb > -55.0f
        && ! statusText.startsWith ("Learned"))
    {
        const float baseDecay = proc.apvts.getRawParameterValue (IDs::decay)->load();
        const bool synced = proc.apvts.getRawParameterValue (IDs::sync)->load() > 0.5f;
        String s = "Adapting live: ";
        if (! synced)
            s << "decay " << String (roundToInt ((proc.effDecay.load() / baseDecay - 1.0f) * 100.0f)) << "%, ";
        s << "low cut " << String (roundToInt (proc.effLowCut.load())) << " Hz, damping "
          << String (proc.effDamping.load() / 1000.0f, 1) << " kHz";
        statusText = s;
    }

    display.repaint();
    repaint (smartArea);
}

void LumenAudioProcessorEditor::learnClicked()
{
    if (proc.analyzer.isLearning())
    {
        proc.analyzer.cancelLearn();
        learnButton.setButtonText ("LEARN");
        statusText = "Cancelled";
        return;
    }
    proc.analyzer.requestLearn();
    statusText = "Analysing material and tempo...";
}

void LumenAudioProcessorEditor::setParam (const char* id, float value)
{
    if (auto* param = proc.apvts.getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    }
}

void LumenAudioProcessorEditor::applySuggestion (const lumen::Features& f)
{
    using namespace lumen::SmartAssist;
    const auto material = classify (f);
    const auto s = suggest (material, f, proc.hostBpm.load());

    const char* ids[] = { IDs::size, IDs::decay, IDs::preDelay, IDs::diffusion, IDs::early, IDs::damping,
                          IDs::lowCut, IDs::highCut, IDs::width, IDs::modDepth, IDs::modRate, IDs::duck,
                          IDs::mix, IDs::sync };
    undoValues.clear();
    for (auto* id : ids)
        if (auto* param = proc.apvts.getParameter (id))
            undoValues.emplace_back (id, param->convertFrom0to1 (param->getValue()));

    setParam (IDs::size, s.size);         setParam (IDs::decay, s.decay);
    setParam (IDs::preDelay, s.preDelay); setParam (IDs::diffusion, s.diffusion);
    setParam (IDs::early, s.early);       setParam (IDs::damping, s.damping);
    setParam (IDs::lowCut, s.lowCut);     setParam (IDs::highCut, s.highCut);
    setParam (IDs::width, s.width);       setParam (IDs::modDepth, s.modDepth);
    setParam (IDs::modRate, s.modRate);   setParam (IDs::duck, s.duck);
    setParam (IDs::sync, 0.0f);

    // Keep 100% wet when the plugin is used on a send/aux bus
    if (proc.apvts.getRawParameterValue (IDs::mix)->load() < 95.0f)
        setParam (IDs::mix, s.mix);

    const double bpm = proc.hostBpm.load();
    statusText = "Learned: " + String (name (material))
               + (bpm > 30.0 ? " @ " + String (roundToInt (bpm)) + " BPM" : String())
               + ". Settings applied.";
    undoButton.setEnabled (true);
}

void LumenAudioProcessorEditor::undoLearn()
{
    for (auto& [id, value] : undoValues)
        setParam (id.toRawUTF8(), value);
    undoValues.clear();
    undoButton.setEnabled (false);
    statusText = "Previous settings restored";
}
