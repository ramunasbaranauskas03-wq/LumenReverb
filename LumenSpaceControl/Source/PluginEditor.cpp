#include "PluginEditor.h"

using namespace lumen::led;

namespace
{
constexpr int W = 1120, HH = 680;
const juce::Colour BC[4] = { C::magenta, C::amber, C::green, C::cyan };
const juce::Colour MAIN = C::violet;

template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}

// correlation bar  -1 .. +1
void corrBar (juce::Graphics& g, juce::Rectangle<float> r, float c, bool labels)
{
    g.setColour (juce::Colour (0xff030508)); g.fillRoundedRectangle (r, 3.0f);
    g.setColour (C::border); g.drawRoundedRectangle (r, 3.0f, 1.0f);
    const int segs = juce::jmax (10, (int) (r.getWidth() / 6.0f)) | 1;
    const float sw = (r.getWidth() - 4.0f) / (float) segs;
    const float pos = juce::jlimit (-1.0f, 1.0f, c);
    for (int i = 0; i < segs; ++i)
    {
        const float t = -1.0f + 2.0f * (i + 0.5f) / (float) segs;
        const bool lit = pos >= 0.0f ? (t >= 0.0f - 1.0f / segs && t <= pos) : (t <= 0.0f + 1.0f / segs && t >= pos);
        const juce::Colour col = t < -0.05f ? C::red : t < 0.3f ? C::amber : C::green;
        auto seg = juce::Rectangle<float> (r.getX() + 2.0f + i * sw + 0.75f, r.getY() + 3.0f, sw - 1.5f, r.getHeight() - 6.0f);
        if (lit) { g.setColour (col.withAlpha (0.25f)); g.fillRoundedRectangle (seg.expanded (1.2f), 2.0f); g.setColour (col); g.fillRoundedRectangle (seg, 1.2f); }
        else { g.setColour (col.withAlpha (0.07f)); g.fillRoundedRectangle (seg, 1.2f); }
    }
    g.setColour (C::text.withAlpha (0.5f)); g.fillRect (r.getCentreX() - 0.5f, r.getY() - 2.0f, 1.0f, r.getHeight() + 4.0f);
    if (labels)
    {
        g.setFont (font (8.5f)); g.setColour (C::dim);
        g.drawText ("-1", r.withY (r.getBottom() + 1.0f).withHeight (10.0f), juce::Justification::centredLeft);
        g.drawText ("0", r.withY (r.getBottom() + 1.0f).withHeight (10.0f), juce::Justification::centred);
        g.drawText ("+1", r.withY (r.getBottom() + 1.0f).withHeight (10.0f), juce::Justification::centredRight);
    }
}
}

//==============================================================================
SpaceXover::SpaceXover (LumenSpaceControlProcessor& p) : proc (p)
{
    for (int k = 0; k < 3; ++k)
        if (auto* prm = proc.apvts.getParameter (IDs::x (k)))
        {
            att[k] = std::make_unique<juce::ParameterAttachment> (*prm, [this] (float) { repaint(); }, nullptr);
            att[k]->sendInitialUpdate();
        }
}
int SpaceXover::numBands() const { return (int) proc.apvts.getRawParameterValue (IDs::bands)->load() + 1; }
float SpaceXover::fToX (float f) const { return (float) getWidth() * std::log10 (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / 3.0f; }
float SpaceXover::xToF (float x) const { return 20.0f * std::pow (10.0f, 3.0f * juce::jlimit (0.0f, 1.0f, x / (float) getWidth())); }
int SpaceXover::handleAt (float x) const
{
    int best = -1; float bd = 9.0f;
    for (int k = 0; k < numBands() - 1; ++k)
    {
        const float d = std::abs (x - fToX (proc.apvts.getRawParameterValue (IDs::x (k))->load()));
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}
void SpaceXover::mouseMove (const juce::MouseEvent& e)
{
    const int h = handleAt ((float) e.x);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}
void SpaceXover::mouseDown (const juce::MouseEvent& e) { drag = handleAt ((float) e.x); if (drag >= 0) att[drag]->beginGesture(); }
void SpaceXover::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    auto* prm = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (IDs::x (drag)));
    if (prm == nullptr) return;
    float f = xToF ((float) e.x);
    if (drag > 0) f = juce::jmax (f, proc.apvts.getRawParameterValue (IDs::x (drag - 1))->load() * 1.25f);
    if (drag + 1 < numBands() - 1) f = juce::jmin (f, proc.apvts.getRawParameterValue (IDs::x (drag + 1))->load() / 1.25f);
    att[drag]->setValueAsPartOfGesture (prm->getNormalisableRange().snapToLegalValue (f));
}
void SpaceXover::mouseUp (const juce::MouseEvent&) { if (drag >= 0) att[drag]->endGesture(); drag = -1; }

