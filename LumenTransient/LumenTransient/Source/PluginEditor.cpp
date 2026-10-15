#include "PluginEditor.h"

using namespace lumen::led;

namespace
{
constexpr int W = 1120, HH = 640;
const juce::Colour BC[4] = { C::magenta, C::amber, C::green, C::cyan };
const juce::Colour MAIN = C::cyan;
const juce::Colour GAINC = C::red;

template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}

juce::Colour bandColour (int b, int nb)
{
    juce::ignoreUnused (nb);
    return BC[juce::jlimit (0, 3, b)];
}

inline float lvl01 (float a) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (a, -60.0f) + 54.0f) / 54.0f); }
}

//==============================================================================
AmountBar::AmountBar (juce::RangedAudioParameter& p, juce::Colour c)
    : colour (c), param (p), att (p, [this] (float v) { value = v; repaint(); }, nullptr)
{
    att.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

void AmountBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff030508)); g.fillRoundedRectangle (r, 3.0f);
    g.setColour (C::border); g.drawRoundedRectangle (r, 3.0f, 1.0f);
    const float norm = param.convertTo0to1 (value);
    const int segs = juce::jmax (10, (int) (r.getWidth() / 6.0f));
    const float sw = (r.getWidth() - 4.0f) / (float) segs;
    for (int i = 0; i < segs; ++i)
    {
        const float t0 = (float) i / segs;
        auto seg = juce::Rectangle<float> (r.getX() + 2.0f + i * sw + 0.75f, r.getY() + 3.0f, sw - 1.5f, r.getHeight() - 6.0f);
        const bool lit = t0 < norm;
        const juce::Colour cc = t0 >= 0.5f ? colour.interpolatedWith (juce::Colours::white, 0.25f * (t0 - 0.5f) * 2.0f) : colour;
        if (lit && isEnabled()) { g.setColour (cc.withAlpha (0.22f)); g.fillRoundedRectangle (seg.expanded (1.2f), 2.0f); g.setColour (cc); g.fillRoundedRectangle (seg, 1.2f); }
        else { g.setColour (colour.withAlpha (0.07f)); g.fillRoundedRectangle (seg, 1.2f); }
    }
    g.setColour (C::dim); g.fillRect (r.getCentreX() - 0.5f, r.getY() - 1.0f, 1.0f, r.getHeight() + 2.0f);   // 100 %
}
void AmountBar::setFromX (float x)
{
    const float n = juce::jlimit (0.0f, 1.0f, (x - 3.0f) / juce::jmax (1.0f, (float) getWidth() - 6.0f));
    att.setValueAsPartOfGesture (param.convertFrom0to1 (n));
}
void AmountBar::mouseDown (const juce::MouseEvent& e) { att.beginGesture(); setFromX ((float) e.x); }
void AmountBar::mouseDrag (const juce::MouseEvent& e) { setFromX ((float) e.x); }
void AmountBar::mouseUp (const juce::MouseEvent&)     { att.endGesture(); }
void AmountBar::mouseDoubleClick (const juce::MouseEvent&) { att.setValueAsCompleteGesture (1.0f); }

//==============================================================================
XoverView::XoverView (LumenTransientProcessor& p) : proc (p)
{
    for (int k = 0; k < 3; ++k)
        if (auto* prm = proc.apvts.getParameter (IDs::x (k)))
        {
            att[k] = std::make_unique<juce::ParameterAttachment> (*prm, [this] (float) { repaint(); }, nullptr);
            att[k]->sendInitialUpdate();
        }
}
float XoverView::fToX (float f) const { return (float) getWidth() * std::log10 (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / 3.0f; }
float XoverView::xToF (float x) const { return 20.0f * std::pow (10.0f, 3.0f * juce::jlimit (0.0f, 1.0f, x / (float) getWidth())); }
int XoverView::handleAt (float x) const
{
    const int nb = (int) proc.apvts.getRawParameterValue (IDs::bands)->load() + 1;
    int best = -1; float bd = 9.0f;
    for (int k = 0; k < nb - 1; ++k)
    {
        const float d = std::abs (x - fToX (proc.apvts.getRawParameterValue (IDs::x (k))->load()));
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}
void XoverView::mouseMove (const juce::MouseEvent& e)
{
    const int h = handleAt ((float) e.x);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
}
void XoverView::mouseDown (const juce::MouseEvent& e)
{
    drag = handleAt ((float) e.x);
    if (drag >= 0) { att[drag]->beginGesture(); return; }
    // click inside a band region selects it
    const int nb = (int) proc.apvts.getRawParameterValue (IDs::bands)->load() + 1;
    const float f = xToF ((float) e.x);
    int b = 0;
    for (int k = 0; k < nb - 1; ++k) if (f > proc.apvts.getRawParameterValue (IDs::x (k))->load()) b = k + 1;
    if (onSelect) onSelect (b);
}
void XoverView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    auto* prm = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (IDs::x (drag)));
    if (prm == nullptr) return;
    float f = xToF ((float) e.x);
    // keep the split points ordered
    if (drag > 0) f = juce::jmax (f, proc.apvts.getRawParameterValue (IDs::x (drag - 1))->load() * 1.25f);
    if (drag < 2) { const int nb = (int) proc.apvts.getRawParameterValue (IDs::bands)->load() + 1; if (drag + 1 < nb - 1) f = juce::jmin (f, proc.apvts.getRawParameterValue (IDs::x (drag + 1))->load() / 1.25f); }
    f = prm->getNormalisableRange().snapToLegalValue (f);
    att[drag]->setValueAsPartOfGesture (f);
}
void XoverView::mouseUp (const juce::MouseEvent&) { if (drag >= 0) att[drag]->endGesture(); drag = -1; }

