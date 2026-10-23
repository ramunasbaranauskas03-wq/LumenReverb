#include "PluginEditor.h"
using namespace lumen::led;

static constexpr int W = 1120, HH = 700;
static const juce::Colour algoCol[8] { C::cyan, C::amber, C::green, C::orange, C::violet, C::magenta, juce::Colour (0xffb8d8ff), C::blue };

static double rtAt (const LumenAudioProcessor& p, double f)
{
    const double mid = p.P (IDs::decay), lo = mid * p.P (IDs::bassMult), hi = mid * p.P (IDs::trebleMult);
    const double wl = 1.0 / (1.0 + std::pow (f / p.P (IDs::xLow), 2.0)), wh = 1.0 / (1.0 + std::pow (p.P (IDs::xHigh) / f, 2.0));
    return mid + (lo - mid) * wl + (hi - mid) * wh;
}

// ================================================================================================ DecayCurve
DecayCurve::DecayCurve (LumenAudioProcessor& p) : proc (p)
{
    setTooltip ("Decay time across frequency.  Drag the middle dot = DECAY, the left dot = bass decay + crossover, the right dot = treble decay + crossover.  "
                "Drag the dashed lines = LOW / HIGH CUT of the reverb.  Double-click resets.");
}
float DecayCurve::maxRt() const
{
    const float d = proc.P (IDs::decay), m = d * std::max (1.0f, std::max (proc.P (IDs::bassMult), proc.P (IDs::trebleMult)));
    for (float s : { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 80.0f }) if (m * 1.15f < s) return s;
    return 80.0f;
}
int DecayCurve::hit (juce::Point<float> pt) const
{
    const float d = proc.P (IDs::decay);
    const juce::Point<float> hb { fx (proc.P (IDs::xLow)), ty (d * proc.P (IDs::bassMult)) }, ht { fx (proc.P (IDs::xHigh)), ty (d * proc.P (IDs::trebleMult)) },
                             hm { fx (std::sqrt (proc.P (IDs::xLow) * proc.P (IDs::xHigh))), ty (d) };
    if (pt.getDistanceFrom (hb) < 11) return 0;
    if (pt.getDistanceFrom (ht) < 11) return 1;
    if (pt.getDistanceFrom (hm) < 11) return 2;
    if (std::abs (pt.x - fx (proc.P (IDs::lowCut))) < 6) return 3;
    if (std::abs (pt.x - fx (proc.P (IDs::highCut))) < 6) return 4;
    return -1;
}
static const char* curveIds[5][2] { { "x_low", "bass_mult" }, { "x_high", "treble_mult" }, { "decay", "" }, { "low_cut", "" }, { "high_cut", "" } };
void DecayCurve::mouseMove (const juce::MouseEvent& e)
{
    const int h = hit (e.position); if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h == 2 ? juce::MouseCursor::UpDownResizeCursor : h >= 3 ? juce::MouseCursor::LeftRightResizeCursor : h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}
