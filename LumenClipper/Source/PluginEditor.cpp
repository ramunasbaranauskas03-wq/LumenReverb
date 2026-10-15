#include "PluginEditor.h"

using namespace lumen::led;

namespace
{
constexpr int W = 1120, HH = 720;
const juce::Colour BC[4] = { C::magenta, C::amber, C::green, C::cyan };
const juce::Colour MAIN = C::orange;
const juce::Colour CUT = C::red;

template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}
juce::String dbStr (float d, int dec = 1) { return (d > 0.04f ? "+" : "") + juce::String (d, dec); }
}

//==============================================================================
void ClipView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, MAIN);
    const float ceil = juce::Decibels::decibelsToGain (proc.apvts.getRawParameterValue (IDs::ceiling)->load());
    const float cy = r.getCentreY(), half = r.getHeight() * 0.47f;
    // vertical scale: linear amplitude, the ceiling sits at 60 % so up to +4.4 dB of "cut" is visible
    const float full = ceil / 0.6f;
    auto yOf = [&] (float a) { return juce::jmin (half, a / full * half); };
    const int N = LumenClipperProcessor::kHist;
    const int start = proc.histPos.load (std::memory_order_acquire);
    const float bw = r.getWidth() / (float) N;
    for (int k = 0; k < N; ++k)
    {
        const auto& f = proc.hist[(start + k) % N];
        const float x = r.getX() + k * bw;
        const float ho = yOf (f.out), hp = yOf (f.pre);
        if (hp > ho + 0.5f)
        {
            g.setColour (CUT.withAlpha (0.75f));
            g.fillRect (x, cy - hp, juce::jmax (1.0f, bw * 0.9f), hp - ho);
            g.fillRect (x, cy + ho, juce::jmax (1.0f, bw * 0.9f), hp - ho);
        }
        g.setColour (MAIN.withAlpha (0.65f).interpolatedWith (C::amber, 0.3f));
        g.fillRect (x, cy - ho, juce::jmax (1.0f, bw * 0.9f), 2.0f * ho);
    }
    // ceiling lines
    const float yc = yOf (ceil);
    juce::Path cl; cl.startNewSubPath (r.getX(), cy - yc); cl.lineTo (r.getRight(), cy - yc);
    cl.startNewSubPath (r.getX(), cy + yc); cl.lineTo (r.getRight(), cy + yc);
    glowPath (g, cl, juce::Colours::white, 1.0f, 0.5f);
    g.setColour (C::dim.withAlpha (0.4f)); g.fillRect (r.getX(), cy, r.getWidth(), 1.0f);
    g.setFont (font (9.0f)); g.setColour (C::text.withAlpha (0.7f));
    g.drawText ("CEILING " + juce::String (proc.apvts.getRawParameterValue (IDs::ceiling)->load(), 1) + " dB", (int) r.getX() + 6, (int) (cy - yc) - 14, 140, 12, juce::Justification::centredLeft);
}

//==============================================================================
void CurveView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, MAIN);
    lumen::clip::Curve c;
    c.set ((int) proc.apvts.getRawParameterValue (IDs::mode)->load(), proc.apvts.getRawParameterValue (IDs::shape)->load(), proc.apvts.getRawParameterValue (IDs::asym)->load());
    const float range = 2.0f;   // +-2 x ceiling (+6 dB) on the input axis
    auto px = [&] (float v) { return r.getCentreX() + v / range * r.getWidth() * 0.47f; };
    auto py = [&] (float v) { return r.getCentreY() - v / range * r.getHeight() * 0.47f; };
    g.setColour (C::dim.withAlpha (0.25f));
    for (float v : { -1.0f, 1.0f }) { g.fillRect (px (v), r.getY() + 2, 1.0f, r.getHeight() - 4); g.fillRect (r.getX() + 2, py (v), r.getWidth() - 4, 1.0f); }
    g.setColour (C::dim.withAlpha (0.45f));
    g.fillRect (r.getCentreX(), r.getY() + 2, 1.0f, r.getHeight() - 4); g.fillRect (r.getX() + 2, r.getCentreY(), r.getWidth() - 4, 1.0f);
    // unity line
    g.setColour (C::dim.withAlpha (0.35f)); g.drawLine (px (-range), py (-range), px (range), py (range), 1.0f);
    juce::Path pth;
    for (int i = 0; i <= 200; ++i)
    {
        const float v = -range + 2.0f * range * i / 200.0f;
        const float y = juce::jlimit (-range, range, (float) c.f (v));
        if (i == 0) pth.startNewSubPath (px (v), py (y)); else pth.lineTo (px (v), py (y));
    }
    glowPath (g, pth, MAIN, 2.0f);
    const float pk = proc.prePeak.load();
    dotSm = juce::jmax (pk, dotSm * 0.85f);
    const float dv = juce::jlimit (0.0f, range, dotSm);
    drawLed (g, { px (dv), py ((float) c.f (dv)) }, 4.0f, dv > 1.0f ? CUT : C::green, 1.0f);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("IN", (int) r.getRight() - 20, (int) r.getCentreY() + 2, 18, 10, juce::Justification::centredRight);
    g.drawText ("OUT", (int) r.getCentreX() + 3, (int) r.getY() + 3, 24, 10, juce::Justification::centredLeft);
}

