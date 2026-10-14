#include "PluginEditor.h"
#include "dsp/DelaySuggest.h"

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
void EchoDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (C::panel);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (C::panelEdge);
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    const float tL = proc.effTimeL.load(), tR = proc.effTimeR.load();
    const float fb = proc.effFeedback.load();
    const int mode = proc.effMode.load();
    const bool frozen = proc.apvts.getRawParameterValue (IDs::freeze)->load() > 0.5f;
    const bool synced = proc.apvts.getRawParameterValue (IDs::sync)->load() > 0.5f;

    auto header = b.reduced (14.0f, 10.0f).removeFromTop (18.0f);
    g.setFont (uiFont (11.5f, true));
    g.setColour (C::dim);
    g.drawText ("ECHO PATTERN", header, Justification::centredLeft);

    String info;
    if (synced)
        info << divisionChoices()[(int) proc.apvts.getRawParameterValue (IDs::division)->load()] << "   ";
    info << String (roundToInt (tL * 1000.0f)) << " ms";
    if (mode != 2 && std::abs (tR - tL) > 0.0005f)
        info << " / " << String (roundToInt (tR * 1000.0f)) << " ms";
    info << "   " << modeChoices()[mode].toUpperCase();
    if (frozen) info = "FROZEN   " + info;
    g.setColour (C::text);
    g.drawText (info, header, Justification::centredRight);

    auto plot = b.reduced (14.0f, 10.0f);
    plot.removeFromTop (26.0f);
    auto axis = plot.removeFromBottom (14.0f);
    const float midY = plot.getCentreY();

    // Build echo list: (time, amplitude, isLeft)
    struct Echo { float t, a; bool left; };
    std::vector<Echo> echoes;
    const float g0 = frozen ? 1.0f : fb;
    const float tMaxWanted = jmax (tL, tR) * 8.0f;

    if (mode == 1) // ping-pong
    {
        float t = tL, a = 1.0f; bool left = true;
        for (int k = 0; k < 24 && t <= tMaxWanted && a > 0.01f; ++k)
        {
            echoes.push_back ({ t, a, left });
            t += left ? tR : tL;
            a *= g0;
            left = ! left;
        }
    }
    else
    {
        for (int ch = 0; ch < (mode == 2 ? 1 : 2); ++ch)
        {
            const float dt = ch == 0 ? tL : tR;
            float a = 1.0f;
            for (int k = 1; k <= 24 && k * dt <= tMaxWanted && a > 0.01f; ++k, a *= g0)
                echoes.push_back ({ k * dt, a, ch == 0 });
        }
    }

    const float tMax = jmax (0.2f, tMaxWanted * 1.02f);
    auto xOf = [&] (float t) { return plot.getX() + plot.getWidth() * (t / tMax); };

    g.setColour (C::track.withAlpha (0.7f));
    g.drawHorizontalLine ((int) midY, plot.getX(), plot.getRight());
    for (int k = 1; k < 8; ++k)
        g.drawVerticalLine ((int) xOf (jmax (tL, tR) * (float) k), plot.getY(), plot.getBottom());

    // Dry hit
    g.setColour (C::text.withAlpha (0.8f));
    g.fillRoundedRectangle (plot.getX(), plot.getY() + 4.0f, 3.0f, plot.getHeight() - 8.0f, 1.5f);

    const float half = plot.getHeight() * 0.5f - 4.0f;
    for (auto& e : echoes)
    {
        const float x = xOf (e.t);
        const float hgt = half * e.a;
        const bool up = (mode == 2) || e.left;
        auto bar = up ? Rectangle<float> (x - 2.5f, midY - hgt, 5.0f, hgt)
                      : Rectangle<float> (x - 2.5f, midY, 5.0f, hgt);
        if (mode == 2)
            bar = Rectangle<float> (x - 2.5f, midY - hgt, 5.0f, hgt * 2.0f);
        g.setColour ((e.left ? C::cyan : C::violet).withAlpha (0.35f + 0.65f * e.a));
        g.fillRoundedRectangle (bar, 2.0f);
    }

    g.setFont (uiFont (10.5f));
    g.setColour (C::dim);
    if (mode != 2)
    {
        g.drawText ("L", Rectangle<float> (plot.getRight() - 14.0f, plot.getY(), 14.0f, 14.0f), Justification::centred);
        g.drawText ("R", Rectangle<float> (plot.getRight() - 14.0f, plot.getBottom() - 14.0f, 14.0f, 14.0f), Justification::centred);
    }
    g.drawText ("0 s", axis, Justification::centredLeft);
    g.drawText (String (tMax * 0.5f, 2) + " s", axis, Justification::centred);
    g.drawText (String (tMax, 2) + " s", axis, Justification::centredRight);
}

//==============================================================================
SidechainPanel::SidechainPanel (LumenDelayProcessor& p) : proc (p)
{
    source.addItemList ({ "Input", "Sidechain" }, 1);
    addAndMakeVisible (source);
    attachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::duckSrc, source);
}

void SidechainPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 0);
    r.removeFromTop (22);
    source.setBounds (r.removeFromTop (28));
}