void DecayCurve::mouseDown (const juce::MouseEvent& e)
{
    drag = hit (e.position);
    if (drag < 0) return;
    for (auto* id : curveIds[drag]) if (*id) proc.apvts.getParameter (id)->beginChangeGesture();
}
void DecayCurve::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    auto set = [&] (const char* id, float v) { if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id))) rp->setValueNotifyingHost (rp->convertTo0to1 (v)); };
    const float d = std::max (0.1f, proc.P (IDs::decay));
    switch (drag)
    {
        case 0: set ("x_low", (float) xf (e.position.x)); set ("bass_mult", yt (e.position.y) / d); break;
        case 1: set ("x_high", (float) xf (e.position.x)); set ("treble_mult", yt (e.position.y) / d); break;
        case 2: set ("decay", yt (e.position.y)); break;
        case 3: set ("low_cut", (float) xf (e.position.x)); break;
        default: set ("high_cut", (float) xf (e.position.x)); break;
    }
    repaint();
}
void DecayCurve::mouseUp (const juce::MouseEvent&)
{
    if (drag >= 0) for (auto* id : curveIds[drag]) if (*id) proc.apvts.getParameter (id)->endChangeGesture();
    drag = -1;
}
void DecayCurve::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hit (e.position); if (h < 0) return;
    for (auto* id : curveIds[h]) if (*id) if (auto* rp = proc.apvts.getParameter (id)) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->getDefaultValue()); rp->endChangeGesture(); }
}
void DecayCurve::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    lcd (g, all, C::cyan);
    auto r = plot();
    const int algo = (int) proc.P (IDs::algo);
    const auto col = algoCol[juce::jlimit (0, 7, algo)];
    g.setFont (mono (9.0f));
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = fx (f); g.setColour (C::border.withAlpha (0.6f)); g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (C::dim); g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) x - 16, (int) r.getBottom() + 1, 32, 12, juce::Justification::centred);
    }
    const float mx = maxRt();
    for (int k = 0; k <= 4; ++k)
    {
        const float s = mx * (float) k / 4.0f, y = ty (s);
        g.setColour (C::border.withAlpha (0.45f)); g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (C::dim); g.drawText (juce::String (s, s < 10 ? 1 : 0) + "s", (int) all.getX() + 4, (int) y - 6, 34, 12, juce::Justification::centredLeft);
    }
    // wet band: shading outside low / high cut
    const float lc = fx (proc.P (IDs::lowCut)), hc = fx (proc.P (IDs::highCut));
    g.setColour (C::bg.withAlpha (0.55f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), r.getY(), lc, r.getBottom()));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (hc, r.getY(), r.getRight(), r.getBottom()));
    // RT curve
    juce::Path c, fill; bool st = false;
    for (float x = r.getX(); x <= r.getRight(); x += 2.0f)
    {
        const float y = ty ((float) rtAt (proc, xf (x)));
        if (! st) { c.startNewSubPath (x, y); fill.startNewSubPath (x, r.getBottom()); st = true; }
        c.lineTo (x, y); fill.lineTo (x, y);
    }
    fill.lineTo (r.getRight(), r.getBottom()); fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (col.withAlpha (0.28f), 0, r.getY(), col.withAlpha (0.03f), 0, r.getBottom(), false)); g.fillPath (fill);
    glowPath (g, c, col, 2.2f, 1.0f);
    // cut lines
    const float dl[] { 4.0f, 3.0f };
    for (int k = 0; k < 2; ++k)
    {
        const float x = k == 0 ? lc : hc; const bool hot = hover == 3 + k || drag == 3 + k;
        g.setColour (C::text.withAlpha (hot ? 0.85f : 0.35f)); g.drawDashedLine ({ x, r.getY(), x, r.getBottom() }, dl, 2, hot ? 1.6f : 1.0f);
        g.setFont (font (9.0f)); g.drawText (k == 0 ? "LOW CUT" : "HIGH CUT", (int) x + (k == 0 ? 4 : -60), (int) r.getY() + 2, 56, 12, k == 0 ? juce::Justification::centredLeft : juce::Justification::centredRight);
    }
    // handles
    const float d = proc.P (IDs::decay);
    const juce::Point<float> hs[3] { { fx (proc.P (IDs::xLow)), ty (d * proc.P (IDs::bassMult)) }, { fx (proc.P (IDs::xHigh)), ty (d * proc.P (IDs::trebleMult)) },
                                     { fx (std::sqrt (proc.P (IDs::xLow) * proc.P (IDs::xHigh))), ty (d) } };
    const char* lab[3] { "BASS", "TREBLE", "DECAY" };
    for (int k = 0; k < 3; ++k)
    {
        const bool hot = hover == k || drag == k; const float rad = hot ? 7.0f : 5.5f;
        g.setColour (col.withAlpha (0.25f)); g.fillEllipse (hs[k].x - rad - 3, hs[k].y - rad - 3, 2 * rad + 6, 2 * rad + 6);
        g.setColour (k == 2 ? C::text : col); g.fillEllipse (hs[k].x - rad, hs[k].y - rad, 2 * rad, 2 * rad);
        if (hot)
        {
            g.setColour (C::text); g.setFont (mono (10.0f));
            const juce::String t = k == 2 ? juce::String (d, 2) + " s" : juce::String (lab[k]) + " " + juce::String (d * proc.P (k == 0 ? IDs::bassMult : IDs::trebleMult), 2) + " s @ " + hzText (proc.P (k == 0 ? IDs::xLow : IDs::xHigh));
            g.drawText (t, (int) hs[k].x - 90, (int) hs[k].y - 24, 180, 14, juce::Justification::centred);
        }
    }
    g.setColour (C::dim); g.setFont (font (9.5f));
    g.drawText ("DECAY TIME vs FREQUENCY", (int) r.getX(), (int) all.getY() + 4, 220, 12, juce::Justification::centredLeft);
}

