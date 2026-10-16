#include "PluginEditor.h"

using namespace lumen::led;
using namespace lumen::glitch;

namespace
{
constexpr int W = 1120, HH = 680;
const juce::Colour MAIN = C::orange;
const juce::Colour LC[kLanes] = { C::magenta, C::violet, C::red, C::blue, C::amber, C::green, C::cyan, C::orange, juce::Colour (0xff5ce0b0), juce::Colour (0xffb98cff) };

template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}
}

//==============================================================================
StepGrid::StepGrid (LumenGlitchProcessor& p) : proc (p)
{
    setTooltip ("Click / drag to paint steps. Drag the bar under a lane name to set its CHANCE. Double-click a lane name to clear the lane. "
                "Time lanes (Repeat..Shuffle): the highest one wins. FX lanes (Gate..Echo) stack.");
}

void StepGrid::paintAt (juce::Point<float> pt)
{
    if (dragLane < 0) return;
    const int st = juce::jlimit (0, steps() - 1, (int) ((pt.x - kLabelW) / cellW()));
    if (st == lastCell) return;
    lastCell = st;
    proc.setStep (dragLane, st, paintOn);
    if (steps() == 16) proc.setStep (dragLane, st + 16, paintOn);
    repaint();
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const int lane = juce::jlimit (0, kLanes - 1, (int) (e.position.y / rowH()));
    if (e.position.x < kLabelW)
    {
        chanceLane = lane;
        mouseDrag (e);
        return;
    }
    const int st = juce::jlimit (0, steps() - 1, (int) ((e.position.x - kLabelW) / cellW()));
    dragLane = lane; lastCell = -1;
    paintOn = ((proc.getPattern (proc.slot(), lane) >> st) & 1u) == 0;
    paintAt (e.position);
}
void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (chanceLane >= 0)
    {
        const float v = juce::jlimit (0.0f, 1.0f, (e.position.x - 10.0f) / (float) (kLabelW - 24));
        if (e.position.y > chanceLane * rowH() + rowH() * 0.5f || e.mouseWasDraggedSinceMouseDown())
            proc.setParam (IDs::prob (chanceLane), std::round (v * 20.0f) / 20.0f);
        repaint();
        return;
    }
    paintAt (e.position);
}
void StepGrid::mouseUp (const juce::MouseEvent&) { dragLane = -1; chanceLane = -1; lastCell = -1; }
void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.position.x >= kLabelW) return;
    const int lane = juce::jlimit (0, kLanes - 1, (int) (e.position.y / rowH()));
    proc.setLanePattern (lane, 0); repaint();
}

void StepGrid::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff04060a)); g.fillRoundedRectangle (r, 4.0f);
    const int ns = steps(), sl = proc.slot();
    const float rh = rowH(), cw = cellW();
    const auto& e = proc.getEngine();
    const int cur = e.uiStep.load() % ns;
    const uint32_t active = e.uiActive.load();
    const bool play = proc.playing.load();
    // playhead
    if (play)
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRect (kLabelW + cur * cw, 0.0f, cw, r.getHeight());
    }
    for (int l = 0; l < kLanes; ++l)
    {
        const float y = l * rh;
        const auto col = LC[l];
        if (l == Gate) { g.setColour (C::border); g.fillRect (0.0f, y - 1.0f, r.getWidth(), 2.0f); }
        // lane header
        const bool on = (active >> l) & 1u;
        drawLed (g, { 12.0f, y + rh * 0.38f }, 3.4f, col, on ? 1.0f : 0.08f);
        g.setFont (font (10.5f)); g.setColour (col.interpolatedWith (juce::Colours::white, on ? 0.5f : 0.15f));
        g.drawText (laneName (l), juce::Rectangle<float> (22.0f, y + 2.0f, kLabelW - 26.0f, rh * 0.5f), juce::Justification::centredLeft);
        const float pr = proc.apvts.getRawParameterValue (IDs::prob (l))->load();
        auto bar = juce::Rectangle<float> (10.0f, y + rh * 0.62f, (float) (kLabelW - 24), 5.0f);
        g.setColour (col.withAlpha (0.12f)); g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (col.withAlpha (0.8f)); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * pr), 2.0f);
        g.setFont (mono (8.5f)); g.setColour (C::dim);
        g.drawText (pr > 0.995f ? juce::String ("") : juce::String (juce::roundToInt (pr * 100)) + "%", juce::Rectangle<float> (kLabelW - 50.0f, y + 2.0f, 40.0f, rh * 0.5f), juce::Justification::centredRight);
        // steps
        const uint32_t bits = proc.getPattern (sl, l);
        for (int s = 0; s < ns; ++s)
        {
            auto c = juce::Rectangle<float> (kLabelW + s * cw, y, cw, rh).reduced (1.5f, 3.0f);
            const bool st = (bits >> s) & 1u;
            const bool beat = (s / 4) % 2 == 0;
            g.setColour (beat ? juce::Colour (0xff0b0f16) : juce::Colour (0xff080b10)); g.fillRoundedRectangle (c, 2.5f);
            if (st)
            {
                const bool hot = play && s == cur && on;
                g.setColour (col.withAlpha (hot ? 0.45f : 0.22f)); g.fillRoundedRectangle (c.expanded (1.0f), 3.0f);
                juce::ColourGradient cg (col.interpolatedWith (juce::Colours::white, hot ? 0.45f : 0.12f), c.getX(), c.getY(), col.darker (0.5f), c.getX(), c.getBottom(), false);
                g.setGradientFill (cg); g.fillRoundedRectangle (c.reduced (1.0f), 2.0f);
            }
            else { g.setColour (col.withAlpha (0.05f)); g.drawRoundedRectangle (c, 2.5f, 1.0f); }
        }
    }
    g.setColour (C::border);
    for (int s = 0; s <= ns; s += 4) g.fillRect (kLabelW + s * cw - 0.5f, 0.0f, s % 16 == 0 ? 1.5f : 1.0f, r.getHeight());
}