void XoverView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, MAIN);
    const int nb = (int) proc.apvts.getRawParameterValue (IDs::bands)->load() + 1;
    float edges[5] { 0 };
    edges[0] = 0.0f;
    for (int k = 0; k < nb - 1; ++k) edges[k + 1] = fToX (proc.apvts.getRawParameterValue (IDs::x (k))->load());
    edges[nb] = r.getWidth();
    // band regions with live activity
    const int hp = (proc.histPos.load() + LumenTransientProcessor::kHist - 1) % LumenTransientProcessor::kHist;
    for (int b = 0; b < nb; ++b)
    {
        const auto col = bandColour (b, nb);
        auto reg = juce::Rectangle<float> (edges[b], 0.0f, edges[b + 1] - edges[b], r.getHeight()).reduced (1.0f, 2.0f);
        const float a = lvl01 (proc.hist[hp].out[b]);
        juce::ColourGradient cg (col.withAlpha (0.04f + 0.22f * a), 0, reg.getBottom(), col.withAlpha (0.0f), 0, reg.getY(), false);
        g.setGradientFill (cg); g.fillRect (reg);
        if (b == selected) { g.setColour (col.withAlpha (0.55f)); g.drawRect (reg, 1.0f); }
        // activity LEDs along the bottom
        const int nLed = juce::jmax (1, (int) (reg.getWidth() / 9.0f));
        for (int i = 0; i < nLed; ++i)
            drawLed (g, { reg.getX() + 5.0f + i * 9.0f, reg.getBottom() - 6.0f }, 1.8f, col, (float) i / nLed < a ? 0.9f : 0.06f);
        g.setFont (font (11.0f)); g.setColour (col.withAlpha (b == selected ? 1.0f : 0.75f));
        g.drawText (bandName (b, nb), reg.withTrimmedBottom (12.0f), juce::Justification::centred);
    }
    // frequency grid
    g.setFont (font (8.5f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = fToX (f);
        g.setColour (C::dim.withAlpha (0.18f)); g.fillRect (x, 2.0f, 1.0f, r.getHeight() - 4.0f);
        g.setColour (C::dim.withAlpha (0.7f));
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), (int) x + 2, 2, 30, 10, juce::Justification::centredLeft);
    }
    // handles
    for (int k = 0; k < nb - 1; ++k)
    {
        const float x = edges[k + 1];
        const bool on = k == hover || k == drag;
        juce::Path pth; pth.startNewSubPath (x, 2.0f); pth.lineTo (x, r.getHeight() - 2.0f);
        glowPath (g, pth, juce::Colours::white, on ? 2.0f : 1.2f, on ? 1.0f : 0.6f);
        g.setColour (juce::Colour (0xff04060a)); g.fillRoundedRectangle (x - 26.0f, 12.0f, 52.0f, 14.0f, 3.0f);
        g.setColour (on ? juce::Colours::white : C::text.withAlpha (0.85f)); g.setFont (mono (9.5f));
        const float f = proc.apvts.getRawParameterValue (IDs::x (k))->load();
        g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + "k" : juce::String ((int) std::round (f)), juce::Rectangle<float> (x - 26.0f, 12.0f, 52.0f, 14.0f), juce::Justification::centred);
    }
}