void SpaceXover::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, MAIN);
    const int nb = numBands();
    float edges[5] {};
    for (int k = 0; k < nb - 1; ++k) edges[k + 1] = fToX (proc.apvts.getRawParameterValue (IDs::x (k))->load());
    edges[nb] = r.getWidth();
    const float cy = r.getCentreY() + 6.0f;
    for (int b = 0; b < nb; ++b)
    {
        const auto col = BC[b];
        auto reg = juce::Rectangle<float> (edges[b], 0.0f, edges[b + 1] - edges[b], r.getHeight()).reduced (1.0f, 2.0f);
        juce::ColourGradient cg (col.withAlpha (0.10f), 0, reg.getBottom(), col.withAlpha (0.0f), 0, reg.getY(), false);
        g.setGradientFill (cg); g.fillRect (reg);
        // stereo image: a horizontal "field" whose half-width is the side/mid ratio (100% = full region)
        const float half = reg.getWidth() * 0.45f;
        auto fieldW = [&] (float w) { return juce::jlimit (2.0f, half, half * juce::jmin (1.0f, w / 1.6f)); };
        const float wi = fieldW (wIn[b]), wo = fieldW (wOut[b]);
        g.setColour (C::text.withAlpha (0.12f)); g.fillRoundedRectangle (reg.getCentreX() - wi, cy - 9.0f, 2 * wi, 18.0f, 4.0f);
        juce::ColourGradient fg (col.withAlpha (0.0f), reg.getCentreX() - wo, cy, col.withAlpha (0.0f), reg.getCentreX() + wo, cy, false);
        fg.addColour (0.5, col.withAlpha (0.75f));
        g.setGradientFill (fg); g.fillRoundedRectangle (reg.getCentreX() - wo, cy - 6.0f, 2 * wo, 12.0f, 4.0f);
        g.setColour (col.withAlpha (0.9f)); g.fillRect (reg.getCentreX() - 0.5f, cy - 11.0f, 1.0f, 22.0f);
        g.setFont (font (10.5f)); g.setColour (col);
        g.drawText (bandName (b, nb), reg.withHeight (16.0f).translated (0, 2), juce::Justification::centred);
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
        g.setColour (juce::Colour (0xff04060a)); g.fillRoundedRectangle (x - 26.0f, r.getHeight() - 30.0f, 52.0f, 14.0f, 3.0f);
        g.setColour (on ? juce::Colours::white : C::text.withAlpha (0.85f)); g.setFont (mono (9.5f));
        const float f = proc.apvts.getRawParameterValue (IDs::x (k))->load();
        g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + "k" : juce::String ((int) std::round (f)), juce::Rectangle<float> (x - 26.0f, r.getHeight() - 30.0f, 52.0f, 14.0f), juce::Justification::centred);
    }
}