// ================================================================================================ TailView
void TailView::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    lcd (g, all, C::cyan);
    auto r = all.reduced (10, 8).withTrimmedTop (16).withTrimmedBottom (4);
    const int algo = (int) proc.P (IDs::algo);
    const auto col = algoCol[juce::jlimit (0, 7, algo)];
    g.setColour (C::dim); g.setFont (font (9.5f));
    g.drawText ("REVERB TAIL (last 5 s)", (int) r.getX(), (int) all.getY() + 4, 200, 12, juce::Justification::centredLeft);
    g.setFont (mono (8.5f));
    for (int db : { 0, -20, -40, -60 })
    {
        const float y = r.getY() + r.getHeight() * (float) -db / 66.0f;
        g.setColour (C::border.withAlpha (0.5f)); g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (C::dim); g.drawText (juce::String (db), (int) r.getRight() - 26, (int) y - 11, 26, 10, juce::Justification::centredRight);
    }
    const int n = LumenAudioProcessor::kHist, pos = proc.histPos.load (std::memory_order_acquire);
    const int show = 500;
    juce::Path p, f; bool st = false;
    for (int i = 0; i < show; ++i)
    {
        const float v = proc.hist[(pos - show + i + 2 * n) % n];
        const float x = r.getX() + r.getWidth() * (float) i / (float) (show - 1);
        const float y = r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, -v / 66.0f);
        if (! st) { p.startNewSubPath (x, y); f.startNewSubPath (x, r.getBottom()); st = true; }
        p.lineTo (x, y); f.lineTo (x, y);
    }
    f.lineTo (r.getRight(), r.getBottom()); f.closeSubPath();
    g.setGradientFill (juce::ColourGradient (col.withAlpha (0.35f), 0, r.getY(), col.withAlpha (0.02f), 0, r.getBottom(), false)); g.fillPath (f);
    g.setColour (col); g.strokePath (p, juce::PathStrokeType (1.3f));
    if (proc.P (IDs::freeze) > 0.5f) { g.setColour (C::cyan); g.setFont (font (12.0f)); g.drawText ("FROZEN", r.toNearestInt(), juce::Justification::centredTop); }
}

// ================================================================================================ Editor
LumenAudioEditor::Knob& LumenAudioEditor::knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip)
{
    auto k = std::make_unique<Knob> (proc.apvts, id, lbl, c, bip); k->setBounds (r); addAndMakeVisible (*k);
    auto* raw = k.get(); owned.push_back (std::move (k)); return *raw;
}
LumenAudioEditor::Toggle& LumenAudioEditor::toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r)
{
    auto k = std::make_unique<Toggle> (proc.apvts, id, txt, c); k->setBounds (r); addAndMakeVisible (*k);
    auto* raw = k.get(); owned.push_back (std::move (k)); return *raw;
}
LumenAudioEditor::Combo& LumenAudioEditor::combo (const juce::String& id, juce::Colour c, juce::Rectangle<int> r)
{
    auto k = std::make_unique<Combo> (proc.apvts, id, c); k->setBounds (r); addAndMakeVisible (*k);
    auto* raw = k.get(); owned.push_back (std::move (k)); return *raw;
}

