#include "PluginEditor.h"

using namespace lumen::led;
using namespace lumen::comp2;

namespace
{
const juce::Colour cAcc  { 0xff4fa3ff };   // dynamics
const juce::Colour cGr   { 0xffe0644f };   // gain reduction
const juce::Colour cUp   { 0xff52c78c };   // upward gain
const juce::Colour cSc   { 0xffe6a440 };   // side-chain
const juce::Colour cChar { 0xffa293ff };   // character
const juce::Colour cPh   { 0xff52c78c };   // phase

juce::String dbStr (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1); }
}

//==============================================================================
void HistoryView::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    g.setColour (C::field); g.fillRoundedRectangle (all, 6.0f);
    g.setColour (C::border); g.drawRoundedRectangle (all.reduced (0.5f), 6.0f, 1.0f);
    auto r = all.reduced (2).withTrimmedRight (30).withTrimmedTop (4).withTrimmedBottom (4);
    auto ly = [&] (float db) { return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    g.setFont (mono (9.0f));
    for (float db = -54; db <= 0; db += 6)
    {
        g.setColour (C::border.withAlpha (db > -1 ? 0.6f : 0.25f)); g.drawHorizontalLine ((int) ly (db), r.getX(), r.getRight());
        g.setColour (C::dim); g.drawText (juce::String ((int) db), (int) r.getRight() + 2, (int) ly (db) - 6, 26, 12, juce::Justification::centredLeft);
    }
    auto& e = proc.engine;
    const int pos = e.histPos.load (std::memory_order_acquire);
    const int N = juce::jmin (Engine::kHist, (int) r.getWidth());
    juce::Path inP, outP, grP, upP;
    inP.startNewSubPath (r.getX(), r.getBottom()); grP.startNewSubPath (r.getX(), r.getY()); upP.startNewSubPath (r.getX(), r.getY());
    for (int k = 0; k < N; ++k)
    {
        const auto& h = e.hist[(pos - N + k + Engine::kHist) & (Engine::kHist - 1)];
        const float x = r.getX() + r.getWidth() * (float) k / (float) (N - 1);
        inP.lineTo (x, ly (h.in));
        if (k == 0) outP.startNewSubPath (x, ly (h.out)); else outP.lineTo (x, ly (h.out));
        const float grH = r.getHeight() * 0.45f * juce::jlimit (0.0f, 1.0f, std::abs (h.gr) / 24.0f);
        grP.lineTo (x, r.getY() + (h.gr < 0 ? grH : 0.0f));
        upP.lineTo (x, r.getY() + (h.gr > 0 ? grH : 0.0f));
    }
    inP.lineTo (r.getRight(), r.getBottom()); inP.closeSubPath();
    grP.lineTo (r.getRight(), r.getY()); grP.closeSubPath();
    upP.lineTo (r.getRight(), r.getY()); upP.closeSubPath();
    g.setGradientFill (juce::ColourGradient (C::text.withAlpha (0.18f), 0, r.getY(), C::text.withAlpha (0.05f), 0, r.getBottom(), false)); g.fillPath (inP);
    g.setColour (C::text.withAlpha (0.35f)); g.strokePath (inP, juce::PathStrokeType (1.0f));
    glowPath (g, outP, cAcc, 1.3f, 0.85f);
    g.setGradientFill (juce::ColourGradient (cGr.withAlpha (0.65f), 0, r.getY(), cGr.withAlpha (0.25f), 0, r.getY() + r.getHeight() * 0.45f, false)); g.fillPath (grP);
    g.setColour (cUp.withAlpha (0.55f)); g.fillPath (upP);

    // threshold
    const float thr = proc.engine.thrEff.load();
    const float ty = ly (thr);
    g.setColour (cAcc.withAlpha (0.8f));
    for (float x = r.getX(); x < r.getRight(); x += 8) g.drawHorizontalLine ((int) ty, x, x + 4);
    g.setFont (mono (9.5f)); g.drawText ("THR " + dbStr (thr), (int) r.getX() + 6, (int) ty - 14, 90, 12, juce::Justification::centredLeft);

    g.setFont (font (10.0f)); g.setColour (C::text.withAlpha (0.8f));
    g.drawText ("IN", (int) r.getRight() - 160, (int) r.getY() + 4, 30, 12, juce::Justification::centredLeft);
    g.setColour (cAcc); g.drawText ("OUT", (int) r.getRight() - 128, (int) r.getY() + 4, 34, 12, juce::Justification::centredLeft);
    g.setColour (cGr); g.drawText ("GAIN REDUCTION", (int) r.getRight() - 90, (int) r.getY() + 4, 90, 12, juce::Justification::centredLeft);

    if (proc.P (IDs::delta) > 0.5f || proc.P (IDs::scListen) > 0.5f)
    {
        const bool d = proc.P (IDs::delta) > 0.5f;
        auto b = juce::Rectangle<float> (r.getX() + 8, r.getY() + 22, d ? 250.0f : 230.0f, 20);
        g.setColour ((d ? C::red : cSc).withAlpha (0.18f)); g.fillRoundedRectangle (b, 4);
        g.setColour (d ? C::red : cSc); g.setFont (font (10.0f));
        g.drawText (d ? "DELTA: hearing what is removed" : "SC LISTEN: hearing the detector", b, juce::Justification::centred);
    }
    if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - msgTime < 8000)
    {
        g.setFont (font (11.0f));
        const float w = std::min (r.getWidth() - 20, (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), message) + 30.0f);
        auto b = juce::Rectangle<float> (r.getCentreX() - w * 0.5f, r.getBottom() - 32, w, 24);
        g.setColour (juce::Colour (0xee23272d)); g.fillRoundedRectangle (b, 5);
        g.setColour (cAcc); g.drawRoundedRectangle (b, 5, 1); g.drawText (message, b, juce::Justification::centred);
    }
}

