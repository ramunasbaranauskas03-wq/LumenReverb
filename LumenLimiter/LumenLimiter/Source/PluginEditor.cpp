#include "PluginEditor.h"

using namespace lumen::led;

namespace
{
const juce::Colour cComp  { 0xff4fa3ff };
const juce::Colour cLevel { 0xff52d6c0 };
const juce::Colour cGr    { 0xffe0644f };
const juce::Colour cLufs  { 0xff4f8dff };
const juce::Colour cTrans { 0xffe6a440 };
const juce::Colour cColor { 0xffa293ff };
const juce::Colour cGood  { 0xff52c78c };

juce::String f1 (float v) { return v <= -69.0f ? juce::String ("--") : juce::String (v, 1); }
juce::String sgn (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1); }
}

//==============================================================================
void LimGraph::addHistogram (float lufs)
{
    if (lufs < -30.0f || lufs > 0.0f) return;
    const int b = juce::jlimit (0, (int) histo.size() - 1, (int) ((lufs + 30.0f) * 4.0f));
    for (auto& h : histo) h *= 0.9995f;
    histo[(size_t) b] += 1.0f;
}

void LimGraph::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    g.setColour (C::field); g.fillRoundedRectangle (all, 6.0f);
    g.setColour (C::border); g.drawRoundedRectangle (all.reduced (0.5f), 6.0f, 1.0f);
    auto r = all.reduced (2).withTrimmedRight (90).withTrimmedTop (6).withTrimmedBottom (6);
    auto side = all.reduced (2).withLeft (r.getRight() + 4).withTrimmedTop (6).withTrimmedBottom (6);
    auto ly = [&] (float db) { return r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 30.0f); };
    g.setFont (mono (9.0f));
    for (float db = 0; db >= -30; db -= 3)
    {
        g.setColour (C::border.withAlpha (std::fmod (-db, 6.0f) < 0.1f ? 0.5f : 0.2f)); g.drawHorizontalLine ((int) ly (db), r.getX(), r.getRight());
        if (std::fmod (-db, 6.0f) < 0.1f) { g.setColour (C::dim); g.drawText (juce::String ((int) db), (int) r.getX() + 2, (int) ly (db) - 12, 24, 11, juce::Justification::centredLeft); }
    }
    const int pos = proc.histPos.load (std::memory_order_acquire);
    const int N = juce::jmin (LumenLimiterProcessor::kHist, (int) r.getWidth());
    juce::Path outP, inP, grP, luP, cgP, clP; bool luStarted = false;
    outP.startNewSubPath (r.getX(), r.getBottom()); inP.startNewSubPath (r.getX(), r.getBottom());
    grP.startNewSubPath (r.getX(), r.getY()); cgP.startNewSubPath (r.getX(), r.getY()); clP.startNewSubPath (r.getX(), r.getY());
    for (int k = 0; k < N; ++k)
    {
        const auto& h = proc.hist[(pos - N + k + LumenLimiterProcessor::kHist) & (LumenLimiterProcessor::kHist - 1)];
        const float x = r.getX() + r.getWidth() * (float) k / (float) (N - 1);
        inP.lineTo (x, ly (h.inPk));
        outP.lineTo (x, ly (h.outPk));
        grP.lineTo (x, ly (-std::abs (h.limGr) - std::abs (h.clip)));
        clP.lineTo (x, ly (-std::abs (h.clip)));
        cgP.lineTo (x, ly (h.compGr));
        if (h.lufsS > -30) { if (! luStarted) { luP.startNewSubPath (x, ly (h.lufsS)); luStarted = true; } else luP.lineTo (x, ly (h.lufsS)); }
    }
    for (auto* p : { &outP, &inP }) { p->lineTo (r.getRight(), r.getBottom()); p->closeSubPath(); }
    grP.lineTo (r.getRight(), r.getY()); grP.closeSubPath(); clP.lineTo (r.getRight(), r.getY()); clP.closeSubPath(); cgP.lineTo (r.getRight(), r.getY()); cgP.closeSubPath();
    g.setColour (C::text.withAlpha (0.07f)); g.fillPath (inP);
    g.setGradientFill (juce::ColourGradient (C::text.withAlpha (0.30f), 0, r.getY(), C::text.withAlpha (0.06f), 0, r.getBottom(), false)); g.fillPath (outP);
    g.setColour (cComp.withAlpha (0.35f)); g.fillPath (cgP);
    g.setColour (cGr.withAlpha (0.55f)); g.fillPath (grP);
    g.setColour (cGr); g.strokePath (grP, juce::PathStrokeType (1.2f));
    g.setColour (C::amber.withAlpha (0.75f)); g.fillPath (clP);
    if (luStarted) glowPath (g, luP, cLufs, 1.8f);

    // target line
    const float tgt = proc.targetLufs();
    if (tgt > -30)
    {
        g.setColour (cGood.withAlpha (0.85f));
        for (float x = r.getX(); x < r.getRight(); x += 9) g.drawHorizontalLine ((int) ly (tgt), x, x + 5);
        g.setFont (mono (9.5f)); g.drawText ("TARGET " + juce::String (tgt, 1), (int) r.getRight() - 110, (int) ly (tgt) - 13, 106, 12, juce::Justification::centredRight);
    }
    // ceiling
    const float ce = proc.P (IDs::ceiling);
    g.setColour (cLevel.withAlpha (0.6f)); g.drawHorizontalLine ((int) ly (ce), r.getX(), r.getRight());

    // legend
    g.setFont (font (9.5f));
    g.setColour (C::text.withAlpha (0.7f)); g.drawText ("OUTPUT", (int) r.getX() + 30, (int) r.getBottom() - 16, 60, 12, juce::Justification::centredLeft);
    g.setColour (cGr); g.drawText ("LIMITER GR", (int) r.getX() + 86, (int) r.getBottom() - 16, 80, 12, juce::Justification::centredLeft);
    g.setColour (cComp); g.drawText ("COMP GR", (int) r.getX() + 160, (int) r.getBottom() - 16, 60, 12, juce::Justification::centredLeft);
    g.setColour (C::amber); g.drawText ("CLIP", (int) r.getX() + 330, (int) r.getBottom() - 16, 40, 12, juce::Justification::centredLeft);
    g.setColour (cLufs); g.drawText ("SHORT-TERM LUFS", (int) r.getX() + 218, (int) r.getBottom() - 16, 110, 12, juce::Justification::centredLeft);

    // side: histogram of short-term loudness on the same scale + bars
    g.setColour (C::border.withAlpha (0.4f)); g.drawVerticalLine ((int) side.getX() - 2, side.getY(), side.getBottom());
    float hm = 1.0f; for (float v : histo) hm = std::max (hm, v);
    juce::Path hp; hp.startNewSubPath (side.getX(), ly (-30));
    for (size_t b = 0; b < histo.size(); ++b)
    {
        const float db = -30.0f + ((float) b + 0.5f) * 0.25f;
        hp.lineTo (side.getX() + (side.getWidth() - 36) * histo[b] / hm, ly (db));
    }
    hp.lineTo (side.getX(), ly (0)); hp.closeSubPath();
    g.setColour (cGood.withAlpha (0.45f)); g.fillPath (hp);
    g.setColour (cGood); g.strokePath (hp, juce::PathStrokeType (1.0f));
    // vertical bars: GR, short-term
    auto bar = [&] (float x, float valDb, juce::Colour c, bool fromTop)
    {
        g.setColour (C::bg); g.fillRect (x, side.getY(), 8.0f, side.getHeight());
        const float y = ly (valDb);
        g.setColour (c);
        if (fromTop) g.fillRect (x, side.getY(), 8.0f, y - side.getY());
        else g.fillRect (x, y, 8.0f, side.getBottom() - y);
    };
    bar (side.getRight() - 30, -std::abs (proc.limGr.load()), cGr, true);
    bar (side.getRight() - 16, proc.lufsS.load(), cLufs, false);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("GR", (int) side.getRight() - 34, (int) side.getBottom() - 2, 16, 10, juce::Justification::centred);

    if (proc.P (IDs::cgm) > 0.5f || (int) proc.P (IDs::monitor) != 0)
    {
        const int mon = (int) proc.P (IDs::monitor);
        juce::String t = mon != 0 ? "MONITOR: " + monitorNames()[mon].toUpperCase() + (mon == 5 ? " (what is removed / added)" : "") : juce::String();
        if (proc.P (IDs::cgm) > 0.5f && mon != 5) t << (t.isEmpty() ? "" : "   ") << "EQUAL LOUDNESS " << sgn (proc.cgmDb.load()) << " dB - turn off before export!";
        auto b = juce::Rectangle<float> (r.getX() + 32, r.getY() + 6, std::min (r.getWidth() - 40, (float) t.length() * 6.6f + 24), 20);
        g.setColour (C::amber.withAlpha (0.16f)); g.fillRoundedRectangle (b, 4);
        g.setColour (C::amber); g.setFont (font (10.0f)); g.drawText (t, b, juce::Justification::centred);
    }
    if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - msgTime < 6000)
    {
        g.setFont (font (11.0f));
        const float w = std::min (r.getWidth() - 20, (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), message) + 30.0f);
        auto b = juce::Rectangle<float> (r.getCentreX() - w * 0.5f, r.getBottom() - 46, w, 24);
        g.setColour (juce::Colour (0xee23272d)); g.fillRoundedRectangle (b, 5);
        g.setColour (cLevel); g.drawRoundedRectangle (b, 5, 1); g.drawText (message, b, juce::Justification::centred);
    }
}