//==============================================================================
ClipXover::ClipXover (LumenClipperProcessor& p) : proc (p)
{
    for (int k = 0; k < 3; ++k)
        if (auto* prm = proc.apvts.getParameter (IDs::x (k)))
        {
            att[k] = std::make_unique<juce::ParameterAttachment> (*prm, [this] (float) { repaint(); }, nullptr);
            att[k]->sendInitialUpdate();
        }
}
int ClipXover::numBands() const { return (int) proc.apvts.getRawParameterValue (IDs::bands)->load() + 1; }
float ClipXover::fToX (float f) const { return (float) getWidth() * std::log10 (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / 3.0f; }
float ClipXover::xToF (float x) const { return 20.0f * std::pow (10.0f, 3.0f * juce::jlimit (0.0f, 1.0f, x / (float) getWidth())); }
int ClipXover::handleAt (float x) const
{
    int best = -1; float bd = 9.0f;
    for (int k = 0; k < numBands() - 1; ++k)
    {
        const float d = std::abs (x - fToX (proc.apvts.getRawParameterValue (IDs::x (k))->load()));
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}
void ClipXover::mouseMove (const juce::MouseEvent& e)
{
    const int h = handleAt ((float) e.x);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}
void ClipXover::mouseDown (const juce::MouseEvent& e) { drag = handleAt ((float) e.x); if (drag >= 0) att[drag]->beginGesture(); }
void ClipXover::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    auto* prm = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (IDs::x (drag)));
    if (prm == nullptr) return;
    float f = xToF ((float) e.x);
    if (drag > 0) f = juce::jmax (f, proc.apvts.getRawParameterValue (IDs::x (drag - 1))->load() * 1.25f);
    if (drag + 1 < numBands() - 1) f = juce::jmin (f, proc.apvts.getRawParameterValue (IDs::x (drag + 1))->load() / 1.25f);
    att[drag]->setValueAsPartOfGesture (prm->getNormalisableRange().snapToLegalValue (f));
}
void ClipXover::mouseUp (const juce::MouseEvent&) { if (drag >= 0) att[drag]->endGesture(); drag = -1; }

