#include "PluginEditor.h"

using namespace lumen::led;

namespace
{
constexpr int W = 1080, HH = 704;
const juce::Colour MAIN = C::green;
const juce::Colour PAT = C::magenta;
const juce::Colour SC[5] = { C::green, C::cyan, C::blue, C::amber, C::red };

template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}
}

//==============================================================================
GateView::GateView (LumenGateProcessor& p) : proc (p)
{
    thrAtt = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter (IDs::thr), [this] (float) { repaint(); }, nullptr);
    setTooltip ("Drag up/down to set the threshold. Dashed line = close threshold (hysteresis)");
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}
void GateView::mouseDown (const juce::MouseEvent& e) { thrAtt->beginGesture(); mouseDrag (e); }
void GateView::mouseDrag (const juce::MouseEvent& e) { thrAtt->setValueAsPartOfGesture (juce::jlimit (-80.0f, 0.0f, std::round (dbOf ((float) e.y) * 2.0f) / 2.0f)); }
void GateView::mouseUp (const juce::MouseEvent&) { thrAtt->endGesture(); }

void GateView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, MAIN);
    const int N = LumenGateProcessor::kHist;
    const int start = proc.histPos.load (std::memory_order_acquire);
    const float bw = (r.getWidth() - 40.0f) / (float) N;
    const int style = (int) proc.apvts.getRawParameterValue (IDs::style)->load();
    const auto col = SC[style];
    // grid
    g.setFont (font (8.5f));
    for (int d = 0; d >= -80; d -= 20)
    {
        g.setColour (C::dim.withAlpha (0.22f)); g.fillRect (r.getX() + 34.0f, yOf ((float) d), r.getWidth() - 36.0f, 1.0f);
        g.setColour (C::dim); g.drawText (juce::String (d), (int) r.getX() + 2, (int) yOf ((float) d) - 5, 28, 10, juce::Justification::centredRight);
    }
    // input level (ghost), output level (colour), key level line
    for (int k = 0; k < N; ++k)
    {
        const auto& f = proc.hist[(start + k) % N];
        const float x = r.getX() + 36.0f + k * bw;
        const float yi = yOf (juce::jmax (-80.0f, juce::Decibels::gainToDecibels (f.in, -80.0f)));
        const float yo = yOf (juce::jmax (-80.0f, juce::Decibels::gainToDecibels (f.out, -80.0f)));
        g.setColour (C::text.withAlpha (0.12f)); g.fillRect (x, yi, juce::jmax (1.0f, bw), r.getBottom() - 6.0f - yi);
        g.setColour (col.withAlpha (0.55f)); g.fillRect (x, yo, juce::jmax (1.0f, bw * 0.85f), r.getBottom() - 6.0f - yo);
    }
    juce::Path key, gr;
    for (int k = 0; k < N; ++k)
    {
        const auto& f = proc.hist[(start + k) % N];
        const float x = r.getX() + 36.0f + k * bw;
        const float yk = yOf (juce::jlimit (-80.0f, 0.0f, f.key));
        if (k == 0) key.startNewSubPath (x, yk); else key.lineTo (x, yk);
        const float yg = r.getY() + 8.0f + juce::jlimit (0.0f, 1.0f, -f.gr / 60.0f) * 40.0f;
        if (k == 0) gr.startNewSubPath (x, yg); else gr.lineTo (x, yg);
    }
    g.setColour (C::text.withAlpha (0.55f)); g.strokePath (key, juce::PathStrokeType (1.0f));
    glowPath (g, gr, C::red, 1.4f, 0.9f);
    // thresholds
    const float thr = proc.apvts.getRawParameterValue (IDs::thr)->load(), hy = proc.apvts.getRawParameterValue (IDs::hyst)->load();
    juce::Path tl; tl.startNewSubPath (r.getX() + 34.0f, yOf (thr)); tl.lineTo (r.getRight() - 2.0f, yOf (thr));
    glowPath (g, tl, juce::Colours::white, 1.4f, 0.9f);
    if ((style == 0 || style == 1 || style == 2) && hy > 0.05f)
    {
        juce::Path hl; hl.startNewSubPath (r.getX() + 34.0f, yOf (thr - hy)); hl.lineTo (r.getRight() - 2.0f, yOf (thr - hy));
        juce::Path dashed; const float dash[] { 5.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, hl, dash, 2);
        g.setColour (juce::Colours::white.withAlpha (0.45f)); g.fillPath (dashed);
    }
    g.setFont (mono (10.0f)); g.setColour (juce::Colours::white);
    g.drawText ("THR " + juce::String (thr, 1) + " dB", (int) r.getRight() - 112, (int) yOf (thr) - 14, 108, 12, juce::Justification::centredRight);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("grey = input   colour = output   line = key level   red = gain reduction", (int) r.getX() + 40, (int) r.getBottom() - 16, 400, 10, juce::Justification::centredLeft);
}