//==============================================================================
LumenLimiterEditor::Knob& LumenLimiterEditor::knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip, const juce::String& tip)
{
    auto k = std::make_unique<Knob> (proc.apvts, id, lbl, c, bip);
    k->setBounds (r); k->setTooltip (tip); addAndMakeVisible (*k);
    auto& ref = *k; owned.push_back (std::move (k)); return ref;
}
LumenLimiterEditor::Toggle& LumenLimiterEditor::toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r, bool round, const juce::String& tip)
{
    auto t = std::make_unique<Toggle> (proc.apvts, id, txt, c, round);
    t->setBounds (r); t->setTooltip (tip); addAndMakeVisible (*t);
    auto& ref = *t; owned.push_back (std::move (t)); return ref;
}
LumenLimiterEditor::Combo& LumenLimiterEditor::combo (const juce::String& id, juce::Colour c, juce::Rectangle<int> r, const juce::String& tip)
{
    auto cb = std::make_unique<Combo> (proc.apvts, id, c);
    cb->setBounds (r); cb->setTooltip (tip); addAndMakeVisible (*cb);
    auto& ref = *cb; owned.push_back (std::move (cb)); return ref;
}

namespace L
{
constexpr int top = 60, colH = 536, leftX = 12, colW = 280, cX = 300, cW = 640, rightX = 948, graphH = 344;
constexpr int botY = top + colH + 8, botH = LumenLimiterEditor::H - botY - 10;
}