void SidechainPanel::paint (Graphics& g)
{
    auto r = getLocalBounds().reduced (10, 0);
    g.setFont (uiFont (12.5f));
    g.setColour (C::dim);
    g.drawText ("DUCK SOURCE", r.removeFromTop (16), Justification::centred);
    r.removeFromTop (6 + 28 + 10);

    const bool useSc = proc.apvts.getRawParameterValue (IDs::duckSrc)->load() > 0.5f;
    const bool connected = proc.scConnected.load();

    auto meter = [&] (const String& name, float v, Colour c)
    {
        auto row = r.removeFromTop (16);
        g.setFont (uiFont (10.5f));
        g.setColour (C::dim);
        g.drawText (name, row.removeFromLeft (24), Justification::centredLeft);
        auto tr = row.reduced (0, 5).toFloat();
        g.setColour (C::track);
        g.fillRoundedRectangle (tr, 2.5f);
        g.setColour (c);
        g.fillRoundedRectangle (tr.withWidth (tr.getWidth() * jlimit (0.0f, 1.0f, v)), 2.5f);
    };

    const float scDb = Decibels::gainToDecibels (proc.scLevel.load(), -60.0f);
    meter ("SC", useSc ? (scDb + 60.0f) / 60.0f : 0.0f, C::cyan);
    meter ("GR", proc.duckReduction.load() / 0.9f, C::amber);

    r.removeFromTop (2);
    g.setFont (uiFont (10.5f));
    if (useSc && ! connected)
    {
        g.setColour (C::amber);
        g.drawFittedText ("Route a track to the sidechain input", r.removeFromTop (28), Justification::centredTop, 2);
    }
    else if (useSc)
    {
        g.setColour (C::cyan);
        g.drawText ("Sidechain active", r.removeFromTop (14), Justification::centred);
    }
}

//==============================================================================
LumenDelayEditor::LumenDelayEditor (LumenDelayProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p), scPanel (p)
{
    setLookAndFeel (&lnf);

    timeKnob = std::make_unique<Knob> (proc.apvts, IDs::time, "Time");
    divKnob  = std::make_unique<Knob> (proc.apvts, IDs::division, "Division");
    addChildComponent (*timeKnob);
    addChildComponent (*divKnob);

    const std::pair<const char*, const char*> knobDefs[] = {
        { IDs::offset, "Offset" },     { IDs::feedback, "Feedback" }, { IDs::diffusion, "Diffusion" },
        { IDs::width, "Width" },
        { IDs::lowCut, "Low Cut" },    { IDs::highCut, "High Cut" },  { IDs::drive, "Drive" },
        { IDs::wow, "Wow" },           { IDs::modRate, "Mod Rate" },
        { IDs::duck, "Ducking" },      { IDs::adapt, "Adapt" },       { IDs::mix, "Mix" },
        { IDs::output, "Output" } };

    for (auto& [id, title] : knobDefs)
    {
        knobs.push_back (std::make_unique<Knob> (proc.apvts, id, title));
        addAndMakeVisible (*knobs.back());
    }

    addAndMakeVisible (display);
    addAndMakeVisible (scPanel);

    learnButton.setTooltip ("Listens to ~6 seconds of audio and sets the delay for the material and tempo");
    learnButton.onClick = [this] { learnClicked(); };
    addAndMakeVisible (learnButton);

    undoButton.onClick = [this] { undoLearn(); };
    undoButton.setEnabled (false);
    addAndMakeVisible (undoButton);

    freezeButton.setClickingTogglesState (true);
    addAndMakeVisible (freezeButton);
    freezeAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::freeze, freezeButton);

    syncButton.setClickingTogglesState (true);
    addAndMakeVisible (syncButton);
    syncAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, IDs::sync, syncButton);

    modeBox.addItemList (modeChoices(), 1);
    addAndMakeVisible (modeBox);
    modeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, IDs::mode, modeBox);

    lastSync = ! (proc.apvts.getRawParameterValue (IDs::sync)->load() > 0.5f);
    updateTimeKnob();

    setSize (900, 650);
    startTimerHz (30);
}

LumenDelayEditor::~LumenDelayEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void LumenDelayEditor::updateTimeKnob()
{
    const bool synced = proc.apvts.getRawParameterValue (IDs::sync)->load() > 0.5f;
    if (synced == lastSync)
        return;
    lastSync = synced;
    divKnob->setVisible (synced);
    timeKnob->setVisible (! synced);
}