//==============================================================================
void TransferView::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    g.setColour (C::field); g.fillRoundedRectangle (all, 6.0f);
    g.setColour (C::border); g.drawRoundedRectangle (all.reduced (0.5f), 6.0f, 1.0f);
    auto r = all.reduced (10).withTrimmedTop (14).withTrimmedLeft (14);
    r = r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight()), std::min (r.getWidth(), r.getHeight()));
    auto px = [&] (float db) { return r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    auto py = [&] (float db) { return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    g.setFont (mono (8.5f));
    for (float db = -60; db <= 0; db += 12)
    {
        g.setColour (C::border.withAlpha (0.35f)); g.drawVerticalLine ((int) px (db), r.getY(), r.getBottom()); g.drawHorizontalLine ((int) py (db), r.getX(), r.getRight());
        g.setColour (C::dim); g.drawText (juce::String ((int) db), (int) r.getX() - 24, (int) py (db) - 6, 20, 12, juce::Justification::centredRight);
    }
    g.setColour (C::dim.withAlpha (0.5f)); g.drawLine (px (-60), py (-60), px (0), py (0), 1.0f);
    g.setFont (font (10.0f)); g.setColour (C::text.withAlpha (0.85f)); g.drawText ("TRANSFER", 10, 4, 100, 14, juce::Justification::centredLeft);

    const auto p = proc.readParams();
    const auto& sd = styleDef (p.style);
    const float W = std::max (p.knee, sd.kneeMin), s = 1.0f - 1.0f / std::max (1.0f, p.ratio), T = proc.engine.thrEff.load();
    juce::Path c;
    for (float in = -60; in <= 0.01f; in += 0.25f)
    {
        float gdb = p.style == Upward ? std::min (upwardGain (in, T, s, W), p.range)
                  : p.style == Smash ? std::clamp (downwardGain (in, T, s, 6, false) + upwardGain (in, T - 8, s, 6), -p.range, p.range)
                  : std::max (downwardGain (in, T, s, W, sd.varimu), -p.range);
        const float out = in + gdb;
        if (in <= -60) c.startNewSubPath (px (in), py (out)); else c.lineTo (px (in), py (out));
    }
    glowPath (g, c, cAcc, 2.0f);
    // knee region + threshold
    g.setColour (cAcc.withAlpha (0.08f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (px (T - W * 0.5f), r.getY(), px (T + W * 0.5f), r.getBottom()));
    g.setColour (cAcc.withAlpha (0.6f)); g.drawVerticalLine ((int) px (T), r.getY(), r.getBottom());
    // live dot
    const float det = proc.engine.detNow.load(), gr = proc.engine.grNow.load();
    if (det > -60)
    {
        const float x = px (det), y = py (det + gr);
        g.setColour (juce::Colours::white.withAlpha (0.25f)); g.fillEllipse (x - 7, y - 7, 14, 14);
        g.setColour (juce::Colours::white); g.fillEllipse (x - 3.5f, y - 3.5f, 7, 7);
    }
    g.setFont (mono (9.0f)); g.setColour (C::dim);
    g.drawText (juce::String (p.ratio >= 99 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")) : juce::String (p.ratio, 1)) + ":1  knee " + juce::String (W, 0) + " dB",
                (int) r.getX() + 4, (int) r.getY() + 2, 140, 12, juce::Justification::centredLeft);
}

void TransferView::mouseDown (const juce::MouseEvent&)
{
    startThr = proc.P (IDs::threshold);
    if (auto* p = proc.apvts.getParameter (IDs::threshold)) p->beginChangeGesture();
}
void TransferView::mouseDrag (const juce::MouseEvent& e)
{
    if (auto* p = proc.apvts.getParameter (IDs::threshold))
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (-60.0f, 0.0f, startThr - (float) e.getDistanceFromDragStartY() * 60.0f / (float) getHeight())));
}
void TransferView::mouseUp (const juce::MouseEvent&) { if (auto* p = proc.apvts.getParameter (IDs::threshold)) p->endChangeGesture(); }

//==============================================================================
LumenCompressorEditor::Knob& LumenCompressorEditor::knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip, const juce::String& tip)
{
    auto k = std::make_unique<Knob> (proc.apvts, id, lbl, c, bip);
    k->setBounds (r); k->setTooltip (tip); addAndMakeVisible (*k);
    auto& ref = *k; owned.push_back (std::move (k)); return ref;
}
LumenCompressorEditor::Toggle& LumenCompressorEditor::toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r, bool round, const juce::String& tip)
{
    auto t = std::make_unique<Toggle> (proc.apvts, id, txt, c, round);
    t->setBounds (r); t->setTooltip (tip); addAndMakeVisible (*t);
    auto& ref = *t; owned.push_back (std::move (t)); return ref;
}
LumenCompressorEditor::Combo& LumenCompressorEditor::combo (const juce::String& id, juce::Colour c, juce::Rectangle<int> r, const juce::String& tip)
{
    auto cb = std::make_unique<Combo> (proc.apvts, id, c);
    cb->setBounds (r); cb->setTooltip (tip); addAndMakeVisible (*cb);
    auto& ref = *cb; owned.push_back (std::move (cb)); return ref;
}