LumenLimiterEditor::LumenLimiterEditor (LumenLimiterProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (W, H);

    // ---- header
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (232, 14, 290, 26);
    for (auto* b : { &prevB, &nextB, &abA, &abB, &abCopy, &matchB, &resetB }) { b->setColour (juce::TextButton::buttonColourId, cLevel); addAndMakeVisible (*b); }
    prevB.setBounds (528, 14, 28, 26); nextB.setBounds (558, 14, 28, 26);
    auto step = [this] (int d) { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + d + n) % n); };
    prevB.onClick = [step] { step (-1); }; nextB.onClick = [step] { step (1); };
    abA.setBounds (598, 14, 30, 26); abB.setBounds (630, 14, 30, 26); abCopy.setBounds (662, 14, 52, 26);
    abA.onClick = [this] { proc.switchAB (0); }; abB.onClick = [this] { proc.switchAB (1); }; abCopy.onClick = [this] { proc.copyAB(); };
    const char* monTips[] { "Normal stereo", "Listen to the Mid (mono) only", "Listen to the Side only", "Left channel only", "Right channel only",
                            "DIFF: hear what the processing removes or adds (the GAIN push is excluded)" };
    for (int m = 0; m < 6; ++m)
    {
        auto& b = monB[(size_t) m];
        b.setButtonText (m == 0 ? "ST" : m == 1 ? "M" : m == 2 ? "S" : m == 3 ? "L" : m == 4 ? "R" : "DIFF");
        b.setColour (juce::TextButton::buttonColourId, C::amber); b.setClickingTogglesState (false); b.setTooltip (monTips[m]);
        b.onClick = [this, m] { proc.setParam (IDs::monitor, (float) m); };
        b.setBounds (728 + (m == 5 ? 5 * 32 : m * 32), 14, m == 5 ? 46 : 30, 26);
        addAndMakeVisible (b);
    }
    toggle (IDs::cgm, "EQUAL LOUD", C::amber, { 944, 14, 120, 26 }, false,
            "Constant-gain monitoring done right: the output is matched to the input by measured LUFS, so you hear only the change in sound. Turn it off before exporting!");
    combo (IDs::quality, cColor, { 1072, 14, 60, 26 }, "Oversampling for the colour and clip stages");
    combo (IDs::dither, C::dim, { 1138, 14, 90, 26 }, "Dither - only on the final export (16-bit NS for CD / 16-bit files)");

    // ---- COMP column
    {
        const int x = L::leftX, y = L::top;
        toggle (IDs::compOn, "", cComp, { x + 8, y + 5, 18, 18 }, true, "Glue compressor on / off");
        knob (IDs::ratio, "RATIO", cComp, { x + 70, y + 30, 140, 150 }, false, "Compression ratio");
        knob (IDs::attack, "ATTACK", cComp, { x + 4, y + 186, 66, 80 }, false, "Comp attack - slower keeps more punch");
        knob (IDs::release, "RELEASE", cComp, { x + 72, y + 186, 66, 80 }, false, "Comp release");
        knob (IDs::scHp, "SC HP", cComp, { x + 140, y + 186, 66, 80 }, false, "Side-chain high-pass: the kick and sub no longer pump the whole mix");
        knob (IDs::compMix, "MIX", cComp, { x + 208, y + 186, 66, 80 }, false, "Parallel compression");
        knob (IDs::thr, "THRESHOLD", cComp, { x + 70, y + 272, 140, 150 }, false, "Comp threshold");
        toggle (IDs::autoRel, "AUTO REL", cComp, { x + 184, y + 440, 88, 24 }, false, "Program-dependent release");
        toggle (IDs::midSide, "MID/SIDE", cComp, { x + 184, y + 470, 88, 24 }, false, "Compress Mid and Side separately (tighter centre, freer width)");
    }
    // ---- LEVEL column
    {
        const int x = L::rightX, y = L::top;
        knob (IDs::loPunch, "LO PUNCH", cTrans, { x + 10, y + 30, 80, 88 }, false, "Pushes the low-end transients (kick) before the limiter - punch survives the loudness");
        knob (IDs::detail, "DETAIL", cTrans, { x + 190, y + 30, 80, 88 }, false, "Pushes the high-frequency transients (hats, snare snap) - clarity at high loudness");
        knob (IDs::gain, "GAIN", cLevel, { x + 60, y + 118, 160, 170 }, false, "How hard the limiter is driven - this is your loudness");
        knob (IDs::ceiling, "CEILING", cLevel, { x + 80, y + 296, 120, 128 }, false, "Output ceiling (true-peak when TP is on)");
        const char* modeTips[] { "Clear: transparent, long program-dependent release - for dynamic and acoustic material",
                                 "Tight: controlled, medium release - the all-rounder",
                                 "Punchy: soft pre-clip shaves only the first edge of transients, keeps the kick hitting",
                                 "Loud: fast release + pre-clip - maximum density for EDM" };
        for (int m = 0; m < 4; ++m)
        {
            auto& b = modeB[(size_t) m];
            b.setButtonText (modeNames()[m].toUpperCase());
            b.setColour (juce::TextButton::buttonColourId, cLevel); b.setClickingTogglesState (false); b.setTooltip (modeTips[m]);
            b.onClick = [this, m] { proc.setParam (IDs::mode, (float) m); };
            b.setBounds (x + 10 + m * 66, y + 452, 62, 30);
            addAndMakeVisible (b);
        }
        toggle (IDs::truePeak, "TRUE PEAK", cLevel, { x + 10, y + 494, 128, 26 }, false, "True-peak limiting (inter-sample peaks) - required by streaming services");
    }
    // ---- loudness panel (center, below the graph)
    {
        const int x = L::cX, y = L::top + L::graphH + 8;
        combo (IDs::target, cGood, { x + 12, y + 116, 160, 26 }, "Loudness target");
        knob (IDs::custom, "CUSTOM", cGood, { x + 178, y + 100, 60, 76 }, false, "Custom target (when Target = Custom)");
        matchB.setBounds (x + 466, y + 116, 110, 26);
        matchB.setTooltip ("Play the loudest part: GAIN adjusts itself until the short-term loudness hits the target");
        matchB.onClick = [this] { if (proc.matching()) proc.stopMatch(); else { proc.startMatch(); graph->clearHistogram(); } };
        resetB.setBounds (x + 582, y + 116, 50, 26);
        resetB.setTooltip ("Reset integrated loudness, LRA, true-peak max and the histogram");
        resetB.onClick = [this] { proc.resetMeters(); graph->clearHistogram(); };
    }
    // ---- bottom: transients / colour & clip / limiter
    {
        const int y = L::botY;
        knob (IDs::loFreq, "PUNCH FREQ", cTrans, { 20, y + 34, 84, 92 }, false, "Lo Punch works below this frequency");
        knob (IDs::detFreq, "DETAIL FREQ", cTrans, { 110, y + 34, 84, 92 }, false, "Detail works above this frequency");
        knob (IDs::transient, "TRANSIENT", cTrans, { 200, y + 34, 84, 92 }, true, "Whole-mix transients: + more attack, - softer");
        combo (IDs::color, cColor, { 430, y + 30, 120, 24 }, "Colour: Warm (tube), Tape (soft, rounded), Bright (exciter) - auto-levelled");
        knob (IDs::drive, "DRIVE", cColor, { 428, y + 60, 84, 92 }, false, "Colour amount");
        knob (IDs::clip, "CLIP", cColor, { 520, y + 60, 84, 92 }, false, "Clipper before the limiter: shaves the sharpest peaks - louder with fewer limiter artefacts");
        knob (IDs::clipShape, "CLIP SHAPE", cColor, { 612, y + 60, 84, 92 }, false, "Soft (round) to hard (crisp) clipping");
        knob (IDs::limRel, "RELEASE", cLevel, { 836, y + 34, 84, 92 }, false, "Limiter release (scaled by the mode)");
        knob (IDs::look, "LOOKAHEAD", cLevel, { 930, y + 34, 84, 92 }, false, "Limiter lookahead: longer = smoother, shorter = punchier");
        knob (IDs::balance, "BALANCE", cLevel, { 1024, y + 34, 84, 92 }, true, "Where the limiting happens: Lows (keeps the highs open on bass-heavy mixes) or Highs (keeps the low end solid on harsh mixes)");
        knob (IDs::link, "LINK", cLevel, { 1118, y + 34, 84, 92 }, false, "Stereo link of the limiter");
    }
    graph = std::make_unique<LimGraph> (proc);
    addAndMakeVisible (*graph); graph->setBounds (L::cX, L::top, L::cW, L::graphH);
    startTimerHz (30);
}