LumenAudioEditor::LumenAudioEditor (LumenAudioProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    // header
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (250, 15, 250, 26);
    for (auto* b : { &prevB, &nextB, &abA, &abB, &abCopy, &learnB, &undoB }) { b->setColour (juce::TextButton::buttonColourId, C::cyan); addAndMakeVisible (*b); }
    prevB.setBounds (506, 15, 26, 26); nextB.setBounds (534, 15, 26, 26);
    prevB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + proc.getNumPrograms() - 1) % proc.getNumPrograms()); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    abA.setBounds (576, 15, 30, 26); abB.setBounds (608, 15, 30, 26); abCopy.setBounds (640, 15, 52, 26);
    abA.onClick = [this] { proc.switchAB (0); }; abB.onClick = [this] { proc.switchAB (1); }; abCopy.onClick = [this] { proc.copyAB(); };
    learnB.setBounds (716, 15, 150, 26); undoB.setBounds (870, 15, 60, 26);
    learnB.setTooltip ("SMART ASSIST: play your track and press LEARN.  The reverb listens for ~6 s and sets algorithm, decay (in time with your tempo), pre-delay, EQ and clean-up for the material.  UNDO restores.");
    learnB.onClick = [this] { learnClicked(); };
    undoB.onClick = [this] { proc.undoLearn(); status = "Restored"; };
    auto& fz = toggle (IDs::freeze, "FREEZE", C::cyan, { 946, 15, 160, 26 });
    fz.setTooltip ("Infinite reverb: holds the current tail forever (new audio no longer enters)");

    // algorithms
    for (int i = 0; i < 8; ++i)
    {
        auto& b = algoB[i]; b.setButtonText (juce::String (lumen::rv2::algos()[(size_t) i].name).toUpperCase());
        b.setColour (juce::TextButton::buttonColourId, algoCol[i]);
        b.setBounds (12 + i * 138, 66, 134, 32);
        b.onClick = [this, i] { proc.setParam (IDs::algo, (float) i); };
        addAndMakeVisible (b);
    }
    // displays
    curve = std::make_unique<DecayCurve> (proc); addAndMakeVisible (*curve); curve->setBounds (12, 108, 470, 236);
    tail = std::make_unique<TailView> (proc); addAndMakeVisible (*tail); tail->setBounds (490, 108, 290, 236);

    // main knobs (right)
    knob (IDs::decay, "DECAY", C::cyan, { 800, 128, 96, 108 });
    knob (IDs::size, "SIZE", C::cyan, { 904, 128, 96, 108 });
    knob (IDs::mix, "MIX", C::cyan, { 1008, 128, 96, 108 });
    knob (IDs::preDelay, "PRE-DELAY", C::text, { 808, 248, 76, 88 });
    combo (IDs::preSync, C::text, { 892, 276, 100, 24 }).setTooltip ("Pre-delay in time with your song (overrides the knob)");
    knob (IDs::out, "OUTPUT", C::text, { 1016, 248, 76, 88 }, true);

    // bottom panels
    const int py = 380, ky = py + 30;
    knob (IDs::diffusion, "DIFFUSION", C::green, { 22, ky, 72, 86 });
    knob (IDs::early, "EARLY", C::green, { 96, ky, 72, 86 }).setTooltip ("Level of the early reflections (the room's first echoes, they give size and position)");
    knob (IDs::modDepth, "MOD DEPTH", C::green, { 22, ky + 104, 72, 86 }).setTooltip ("Slow random modulation inside the tail: smooth and lush, never metallic");
    knob (IDs::modRate, "MOD RATE", C::green, { 96, ky + 104, 72, 86 });

    knob (IDs::bassMult, "BASS", C::amber, { 196, ky, 72, 86 }).setTooltip ("Decay of the low end relative to DECAY (x1 = same)");
    knob (IDs::xLow, "BASS X", C::amber, { 270, ky, 72, 86 });
    knob (IDs::trebleMult, "TREBLE", C::amber, { 196, ky + 104, 72, 86 }).setTooltip ("Decay of the highs relative to DECAY - lower = darker, more natural tail");
    knob (IDs::xHigh, "TREBLE X", C::amber, { 270, ky + 104, 72, 86 });

    knob (IDs::lowCut, "LOW CUT", C::orange, { 370, ky, 72, 86 });
    knob (IDs::highCut, "HIGH CUT", C::orange, { 444, ky, 72, 86 });
    knob (IDs::width, "WIDTH", C::orange, { 370, ky + 104, 72, 86 });
    knob (IDs::bassMono, "BASS MONO", C::orange, { 444, ky + 104, 72, 86 }).setTooltip ("Reverb below this frequency is mono: a wide, clean tail that never smears your sub");

    knob (IDs::clarity, "CLARITY", C::violet, { 544, ky, 76, 90 }).setTooltip ("Smart multiband ducking: in every band the reverb steps back where your dry sound is busy, and blooms in the gaps.  The mix stays clean without killing the space.");
    knob (IDs::duck, "DUCKING", C::violet, { 624, ky, 76, 90 }).setTooltip ("Classic ducking: the whole reverb dips while the dry signal plays");
    knob (IDs::adapt, "ADAPT", C::violet, { 704, ky, 76, 90 }).setTooltip ("SMART ADAPT: follows the music live - busy, transient material gets a shorter, more ducked tail, bright material a darker tail, bassy material a cleaner low end");

    knob (IDs::shimmer, "SHIMMER", juce::Colour (0xffb8d8ff), { 812, ky, 80, 92 }).setTooltip ("Pitch-shifted feedback: an angelic octave (or fifth) that grows inside the tail.  Works in every algorithm.");
    combo (IDs::shimmerPitch, juce::Colour (0xffb8d8ff), { 900, ky + 30, 80, 24 }).setTooltip ("Shimmer interval in semitones");

    setSize (W, HH);
    startTimerHz (30);
}