//==============================================================================
void Scope::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (trail.getWidth() != getWidth() || trail.getHeight() != getHeight()) trail = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    {
        juce::Graphics tg (trail);
        // fade the old trail
        juce::Image::BitmapData bd (trail, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < bd.height; ++y)
            for (int x = 0; x < bd.width; ++x)
            {
                auto* px = bd.getPixelPointer (x, y);
                for (int c = 0; c < 4; ++c) px[c] = (juce::uint8) (px[c] * 0.72f);
            }
        const auto& e = proc.getEngine();
        float pk = 1e-4f;
        for (int i = 0; i < lumen::sc::SpaceEngine::kScope; ++i) pk = juce::jmax (pk, std::abs (e.scopeL[i]), std::abs (e.scopeR[i]));
        const float sc = 0.42f * r.getWidth() / juce::jmax (0.25f, pk);
        const float cx = r.getCentreX(), cy = r.getCentreY();
        for (int i = 0; i < lumen::sc::SpaceEngine::kScope; ++i)
        {
            const float l = e.scopeL[i], rr = e.scopeR[i];
            const float x = cx + (rr - l) * 0.7071f * sc, y = cy - (l + rr) * 0.7071f * sc;
            const float s = juce::jlimit (-1.0f, 1.0f, (rr - l) / (std::abs (l) + std::abs (rr) + 1e-6f));
            tg.setColour (C::violet.interpolatedWith (C::cyan, 0.5f + 0.5f * s).withAlpha (0.35f));
            tg.fillRect (x - 0.8f, y - 0.8f, 1.6f, 1.6f);
        }
    }
    g.setColour (juce::Colour (0xff020306)); g.fillEllipse (r.reduced (2.0f));
    // grid: L / M / R axes
    g.setColour (C::border);
    g.drawEllipse (r.reduced (2.0f), 1.0f);
    g.drawLine (r.getCentreX(), r.getY() + 4, r.getCentreX(), r.getBottom() - 4, 1.0f);
    g.drawLine (r.getX() + 4, r.getCentreY(), r.getRight() - 4, r.getCentreY(), 1.0f);
    const float d = r.getWidth() * 0.35f;
    g.setColour (C::border.withAlpha (0.6f));
    g.drawLine (r.getCentreX() - d, r.getCentreY() - d, r.getCentreX() + d, r.getCentreY() + d, 1.0f);
    g.drawLine (r.getCentreX() + d, r.getCentreY() - d, r.getCentreX() - d, r.getCentreY() + d, 1.0f);
    g.drawImageAt (trail, 0, 0);
    g.setFont (font (9.5f)); g.setColour (C::dim);
    g.drawText ("M", (int) r.getCentreX() - 10, 6, 20, 12, juce::Justification::centred);
    g.drawText ("L", (int) (r.getCentreX() - d) - 14, (int) (r.getCentreY() - d) - 12, 14, 12, juce::Justification::centred);
    g.drawText ("R", (int) (r.getCentreX() + d), (int) (r.getCentreY() - d) - 12, 14, 12, juce::Justification::centred);
    g.drawText ("+S", (int) r.getRight() - 24, (int) r.getCentreY() - 14, 20, 12, juce::Justification::centred);
    g.drawText ("-S", (int) r.getX() + 4, (int) r.getCentreY() - 14, 20, 12, juce::Justification::centred);
}