//==============================================================================
BandLane::BandLane (LumenTransientProcessor& p, int b) : proc (p), band (b)
{
    solo = std::make_unique<Toggle> (proc.apvts, IDs::solo (b), "S", C::amber, true);
    byp = std::make_unique<Toggle> (proc.apvts, IDs::byp (b), "BYP", C::red, true);
    solo->setTooltip ("Solo this band");
    byp->setTooltip ("Bypass the shaping of this band");
    addAndMakeVisible (*solo); addAndMakeVisible (*byp);
    if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (IDs::amount (b))))
    {
        amount = std::make_unique<AmountBar> (*prm, BC[b]);
        amount->setTooltip ("Band amount: scales Punch / Body / Sustain / Gate (100% = as set, 200% = double)");
        addAndMakeVisible (*amount);
    }
}

void BandLane::resized()
{
    const int w = getWidth();
    byp->setBounds (w - 30, 4, 22, 22);
    solo->setBounds (w - 56, 4, 22, 22);
    if (amount) amount->setBounds (60, getHeight() - 30, w - 72, 20);
}

void BandLane::paint (juce::Graphics& g)
{
    const auto col = bandColour (band, numBands);
    if (amount) amount->colour = col;
    auto r = getLocalBounds().toFloat();
    panel (g, r, active ? juce::String (bandName (band, numBands)) : juce::String ("—"), col, active);
    if (selected && active)
    {
        g.setColour (col.withAlpha (0.08f)); g.fillRoundedRectangle (r.reduced (1.0f), 8.0f);
        g.setColour (col.withAlpha (0.65f)); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.4f);
    }
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("S", (int) r.getRight() - 56, 25, 22, 10, juce::Justification::centred);
    g.drawText ("BYP", (int) r.getRight() - 32, 25, 26, 10, juce::Justification::centred);

    auto d = juce::Rectangle<float> (8.0f, 38.0f, r.getWidth() - 16.0f, r.getHeight() - 96.0f);
    lcd (g, d, col);
    if (! active) return;
    const int N = LumenTransientProcessor::kHist;
    const int start = proc.histPos.load (std::memory_order_acquire);
    const float cy = d.getCentreY(), hh = d.getHeight() * 0.46f;
    const float bw = d.getWidth() / (float) N;
    g.setColour (C::dim.withAlpha (0.3f)); g.fillRect (d.getX(), cy, d.getWidth(), 1.0f);
    // waveform: input (ghost) and output (band colour)
    juce::Path pin, pout;
    for (int k = 0; k < N; ++k)
    {
        const auto& f = proc.hist[(start + k) % N];
        const float x = d.getX() + k * bw;
        const float hi = lvl01 (f.in[band]) * hh, ho = lvl01 (f.out[band]) * hh;
        g.setColour (C::text.withAlpha (0.10f)); g.fillRect (x, cy - hi, juce::jmax (1.0f, bw), 2.0f * hi);
        g.setColour (col.withAlpha (0.55f)); g.fillRect (x, cy - ho, juce::jmax (1.0f, bw * 0.9f), 2.0f * ho);
    }
    // gain curve (dB), +/-18 dB full scale
    juce::Path gp;
    for (int k = 0; k < N; ++k)
    {
        const float gdb = proc.hist[(start + k) % N].g[band];
        const float x = d.getX() + k * bw, y = cy - juce::jlimit (-18.0f, 18.0f, gdb) * hh / 18.0f;
        if (k == 0) gp.startNewSubPath (x, y); else gp.lineTo (x, y);
    }
    glowPath (g, gp, GAINC, 1.3f);
    // live readout
    const float gNow = proc.hist[(start + N - 1) % N].g[band];
    g.setFont (mono (10.0f)); g.setColour (GAINC.interpolatedWith (juce::Colours::white, 0.3f));
    g.drawText ((gNow > 0.05f ? "+" : "") + juce::String (gNow, 1) + " dB", juce::Rectangle<float> (d.getRight() - 64.0f, d.getY() + 3.0f, 60.0f, 12.0f), juce::Justification::centredRight);
    // summary line: punch / sustain values
    auto val = [this] (const juce::String& id) { return proc.apvts.getRawParameterValue (id)->load(); };
    auto pc = [] (float v) { return (v > 0.004f ? "+" : "") + juce::String ((int) std::round (v * 100.0f)); };
    g.setFont (font (9.0f)); g.setColour (C::text.withAlpha (0.65f));
    g.drawText ("PK " + pc (val (IDs::punch (band))) + "  BD " + pc (val (IDs::body (band))) + "  SU " + pc (val (IDs::sus (band))),
                juce::Rectangle<float> (8.0f, d.getBottom() + 3.0f, r.getWidth() - 16.0f, 14.0f), juce::Justification::centredLeft);
    g.setColour (C::dim); g.drawText ("AMOUNT", juce::Rectangle<float> (8.0f, r.getHeight() - 30.0f, 52.0f, 20.0f), juce::Justification::centredLeft);
}