//==============================================================================
LumenGlitchEditor::LumenGlitchEditor (LumenGlitchProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    grid = std::make_unique<StepGrid> (proc); addAndMakeVisible (*grid); grid->setBounds (26, 134, 1068, 324);

    for (int i = 0; i < 8; ++i)
    {
        slotB[i].setButtonText (slotNames()[i]);
        slotB[i].setTooltip ("Pattern slot " + slotNames()[i] + " (MIDI: C2..G2 switch slots live)");
        slotB[i].onClick = [this, i] { proc.setParam (IDs::slot, (float) i); };
        addAndMakeVisible (slotB[i]); slotB[i].setBounds (130 + i * 32, 98, 30, 26);
    }
    copyBox.addItem ("COPY TO...", 1);
    for (int i = 0; i < 8; ++i) copyBox.addItem ("Copy to " + slotNames()[i], i + 2);
    copyBox.setSelectedId (1, juce::dontSendNotification);
    copyBox.onChange = [this] { const int id = copyBox.getSelectedId(); if (id >= 2) proc.copySlotTo (id - 2); copyBox.setSelectedId (1, juce::dontSendNotification); };
    copyBox.setColour (juce::ComboBox::textColourId, MAIN);
    addAndMakeVisible (copyBox); copyBox.setBounds (392, 98, 112, 26);
    clearB.onClick = [this] { proc.clearSlot(); };
    clearB.setColour (juce::TextButton::buttonColourId, C::dim);
    addAndMakeVisible (clearB); clearB.setBounds (510, 98, 64, 26);
    styleBox.addItemList ({ "Build-up", "Drop Fill", "Stutter Roll", "Dubstep", "Trap", "Half-Time", "Random" }, 1);
    styleBox.setSelectedId (2, juce::dontSendNotification);
    styleBox.setColour (juce::ComboBox::textColourId, C::green);
    addAndMakeVisible (styleBox); styleBox.setBounds (600, 98, 120, 26);
    genB.setColour (juce::TextButton::buttonColourId, C::green);
    genB.setTooltip ("Writes a new pattern in the chosen style into the current slot (press again for another variation)");
    genB.onClick = [this] { proc.generate ((LumenGlitchProcessor::Style) (styleBox.getSelectedId() - 1)); };
    addAndMakeVisible (genB); genB.setBounds (724, 97, 92, 28);
    auto* md = add<Toggle> (*this, owned, st, IDs::midi, "MIDI CONTROL", C::cyan); md->setBounds (830, 99, 130, 24);
    md->setTooltip ("MIDI notes C1..A1 = hold a lane live (like Stutter Edit), C2..G2 = switch pattern slot A..H. Route a MIDI track to Lumen Glitch in Ableton.");
    auto* stc = add<Combo> (*this, owned, st, IDs::steps, MAIN); stc->setBounds (966, 99, 56, 24);
    stc->setTooltip ("16 or 32 steps");
    auto* rc = add<Combo> (*this, owned, st, IDs::rate, MAIN); rc->setBounds (1026, 99, 66, 24);
    rc->setTooltip ("Step length");

    // lane parameter groups
    struct Kn { const char* id; const char* lb; int lane; int x; const char* tip; };
    const Kn ks[] {
        { "repeat_roll", "ROLL", Repeat, 38, "Each repeat gets shorter -> accelerating snare-roll / build-up stutter" },
        { "stop_time", "TIME", TapeStop, 112, "How long the tape takes to stop (part of the step)" },
        { "stop_curve", "CURVE", TapeStop, 172, "Shape of the slow-down" },
        { "scratch_depth", "DEPTH", Scratch, 236, "Scratch movement - above ~70% the record goes backwards" },
        { "shuffle_range", "RANGE", Shuffle, 298, "How many steps back Shuffle may grab audio from" },
        { "gate_length", "LENGTH", Gate, 360, "Open part of each gated step" },
        { "gate_smooth", "SMOOTH", Gate, 420, "Gate edges: 0.3 ms = hard chop, 30 ms = soft pump" },
        { "crush_bits", "BITS", Crush, 482, "Bit depth" },
        { "crush_downsample", "DOWN", Crush, 542, "Sample-rate reduction" },
        { "filter_sweep", "SWEEP", Filter, 604, "How far the filter sweeps during a step" },
        { "filter_res", "RES", Filter, 664, "Filter resonance" },
        { "echo_feedback", "FDBK", Echo, 728, "Echo feedback (ping-pong) - the tail rings out after the step" },
        { "echo_level", "LEVEL", Echo, 788, "Echo level" },
    };
    for (auto& k : ks) { auto* kn = add<Knob> (*this, owned, st, k.id, k.lb, LC[k.lane]); kn->setBounds (k.x, 542, 60, 80); kn->slider.setTooltip (k.tip); }
    auto* rr = add<Combo> (*this, owned, st, IDs::repRate, LC[Repeat]); rr->setBounds (26, 512, 84, 22); rr->setTooltip ("Repeat slice size");
    auto* ft = add<Combo> (*this, owned, st, IDs::fltType, LC[Filter]); ft->setBounds (604, 512, 120, 22);
    auto* et = add<Combo> (*this, owned, st, IDs::echoTime, LC[Echo]); et->setBounds (728, 512, 120, 22);

    auto* pm = add<Combo> (*this, owned, st, IDs::play, MAIN); pm->setBounds (880, 504, 212, 24);
    pm->setTooltip ("Always, or only as a FILL: every 2nd bar, the last bar of every 4 / 8 bars (perfect before drops), or MIDI only");
    const char* gid[] = { "swing", "crossfade", "mix", "out" };
    const char* glb[] = { "SWING", "XFADE", "MIX", "OUTPUT" };
    const char* gtp[] = { "Swing of the step grid", "Crossfade at every switch and repeat slice (anti-click)", "Dry / wet", "Output level" };
    for (int i = 0; i < 4; ++i) { auto* kn = add<Knob> (*this, owned, st, gid[i], glb[i], MAIN, i == 3); kn->setBounds (876 + i * 55, 542, 56, 80); kn->slider.setTooltip (gtp[i]); }

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setColour (juce::ComboBox::textColourId, MAIN);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    presetBox.setTooltip ("Presets load into the selected pattern slot - build your own bank of 8");
    addAndMakeVisible (presetBox); presetBox.setBounds (412, 18, 270, 28);
    for (auto* b : { &prevB, &nextB }) { b->setColour (juce::TextButton::buttonColourId, MAIN); addAndMakeVisible (*b); }
    prevB.setBounds (378, 18, 28, 28); nextB.setBounds (688, 18, 28, 28);
    prevB.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    for (auto* b : { &aB, &bB, &copyB }) addAndMakeVisible (*b);
    aB.setBounds (730, 18, 30, 28); bB.setBounds (762, 18, 30, 28); copyB.setBounds (796, 18, 40, 28);
    aB.onClick = [this] { proc.switchAB (0); };
    bB.onClick = [this] { proc.switchAB (1); };
    copyB.onClick = [this] { proc.copyAB(); };
    copyB.setColour (juce::TextButton::buttonColourId, C::dim);

    setSize (W, HH);
    startTimerHz (30);
}