//==============================================================================
LumenSpaceControlEditor::LumenSpaceControlEditor (LumenSpaceControlProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    xover = std::make_unique<SpaceXover> (proc); addAndMakeVisible (*xover);
    scope = std::make_unique<Scope> (proc); addAndMakeVisible (*scope);
    add<Combo> (*this, owned, st, IDs::bands, MAIN)->setBounds (990, 76, 104, 20);
    auto* md = add<Combo> (*this, owned, st, IDs::mode, MAIN);
    md->setBounds (860, 76, 124, 20);
    md->setTooltip ("Min Phase: zero latency.  Linear Phase: no phase shift at all between bands (adds ~85 ms latency, for mastering)");

    for (int b = 0; b < 4; ++b)
    {
        const auto col = BC[b];
        auto* w = add<Knob> (*this, owned, st, IDs::w (b), "WIDTH", col);
        auto* sp = add<Knob> (*this, owned, st, IDs::spread (b), "SPREAD", col);
        auto* gn = add<Knob> (*this, owned, st, IDs::gain (b), "GAIN", col, true);
        auto* so = add<Toggle> (*this, owned, st, IDs::solo (b), "S", C::amber, true);
        auto* mu = add<Toggle> (*this, owned, st, IDs::mute (b), "M", C::red, true);
        w->slider.setTooltip ("Side level of this band: 0% = mono, 100% = untouched, 200% = twice as wide");
        sp->slider.setTooltip ("Widens even MONO sounds (adds a 90-degree copy to the side) - always 100% mono-compatible");
        so->setTooltip ("Solo band"); mu->setTooltip ("Mute band");
        bandComps[b] = { w, sp, gn, so, mu };
    }

    const char* gid[] = { "width", "mono_below", "safe", "mix", "out" };
    const char* glb[] = { "WIDTH", "MONO BELOW", "PHASE SAFE", "MIX", "OUTPUT" };
    for (int i = 0; i < 5; ++i) add<Knob> (*this, owned, st, gid[i], glb[i], MAIN, i == 4)->setBounds (26 + i * 92, 518, 92, 104);
    add<Toggle> (*this, owned, st, IDs::monoCheck, "MONO CHECK", C::amber)->setBounds (498, 528, 124, 26);
    add<Toggle> (*this, owned, st, IDs::sideOnly, "SIDE ONLY", C::cyan)->setBounds (498, 562, 124, 26);

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

    goalBox.addItemList ({ "Natural", "Wide", "Club Safe", "Huge" }, 1);
    goalBox.setSelectedId (1, juce::dontSendNotification);
    goalBox.setColour (juce::ComboBox::textColourId, C::cyan);
    addAndMakeVisible (goalBox); goalBox.setBounds (664, 528, 140, 26);
    assistB.setColour (juce::TextButton::buttonColourId, C::cyan);
    assistB.setTooltip ("Play your track: Smart Assist measures every band's width for 5 seconds and sets the bands for the chosen goal");
    assistB.onClick = [this] { proc.startAssist ((LumenSpaceControlProcessor::Goal) (goalBox.getSelectedId() - 1)); };
    addAndMakeVisible (assistB); assistB.setBounds (812, 527, 94, 28);

    setSize (W, HH);
    startTimerHz (30);
}

LumenSpaceControlEditor::~LumenSpaceControlEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenSpaceControlEditor::resized()
{
    const int nb = (int) pv (IDs::bands) + 1;
    xover->setBounds (26, 100, 1068, 68);
    scope->setBounds (824, 210, 240, 240);
    const int x0 = 16, total = 760, gap = 8;
    const int cw = (total - (nb - 1) * gap) / nb;
    for (int b = 0; b < 4; ++b)
    {
        const bool on = b < nb;
        for (auto* c : bandComps[b]) c->setVisible (on);
        if (! on) continue;
        const int cx = x0 + b * (cw + gap);
        const int mid = cx + cw / 2;
        bandComps[b][3]->setBounds (cx + cw - 58, 188, 22, 22);
        bandComps[b][4]->setBounds (cx + cw - 32, 188, 22, 22);
        bandComps[b][0]->setBounds (mid - 64, 216, 128, 120);
        const int sw = juce::jmin (90, cw / 2 - 8);
        bandComps[b][1]->setBounds (mid - sw - 4, 338, sw, 88);
        bandComps[b][2]->setBounds (mid + 4, 338, sw, 88);
    }
}