namespace L
{
constexpr int dispY = 64, dispH = 286, styleY = 358, rowA = 418, rowAH = 176, rowB = rowA + rowAH + 8, rowBH = LumenCompressorEditor::H - rowB - 10;
constexpr int leftW = 748, rightX = 768, rightW = LumenCompressorEditor::W - rightX - 12;
constexpr int kw = 90, kh = 96;
}

LumenCompressorEditor::LumenCompressorEditor (LumenCompressorProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (W, H);

    // ---- header
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (282, 15, 270, 26);
    for (auto* b : { &prevB, &nextB, &abA, &abB, &abCopy, &learnB, &alignB }) { b->setColour (juce::TextButton::buttonColourId, cAcc); addAndMakeVisible (*b); }
    prevB.setBounds (558, 15, 28, 26); nextB.setBounds (588, 15, 28, 26);
    auto step = [this] (int d) { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + d + n) % n); };
    prevB.onClick = [step] { step (-1); }; nextB.onClick = [step] { step (1); };
    abA.setBounds (626, 15, 30, 26); abB.setBounds (658, 15, 30, 26); abCopy.setBounds (690, 15, 52, 26);
    abA.onClick = [this] { proc.switchAB (0); }; abB.onClick = [this] { proc.switchAB (1); }; abCopy.onClick = [this] { proc.copyAB(); };
    combo (IDs::quality, C::amber, { 752, 15, 70, 26 }, "Oversampling for the character and fast gain changes (less aliasing, more CPU)");
    learnB.setBounds (832, 15, 96, 26);
    learnB.setTooltip ("Play the track for ~6 s: Lumen Compressor recognises the material and sets style, timing and threshold");
    learnB.onClick = [this] { if (proc.analyzer.isLearning()) proc.analyzer.cancelLearn(); else proc.analyzer.requestLearn(); };
    toggle (IDs::delta, "DELTA", C::red, { 938, 15, 112, 26 }, false, "Hear only what the compressor removes");
    toggle (IDs::autoGain, "AUTO GAIN", C::green, { 1058, 15, 130, 26 }, false, "Keeps the output as loud as the input - honest A/B");

    history = std::make_unique<HistoryView> (proc);  addAndMakeVisible (*history);  history->setBounds (12, L::dispY, L::leftW, L::dispH);
    transfer = std::make_unique<TransferView> (proc); addAndMakeVisible (*transfer); transfer->setBounds (L::rightX, L::dispY, 282, L::dispH);

    // ---- style bar
    const int sw = (W - 24 - (kNumStyles - 1) * 4) / kNumStyles;
    for (int s = 0; s < kNumStyles; ++s)
    {
        auto& b = styleB[(size_t) s];
        b.setButtonText (styleNames()[s].toUpperCase());
        b.setColour (juce::TextButton::buttonColourId, cAcc);
        b.setClickingTogglesState (false);
        b.setTooltip (styleInfo (s));
        b.onClick = [this, s] { proc.setParam (IDs::style, (float) s); };
        b.setBounds (12 + s * (sw + 4), L::styleY, sw, 30);
        addAndMakeVisible (b);
    }

    // ---- DYNAMICS
    {
        const int x = 12, y = L::rowA + 36;
        knob (IDs::threshold, "THRESHOLD", cAcc, { x + 6, y, L::kw, L::kh + 10 }, false, "Level where compression starts (you can also drag the transfer graph)");
        knob (IDs::ratio, "RATIO", cAcc, { x + 98, y, L::kw, L::kh + 10 }, false, "How hard: 2:1 gentle ... inf:1 limiting");
        knob (IDs::knee, "KNEE", cAcc, { x + 190, y, L::kw, L::kh }, false, "Soft knee width: 0 = hard and punchy, 12+ = smooth");
        knob (IDs::attack, "ATTACK", cAcc, { x + 282, y, L::kw, L::kh }, false, "How fast it grabs. Slower = more transient / punch passes through");
        knob (IDs::release, "RELEASE", cAcc, { x + 374, y, L::kw, L::kh }, false, "How fast it lets go. Set it to the groove for natural movement");
        knob (IDs::hold, "HOLD", cAcc, { x + 466, y, L::kw, L::kh }, false, "Keeps the reduction for a moment before releasing - great for pumping and gating-style effects");
        knob (IDs::range, "RANGE", cAcc, { x + 558, y, L::kw, L::kh }, false, "Maximum gain reduction (or lift in Upward / Smash)");
        knob (IDs::lookahead, "LOOKAHEAD", cAcc, { x + 650, y, L::kw, L::kh }, false, "Sees peaks before they arrive - catches transients cleanly (adds latency, compensated by the DAW)");
        toggle (IDs::autoRel, "AUTO REL", cAcc, { x + L::leftW - 236, L::rowA + 4, 110, 22 }, false, "Program-dependent release: fast after transients, slow under sustained compression");
        toggle (IDs::autoThr, "AUTO THR", cAcc, { x + L::leftW - 120, L::rowA + 4, 110, 22 }, false, "Threshold follows the input loudness - same compression whether the track is quiet or hot");
    }
    // ---- CHARACTER & OUTPUT
    {
        const int x = L::rightX, y = L::rowA;
        combo (IDs::character, cChar, { x + 10, y + 34, 140, 24 }, "Analog character: Tube (warm, even), Tape (soft, rounded), Transformer (thick low end), Diode (gritty), Bright (exciter)");
        toggle (IDs::charPost, "AFTER COMP", cChar, { x + 158, y + 34, 120, 24 }, false, "Off: saturate before compression (glued). On: after (more grit on peaks)");
        knob (IDs::drive, "DRIVE", cChar, { x + 6, y + 66, L::kw - 6, L::kh + 6 }, false, "Character amount (auto-levelled - you hear the colour, not more volume)");
        knob (IDs::makeup, "MAKEUP", cAcc, { x + 92, y + 66, L::kw - 6, L::kh + 6 }, true, "Makeup gain");
        knob (IDs::mix, "MIX", cAcc, { x + 178, y + 66, L::kw - 6, L::kh + 6 }, false, "Parallel (New York) compression: blend dry and compressed");
        knob (IDs::output, "OUTPUT", cAcc, { x + 264, y + 66, L::kw - 6, L::kh + 6 }, true, "Output level");
    }
    // ---- SIDE-CHAIN
    {
        const int x = 12, y = L::rowB;
        combo (IDs::scSource, cSc, { x + 10, y + 32, 140, 24 }, "Internal = the track itself. External SC = another track (e.g. the kick). Tempo Sync = pumps to the song tempo, no kick needed");
        combo (IDs::chanMode, cSc, { x + 158, y + 32, 130, 24 }, "Stereo, Mid/Side, or compress only Mid / Side / Left / Right");
        combo (IDs::division, cSc, { x + 296, y + 32, 86, 24 }, "Tempo Sync note length");
        toggle (IDs::scListen, "LISTEN", cSc, { x + 390, y + 32, 100, 24 }, false, "Hear the side-chain (detector) signal");
        knob (IDs::scHp, "SC HP", cSc, { x + 6, y + 62, L::kw, L::kh }, false, "Side-chain high-pass: stops the bass / kick from pumping the whole mix");
        knob (IDs::scLp, "SC LP", cSc, { x + 98, y + 62, L::kw, L::kh }, false, "Side-chain low-pass: react only to the kick's low end");
        knob (IDs::scBellF, "BELL FREQ", cSc, { x + 190, y + 62, L::kw, L::kh }, false, "Side-chain bell frequency (e.g. 6 kHz to react to sibilance)");
        knob (IDs::scBellG, "BELL GAIN", cSc, { x + 282, y + 62, L::kw, L::kh }, true, "Boost = more sensitive at that frequency, cut = less");
        knob (IDs::link, "STEREO LINK", cSc, { x + 374, y + 62, L::kw, L::kh }, false, "100 % = both channels move together (stable image), 0 % = independent");
        knob (IDs::offset, "SYNC OFFSET", cSc, { x + 466, y + 62, L::kw, L::kh }, true, "Shifts the Tempo Sync pump earlier / later against the groove");
    }
    // ---- PHASE ALIGN
    {
        const int x = L::rightX, y = L::rowB;
        toggle (IDs::phaseOn, "", cPh, { x + 8, y + 5, 18, 18 }, true, "Phase Align on / off");
        alignB.setBounds (x + 10, y + 32, 120, 24);
        alignB.setTooltip ("Kick on the side-chain, bass on this track: play both and click - finds the rotation and delay that lock their low end");
        alignB.onClick = [this]
        {
            if (proc.phase.isCapturing()) { proc.phase.cancelCapture(); return; }
            if (! proc.scActive.load()) { phaseStatus = "Route the kick to the side-chain first"; return; }
            proc.phase.requestCapture(); phaseStatus = {};
        };
        knob (IDs::phaseRot, "ROTATION", cPh, { x + 6, y + 62, L::kw - 6, L::kh }, false, "Phase rotation (all frequencies, amplitude unchanged)");
        knob (IDs::phaseDly, "DELAY", cPh, { x + 92, y + 62, L::kw - 6, L::kh }, false, "Time shift of this track");
        knob (IDs::phaseFoc, "FOCUS", cPh, { x + 178, y + 62, L::kw - 6, L::kh }, false, "Low-end band compared by ALIGN");
    }
    startTimerHz (30);
}