//==============================================================================
int StepGrid::stepAt (float x) const
{
    const int steps = stepsFor ((int) proc.apvts.getRawParameterValue (IDs::steps)->load());
    return juce::jlimit (0, steps - 1, (int) (x / ((float) getWidth() / (float) steps)));
}
void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    dragStep = stepAt ((float) e.x); dragged = false;
    downLevel = proc.apvts.getRawParameterValue (IDs::step (dragStep))->load();
    proc.setStep (dragStep, downLevel > 0.01f ? 0.0f : 1.0f);
}
void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    const int s = stepAt ((float) e.x);
    const float lv = juce::jlimit (0.0f, 1.0f, 1.0f - (float) e.y / (float) getHeight());
    proc.setStep (s, lv < 0.06f ? 0.0f : lv);
    dragged = true;
}
void StepGrid::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, PAT);
    const int steps = stepsFor ((int) proc.apvts.getRawParameterValue (IDs::steps)->load());
    const int cur = proc.curStep.load();
    const bool patOn = (int) proc.apvts.getRawParameterValue (IDs::mode)->load() != 0;
    const float cw = r.getWidth() / (float) steps;
    for (int i = 0; i < steps; ++i)
    {
        const float lv = proc.apvts.getRawParameterValue (IDs::step (i))->load();
        auto cell = juce::Rectangle<float> (r.getX() + i * cw, r.getY(), cw, r.getHeight()).reduced (juce::jmin (3.0f, cw * 0.12f), 6.0f);
        g.setColour (i % 4 == 0 ? C::border.brighter (0.3f) : C::border); g.drawRoundedRectangle (cell, 2.0f, 1.0f);
        const bool isCur = patOn && i == cur;
        if (lv > 0.0f)
        {
            auto bar = cell.withTrimmedTop (cell.getHeight() * (1.0f - lv));
            g.setColour (PAT.withAlpha (isCur ? 0.35f : 0.18f)); g.fillRoundedRectangle (bar.expanded (1.5f), 3.0f);
            g.setColour (isCur ? PAT.interpolatedWith (juce::Colours::white, 0.45f) : PAT.withAlpha (0.85f)); g.fillRoundedRectangle (bar, 2.0f);
        }
        if (isCur) { g.setColour (juce::Colours::white.withAlpha (0.18f)); g.fillRoundedRectangle (cell, 2.0f); }
    }
}