LumenAudioEditor::~LumenAudioEditor() { setLookAndFeel (nullptr); }

void LumenAudioEditor::learnClicked()
{
    if (proc.analyzer.isLearning()) { proc.analyzer.cancelLearn(); learnB.setButtonText ("LEARN"); status = "Cancelled"; return; }
    proc.analyzer.requestLearn(); status = "Listening... play the part you want reverb on";
}

void LumenAudioEditor::timerCallback()
{
    const int algo = juce::jlimit (0, 7, (int) pv (IDs::algo));
    for (int i = 0; i < 8; ++i) algoB[i].setToggleState (i == algo, juce::dontSendNotification);
    inM = std::max (proc.inLevel.load(), inM * 0.85f); outM = std::max (proc.outLevel.load(), outM * 0.85f); wetM = std::max (proc.wetLevel.load(), wetM * 0.85f);
    duckM += (proc.duckDb.load() - duckM) * 0.3f; for (int b = 0; b < 4; ++b) clarM[b] += (proc.clarityDb[b].load() - clarM[b]) * 0.3f;
    const bool learning = proc.analyzer.isLearning();
    if (learning) { learnB.setButtonText ("LISTENING " + juce::String (juce::roundToInt (proc.analyzer.getLearnProgress() * 100)) + "%"); learnB.setToggleState (true, juce::dontSendNotification); }
    else if (wasLearning) { learnB.setButtonText ("LEARN"); learnB.setToggleState (false, juce::dontSendNotification); }
    wasLearning = learning;
    lumen::Features learned;
    if (proc.analyzer.consumeLearnResult (learned))
    {
        const auto m = lumen::SmartAssist::classify (learned);
        proc.applySuggestion (lumen::SmartAssist::suggest (m, learned, proc.hostBpm.load()));
        status = "Learned: " + juce::String (lumen::SmartAssist::name (m)) + "  ->  " + lumen::rv2::algos()[(size_t) juce::jlimit (0, 7, (int) pv (IDs::algo))].name
               + ", " + juce::String (pv (IDs::decay), 2) + " s" + (proc.hostBpm.load() > 30 ? " (in tempo)" : "");
    }
    undoB.setEnabled (proc.canUndo());
    if (lastProgram != proc.programVersion.load() + proc.getCurrentProgram() * 1000)
    { lastProgram = proc.programVersion.load() + proc.getCurrentProgram() * 1000; presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    abA.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification); abB.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    curve->repaint(); tail->repaint();
    repaint (0, 0, W, 60); repaint (788, 100, 332, 260); repaint (530, 370, 590, 330);
}

void LumenAudioEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    g.setColour (C::panelTop); g.fillRect (0, 0, W, 56);
    g.setColour (C::border); g.drawHorizontalLine (56, 0, (float) W);
    g.setColour (C::text); g.setFont (font (24.0f)); g.drawText ("LUMEN", 18, 12, 100, 32, juce::Justification::centredLeft);
    g.setColour (C::cyan); g.drawText ("REVERB", 108, 12, 120, 32, juce::Justification::centredLeft);
    g.setColour (C::dim); g.setFont (font (9.5f, false)); g.drawText ("v2", 206, 24, 30, 14, juce::Justification::centredLeft);

    const int algo = juce::jlimit (0, 7, (int) pv (IDs::algo));
    const auto col = algoCol[algo];
    // main knob panel + meters
    panel (g, { 788, 104, 320, 244 }, "", col, true);
    g.setColour (C::dim); g.setFont (font (10.0f, false));
    juce::String eff = "decay " + juce::String (proc.effDecay.load(), 2) + " s   pre " + juce::String (proc.effPre.load(), 0) + " ms";
    if (proc.hostBpm.load() > 30) eff << "   " << juce::String (proc.hostBpm.load(), 0) << " BPM";
    g.drawText (eff, 800, 108, 300, 16, juce::Justification::centredLeft);
    // in / wet / out meters
    ledMeter (g, { 12, 352, 300, 8 }, inM, inM, false, 0);
    ledMeter (g, { 320, 352, 300, 8 }, wetM, wetM, false, 0);
    ledMeter (g, { 628, 352, 300, 8 }, outM, outM, false, 0);
    g.setColour (C::dim); g.setFont (font (8.5f));
    g.drawText ("IN", 12, 361, 40, 10, juce::Justification::centredLeft); g.drawText ("REVERB", 320, 361, 60, 10, juce::Justification::centredLeft); g.drawText ("OUT", 628, 361, 40, 10, juce::Justification::centredLeft);
    if (status.isNotEmpty()) { g.setColour (C::cyan); g.setFont (font (10.5f)); g.drawText (status, 936, 352, 172, 20, juce::Justification::centredRight); }

    const int py = 380;
    panel (g, { 12, (float) py, 166, 308 }, "CHARACTER", C::green, true);
    panel (g, { 186, (float) py, 166, 308 }, "DECAY SHAPE", C::amber, true);
    panel (g, { 360, (float) py, 166, 308 }, "REVERB EQ", C::orange, true);
    panel (g, { 534, (float) py, 256, 308 }, "CLEAN MIX", C::violet, true);
    panel (g, { 798, (float) py, 310, 308 }, "SHIMMER", juce::Colour (0xffb8d8ff), pv (IDs::shimmer) > 0.001f);
    // clean-mix meters: per-band clarity reduction + duck
    {
        const char* bn[4] { "LOW", "LOW-MID", "HI-MID", "HIGH" };
        g.setColour (C::dim); g.setFont (font (9.0f)); g.drawText ("WHERE THE REVERB STEPS BACK", 548, py + 136, 230, 12, juce::Justification::centredLeft);
        for (int b = 0; b < 5; ++b)
        {
            const float v = b < 4 ? clarM[b] : duckM;
            auto r = juce::Rectangle<float> (548.0f, (float) (py + 156 + b * 28), 228, 16);
            g.setColour (C::dim); g.setFont (font (9.0f)); g.drawText (b < 4 ? bn[b] : "DUCK", r.removeFromLeft (54).toNearestInt(), juce::Justification::centredLeft);
            g.setColour (C::field); g.fillRoundedRectangle (r, 3.0f);
            const float t = juce::jlimit (0.0f, 1.0f, -v / 18.0f);
            g.setColour (C::violet.withAlpha (0.85f)); g.fillRoundedRectangle (r.withWidth (r.getWidth() * t), 3.0f);
            g.setColour (C::text); g.setFont (mono (9.0f)); g.drawText (juce::String (v, 1) + " dB", r.toNearestInt().reduced (4, 0), juce::Justification::centredRight);
        }
    }
    // shimmer / algorithm text
    g.setColour (C::dim); g.setFont (font (10.0f, false));
    static const char* about[8] {
        "HALL: big, smooth concert hall.  The all-rounder for vocals, leads and pads.",
        "PLATE: dense and bright, no room sound.  Classic on vocals, snares and synth leads.",
        "ROOM: real room with strong early reflections.  Drums, claps, glue.",
        "CHAMBER: warm echo chamber between room and hall.  Vocals and busses.",
        "CATHEDRAL: huge stone space with a slow build.  Breakdowns, pads, FX.",
        "BLOOM: the tail swells up after the note.  Dreamy pads, future bass, vocals.",
        "SHIMMER: hall with pitched feedback - turn up SHIMMER for the angelic octave.",
        "AMBIENCE: short, invisible space.  Adds depth and glue without a tail." };
    g.drawFittedText (about[algo], juce::Rectangle<int> (812, py + 140, 284, 60), juce::Justification::topLeft, 4);
    g.drawFittedText ("SMART ASSIST: press LEARN while your track plays - algorithm, decay in time with the song, pre-delay, EQ and clean-up are set for the material.",
                      juce::Rectangle<int> (812, py + 210, 284, 80), juce::Justification::topLeft, 5);
}