LumenCompressorEditor::~LumenCompressorEditor() { setLookAndFeel (nullptr); }

void LumenCompressorEditor::timerCallback()
{
    const int v = proc.programVersion.load() + proc.getCurrentProgram() * 100000;
    if (v != lastVersion) { lastVersion = v; presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    abA.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification); abB.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    const int st = (int) proc.P (IDs::style);
    for (int s = 0; s < kNumStyles; ++s) styleB[(size_t) s].setToggleState (s == st, juce::dontSendNotification);

    lumen::Features f;
    if (proc.analyzer.consumeLearnResult (f)) { history->message = proc.applyLearn (f); history->msgTime = juce::Time::getMillisecondCounter(); }
    const bool lr = proc.analyzer.isLearning();
    learnB.setButtonText (lr ? "LISTEN " + juce::String (juce::roundToInt (proc.analyzer.getLearnProgress() * 100)) + "%" : "LEARN");
    learnB.setToggleState (lr, juce::dontSendNotification);

    const bool cap = proc.phase.isCapturing();
    alignB.setButtonText (cap ? "LISTEN " + juce::String (juce::roundToInt (proc.phase.captureProgress() * 100)) + "%" : "ALIGN");
    alignB.setToggleState (cap, juce::dontSendNotification);
    lumen::PhaseAligner::Result res;
    if (proc.phase.takeResult (res))
    {
        if (! res.valid) phaseStatus = "Not enough low end in kick or bass";
        else
        {
            proc.setParam (IDs::phaseRot, res.rotationDeg); proc.setParam (IDs::phaseDly, res.delayMs); proc.setParam (IDs::phaseOn, 1.0f);
            phaseStatus = "Aligned " + juce::String (res.corrBefore, 2) + " -> " + juce::String (res.corrAfter, 2);
        }
    }

    inM = std::max (proc.engine.inPk.load(), inM * 0.85f); outM = std::max (proc.engine.outPk.load(), outM * 0.85f);
    grM = proc.engine.grNow.load();
    history->repaint(); transfer->repaint();
    repaint (1056, L::dispY, W - 1056, L::dispH);
    repaint (0, L::styleY + 30, W, L::rowA - L::styleY - 30);
    repaint (L::rightX, L::rowB, L::rightW, 30); repaint (L::rightX + 260, L::rowB + 30, L::rightW - 260, 130);
}

void LumenCompressorEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    g.setColour (C::panelTop); g.fillRect (0, 0, W, 56);
    g.setColour (C::border); g.drawHorizontalLine (56, 0, (float) W);
    g.setColour (C::text); g.setFont (font (22.0f)); g.drawText ("LUMEN", 16, 12, 90, 32, juce::Justification::centredLeft);
    g.setColour (cAcc); g.drawText ("COMPRESSOR", 96, 12, 170, 32, juce::Justification::centredLeft);
    g.setColour (C::dim); g.setFont (font (9.5f, false)); g.drawText ("v2", 262, 24, 18, 14, juce::Justification::centredLeft);

    // meters
    {
        auto m = juce::Rectangle<float> (1058, (float) L::dispY, (float) (W - 1058 - 12), (float) L::dispH);
        g.setColour (C::field); g.fillRoundedRectangle (m, 6.0f);
        g.setColour (C::border); g.drawRoundedRectangle (m.reduced (0.5f), 6.0f, 1.0f);
        const float top = m.getY() + 30, h = m.getHeight() - 64;
        auto bar = [&] (float x, const char* lbl, float lin)
        {
            ledMeter (g, { x, top, 12, h }, lin, lin, true, 0);
            g.setColour (C::dim); g.setFont (font (9.0f)); g.drawText (lbl, (int) x - 10, (int) (top + h + 4), 32, 12, juce::Justification::centred);
            g.setFont (mono (9.0f)); g.setColour (C::text);
            g.drawText (juce::String (juce::Decibels::gainToDecibels (lin, -60.0f), 1), (int) x - 14, (int) m.getY() + 10, 40, 12, juce::Justification::centred);
        };
        bar (m.getX() + 18, "IN", inM);
        // GR bar (top down)
        const float gx = m.getX() + 56;
        g.setColour (C::field.brighter (0.05f)); g.fillRoundedRectangle (gx, top, 12, h, 2);
        const float gh = h * juce::jlimit (0.0f, 1.0f, std::abs (grM) / 24.0f);
        g.setColour (grM < 0 ? cGr : cUp); g.fillRoundedRectangle (gx, top, 12, gh, 2);
        g.setColour (C::dim); g.setFont (font (9.0f)); g.drawText ("GR", (int) gx - 10, (int) (top + h + 4), 32, 12, juce::Justification::centred);
        g.setFont (mono (9.0f)); g.setColour (grM < -0.05f ? cGr : C::text); g.drawText (dbStr (grM), (int) gx - 14, (int) m.getY() + 10, 40, 12, juce::Justification::centred);
        bar (m.getX() + 94, "OUT", outM);
        if (proc.P (IDs::autoGain) > 0.5f)
        {
            g.setFont (mono (8.5f)); g.setColour (C::green);
            g.drawText ("AG " + dbStr (proc.engine.autoGainDb.load()), (int) m.getX(), (int) m.getBottom() - 16, (int) m.getWidth(), 12, juce::Justification::centred);
        }
    }

    // style description
    g.setColour (C::dim); g.setFont (font (10.5f, false));
    g.drawText (styleInfo ((int) proc.P (IDs::style)), 14, L::styleY + 34, W - 28, 18, juce::Justification::centredLeft);
    const int src = (int) proc.P (IDs::scSource);
    if (src == 1)
    {
        const bool on = proc.scActive.load();
        g.setColour (on ? cSc : C::red); g.setFont (font (10.0f));
        g.drawText (on ? "EXTERNAL SIDE-CHAIN: signal OK" : "EXTERNAL SIDE-CHAIN: no signal - route a track to the side-chain input", 14, L::styleY + 34, W - 28, 18, juce::Justification::centredRight);
    }
    else if (src == 2)
    {
        g.setColour (cSc); g.setFont (font (10.0f));
        g.drawText ("TEMPO SYNC: pumping on every " + divisionNames()[(int) proc.P (IDs::division)], 14, L::styleY + 34, W - 28, 18, juce::Justification::centredRight);
    }

    panel (g, juce::Rectangle<float> (12, (float) L::rowA, (float) L::leftW, (float) L::rowAH), "DYNAMICS", cAcc);
    panel (g, juce::Rectangle<float> ((float) L::rightX, (float) L::rowA, (float) L::rightW, (float) L::rowAH), "CHARACTER & OUTPUT", cChar);
    panel (g, juce::Rectangle<float> (12, (float) L::rowB, (float) L::leftW, (float) L::rowBH), "SIDE-CHAIN", cSc);
    panel (g, juce::Rectangle<float> ((float) L::rightX, (float) L::rowB, (float) L::rightW, (float) L::rowBH), "", cPh, proc.P (IDs::phaseOn) > 0.5f);
    g.setColour (proc.P (IDs::phaseOn) > 0.5f ? cPh : C::dim); g.setFont (font (12.0f));
    g.drawText ("PHASE ALIGN  KICK / BASS", L::rightX + 32, L::rowB + 4, 220, 20, juce::Justification::centredLeft);

    // phase info
    {
        const int x = L::rightX + 270, y = L::rowB + 36;
        g.setFont (mono (9.5f)); g.setColour (C::dim);
        if (proc.lowEndPresent.load())
        {
            const float b = proc.corrBefore.load(), a = proc.corrAfter.load();
            g.drawText ("LOW-END CORR.", x, y, 140, 12, juce::Justification::centredLeft);
            g.setColour (b < 0 ? C::orange : cPh); g.drawText ("before " + juce::String (b, 2), x, y + 16, 140, 12, juce::Justification::centredLeft);
            g.setColour (a < 0 ? C::orange : cPh); g.drawText ("after  " + juce::String (a, 2), x, y + 32, 140, 12, juce::Justification::centredLeft);
        }
        else g.drawText ("needs kick on SC", x, y, 140, 12, juce::Justification::centredLeft);
        g.setColour (C::text.withAlpha (0.8f)); g.setFont (font (9.5f, false));
        g.drawFittedText (phaseStatus, x, y + 54, 140, 40, juce::Justification::topLeft, 3);
    }
}