//==============================================================================
LumenGateEditor::LumenGateEditor (LumenGateProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    const auto mn = modeNames(), sn = styleNames();
    for (int m = 0; m < 3; ++m)
    {
        modeB[m].setButtonText (m == 2 ? juce::String ("BOTH") : mn[m].toUpperCase());
        modeB[m].onClick = [this, m] { proc.setParam (IDs::mode, (float) m); };
        addAndMakeVisible (modeB[m]); modeB[m].setBounds (28 + m * 118, 78, 112, 30);
    }
    for (int s = 0; s < 5; ++s)
    {
        styleB[s].setButtonText (sn[s].toUpperCase());
        styleB[s].onClick = [this, s] { proc.setParam (IDs::style, (float) s); };
        addAndMakeVisible (styleB[s]); styleB[s].setBounds (420 + s * 128, 78, 122, 30);
    }
    styleB[2].setTooltip ("Program-dependent release: short notes close fast, long notes fade naturally");
    styleB[3].setTooltip ("Upward expander: makes hits jump out above the threshold");
    styleB[4].setTooltip ("Ducker: turns the signal DOWN when the key (e.g. external sidechain) is above the threshold");

    view = std::make_unique<GateView> (proc); addAndMakeVisible (*view); view->setBounds (26, 148, 680, 196);
    add<Knob> (*this, owned, st, IDs::thr, "THRESHOLD", MAIN)->setBounds (734, 140, 130, 140);
    add<Knob> (*this, owned, st, IDs::range, "RANGE", MAIN)->setBounds (872, 140, 90, 100);
    add<Knob> (*this, owned, st, IDs::ratio, "RATIO", MAIN)->setBounds (964, 140, 90, 100);
    learnB.setColour (juce::TextButton::buttonColourId, C::cyan);
    learnB.setTooltip ("Play the track for 4 s: finds the noise floor and the signal, sets threshold and hysteresis");
    learnB.onClick = [this] { proc.startLearn(); };
    addAndMakeVisible (learnB); learnB.setBounds (734, 290, 150, 28);

    const char* did[] { "attack", "hold", "release", "knee", "hysteresis", "lookahead" };
    const char* dlb[] { "ATTACK", "HOLD", "RELEASE", "KNEE", "HYSTERESIS", "LOOKAHEAD" };
    for (int i = 0; i < 6; ++i) add<Knob> (*this, owned, st, did[i], dlb[i], MAIN)->setBounds (26 + i * 86, 384, 86, 100);
    auto* src = add<Combo> (*this, owned, st, IDs::sc, C::violet); src->setBounds (566, 388, 150, 24);
    add<Knob> (*this, owned, st, IDs::scHp, "KEY HP", C::violet)->setBounds (722, 384, 80, 100);
    add<Knob> (*this, owned, st, IDs::scLp, "KEY LP", C::violet)->setBounds (802, 384, 80, 100);
    add<Knob> (*this, owned, st, IDs::link, "LINK", C::violet)->setBounds (882, 384, 80, 100);
    auto* li = add<Toggle> (*this, owned, st, IDs::listen, "KEY LISTEN", C::violet); li->setBounds (566, 420, 150, 24);
    li->setTooltip ("Hear the filtered key signal to tune the key filters");
    auto* mi = add<Toggle> (*this, owned, st, IDs::midi, "MIDI TRIGGER", C::amber); mi->setBounds (566, 452, 150, 24);
    mi->setTooltip ("Open the gate with MIDI notes (route a MIDI track into Lumen Gate)");
    add<Knob> (*this, owned, st, IDs::mix, "MIX", C::text)->setBounds (966, 384, 92, 100);

    grid = std::make_unique<StepGrid> (proc); addAndMakeVisible (*grid); grid->setBounds (26, 528, 640, 110);
    const char* tl[] { "RANDOM", "INVERT", "<", ">", "ALL ON", "CLEAR" };
    for (int i = 0; i < 6; ++i)
    {
        toolB[i].setButtonText (tl[i]); toolB[i].setColour (juce::TextButton::buttonColourId, PAT);
        addAndMakeVisible (toolB[i]);
    }
    toolB[0].setBounds (26, 646, 104, 26); toolB[1].setBounds (136, 646, 104, 26);
    toolB[2].setBounds (256, 646, 44, 26); toolB[3].setBounds (304, 646, 44, 26);
    toolB[4].setBounds (452, 646, 104, 26); toolB[5].setBounds (562, 646, 104, 26);
    toolB[0].onClick = [this] { proc.patternRandom(); };
    toolB[1].onClick = [this] { proc.patternInvert(); };
    toolB[2].onClick = [this] { proc.patternShift (-1); };
    toolB[3].onClick = [this] { proc.patternShift (1); };
    toolB[4].onClick = [this] { proc.patternClear (true); };
    toolB[5].onClick = [this] { proc.patternClear (false); };

    add<Combo> (*this, owned, st, IDs::steps, PAT)->setBounds (682, 528, 80, 24);
    add<Combo> (*this, owned, st, IDs::rate, PAT)->setBounds (682, 572, 80, 24);
    const char* pid_[] { "swing", "gate_length", "p_attack", "p_release", "depth", "stereo_offset" };
    const char* plb[] { "SWING", "GATE LEN", "ATTACK", "RELEASE", "DEPTH", "STEREO" };
    for (int i = 0; i < 6; ++i) add<Knob> (*this, owned, st, pid_[i], plb[i], PAT)->setBounds (770 + (i % 3) * 72, 520 + (i / 3) * 84, 72, 84);
    add<Knob> (*this, owned, st, IDs::out, "OUTPUT", C::text, true)->setBounds (986, 560, 80, 96);

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setColour (juce::ComboBox::textColourId, MAIN);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (330, 18, 300, 28);
    for (auto* b : { &prevB, &nextB }) { b->setColour (juce::TextButton::buttonColourId, MAIN); addAndMakeVisible (*b); }
    prevB.setBounds (296, 18, 28, 28); nextB.setBounds (636, 18, 28, 28);
    prevB.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    for (auto* b : { &aB, &bB, &copyB }) addAndMakeVisible (*b);
    aB.setBounds (676, 18, 30, 28); bB.setBounds (708, 18, 30, 28); copyB.setBounds (742, 18, 40, 28);
    aB.onClick = [this] { proc.switchAB (0); };
    bB.onClick = [this] { proc.switchAB (1); };
    copyB.onClick = [this] { proc.copyAB(); };
    copyB.setColour (juce::TextButton::buttonColourId, C::dim);

    setSize (W, HH);
    startTimerHz (30);
}