LumenLimiterEditor::~LumenLimiterEditor() { setLookAndFeel (nullptr); }

void LumenLimiterEditor::timerCallback()
{
    const int v = proc.programVersion.load() + proc.getCurrentProgram() * 100000;
    if (v != lastVersion) { lastVersion = v; presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    abA.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification); abB.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    const int md = (int) proc.P (IDs::mode), mon = (int) proc.P (IDs::monitor);
    for (int m = 0; m < 4; ++m) modeB[(size_t) m].setToggleState (m == md, juce::dontSendNotification);
    for (int m = 0; m < 6; ++m) monB[(size_t) m].setToggleState (m == mon, juce::dontSendNotification);
    if (proc.lufsM.load() > -60.0f) graph->addHistogram (proc.lufsS.load());
    if (proc.matching() && ++matchTick % 15 == 0) { const auto s = proc.matchStep(); if (s.isNotEmpty()) { graph->message = s; graph->msgTime = juce::Time::getMillisecondCounter(); } }
    matchB.setToggleState (proc.matching(), juce::dontSendNotification);
    needle += (proc.compGr.load() - needle) * 0.35f;
    grBar = std::max (std::abs (proc.limGr.load()), grBar * 0.9f);
    outL = std::max (proc.outPkL.load(), outL * 0.88f); outR = std::max (proc.outPkR.load(), outR * 0.88f); inBar = std::max (proc.inPk.load(), inBar * 0.88f);
    graph->repaint();
    repaint (L::leftX, L::top + 430, 180, 106);
    repaint (L::cX, L::top + L::graphH + 8, L::cW, L::colH - L::graphH - 8);
    repaint (L::rightX, L::top, L::colW, 30); repaint (L::rightX + 90, L::top + 30, 100, 88);
    repaint (L::rightX + 150, L::top + 490, 130, 40);
    repaint (296, L::botY + 120, 120, 60); repaint (700, L::botY + 30, 120, 120);
}