void ClipXover::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, MAIN);
    const int nb = numBands();
    if (nb == 1)
    {
        g.setFont (font (11.0f)); g.setColour (C::dim);
        g.drawFittedText ("SINGLE BAND\nchoose 2-4 bands to clip lows, mids and highs differently", r.reduced (10).toNearestInt(), juce::Justification::centred, 3);
        return;
    }
    float edges[5] {};
    for (int k = 0; k < nb - 1; ++k) edges[k + 1] = fToX (proc.apvts.getRawParameterValue (IDs::x (k))->load());
    edges[nb] = r.getWidth();
    const int hp = (proc.histPos.load() + LumenClipperProcessor::kHist - 1) % LumenClipperProcessor::kHist;
    for (int b = 0; b < nb; ++b)
    {
        const auto col = BC[b];
        auto reg = juce::Rectangle<float> (edges[b], 0.0f, edges[b + 1] - edges[b], r.getHeight()).reduced (1.0f, 2.0f);
        const float lv = proc.hist[hp].band[b];
        const float over = lv > 1.0f ? juce::jmin (1.0f, juce::Decibels::gainToDecibels (lv) / 6.0f) : 0.0f;
        juce::ColourGradient cg (col.withAlpha (0.08f + 0.25f * juce::jmin (1.0f, lv)), 0, reg.getBottom(), col.withAlpha (0.0f), 0, reg.getY(), false);
        g.setGradientFill (cg); g.fillRect (reg);
        if (over > 0.0f) { g.setColour (CUT.withAlpha (0.15f + 0.4f * over)); g.fillRect (reg.withTrimmedTop (reg.getHeight() * (1.0f - over))); }
        g.setFont (font (10.5f)); g.setColour (col);
        g.drawText (bandName (b, nb), reg.withHeight (16.0f).translated (0, 4), juce::Justification::centred);
    }
    g.setFont (font (8.5f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = fToX (f);
        g.setColour (C::dim.withAlpha (0.18f)); g.fillRect (x, 2.0f, 1.0f, r.getHeight() - 4.0f);
        g.setColour (C::dim.withAlpha (0.7f));
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), (int) x + 2, (int) r.getHeight() - 12, 30, 10, juce::Justification::centredLeft);
    }
    for (int k = 0; k < nb - 1; ++k)
    {
        const float x = edges[k + 1];
        const bool on = k == hover || k == drag;
        juce::Path pth; pth.startNewSubPath (x, 2.0f); pth.lineTo (x, r.getHeight() - 2.0f);
        glowPath (g, pth, juce::Colours::white, on ? 2.0f : 1.2f, on ? 1.0f : 0.6f);
        g.setColour (juce::Colour (0xff04060a)); g.fillRoundedRectangle (x - 24.0f, r.getCentreY() - 7.0f, 48.0f, 14.0f, 3.0f);
        g.setColour (on ? juce::Colours::white : C::text.withAlpha (0.85f)); g.setFont (mono (9.5f));
        const float f = proc.apvts.getRawParameterValue (IDs::x (k))->load();
        g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + "k" : juce::String ((int) std::round (f)), juce::Rectangle<float> (x - 24.0f, r.getCentreY() - 7.0f, 48.0f, 14.0f), juce::Justification::centred);
    }
}