LumenGateEditor::~LumenGateEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenGateEditor::timerCallback()
{
    proc.finishLearnIfReady();
    if (proc.programVersion.load() != lastProgram) { lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    const int m = (int) pv (IDs::mode), s = (int) pv (IDs::style);
    for (int i = 0; i < 3; ++i) modeB[i].setColour (juce::TextButton::buttonColourId, i == m ? (i == 1 ? PAT : i == 2 ? C::amber : MAIN) : C::dim);
    for (int i = 0; i < 5; ++i) styleB[i].setColour (juce::TextButton::buttonColourId, i == s ? SC[i] : C::dim);
    aB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 0 ? MAIN : C::dim);
    bB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 1 ? MAIN : C::dim);
    learnB.setEnabled (! proc.isLearning());
    repaint();
}

void LumenGateEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (MAIN.withAlpha (0.0f), 0, 64, PAT.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.25, MAIN.withAlpha (0.75f)); line.addColour (0.75, PAT.withAlpha (0.75f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (MAIN.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (MAIN, 20, 0, PAT, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 146.0f, 18.0f, 130.0f, 28.0f }, "GATE", MAIN, 2.2f, juce::Justification::centredLeft, false, 10.5f);
    }
    const int hp = (proc.histPos.load() + LumenGateProcessor::kHist - 1) % LumenGateProcessor::kHist;
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("IN", 800, 18, 20, 10, juce::Justification::centredLeft);
    g.drawText ("OUT", 800, 36, 24, 10, juce::Justification::centredLeft);
    ledMeter (g, { 826.0f, 18.0f, 238.0f, 9.0f }, proc.hist[hp].in, proc.hist[hp].in, false, 30);
    ledMeter (g, { 826.0f, 36.0f, 238.0f, 9.0f }, proc.hist[hp].out, proc.hist[hp].out, false, 30);

    panel (g, { 16.0f, 70.0f, 1048.0f, 46.0f }, "", MAIN);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("STYLE", 382, 88, 36, 12, juce::Justification::centredLeft);
    const int style = (int) pv (IDs::style);
    panel (g, { 16.0f, 122.0f, 700.0f, 230.0f }, "DYNAMICS", SC[style]);
    // gate status LED
    drawLed (g, { 690.0f, 135.0f }, 4.0f, proc.gateOpen.load() ? C::green : C::red, 1.0f);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText (proc.gateOpen.load() ? "OPEN" : "CLOSED", 620, 129, 62, 12, juce::Justification::centredRight);
    panel (g, { 724.0f, 122.0f, 340.0f, 230.0f }, "", SC[style]);
    {
        auto sr = juce::Rectangle<float> (890.0f, 290.0f, 164.0f, 28.0f);
        lcd (g, sr, C::cyan);
        if (proc.isLearning())
        {
            const float pr = proc.learnProgress();
            for (int k = 0; k < 16; ++k) drawLed (g, { sr.getX() + 10.0f + k * 9.4f, sr.getCentreY() }, 2.6f, C::cyan, pr * 16.0f > (float) k ? 1.0f : 0.1f);
        }
        else status.draw (g, sr.reduced (4.0f, 2.0f), proc.getLearnResult().isEmpty() ? juce::String ("READY") : juce::String ("SET"), C::cyan, 2.0f, juce::Justification::centred, false, 10.0f);
        g.setFont (font (9.0f)); g.setColour (proc.getLearnResult().isNotEmpty() ? C::cyan.withAlpha (0.85f) : C::dim);
        g.drawFittedText (proc.getLearnResult().isNotEmpty() && ! proc.isLearning() ? proc.getLearnResult() : juce::String ("AUTO THRESHOLD: play 4 s, the gate sets itself."),
                          juce::Rectangle<int> (736, 322, 320, 26), juce::Justification::topLeft, 2);
    }
    panel (g, { 16.0f, 360.0f, 1048.0f, 130.0f }, "TIMING & KEY", MAIN);
    if (pv (IDs::sc) > 0.5f && ! proc.scConnected.load()) { g.setFont (font (9.0f)); g.setColour (C::violet); g.drawText ("no sidechain input routed", 566, 364, 160, 12, juce::Justification::centredLeft); }
    panel (g, { 16.0f, 498.0f, 1048.0f, 196.0f }, "PATTERN (TRANCE GATE)", PAT);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("STEPS", 682, 516, 80, 10, juce::Justification::centredLeft);
    g.drawText ("RATE", 682, 560, 80, 10, juce::Justification::centredLeft);
    g.drawFittedText ("Pattern runs with the song (free-running when stopped). Mode PATTERN or BOTH.", juce::Rectangle<int> (682, 602, 86, 80), juce::Justification::topLeft, 6);
}