//==============================================================================
LumenTransientEditor::LumenTransientEditor (LumenTransientProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;

    xover = std::make_unique<XoverView> (proc);
    xover->onSelect = [this] (int b) { select (b); };
    addAndMakeVisible (*xover);
    add<Combo> (*this, owned, st, IDs::bands, MAIN)->setBounds (990, 76, 104, 20);

    for (int b = 0; b < 4; ++b)
    {
        lanes[b] = std::make_unique<BandLane> (proc, b);
        lanes[b]->onSelect = [this] (int bb) { select (bb); };
        addAndMakeVisible (*lanes[b]);
        const auto col = BC[b];
        shapeKnobs[b] = { add<Knob> (*this, owned, st, IDs::punch (b), "PUNCH", col, true), add<Knob> (*this, owned, st, IDs::body (b), "BODY", col, true),
                          add<Knob> (*this, owned, st, IDs::sus (b), "SUSTAIN", col, true), add<Knob> (*this, owned, st, IDs::gate (b), "GATE", col) };
        charKnobs[b] = { add<Knob> (*this, owned, st, IDs::drive (b), "DRIVE", col), add<Knob> (*this, owned, st, IDs::time (b), "TIME", col),
                         add<Knob> (*this, owned, st, IDs::gain (b), "GAIN", col), add<Knob> (*this, owned, st, IDs::amount (b), "AMOUNT", col) };
        const juce::Rectangle<int> slots[4] { { 832, 246, 120, 100 }, { 968, 246, 120, 100 }, { 832, 364, 120, 100 }, { 968, 364, 120, 100 } };
        for (int i = 0; i < 4; ++i) { shapeKnobs[b][(size_t) i]->setBounds (slots[i]); charKnobs[b][(size_t) i]->setBounds (slots[i]); }
    }
    for (auto* t : { &tabShape, &tabChar })
    {
        addAndMakeVisible (*t);
        t->setClickingTogglesState (false);
    }
    tabShape.setBounds (828, 204, 132, 24); tabChar.setBounds (964, 204, 132, 24);
    tabShape.onClick = [this] { tab = 0; updateVisibility(); };
    tabChar.onClick = [this] { tab = 1; updateVisibility(); };

    // global
    const char* gid[] = { "lookahead", "link", "mix", "out" };
    const char* glb[] = { "LOOKAHEAD", "STEREO LINK", "MIX", "OUTPUT" };
    for (int i = 0; i < 4; ++i) add<Knob> (*this, owned, st, gid[i], glb[i], MAIN, i == 3)->setBounds (28 + i * 96, 504, 96, 104);
    add<Toggle> (*this, owned, st, IDs::clip, "SOFT CLIP", C::amber)->setBounds (424, 510, 112, 24);
    add<Toggle> (*this, owned, st, IDs::autoGain, "AUTO GAIN", C::green)->setBounds (424, 542, 112, 24);
    add<Toggle> (*this, owned, st, IDs::sc, "SIDECHAIN", C::violet)->setBounds (544, 510, 112, 24);
    auto* dt = add<Toggle> (*this, owned, st, IDs::delta, "DELTA", C::red);
    dt->setBounds (544, 542, 112, 24);
    dt->setTooltip ("Listen to only what the shaper adds or removes");
    auto* osb = add<Combo> (*this, owned, st, IDs::os, MAIN);
    osb->setBounds (504, 580, 64, 20);
    osb->setTooltip ("Oversampling: cleaner Drive and very fast attacks (adds a little latency)");

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setColour (juce::ComboBox::textColourId, MAIN);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (372, 18, 270, 28);
    for (auto* b : { &prevB, &nextB }) { b->setColour (juce::TextButton::buttonColourId, MAIN); addAndMakeVisible (*b); }
    prevB.setBounds (338, 18, 28, 28); nextB.setBounds (648, 18, 28, 28);
    prevB.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };

    for (auto* b : { &aB, &bB, &copyB }) addAndMakeVisible (*b);
    aB.setBounds (690, 18, 30, 28); bB.setBounds (722, 18, 30, 28); copyB.setBounds (756, 18, 40, 28);
    aB.onClick = [this] { proc.switchAB (0); };
    bB.onClick = [this] { proc.switchAB (1); };
    copyB.onClick = [this] { proc.copyAB(); };
    copyB.setTooltip ("Copy the current setting to the other slot");
    copyB.setColour (juce::TextButton::buttonColourId, C::dim);

    goalBox.addItemList ({ "More Punch", "Tighter", "Punch + Tight", "Softer", "Clarity" }, 1);
    goalBox.setSelectedId (1, juce::dontSendNotification);
    goalBox.setColour (juce::ComboBox::textColourId, C::violet);
    addAndMakeVisible (goalBox); goalBox.setBounds (700, 510, 140, 24);
    learnB.setColour (juce::TextButton::buttonColourId, C::violet);
    learnB.setTooltip ("Play your drums / loop: LEARN listens for 5 seconds and sets every band for the chosen goal");
    learnB.onClick = [this] { proc.startLearn ((LumenTransientProcessor::Goal) (goalBox.getSelectedId() - 1)); };
    addAndMakeVisible (learnB); learnB.setBounds (848, 508, 80, 28);

    setSize (W, HH);
    select (0);
    startTimerHz (30);
}