void LumenDelayEditor::paint (Graphics& g)
{
    g.setGradientFill (ColourGradient (C::bg2, 0, 0, C::bg, 0, (float) getHeight(), false));
    g.fillAll();

    g.setFont (uiFont (26.0f, true));
    g.setGradientFill (ColourGradient (C::cyan, 20, 0, C::violet, 160, 0, false));
    g.drawText ("LUMEN", 20, 14, 200, 34, Justification::centredLeft);
    g.setFont (uiFont (12.0f));
    g.setColour (C::dim);
    g.drawText ("SMART DELAY", 128, 24, 200, 18, Justification::centredLeft);

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
    const char* sections[] = { "TIME & FEEDBACK", "TONE & TAPE", "DYNAMICS & OUTPUT" };
    for (int row = 0; row < 3; ++row)
    {
        const int y = 276 + row * 120;
        g.setFont (uiFont (11.0f, true));
        g.setColour (C::dim.withAlpha (0.8f));
        g.drawText (sections[row], 20, y, 200, 14, Justification::centredLeft);
        g.setColour (C::panelEdge);
        g.drawHorizontalLine (y + 7, 150.0f, (float) getWidth() - 20.0f);
    }
}

void LumenDelayEditor::resized()
{
    syncButton.setBounds (430, 18, 80, 28);
    modeBox.setBounds (520, 18, 150, 28);
    freezeButton.setBounds (680, 18, 100, 28);

    display.setBounds (16, 66, 548, 196);
    smartArea = { 576, 66, 308, 196 };
    learnButton.setBounds (smartArea.getX() + 14, smartArea.getBottom() - 42, 190, 30);
    undoButton.setBounds (smartArea.getX() + 212, smartArea.getBottom() - 42, 82, 30);

    const int colW = (getWidth() - 32) / 5;
    auto cell = [&] (int row, int col) { return Rectangle<int> (16 + col * colW, 294 + row * 120, colW, 100); };

    timeKnob->setBounds (cell (0, 0));
    divKnob->setBounds (cell (0, 0));

    // Row 0: offset, feedback, diffusion, width  (cols 1-4)
    for (int i = 0; i < 4; ++i)  knobs[(size_t) i]->setBounds (cell (0, i + 1));
    // Row 1: low cut, high cut, drive, wow, mod rate
    for (int i = 4; i < 9; ++i)  knobs[(size_t) i]->setBounds (cell (1, i - 4));
    // Row 2: ducking, [sidechain panel], adapt, mix, output
    knobs[9]->setBounds (cell (2, 0));
    scPanel.setBounds (cell (2, 1));
    for (int i = 10; i < 13; ++i) knobs[(size_t) i]->setBounds (cell (2, i - 8));
}

void LumenDelayEditor::timerCallback()
{
    updateTimeKnob();

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
        const float baseFb = proc.apvts.getRawParameterValue (IDs::feedback)->load();
        String s = "Adapting live: feedback " + String (roundToInt (proc.effFeedback.load() * 100.0f)) + "%";
        if (baseFb > 0.5f)
            s << " (" << String (roundToInt ((proc.effFeedback.load() * 100.0f / baseFb - 1.0f) * 100.0f)) << "%)";
        s << ", high cut " << String (proc.effHighCut.load() / 1000.0f, 1) << " kHz";
        statusText = s;
    }

    display.repaint();
    scPanel.repaint();
    repaint (smartArea);
}

void LumenDelayEditor::learnClicked()
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

void LumenDelayEditor::setParam (const char* id, float value)
{
    if (auto* param = proc.apvts.getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    }
}

void LumenDelayEditor::applySuggestion (const lumen::Features& f)
{
    using namespace lumen::SmartAssist;
    const auto material = classify (f);
    const auto s = suggestDelay (material, f);

    const char* ids[] = { IDs::sync, IDs::division, IDs::mode, IDs::offset, IDs::feedback, IDs::diffusion,
                          IDs::width, IDs::lowCut, IDs::highCut, IDs::drive, IDs::wow, IDs::modRate,
                          IDs::duck, IDs::mix };
    undoValues.clear();
    for (auto* id : ids)
        if (auto* param = proc.apvts.getParameter (id))
            undoValues.emplace_back (id, param->convertFrom0to1 (param->getValue()));

    setParam (IDs::sync, 1.0f);
    setParam (IDs::division, (float) s.division);
    setParam (IDs::mode, (float) s.mode);
    setParam (IDs::offset, s.offset);       setParam (IDs::feedback, s.feedback);
    setParam (IDs::diffusion, s.diffusion); setParam (IDs::width, s.width);
    setParam (IDs::lowCut, s.lowCut);       setParam (IDs::highCut, s.highCut);
    setParam (IDs::drive, s.drive);         setParam (IDs::wow, s.wow);
    setParam (IDs::modRate, s.modRate);     setParam (IDs::duck, s.duck);

    // Keep 100% wet when used on a send/return
    if (proc.apvts.getRawParameterValue (IDs::mix)->load() < 95.0f)
        setParam (IDs::mix, s.mix);

    const double bpm = proc.hostBpm.load();
    statusText = "Learned: " + String (name (material)) + " -> " + divisionChoices()[s.division]
               + (bpm > 30.0 ? " @ " + String (roundToInt (bpm)) + " BPM" : String())
               + ". Settings applied.";
    undoButton.setEnabled (true);
}

void LumenDelayEditor::undoLearn()
{
    for (auto& [id, value] : undoValues)
        setParam (id.toRawUTF8(), value);
    undoValues.clear();
    undoButton.setEnabled (false);
    statusText = "Previous settings restored";
}