void LumenLimiterEditor::drawNeedle (juce::Graphics& g, juce::Rectangle<float> r, float grDb)
{
    g.setColour (juce::Colour (0xffe9e3d2)); g.fillRoundedRectangle (r, 5);
    g.setColour (juce::Colour (0xff7a7362)); g.drawRoundedRectangle (r.reduced (0.5f), 5, 1);
    const auto pivot = juce::Point<float> (r.getCentreX(), r.getBottom() + 6.0f);
    const float rad = r.getHeight() * 0.92f;
    auto ang = [] (float db) { return juce::jmap (std::sqrt (juce::jlimit (0.0f, 20.0f, db) / 20.0f), 0.0f, 1.0f, 0.95f, -0.95f); };
    g.setFont (font (8.5f));
    for (float db : { 0.0f, 1.0f, 2.0f, 3.0f, 5.0f, 7.0f, 10.0f, 15.0f, 20.0f })
    {
        const float a = ang (db);
        const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
        g.setColour (db >= 10 ? juce::Colour (0xffb03a2e) : juce::Colour (0xff3a352c)); g.drawLine ({ pivot + dir * (rad - 2), pivot + dir * (rad - 10) }, 1.2f);
        const auto pt = pivot + dir * (rad - 19);
        g.drawText (juce::String ((int) db), (int) pt.x - 10, (int) pt.y - 5, 20, 10, juce::Justification::centred);
    }
    g.setColour (juce::Colour (0xff3a352c)); g.drawText ("GR", (int) r.getRight() - 26, (int) r.getY() + 4, 22, 10, juce::Justification::centred);
    const float a = ang (-grDb);
    const auto tip = pivot + juce::Point<float> (std::sin (a), -std::cos (a)) * (rad - 4);
    juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (r.toNearestInt());
    g.setColour (juce::Colour (0xffc0392b)); g.drawLine ({ pivot, tip }, 1.6f);
}

void LumenLimiterEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    g.setColour (C::panelTop); g.fillRect (0, 0, W, 52);
    g.setColour (C::border); g.drawHorizontalLine (52, 0, (float) W);
    g.setColour (C::text); g.setFont (font (22.0f)); g.drawText ("LUMEN", 16, 10, 90, 32, juce::Justification::centredLeft);
    g.setColour (cLevel); g.drawText ("LIMITER", 96, 10, 110, 32, juce::Justification::centredLeft);
    g.setColour (C::dim); g.setFont (font (9.5f, false)); g.drawText ("v2", 198, 22, 20, 14, juce::Justification::centredLeft);

    const bool compOn = proc.P (IDs::compOn) > 0.5f;
    panel (g, juce::Rectangle<float> ((float) L::leftX, (float) L::top, (float) L::colW, (float) L::colH), "", cComp, compOn);
    g.setColour (compOn ? cComp : C::dim); g.setFont (font (12.0f)); g.drawText ("GLUE COMP", L::leftX + 32, L::top + 4, 120, 20, juce::Justification::centredLeft);
    drawNeedle (g, { (float) L::leftX + 10, (float) L::top + 440, 166, 86 }, needle);

    panel (g, juce::Rectangle<float> ((float) L::rightX, (float) L::top, (float) L::colW, (float) L::colH), "LEVEL", cLevel);
    // punch / detail activity
    {
        const float pu = proc.punchDb.load(), de = proc.detailDb.load();
        const float x = (float) L::rightX + 96, y = (float) L::top + 44;
        g.setFont (mono (9.0f));
        g.setColour (C::field); g.fillRoundedRectangle (x, y, 88, 52, 4);
        g.setColour (cTrans.withAlpha (0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, pu / 4.0f))); g.fillRoundedRectangle (x + 6, y + 6, 34, 40 * juce::jlimit (0.05f, 1.0f, pu / 6.0f), 2);
        g.setColour (cTrans.withAlpha (0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, de / 4.0f))); g.fillRoundedRectangle (x + 48, y + 6, 34, 40 * juce::jlimit (0.05f, 1.0f, de / 6.0f), 2);
        g.setColour (C::dim); g.drawText ("+" + juce::String (pu, 1), (int) x + 2, (int) y + 52, 40, 11, juce::Justification::centred);
        g.drawText ("+" + juce::String (de, 1), (int) x + 46, (int) y + 52, 40, 11, juce::Justification::centred);
    }
    {
        // GR readout + TP target warning next to the TP toggle
        const float gr = grBar;
        g.setFont (mono (10.5f)); g.setColour (gr > 0.05f ? cGr : C::dim);
        g.drawText ("GR " + juce::String (-std::abs (gr), 1) + " dB", L::rightX + 150, L::top + 494, 120, 12, juce::Justification::centredLeft);
        g.setColour (C::dim); g.drawText ("Latency " + juce::String (proc.getLatencySamples()) + " smp", L::rightX + 150, L::top + 508, 120, 12, juce::Justification::centredLeft);
    }

    // ---- loudness panel
    {
        const int x = L::cX, y = L::top + L::graphH + 8, h = L::colH - L::graphH - 8;
        panel (g, juce::Rectangle<float> ((float) x, (float) y, (float) L::cW, (float) h), "LOUDNESS", cLufs);
        struct Cell { const char* lbl; juce::String val; juce::Colour c; };
        const float st = proc.lufsS.load(), it = proc.lufsI.load(), mo = proc.lufsM.load(), tp = juce::Decibels::gainToDecibels (proc.tpMax.load(), -70.0f);
        const float plr = (it > -69 && tp > -69) ? tp - it : 0.0f;
        const float tTp = proc.targetTp();
        const Cell cells[] {
            { "SHORT-TERM", f1 (st), cGood }, { "INTEGRATED", f1 (it), cGood }, { "MOMENTARY", f1 (mo), C::text },
            { "TRUE PEAK", f1 (tp), tp > tTp + 0.05f ? C::red : C::text }, { "LRA", juce::String (proc.lra.load(), 1), C::text }, { "PLR (DYN)", it > -69 ? juce::String (plr, 1) : "--", C::text } };
        for (int k = 0; k < 6; ++k)
        {
            const int cx = x + 12 + k * 104;
            g.setColour (cells[k].c); g.setFont (font (24.0f)); g.drawText (cells[k].val, cx, y + 30, 100, 30, juce::Justification::centredLeft);
            g.setColour (C::dim); g.setFont (font (9.0f)); g.drawText (cells[k].lbl, cx, y + 60, 100, 12, juce::Justification::centredLeft);
        }
        // shift to target
        const float tgt = proc.targetLufs(), ref = it > -69 ? it : st;
        g.setFont (font (9.0f)); g.setColour (C::dim); g.drawText ("TARGET", x + 12, y + 102, 80, 12, juce::Justification::centredLeft);
        if (ref > -60)
        {
            const float shift = tgt - ref;
            const bool ok = std::abs (shift) < 0.5f;
            g.setColour (ok ? cGood : std::abs (shift) < 2.0f ? C::amber : C::text); g.setFont (font (20.0f));
            g.drawText (ok ? juce::String ("ON TARGET") : (shift > 0 ? "+" : "") + juce::String (shift, 1) + " dB", x + 250, y + 112, 200, 26, juce::Justification::centredLeft);
            g.setFont (font (9.0f)); g.setColour (C::dim);
            g.drawText (ok ? "integrated matches " + juce::String (tgt, 1) + " LUFS" : shift > 0 ? "louder needed (raise GAIN)" : "quieter needed (lower GAIN)", x + 250, y + 140, 220, 12, juce::Justification::centredLeft);
            if (tp > tTp + 0.05f) { g.setColour (C::red); g.drawText ("TRUE PEAK above " + juce::String (tTp, 1) + " dBTP for this target", x + 12, y + 156, 300, 12, juce::Justification::centredLeft); }
        }
        else { g.setColour (C::dim); g.setFont (font (11.0f)); g.drawText ("play the track...", x + 250, y + 116, 200, 20, juce::Justification::centredLeft); }
        g.setFont (mono (9.0f)); g.setColour (C::dim);
        g.drawText ("IN " + f1 (proc.inLufsS.load()) + " LUFS", x + 466, y + 150, 160, 12, juce::Justification::centredLeft);
    }

    // ---- bottom panels
    const int y = L::botY;
    panel (g, juce::Rectangle<float> (12, (float) y, 400, (float) L::botH), "TRANSIENTS", cTrans);
    panel (g, juce::Rectangle<float> (420, (float) y, 400, (float) L::botH), "COLOUR & CLIP", cColor);
    panel (g, juce::Rectangle<float> (828, (float) y, 400, (float) L::botH), "LIMITER", cLevel);
    {
        g.setFont (mono (9.5f)); g.setColour (C::dim);
        const float gl = proc.limGrLo.load(), gh = proc.limGrHi.load();
        g.drawText ("GR lows " + juce::String (-std::abs (gl), 1) + "  highs " + juce::String (-std::abs (gh), 1) + " dB", 836, y + 140, 300, 12, juce::Justification::centredLeft);
        const float cd = proc.clipDb.load();
        g.setColour (cd > 0.05f ? C::amber : C::dim);
        g.drawText ("clip " + juce::String (-cd, 1) + " dB", 706, y + 100, 100, 12, juce::Justification::centredLeft);
        g.setColour (C::dim); g.setFont (font (9.5f, false));
        g.drawFittedText ("Lo Punch + Detail keep the kick and the snap alive when you push the limiter hard.", 296, y + 40, 108, 90, juce::Justification::topLeft, 7);
    }
}