LumenTransientEditor::~LumenTransientEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenTransientEditor::select (int b)
{
    const int nb = (int) pv (IDs::bands) + 1;
    sel = juce::jlimit (0, nb - 1, b);
    updateVisibility();
}

void LumenTransientEditor::updateVisibility()
{
    for (int b = 0; b < 4; ++b)
    {
        for (auto* c : shapeKnobs[b]) c->setVisible (b == sel && tab == 0);
        for (auto* c : charKnobs[b]) c->setVisible (b == sel && tab == 1);
        lanes[b]->selected = b == sel;
    }
    xover->selected = sel;
    tabShape.setColour (juce::TextButton::buttonColourId, tab == 0 ? BC[sel] : C::dim);
    tabChar.setColour (juce::TextButton::buttonColourId, tab == 1 ? BC[sel] : C::dim);
    repaint();
}

void LumenTransientEditor::resized()
{
    const int nb = (int) pv (IDs::bands) + 1;
    const int x0 = 16, total = 788, gap = 8;
    for (int b = 0; b < 4; ++b)
    {
        lanes[b]->numBands = nb;
        lanes[b]->active = b < nb;
        lanes[b]->setVisible (b < nb);
    }
    const int lw = (total - (nb - 1) * gap) / nb;
    for (int b = 0; b < nb; ++b) lanes[b]->setBounds (x0 + b * (lw + gap), 172, lw, 296);
    xover->setBounds (26, 100, 1068, 58);
}

void LumenTransientEditor::timerCallback()
{
    proc.finishLearnIfReady();
    if (proc.programVersion.load() != lastProgram) { lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    const int nb = (int) pv (IDs::bands) + 1;
    if (nb != lastBands) { lastBands = nb; resized(); select (sel); }
    aB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 0 ? MAIN : C::dim);
    bB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 1 ? MAIN : C::dim);
    learnB.setEnabled (! proc.isLearning());
    repaint();
}

void LumenTransientEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (C::magenta.withAlpha (0.0f), 0, 64, C::cyan.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.2, C::magenta.withAlpha (0.7f)); line.addColour (0.45, C::amber.withAlpha (0.7f));
    line.addColour (0.65, C::green.withAlpha (0.7f)); line.addColour (0.85, C::cyan.withAlpha (0.7f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (MAIN.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (C::magenta, 20, 0, C::cyan, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 146.0f, 18.0f, 170.0f, 28.0f }, "TRANSIENT 2", MAIN, 1.85f, juce::Justification::centredLeft, false, 10.5f);
    }
    // header meters
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("IN", 812, 22, 20, 10, juce::Justification::centredLeft);
    g.drawText ("OUT", 812, 36, 24, 10, juce::Justification::centredLeft);
    ledMeter (g, { 838.0f, 22.0f, 266.0f, 9.0f }, proc.inPeak.load(), proc.inPeak.load(), false, 34);
    ledMeter (g, { 838.0f, 36.0f, 266.0f, 9.0f }, proc.outPeak.load(), proc.outPeak.load(), false, 34);

    // crossover strip
    const int nb = (int) pv (IDs::bands) + 1;
    panel (g, { 16.0f, 72.0f, 1088.0f, 92.0f }, "MULTIBAND", MAIN);
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("drag the white lines to move the split points  /  click a band to edit it", 130, 77, 420, 16, juce::Justification::centredLeft);

    // band editor
    const auto sc = bandColour (sel, nb);
    panel (g, { 816.0f, 172.0f, 288.0f, 296.0f }, "", sc);
    bandTitle.draw (g, { 842.0f, 176.0f, 252.0f, 24.0f }, juce::String ("BAND ") + juce::String (sel + 1) + "  " + bandName (sel, nb), sc, 1.7f, juce::Justification::centredLeft, false, 10.0f);
    g.setFont (font (10.0f)); g.setColour (C::dim);
    if (tab == 0)
    {
        g.drawText ("ATTACK", 832, 232, 100, 12, juce::Justification::centredLeft);
        g.drawText ("RELEASE", 832, 350, 100, 12, juce::Justification::centredLeft);
        g.setColour (C::border); g.fillRect (888, 238, 202, 1); g.fillRect (892, 356, 198, 1);
    }
    else
    {
        g.drawText ("CHARACTER", 832, 232, 100, 12, juce::Justification::centredLeft);
        g.drawText ("LEVEL", 832, 350, 100, 12, juce::Justification::centredLeft);
        g.setColour (C::border); g.fillRect (912, 238, 178, 1); g.fillRect (878, 356, 212, 1);
    }

    // global
    panel (g, { 16.0f, 476.0f, 1088.0f, 152.0f }, "GLOBAL", MAIN);
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("OVERSAMPLING", 424, 580, 80, 20, juce::Justification::centredLeft);
    g.drawFittedText ("Latency " + juce::String (proc.getLatencySamples()) + " smp", juce::Rectangle<int> (576, 580, 104, 20), juce::Justification::centredLeft, 1);
    if (pv (IDs::autoGain) > 0.5f)
    {
        g.setColour (C::green.withAlpha (0.8f));
        const float a = proc.agDb.load();
        g.drawText ("AUTO " + juce::String (a > 0.05f ? "+" : "") + juce::String (a, 1) + " dB", 424, 488, 120, 14, juce::Justification::centredLeft);
    }
    if (pv (IDs::sc) > 0.5f && ! proc.scConnected.load())
    {
        g.setColour (C::violet.withAlpha (0.8f)); g.drawText ("NO SIDECHAIN INPUT", 544, 488, 130, 14, juce::Justification::centredLeft);
    }
    // learn
    g.setColour (C::border); g.fillRect (686, 492, 1, 124);
    g.setFont (font (11.0f)); g.setColour (C::violet.interpolatedWith (juce::Colours::white, 0.3f));
    g.drawText ("SMART LEARN", 700, 486, 140, 16, juce::Justification::centredLeft);
    auto sr = juce::Rectangle<float> (936.0f, 508.0f, 156.0f, 28.0f);
    lcd (g, sr, C::violet);
    if (proc.isLearning())
    {
        const float pr = proc.learnProgress();
        for (int k = 0; k < 16; ++k) drawLed (g, { sr.getX() + 9.0f + k * 9.2f, sr.getCentreY() }, 2.6f, C::violet, pr * 16.0f > (float) k ? 1.0f : 0.1f);
    }
    else
        status.draw (g, sr.reduced (4.0f, 2.0f), proc.getLearnResult().isEmpty() ? juce::String ("READY") : juce::String ("LEARNED"), C::violet, 2.0f, juce::Justification::centred, false, 10.0f);
    g.setFont (font (9.5f));
    if (proc.getLearnResult().isNotEmpty() && ! proc.isLearning())
    {
        g.setColour (C::violet.withAlpha (0.85f));
        g.drawFittedText ("Energy: " + proc.getLearnResult(), juce::Rectangle<int> (700, 546, 392, 16), juce::Justification::centredLeft, 1);
    }
    g.setColour (C::dim);
    g.drawFittedText ("Pick a goal, press LEARN and play 5 s of your track. Every band is set for you - then fine-tune.",
                      juce::Rectangle<int> (700, 568, 392, 40), juce::Justification::topLeft, 2);
}