void LumenSpaceControlEditor::timerCallback()
{
    proc.finishAssistIfReady();
    if (proc.programVersion.load() != lastProgram) { lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    const int nb = (int) pv (IDs::bands) + 1;
    if (nb != lastBands) { lastBands = nb; resized(); }
    const auto& e = proc.getEngine();
    for (int b = 0; b < 4; ++b)
    {
        const float wi = lumen::sc::SpaceEngine::widthOf (e.bandEmIn[b], e.bandEsIn[b]);
        const float wo = lumen::sc::SpaceEngine::widthOf (e.bandEm[b], e.bandEs[b]);
        wIn[b] += (wi - wIn[b]) * 0.25f; wOut[b] += (wo - wOut[b]) * 0.25f;
        bandCorr[b] += (lumen::sc::SpaceEngine::corrOf (e.bandEm[b], e.bandEs[b]) - bandCorr[b]) * 0.25f;
        xover->wIn[b] = wIn[b]; xover->wOut[b] = wOut[b];
    }
    const float c = e.correlation();
    if (std::isfinite (c)) corrSm += (c - corrSm) * 0.3f;
    aB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 0 ? MAIN : C::dim);
    bB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 1 ? MAIN : C::dim);
    assistB.setEnabled (! proc.isAssisting());
    repaint();
}

void LumenSpaceControlEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (C::violet.withAlpha (0.0f), 0, 64, C::cyan.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.25, C::violet.withAlpha (0.75f)); line.addColour (0.75, C::cyan.withAlpha (0.75f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (MAIN.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (C::violet, 20, 0, C::cyan, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 146.0f, 18.0f, 220.0f, 28.0f }, "SPACE CONTROL", MAIN, 1.85f, juce::Justification::centredLeft, false, 10.5f);
    }
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("IN", 840, 16, 20, 10, juce::Justification::centredLeft);
    g.drawText ("OUT", 840, 38, 24, 10, juce::Justification::centredLeft);
    ledMeter (g, { 866.0f, 14.0f, 238.0f, 7.0f }, proc.inPeak[0].load(), proc.inPeak[0].load(), false, 32);
    ledMeter (g, { 866.0f, 23.0f, 238.0f, 7.0f }, proc.inPeak[1].load(), proc.inPeak[1].load(), false, 32);
    ledMeter (g, { 866.0f, 36.0f, 238.0f, 7.0f }, proc.outPeak[0].load(), proc.outPeak[0].load(), false, 32);
    ledMeter (g, { 866.0f, 45.0f, 238.0f, 7.0f }, proc.outPeak[1].load(), proc.outPeak[1].load(), false, 32);

    const int nb = (int) pv (IDs::bands) + 1;
    const bool linear = pv (IDs::mode) > 0.5f;
    panel (g, { 16.0f, 72.0f, 1088.0f, 104.0f }, "STEREO FIELD", MAIN);
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("grey = input width, colour = output width  /  drag the white lines to move the split points", 140, 77, 520, 16, juce::Justification::centredLeft);
    if (linear)
    {
        g.setColour (proc.isFirReady() ? C::green : C::amber);
        g.drawText (proc.isFirReady() ? "LINEAR PHASE  " + juce::String (proc.getLatencySamples()) + " smp" : "DESIGNING...", 690, 77, 160, 16, juce::Justification::centredRight);
    }

    // bands
    const int total = 760, gap = 8, cw = (total - (nb - 1) * gap) / nb;
    for (int b = 0; b < nb; ++b)
    {
        const float cx = 16.0f + b * (cw + gap);
        const auto col = BC[b];
        const bool muted = pv (IDs::mute (b)) > 0.5f;
        panel (g, { cx, 184.0f, (float) cw, 300.0f }, bandName (b, nb), col, ! muted);
        g.setFont (font (8.5f)); g.setColour (C::dim);
        g.drawText ("S", (int) cx + cw - 58, 209, 22, 10, juce::Justification::centred);
        g.drawText ("M", (int) cx + cw - 32, 209, 22, 10, juce::Justification::centred);
        // width in -> out
        const float yy = 432.0f;
        g.setFont (mono (10.0f));
        g.setColour (C::dim);
        g.drawText ("IN " + juce::String ((int) std::round (wIn[b] * 100.0f)) + "%", juce::Rectangle<float> (cx + 10, yy, cw * 0.5f - 10, 14), juce::Justification::centredLeft);
        g.setColour (col);
        g.drawText ("OUT " + juce::String ((int) std::round (wOut[b] * 100.0f)) + "%", juce::Rectangle<float> (cx + cw * 0.5f, yy, cw * 0.5f - 10, 14), juce::Justification::centredRight);
        corrBar (g, { cx + 10.0f, 452.0f, cw - 20.0f, 14.0f }, bandCorr[b], false);
        g.setFont (font (8.0f)); g.setColour (C::dim);
        g.drawText ("CORRELATION", juce::Rectangle<float> (cx + 10, 467, cw - 20.0f, 10), juce::Justification::centred);
    }

    // scope
    panel (g, { 784.0f, 184.0f, 320.0f, 300.0f }, "VECTORSCOPE", MAIN);
    corrBar (g, { 800.0f, 456.0f, 288.0f, 14.0f }, corrSm, false);
    g.setFont (mono (10.0f)); g.setColour (corrSm < 0.0f ? C::red : C::text.withAlpha (0.8f));
    g.drawText ("CORR " + juce::String (corrSm, 2), 980, 188, 110, 14, juce::Justification::centredRight);
    const auto& e = proc.getEngine();
    if (pv (IDs::safe) > -0.99f && e.safeGain() < 0.98f)
    {
        g.setColour (C::amber); g.drawText ("SAFE " + juce::String (juce::Decibels::gainToDecibels (e.safeGain()), 1) + " dB", 800, 436, 120, 14, juce::Justification::centredLeft);
    }
    if (pv (IDs::monoCheck) > 0.5f) { g.setColour (C::amber); g.drawText ("MONO", 1030, 436, 60, 14, juce::Justification::centredRight); }
    if (pv (IDs::sideOnly) > 0.5f) { g.setColour (C::cyan); g.drawText ("SIDE", 1030, 436, 60, 14, juce::Justification::centredRight); }

    // global
    panel (g, { 16.0f, 492.0f, 1088.0f, 176.0f }, "GLOBAL", MAIN);
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawFittedText ("MONO CHECK: hear the mix in mono.  SIDE ONLY: hear only what is stereo.", juce::Rectangle<int> (498, 596, 136, 50), juce::Justification::topLeft, 4);
    g.setColour (C::border); g.fillRect (646, 506, 1, 150);
    g.setFont (font (11.0f)); g.setColour (C::cyan.interpolatedWith (juce::Colours::white, 0.3f));
    g.drawText ("SMART ASSIST", 664, 504, 160, 16, juce::Justification::centredLeft);
    auto sr = juce::Rectangle<float> (916.0f, 527.0f, 176.0f, 28.0f);
    lcd (g, sr, C::cyan);
    if (proc.isAssisting())
    {
        const float pr = proc.assistProgress();
        for (int k = 0; k < 18; ++k) drawLed (g, { sr.getX() + 9.0f + k * 9.3f, sr.getCentreY() }, 2.6f, C::cyan, pr * 18.0f > (float) k ? 1.0f : 0.1f);
    }
    else
        status.draw (g, sr.reduced (4.0f, 2.0f), proc.getAssistResult().isEmpty() ? juce::String ("READY") : juce::String ("APPLIED"), C::cyan, 2.0f, juce::Justification::centred, false, 10.0f);
    g.setFont (font (9.5f));
    if (proc.getAssistResult().isNotEmpty() && ! proc.isAssisting())
    {
        g.setColour (C::cyan.withAlpha (0.85f));
        g.drawFittedText (proc.getAssistResult(), juce::Rectangle<int> (664, 566, 428, 16), juce::Justification::centredLeft, 1);
    }
    g.setColour (C::dim);
    g.drawFittedText ("Pick a goal, press ANALYZE and play 5 s. Lows go mono, every band is moved towards the target width, "
                      "mono sources get SPREAD instead of width. Then fine-tune.", juce::Rectangle<int> (664, 590, 428, 50), juce::Justification::topLeft, 3);
}