//==============================================================================
LumenClipperEditor::LumenClipperEditor (LumenClipperProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    view = std::make_unique<ClipView> (proc); addAndMakeVisible (*view); view->setBounds (26, 100, 664, 222);
    curve = std::make_unique<CurveView> (proc); addAndMakeVisible (*curve); curve->setBounds (720, 100, 212, 222);
    xover = std::make_unique<ClipXover> (proc); addAndMakeVisible (*xover); xover->setBounds (26, 368, 420, 110);
    add<Combo> (*this, owned, st, IDs::bands, MAIN)->setBounds (976, 344, 118, 20);

    for (int b = 0; b < 4; ++b)
    {
        const int x0 = 458 + b * 160;
        const auto col = BC[b];
        auto* m = add<Combo> (*this, owned, st, IDs::bmode (b), col); m->setBounds (x0, 388, 96, 22);
        auto* nc = add<Toggle> (*this, owned, st, IDs::byp (b), "", C::red, true); nc->setBounds (x0 + 102, 386, 24, 24);
        auto* so = add<Toggle> (*this, owned, st, IDs::solo (b), "", C::amber, true); so->setBounds (x0 + 128, 386, 24, 24);
        auto* tr = add<Knob> (*this, owned, st, IDs::trim (b), "DRIVE", col, true); tr->setBounds (x0 + 6, 412, 84, 70);
        nc->setTooltip ("NO CLIP: this band passes untouched (e.g. keep the sub clean)");
        so->setTooltip ("Solo band");
        m->setTooltip ("Clip mode of this band (Main = same as the main mode)");
        bandComps[b] = { m, nc, so, tr };
    }

    auto* dr = add<Knob> (*this, owned, st, IDs::drive, "DRIVE", MAIN); dr->setBounds (28, 518, 150, 166);
    const char* kid[] = { "ceiling", "shape", "asym", "side" };
    const char* klb[] = { "CEILING", "SHAPE", "ASYMMETRY", "SIDE DRIVE" };
    for (int i = 0; i < 4; ++i) add<Knob> (*this, owned, st, kid[i], klb[i], MAIN, i == 2 || i == 3)->setBounds (190 + i * 90, 512, 90, 100);
    add<Knob> (*this, owned, st, IDs::out, "OUTPUT", MAIN, true)->setBounds (552, 512, 90, 100);
    add<Knob> (*this, owned, st, IDs::mix, "MIX", MAIN)->setBounds (642, 512, 90, 100);
    auto* mode = add<Combo> (*this, owned, st, IDs::mode, MAIN); mode->setBounds (190, 634, 150, 26);
    auto* osb = add<Combo> (*this, owned, st, IDs::os, MAIN); osb->setBounds (348, 634, 72, 26);
    osb->setTooltip ("Oversampling: higher = less aliasing (more CPU / latency)");
    add<Combo> (*this, owned, st, IDs::stereo, MAIN)->setBounds (428, 634, 124, 26);
    auto* ad = add<Toggle> (*this, owned, st, IDs::adaa, "ADAA", C::green); ad->setBounds (190, 672, 90, 26);
    ad->setTooltip ("Anti-derivative anti-aliasing: removes most aliasing even at low oversampling");
    auto* tp = add<Toggle> (*this, owned, st, IDs::tp, "TRUE PEAK", C::cyan); tp->setBounds (286, 672, 112, 26);
    tp->setTooltip ("Keeps inter-sample (true) peaks under the ceiling - use as the last plugin on the master");
    auto* gm = add<Toggle> (*this, owned, st, IDs::match, "GAIN MATCH", C::violet); gm->setBounds (404, 672, 120, 26);
    gm->setTooltip ("Level-matched listening: hear the tone of the clipping, not just 'louder'");
    auto* dl = add<Toggle> (*this, owned, st, IDs::delta, "DELTA", C::red); dl->setBounds (530, 672, 92, 26);
    dl->setTooltip ("Hear only what the clipper removes");

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setColour (juce::ComboBox::textColourId, MAIN);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (412, 18, 260, 28);
    for (auto* b : { &prevB, &nextB }) { b->setColour (juce::TextButton::buttonColourId, MAIN); addAndMakeVisible (*b); }
    prevB.setBounds (378, 18, 28, 28); nextB.setBounds (678, 18, 28, 28);
    prevB.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    for (auto* b : { &aB, &bB, &copyB }) addAndMakeVisible (*b);
    aB.setBounds (720, 18, 30, 28); bB.setBounds (752, 18, 30, 28); copyB.setBounds (786, 18, 40, 28);
    aB.onClick = [this] { proc.switchAB (0); };
    bB.onClick = [this] { proc.switchAB (1); };
    copyB.onClick = [this] { proc.copyAB(); };
    copyB.setTooltip ("Copy the current setting to the other slot");
    copyB.setColour (juce::TextButton::buttonColourId, C::dim);

    goalBox.addItemList ({ "Transparent", "Punchy", "Loud", "Smash" }, 1);
    goalBox.setSelectedId (2, juce::dontSendNotification);
    goalBox.setColour (juce::ComboBox::textColourId, C::cyan);
    addAndMakeVisible (goalBox); goalBox.setBounds (760, 538, 140, 26);
    smartB.setColour (juce::TextButton::buttonColourId, C::cyan);
    smartB.setTooltip ("Play the loudest part of your track: Smart Drive measures the peaks for 5 s and sets DRIVE for the chosen amount of clipping");
    smartB.onClick = [this] { proc.startSmart ((LumenClipperProcessor::Goal) (goalBox.getSelectedId() - 1)); };
    addAndMakeVisible (smartB); smartB.setBounds (908, 537, 90, 28);

    setSize (W, HH);
    startTimerHz (30);
}

LumenClipperEditor::~LumenClipperEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenClipperEditor::mouseDown (const juce::MouseEvent& e)
{
    if (juce::Rectangle<int> (944, 140, 152, 40).contains (e.getPosition())) proc.resetPeaks();
}

void LumenClipperEditor::timerCallback()
{
    proc.finishSmartIfReady();
    if (proc.programVersion.load() != lastProgram) { lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    const int nb = (int) pv (IDs::bands) + 1;
    if (nb != lastBands)
    {
        lastBands = nb;
        for (int b = 0; b < 4; ++b) for (auto* c : bandComps[b]) { c->setVisible (nb > 1 && b < nb); }
    }
    const float gr = proc.grDb.load();
    grSm = gr < grSm ? gr : grSm + (gr - grSm) * 0.15f;
    aB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 0 ? MAIN : C::dim);
    bB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 1 ? MAIN : C::dim);
    smartB.setEnabled (! proc.isSmartRunning());
    repaint();
}

void LumenClipperEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (C::red.withAlpha (0.0f), 0, 64, C::amber.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.25, C::red.withAlpha (0.75f)); line.addColour (0.75, C::amber.withAlpha (0.75f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (MAIN.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (C::red, 20, 0, C::amber, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 146.0f, 18.0f, 170.0f, 28.0f }, "CLIPPER", MAIN, 1.85f, juce::Justification::centredLeft, false, 10.5f);
    }
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("IN", 840, 16, 20, 10, juce::Justification::centredLeft);
    g.drawText ("OUT", 840, 38, 24, 10, juce::Justification::centredLeft);
    ledMeter (g, { 866.0f, 14.0f, 238.0f, 7.0f }, proc.inPk[0].load(), proc.inPk[0].load(), false, 32);
    ledMeter (g, { 866.0f, 23.0f, 238.0f, 7.0f }, proc.inPk[1].load(), proc.inPk[1].load(), false, 32);
    ledMeter (g, { 866.0f, 36.0f, 238.0f, 7.0f }, proc.outPk[0].load(), proc.outPk[0].load(), false, 32);
    ledMeter (g, { 866.0f, 45.0f, 238.0f, 7.0f }, proc.outPk[1].load(), proc.outPk[1].load(), false, 32);

    // clip view + curve
    panel (g, { 16.0f, 72.0f, 684.0f, 260.0f }, "CLIP VIEW", MAIN);
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("orange = output   red = what was clipped off", 120, 77, 300, 16, juce::Justification::centredLeft);
    panel (g, { 708.0f, 72.0f, 396.0f, 260.0f }, "CURVE", MAIN);
    // readouts
    const float x0 = 944.0f;
    auto readout = [&] (float y, const juce::String& label, const juce::String& val, juce::Colour col)
    {
        g.setFont (font (8.5f)); g.setColour (C::dim); g.drawText (label, (int) x0, (int) y, 150, 10, juce::Justification::centredLeft);
        g.setFont (mono (15.0f)); g.setColour (col); g.drawText (val, (int) x0, (int) y + 10, 150, 18, juce::Justification::centredLeft);
    };
    const float tpm = proc.tpMax.load();
    readout (100.0f, "CLIPPING", dbStr (grSm) + " dB", grSm < -0.05f ? CUT.interpolatedWith (juce::Colours::white, 0.2f) : C::green);
    readout (140.0f, "OUT PEAK (click = reset)", tpm > 1e-6f ? dbStr (juce::Decibels::gainToDecibels (tpm), 2) + " dB" : juce::String ("-inf"), tpm > juce::Decibels::decibelsToGain (pv (IDs::ceiling)) * 1.0001f ? CUT : C::text);
    const float ls = proc.lufsS.load(), li = proc.lufsI.load();
    readout (180.0f, "LUFS SHORT / INTEGRATED", (ls > -99 ? juce::String (ls, 1) : juce::String ("-")) + " / " + (li > -99 ? juce::String (li, 1) : juce::String ("-")), C::amber);
    if (pv (IDs::match) > 0.5f) readout (220.0f, "GAIN MATCH", dbStr (proc.matchDb.load()) + " dB", C::violet);
    else if (pv (IDs::tp) > 0.5f) readout (220.0f, "TRUE PEAK LIMIT", dbStr (proc.tpGr.load()) + " dB", C::cyan);
    readout (260.0f, "LATENCY", juce::String (proc.getLatencySamples()) + " smp", C::dim.brighter (0.4f));
    g.setFont (font (9.0f)); g.setColour (MAIN.withAlpha (0.85f));
    g.drawText (modeNames()[(int) pv (IDs::mode)].toUpperCase() + "  /  SHAPE " + juce::String ((int) std::round (pv (IDs::shape) * 100)) + "%", 720, 300, 212, 14, juce::Justification::centred);

    // multiband
    panel (g, { 16.0f, 340.0f, 1088.0f, 148.0f }, "MULTIBAND", MAIN);
    const int nb = (int) pv (IDs::bands) + 1;
    if (nb > 1)
    {
        const int hp = (proc.histPos.load() + LumenClipperProcessor::kHist - 1) % LumenClipperProcessor::kHist;
        for (int b = 0; b < nb; ++b)
        {
            const float bx = 458.0f + b * 160.0f;
            g.setFont (font (10.5f)); g.setColour (BC[b]);
            g.drawText (bandName (b, nb), (int) bx, 368, 100, 16, juce::Justification::centredLeft);
            g.setFont (font (7.5f)); g.setColour (C::dim);
            g.drawText ("NO CLIP", (int) bx + 92, 372, 44, 10, juce::Justification::centred);
            g.drawText ("S", (int) bx + 128, 372, 24, 10, juce::Justification::centred);
            // band clip LEDs (dB over the clip point)
            const float lv = proc.hist[hp].band[b];
            const float over = lv > 1.0f ? juce::Decibels::gainToDecibels (lv) : 0.0f;
            for (int k = 0; k < 8; ++k)
                drawLed (g, { bx + 108.0f + (k % 2) * 12.0f, 474.0f - (k / 2) * 14.0f - (k % 2) * 7.0f }, 2.6f, k < 2 ? C::amber : CUT, over > k * 0.75f ? 1.0f : 0.07f);
            g.setFont (mono (9.0f)); g.setColour (over > 0.05f ? CUT : C::dim);
            g.drawText ("-" + juce::String (over, 1), (int) bx + 96, 414, 50, 12, juce::Justification::centred);
        }
    }
    else
    {
        g.setFont (font (10.0f)); g.setColour (C::dim);
        g.drawFittedText ("Multiband clipping: split the signal into 2-4 bands, give each band its own mode and drive, or set NO CLIP to keep a band "
                          "(for example the sub) untouched. A final stage keeps the sum under the ceiling.", juce::Rectangle<int> (470, 380, 600, 80), juce::Justification::centredLeft, 4);
    }

    // global
    panel (g, { 16.0f, 496.0f, 1088.0f, 212.0f }, "CLIPPER", MAIN);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("MODE", 190, 620, 150, 12, juce::Justification::centredLeft);
    g.drawText ("OVERSAMPLING", 348, 620, 80, 12, juce::Justification::centredLeft);
    g.drawText ("STEREO", 428, 620, 124, 12, juce::Justification::centredLeft);
    // big gain reduction dot-matrix on the drive side
    g.setColour (C::border); g.fillRect (744, 508, 1, 188);
    g.setFont (font (11.0f)); g.setColour (C::cyan.interpolatedWith (juce::Colours::white, 0.3f));
    g.drawText ("SMART DRIVE", 760, 512, 160, 16, juce::Justification::centredLeft);
    auto sr = juce::Rectangle<float> (1006.0f, 537.0f, 88.0f, 28.0f);
    lcd (g, sr, C::cyan);
    if (proc.isSmartRunning())
    {
        const float pr = proc.smartProgress();
        for (int k = 0; k < 9; ++k) drawLed (g, { sr.getX() + 8.0f + k * 9.0f, sr.getCentreY() }, 2.6f, C::cyan, pr * 9.0f > (float) k ? 1.0f : 0.1f);
    }
    else status.draw (g, sr.reduced (3.0f, 2.0f), proc.getSmartResult().isEmpty() ? juce::String ("READY") : juce::String ("SET"), C::cyan, 1.7f, juce::Justification::centred, false, 10.0f);
    g.setFont (font (9.5f));
    if (proc.getSmartResult().isNotEmpty() && ! proc.isSmartRunning())
    {
        g.setColour (C::cyan.withAlpha (0.85f));
        g.drawFittedText (proc.getSmartResult(), juce::Rectangle<int> (760, 574, 334, 16), juce::Justification::centredLeft, 1);
    }
    g.setColour (C::dim);
    g.drawFittedText ("Pick how hard you want it, press ANALYZE and play the loudest 5 s of your track. "
                      "Smart Drive measures the peaks and sets DRIVE so the hits are clipped by the right amount.",
                      juce::Rectangle<int> (760, 598, 334, 56), juce::Justification::topLeft, 4);
    g.setFont (font (9.0f)); g.setColour (C::dim.brighter (0.2f));
    g.drawFittedText ("Every mode clips at exactly the ceiling - switch modes and compare by ear without re-gaining.",
                      juce::Rectangle<int> (760, 660, 334, 30), juce::Justification::topLeft, 2);
}