LumenGlitchEditor::~LumenGlitchEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenGlitchEditor::timerCallback()
{
    proc.syncMidiSlot();
    if (proc.programVersion.load() != lastProgram) { lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    const int s = proc.slot();
    for (int i = 0; i < 8; ++i)
    {
        bool used = false; for (int l = 0; l < kLanes; ++l) used |= proc.getPattern (i, l) != 0;
        slotB[i].setColour (juce::TextButton::buttonColourId, i == s ? MAIN : used ? MAIN.withAlpha (0.35f) : C::dim);
    }
    inPk = juce::jmax (proc.inPeak.load(), inPk * 0.85f); outPk = juce::jmax (proc.outPeak.load(), outPk * 0.85f);
    aB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 0 ? MAIN : C::dim);
    bB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 1 ? MAIN : C::dim);
    repaint();
}

void LumenGlitchEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (C::orange.withAlpha (0.0f), 0, 64, C::magenta.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.25, C::orange.withAlpha (0.75f)); line.addColour (0.75, C::magenta.withAlpha (0.75f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (MAIN.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (C::orange, 20, 0, C::magenta, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 146.0f, 18.0f, 220.0f, 28.0f }, "GLITCH", MAIN, 1.85f, juce::Justification::centredLeft, false, 10.5f);
    }
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("IN", 852, 14, 24, 10, juce::Justification::centredLeft);
    g.drawText ("OUT", 852, 36, 24, 10, juce::Justification::centredLeft);
    ledMeter (g, { 878.0f, 13.0f, 226.0f, 9.0f }, inPk, inPk, false, 30);
    ledMeter (g, { 878.0f, 35.0f, 226.0f, 9.0f }, outPk, outPk, false, 30);

    panel (g, { 16.0f, 72.0f, 1088.0f, 394.0f }, "PATTERN", MAIN);
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("SLOT", 92, 104, 36, 14, juce::Justification::centredLeft);
    {
        const auto& e = proc.getEngine();
        const juce::String m = e.timeMode() >= 0 ? juce::String (laneName (e.timeMode())) : juce::String (proc.playing.load() ? "LIVE" : "STOPPED (FREE CLOCK)");
        g.setColour (e.timeMode() >= 0 ? C::orange : C::dim);
        g.drawText (m, 760, 76, 334, 14, juce::Justification::centredRight);
    }

    panel (g, { 16.0f, 474.0f, 846.0f, 194.0f }, "LANE SETTINGS", MAIN);
    g.setFont (font (9.0f));
    struct Gr { const char* t; int lane; int x, w; };
    const Gr gr[] { { "REPEAT", Repeat, 26, 80 }, { "TAPE STOP", TapeStop, 112, 120 }, { "SCRATCH", Scratch, 236, 60 }, { "SHUFFLE", Shuffle, 298, 60 },
                    { "GATE", Gate, 360, 120 }, { "CRUSH", Crush, 482, 120 }, { "FILTER", Filter, 604, 120 }, { "ECHO", Echo, 728, 120 } };
    for (auto& x : gr)
    {
        g.setColour (LC[x.lane]); g.drawText (x.t, x.x, 496, x.w, 12, juce::Justification::centredLeft);
        g.setColour (LC[x.lane].withAlpha (0.25f)); g.fillRect ((float) x.x, 538.0f, (float) x.w - 4.0f, 1.0f);
    }
    g.setColour (C::dim); g.setFont (font (9.0f));
    g.drawFittedText ("MIDI: C1 Repeat, C#1 Reverse, D1 Tape Stop, D#1 Half Speed, E1 Scratch, F1 Shuffle, F#1 Gate, G1 Crush, G#1 Filter, A1 Echo  /  C2..G2 = slots A..H",
                      juce::Rectangle<int> (26, 626, 826, 34), juce::Justification::centredLeft, 2);

    panel (g, { 868.0f, 474.0f, 236.0f, 194.0f }, "GLOBAL", MAIN);
}
