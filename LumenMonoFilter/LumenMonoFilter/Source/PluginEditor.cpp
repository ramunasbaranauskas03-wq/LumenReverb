#include "PluginEditor.h"

using namespace lumen::led;

namespace
{
constexpr int W = 1120, HH = 680;
const juce::Colour MAIN = C::cyan, LOWC = C::magenta, HIGHC = C::cyan, HPFC = C::amber;

template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}

juce::Colour corrColour (float c) { return c < 0.0f ? C::red : c < 0.5f ? C::amber.interpolatedWith (C::red, juce::jlimit (0.0f, 1.0f, (0.5f - c) * 2.0f) * 0.4f) : C::green; }

void corrBar (juce::Graphics& g, juce::Rectangle<float> r, float c)
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
}
}

//==============================================================================
WidthGraph::WidthGraph (LumenMonoFilterProcessor& p, const Analysis& a) : proc (p), an (a)
{
    const juce::String ids[] = { IDs::lowF, IDs::lowW, IDs::highF, IDs::highW, IDs::hpf };
    for (int k = 0; k < 5; ++k)
        if (auto* prm = proc.apvts.getParameter (ids[k]))
            att[k] = std::make_unique<juce::ParameterAttachment> (*prm, [this] (float) { repaint(); }, nullptr);
    setTooltip ("Drag LOW / HIGH: frequency (left-right) and width (up-down). Below LOW = low width, above HIGH = high width, smooth curve between. "
                "Drag the amber HPF handle along the floor. Double-click a node to reset it.");
}

int WidthGraph::hit (juce::Point<float> p) const
{
    const juce::Point<float> pts[] { { fToX (pv (IDs::lowF)), wToY (pv (IDs::lowW)) }, { fToX (pv (IDs::highF)), wToY (pv (IDs::highW)) },
                                     { fToX (juce::jmax (20.0f, pv (IDs::hpf))), (float) getHeight() - 12.0f } };
    int best = -1; float bd = 14.0f;
    for (int k = 0; k < 3; ++k) { const float d = pts[k].getDistanceFrom (p); if (d < bd) { bd = d; best = k; } }
    return best;
}
void WidthGraph::mouseMove (const juce::MouseEvent& e)
{
    const int h = hit (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}
void WidthGraph::mouseDown (const juce::MouseEvent& e)
{
    drag = hit (e.position);
    if (drag == Low) { att[0]->beginGesture(); att[1]->beginGesture(); }
    else if (drag == High) { att[2]->beginGesture(); att[3]->beginGesture(); }
    else if (drag == Hpf) att[4]->beginGesture();
}
void WidthGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    auto setV = [this] (int k, const juce::String& id, float v)
    {
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id)))
            att[k]->setValueAsPartOfGesture (rp->getNormalisableRange().snapToLegalValue (v));
    };
    float f = xToF (e.position.x);
    const float w = std::round (yToW (e.position.y) * 100.0f) / 100.0f;
    if (drag == Low) { setV (0, IDs::lowF, juce::jmin (f, pv (IDs::highF) / 1.05f)); setV (1, IDs::lowW, w); }
    else if (drag == High) { setV (2, IDs::highF, juce::jmax (f, pv (IDs::lowF) * 1.05f)); setV (3, IDs::highW, w); }
    else setV (4, IDs::hpf, f < 18.0f ? 0.0f : juce::jmin (f, 200.0f));
}
void WidthGraph::mouseUp (const juce::MouseEvent&)
{
    if (drag == Low) { att[0]->endGesture(); att[1]->endGesture(); }
    else if (drag == High) { att[2]->endGesture(); att[3]->endGesture(); }
    else if (drag == Hpf) att[4]->endGesture();
    drag = -1;
}
void WidthGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hit (e.position);
    if (h == Low) att[1]->setValueAsCompleteGesture (0.0f);
    else if (h == High) att[3]->setValueAsCompleteGesture (1.0f);
    else if (h == Hpf) att[4]->setValueAsCompleteGesture (0.0f);
}

void WidthGraph::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, MAIN);
    const float wdt = r.getWidth(), bottom = top() + plotH();
    const double fs = proc.getFs();
    // grid
    g.setFont (font (8.5f));
    for (float f : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = fToX (f);
        g.setColour (C::dim.withAlpha (0.16f)); g.fillRect (x, top(), 1.0f, plotH());
        g.setColour (C::dim.withAlpha (0.7f));
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), (int) x + 2, (int) r.getHeight() - 16, 30, 12, juce::Justification::centredLeft);
    }
    for (float w : { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f })
    {
        const float y = wToY (w);
        g.setColour (std::abs (w - 1.0f) < 0.01f ? C::text.withAlpha (0.22f) : C::dim.withAlpha (0.13f)); g.fillRect (0.0f, y, wdt, 1.0f);
        g.setColour (C::dim.withAlpha (0.75f));
        g.drawText (juce::String ((int) (w * 100)) + "%", (int) wdt - 40, (int) y - 12, 36, 11, juce::Justification::centredRight);
    }
    // spectra (dB -90..0 over the plot height)
    auto dbY = [&] (float db) { return bottom - juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 90.0f) * plotH(); };
    auto spectrumPath = [&] (const float* mag, bool closed)
    {
        juce::Path pth; bool started = false;
        const int nb = Analysis::kN / 2;
        float lastX = -10.0f;
        for (int k = 1; k < nb; ++k)
        {
            const float x = fToX ((float) (k * fs / Analysis::kN));
            if (x < 0.0f) continue;
            if (x > wdt) break;
            if (x - lastX < 1.5f && k < nb - 1) continue;
            lastX = x;
            const float y = dbY (mag[k]);
            if (! started) { pth.startNewSubPath (x, closed ? bottom : y); if (closed) pth.lineTo (x, y); started = true; }
            else pth.lineTo (x, y);
        }
        if (closed && started) { pth.lineTo (lastX, bottom); pth.closeSubPath(); }
        return pth;
    };
    {
        auto pm = spectrumPath (an.midOut, true);
        g.setColour (C::violet.withAlpha (0.13f)); g.fillPath (pm);
        g.setColour (C::violet.withAlpha (0.45f)); g.strokePath (pm, juce::PathStrokeType (1.0f));
        auto ps = spectrumPath (an.sideOut, true);
        juce::ColourGradient sg (C::cyan.withAlpha (0.28f), 0, top(), C::cyan.withAlpha (0.04f), 0, bottom, false);
        g.setGradientFill (sg); g.fillPath (ps);
        g.setColour (C::cyan.withAlpha (0.65f)); g.strokePath (ps, juce::PathStrokeType (1.0f));
        auto pin = spectrumPath (an.sideIn, false);
        g.setColour (C::text.withAlpha (0.35f)); g.strokePath (pin, juce::PathStrokeType (1.0f));
    }
    // HPF region
    const float hpf = pv (IDs::hpf);
    if (hpf >= 15.0f)
    {
        const float x = fToX (hpf);
        juce::ColourGradient hg (HPFC.withAlpha (0.18f), 0, 0, HPFC.withAlpha (0.02f), x, 0, false);
        g.setGradientFill (hg); g.fillRect (0.0f, top(), x, plotH());
    }
    // width envelope
    lumen::mono::Params p = proc.readParams();
    juce::Path curve, fill;
    for (int px = 0; px <= (int) wdt; px += 2)
    {
        const float f = xToF ((float) px);
        const float w = lumen::mono::widthAt (p, f) * lumen::mono::hpfMag (p, f) * std::pow (10.0f, p.sideDb / 20.0f);
        const float y = wToY (juce::jlimit (0.0f, 2.0f, w));
        if (px == 0) { curve.startNewSubPath ((float) px, y); fill.startNewSubPath ((float) px, bottom); fill.lineTo ((float) px, y); }
        else { curve.lineTo ((float) px, y); fill.lineTo ((float) px, y); }
    }
    fill.lineTo (wdt, bottom); fill.closeSubPath();
    juce::ColourGradient fg (LOWC.withAlpha (0.16f), fToX (p.lowF), 0, HIGHC.withAlpha (0.16f), fToX (p.highF), 0, false);
    g.setGradientFill (fg); g.fillPath (fill);
    glowPath (g, curve, juce::Colours::white, 2.0f, 0.9f);
    // mono zone label
    if (p.lowW < 0.05f)
    {
        const float x = fToX (juce::jmin (p.lowF, p.highF));
        g.setColour (LOWC.withAlpha (0.8f)); g.setFont (font (11.0f));
        if (x > 60.0f) g.drawText ("MONO", juce::Rectangle<float> (4.0f, bottom - 34.0f, x - 8.0f, 14.0f), juce::Justification::centred);
    }
    // nodes
    auto node = [&] (juce::Point<float> c, juce::Colour col, const juce::String& name, const juce::String& val, bool on)
    {
        g.setColour (col.withAlpha (on ? 0.35f : 0.18f)); g.fillEllipse (c.x - 13, c.y - 13, 26, 26);
        g.setColour (juce::Colour (0xff04060a)); g.fillEllipse (c.x - 7, c.y - 7, 14, 14);
        g.setColour (col); g.drawEllipse (c.x - 7, c.y - 7, 14, 14, on ? 2.4f : 1.6f);
        drawLed (g, c, 2.6f, col, 1.0f);
        auto lb = juce::Rectangle<float> (c.x - 50, c.y < top() + 40 ? c.y + 14 : c.y - 34, 100, 20);
        lb.setX (juce::jlimit (2.0f, wdt - 102.0f, lb.getX()));
        g.setColour (juce::Colour (0xd004060a)); g.fillRoundedRectangle (lb, 3.0f);
        g.setColour (col); g.setFont (font (9.0f)); g.drawText (name, lb.withHeight (10.0f), juce::Justification::centred);
        g.setColour (C::text); g.setFont (mono (9.5f)); g.drawText (val, lb.withTrimmedTop (9.0f), juce::Justification::centred);
    };
    node ({ fToX (p.lowF), wToY (p.lowW) }, LOWC, "LOW", hzText (p.lowF) + "  " + juce::String (juce::roundToInt (p.lowW * 100)) + "%", hover == Low || drag == Low);
    node ({ fToX (p.highF), wToY (p.highW) }, HIGHC, "HIGH", hzText (p.highF) + "  " + juce::String (juce::roundToInt (p.highW * 100)) + "%", hover == High || drag == High);
    {
        const float x = fToX (juce::jmax (20.0f, hpf)), y = (float) getHeight() - 12.0f;
        const bool on = hover == Hpf || drag == Hpf;
        juce::Path tri; tri.addTriangle (x - 7, y + 6, x + 7, y + 6, x, y - 7);
        g.setColour (HPFC.withAlpha (hpf >= 15.0f ? 1.0f : 0.45f)); g.fillPath (tri);
        if (on) { g.setColour (juce::Colours::white); g.strokePath (tri, juce::PathStrokeType (1.2f)); }
        g.setFont (font (8.5f)); g.setColour (HPFC);
        g.drawText (hpf >= 15.0f ? "HPF " + hzText (hpf) : juce::String ("HPF"), (int) x + 9, (int) y - 8, 80, 12, juce::Justification::centredLeft);
    }
    // legend
    g.setFont (font (8.5f));
    g.setColour (C::violet.withAlpha (0.8f)); g.drawText ("MID", 8, 4, 30, 12, juce::Justification::centredLeft);
    g.setColour (C::cyan); g.drawText ("SIDE OUT", 38, 4, 60, 12, juce::Justification::centredLeft);
    g.setColour (C::text.withAlpha (0.5f)); g.drawText ("SIDE IN", 100, 4, 60, 12, juce::Justification::centredLeft);
    g.setColour (C::text.withAlpha (0.85f)); g.drawText ("WIDTH", 162, 4, 60, 12, juce::Justification::centredLeft);
}

//==============================================================================
void CorrStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, MAIN);
    const float w = r.getWidth(), h = r.getHeight() - 14.0f, mid = h * 0.5f + 2.0f;
    auto fToX = [w] (float f) { return w * std::log10 (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / 3.0f; };
    g.setColour (C::text.withAlpha (0.15f)); g.fillRect (0.0f, mid, w, 1.0f);
    g.setFont (font (8.0f)); g.setColour (C::dim);
    g.drawText ("+1", 2, 1, 20, 10, juce::Justification::centredLeft);
    g.drawText ("-1", 2, (int) h - 8, 20, 10, juce::Justification::centredLeft);
    float emax = 1e-12f; for (float e : an.energy) emax = juce::jmax (emax, e);
    for (int b = 0; b < Analysis::kBands; ++b)
    {
        const float fc = Analysis::bandCentre (b);
        if (fc > 20000.0f) break;
        const float x0 = fToX (fc / std::pow (2.0f, 1.0f / 6.0f)) + 1.5f, x1 = fToX (fc * std::pow (2.0f, 1.0f / 6.0f)) - 1.5f;
        const float active = an.energy[b] / emax > 1e-5f ? 1.0f : 0.25f;
        const float ci = an.corrIn[b], co = an.corrOut[b];
        const float yi = mid - ci * (h * 0.5f - 3.0f), yo = mid - co * (h * 0.5f - 3.0f);
        g.setColour (C::text.withAlpha (0.18f * active)); g.fillRect (x0, juce::jmin (mid, yi), x1 - x0, std::abs (yi - mid));
        const auto col = corrColour (co);
        g.setColour (col.withAlpha (0.25f * active)); g.fillRect (x0 + 2, juce::jmin (mid, yo) - 1, x1 - x0 - 4, std::abs (yo - mid) + 2);
        g.setColour (col.withAlpha (0.85f * active)); g.fillRect (x0 + 3, juce::jmin (mid, yo), x1 - x0 - 6, std::abs (yo - mid));
    }
    g.setColour (C::dim.withAlpha (0.8f)); g.setFont (font (8.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), (int) fToX (f) - 12, (int) r.getHeight() - 12, 24, 10, juce::Justification::centred);
}

//==============================================================================
LumenMonoFilterEditor::LumenMonoFilterEditor (LumenMonoFilterProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    win.resize ((size_t) Analysis::kN);
    for (int i = 0; i < Analysis::kN; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) Analysis::kN);
    for (auto& b : buf) b.assign ((size_t) Analysis::kN * 2, 0.0f);
    for (auto* a : { an.midOut, an.sideOut, an.sideIn }) std::fill (a, a + Analysis::kN / 2, -120.0f);
    std::fill (std::begin (an.corrIn), std::end (an.corrIn), 1.0f); std::fill (std::begin (an.corrOut), std::end (an.corrOut), 1.0f);

    graph = std::make_unique<WidthGraph> (proc, an); addAndMakeVisible (*graph); graph->setBounds (26, 96, 740, 296);
    strip = std::make_unique<CorrStrip> (an); addAndMakeVisible (*strip); strip->setBounds (26, 432, 740, 64);

    auto* md = add<Combo> (*this, owned, st, IDs::mode, MAIN); md->setBounds (800, 98, 128, 22);
    md->setTooltip ("Zero Latency: minimum-phase filters, for tracking and sound design.  Linear Phase: the exact curve with no phase shift at all (mastering, adds latency)");
    auto* hq = add<Toggle> (*this, owned, st, IDs::hq, "HQ", MAIN); hq->setBounds (934, 98, 62, 22);
    hq->setTooltip ("Linear Phase only: twice as long FIR - steeper, more exact curve at very low frequencies (more latency)");
    auto* src = add<Combo> (*this, owned, st, IDs::source, MAIN); src->setBounds (1002, 98, 88, 22);
    src->setTooltip ("Stereo, or use only the Left / Right channel (e.g. one mic of a pair)");

    struct K { const char* id; const char* lb; juce::Colour c; bool bi; int x, y; const char* tip; };
    const K ks[] {
        { "low_freq", "LOW FREQ", LOWC, false, 796, 130, "Below this frequency the stereo width is LOW WIDTH (0% = perfectly mono)" },
        { "low_width", "LOW WIDTH", LOWC, false, 872, 130, "Width of everything below the LOW node. 0% = mono, 100% = untouched" },
        { "high_freq", "HIGH FREQ", HIGHC, false, 948, 130, "Above this frequency the width is HIGH WIDTH. The curve between the nodes is smooth" },
        { "high_width", "HIGH WIDTH", HIGHC, false, 1024, 130, "Width above the HIGH node. 100% = untouched, up to 200%" },
        { "hpf", "HPF", HPFC, false, 796, 236, "High-pass filter (24 dB/oct) for rumble and DC - removed from mid AND side" },
        { "align_time", "ALIGN TIME", C::amber, true, 872, 236, "Manual L/R time offset of the low band (when AUTO is off)" },
    };
    for (auto& k : ks) { auto* kn = add<Knob> (*this, owned, st, k.id, k.lb, k.c, k.bi); kn->setBounds (k.x, k.y, 74, 96); kn->slider.setTooltip (k.tip); }
    auto* al = add<Toggle> (*this, owned, st, IDs::align, "PHASE ALIGN", C::amber); al->setBounds (958, 240, 132, 24);
    al->setTooltip ("Time- and polarity-aligns the left and right LOW band BEFORE it is made mono - phasey stereo bass sums without losing level");
    auto* au = add<Toggle> (*this, owned, st, IDs::autoAlign, "AUTO TRACK", C::amber); au->setBounds (958, 270, 132, 24);
    au->setTooltip ("Continuously measures the L/R offset (+-3 ms) and polarity of the lows and follows it - works in BOTH phase modes");
    auto* fl = add<Toggle> (*this, owned, st, IDs::flip, "FLIP R POLARITY", C::amber); fl->setBounds (958, 300, 132, 24);
    fl->setTooltip ("Manual polarity flip of the right channel's low band (when AUTO is off)");

    const char* bid[] = { "mid_trim", "side_trim", "out" };
    const char* blb[] = { "MID TRIM", "SIDE TRIM", "OUTPUT" };
    for (int i = 0; i < 3; ++i) add<Knob> (*this, owned, st, bid[i], blb[i], MAIN, true)->setBounds (28 + i * 80, 534, 78, 100);
    auto* at = add<Toggle> (*this, owned, st, IDs::autoTrim, "AUTO TRIM", MAIN); at->setBounds (272, 540, 120, 24);
    at->setTooltip ("Keeps the loudness the same when the width changes - judge the width, not the level");

    const char* mn[] = { "FULL", "LOW", "HIGH", "MONO", "SIDE" };
    const char* mt[] = { "Normal output", "Solo the band below the crossover (between the nodes)", "Solo the band above the crossover",
                         "Listen in mono - check what survives on club / phone speakers", "Listen to the side signal only - hear what is stereo" };
    for (int i = 0; i < 5; ++i)
    {
        monB[i].setButtonText (mn[i]); monB[i].setTooltip (mt[i]);
        monB[i].onClick = [this, i] { proc.setParam (IDs::monitor, (float) i); };
        addAndMakeVisible (monB[i]); monB[i].setBounds (272 + i * 76, 600, 72, 26);
    }

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setColour (juce::ComboBox::textColourId, MAIN);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
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
    copyB.setTooltip ("Copy the current setting to the other slot");
    copyB.setColour (juce::TextButton::buttonColourId, C::dim);

    goalBox.addItemList ({ "Master", "Bass / 808", "Recording" }, 1);
    goalBox.setSelectedId (1, juce::dontSendNotification);
    goalBox.setColour (juce::ComboBox::textColourId, C::green);
    addAndMakeVisible (goalBox); goalBox.setBounds (900, 536, 96, 26);
    focusB.setColour (juce::TextButton::buttonColourId, C::green);
    focusB.setTooltip ("Play the track: AUTO FOCUS measures the width, the correlation per 1/3 octave and the L/R offset of the lows for 5 s, "
                       "then places the nodes, HPF and phase align for the goal");
    focusB.onClick = [this] { proc.startFocus ((LumenMonoFilterProcessor::Goal) (goalBox.getSelectedId() - 1)); };
    addAndMakeVisible (focusB); focusB.setBounds (1000, 535, 92, 28);

    setSize (W, HH);
    startTimerHz (30);
}

LumenMonoFilterEditor::~LumenMonoFilterEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenMonoFilterEditor::analyse()
{
    constexpr int N = Analysis::kN;
    const int pos = proc.ringPos.load (std::memory_order_acquire);
    for (int c = 0; c < 4; ++c)
    {
        auto& b = buf[c];
        for (int i = 0; i < N; ++i) b[(size_t) i] = proc.ring[c][(pos - N + i + LumenMonoFilterProcessor::kRing) & (LumenMonoFilterProcessor::kRing - 1)] * win[(size_t) i];
        std::fill (b.begin() + N, b.end(), 0.0f);
        fft.performRealOnlyForwardTransform (b.data());
    }
    const double fs = proc.getFs();
    const float norm = 4.0f / (float) N;
    double acc[2][Analysis::kBands][3] {};
    for (int k = 1; k < N / 2; ++k)
    {
        const float lir = buf[0][(size_t) (2 * k)], lii = buf[0][(size_t) (2 * k + 1)], rir = buf[1][(size_t) (2 * k)], rii = buf[1][(size_t) (2 * k + 1)];
        const float lor = buf[2][(size_t) (2 * k)], loi = buf[2][(size_t) (2 * k + 1)], ror = buf[3][(size_t) (2 * k)], roi = buf[3][(size_t) (2 * k + 1)];
        auto db = [norm] (float re, float im) { return juce::Decibels::gainToDecibels (std::sqrt (re * re + im * im) * norm, -120.0f); };
        auto smooth = [] (float& s, float v) { s += (v - s) * (v > s ? 0.6f : 0.15f); };
        smooth (an.midOut[k], db (0.5f * (lor + ror), 0.5f * (loi + roi)));
        smooth (an.sideOut[k], db (0.5f * (lor - ror), 0.5f * (loi - roi)));
        smooth (an.sideIn[k], db (0.5f * (lir - rir), 0.5f * (lii - rii)));
        const double f = k * fs / N;
        const int band = (int) std::floor (3.0 * std::log2 (f / 20.0) + 0.0);
        if (band < 0 || band >= Analysis::kBands) continue;
        acc[0][band][0] += (double) lir * lir + (double) lii * lii; acc[0][band][1] += (double) rir * rir + (double) rii * rii; acc[0][band][2] += (double) lir * rir + (double) lii * rii;
        acc[1][band][0] += (double) lor * lor + (double) loi * loi; acc[1][band][1] += (double) ror * ror + (double) roi * roi; acc[1][band][2] += (double) lor * ror + (double) loi * roi;
    }
    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < Analysis::kBands; ++b)
        {
            for (int j = 0; j < 3; ++j) cs[s][b][j] = cs[s][b][j] * 0.8 + acc[s][b][j];
            const double d = std::sqrt (cs[s][b][0] * cs[s][b][1]);
            const float c = d < 1e-10 ? 1.0f : (float) juce::jlimit (-1.0, 1.0, cs[s][b][2] / d);
            (s == 0 ? an.corrIn : an.corrOut)[b] = c;
            if (s == 1) an.energy[b] = (float) (cs[1][b][0] + cs[1][b][1]);
        }
}

void LumenMonoFilterEditor::timerCallback()
{
    proc.finishFocusIfReady();
    if (proc.programVersion.load() != lastProgram) { lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    analyse();
    cIn += (proc.corrIn.load() - cIn) * 0.3f; cOut += (proc.corrOut.load() - cOut) * 0.3f;
    const int mon = (int) pv (IDs::monitor);
    const juce::Colour mc[] = { MAIN, LOWC, HIGHC, C::amber, C::cyan };
    for (int i = 0; i < 5; ++i) monB[i].setColour (juce::TextButton::buttonColourId, i == mon ? mc[i] : C::dim);
    aB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 0 ? MAIN : C::dim);
    bB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 1 ? MAIN : C::dim);
    focusB.setEnabled (! proc.isFocusing());
    repaint();
}

void LumenMonoFilterEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (LOWC.withAlpha (0.0f), 0, 64, HIGHC.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.25, LOWC.withAlpha (0.75f)); line.addColour (0.75, HIGHC.withAlpha (0.75f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (MAIN.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (LOWC, 20, 0, HIGHC, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 146.0f, 18.0f, 220.0f, 28.0f }, "MONOFILTER", MAIN, 1.85f, juce::Justification::centredLeft, false, 10.5f);
    }
    // header: correlation in / out
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("IN", 852, 14, 24, 12, juce::Justification::centredLeft);
    g.drawText ("OUT", 852, 36, 24, 12, juce::Justification::centredLeft);
    corrBar (g, { 878.0f, 12.0f, 180.0f, 14.0f }, cIn);
    corrBar (g, { 878.0f, 34.0f, 180.0f, 14.0f }, cOut);
    g.setFont (mono (9.5f));
    g.setColour (corrColour (cIn)); g.drawText (juce::String (cIn, 2), 1060, 12, 46, 14, juce::Justification::centredRight);
    g.setColour (corrColour (cOut)); g.drawText (juce::String (cOut, 2), 1060, 34, 46, 14, juce::Justification::centredRight);

    const bool linear = pv (IDs::mode) > 0.5f;
    panel (g, { 16.0f, 72.0f, 760.0f, 330.0f }, "STEREO WIDTH BY FREQUENCY", MAIN);
    g.setFont (font (9.0f));
    {
        const juce::String lat = juce::String (proc.getLatencySamples()) + " smp / " + juce::String (proc.getLatencySamples() / proc.getFs() * 1000.0, 1) + " ms";
        g.setColour (linear ? (proc.isFirReady() ? C::green : C::amber) : C::dim);
        g.drawText (linear ? (proc.isFirReady() ? "LINEAR PHASE  " + lat : juce::String ("DESIGNING FIR...")) : "ZERO LATENCY" + (pv (IDs::align) > 0.5f ? "  (+ALIGN " + lat + ")" : juce::String()),
                    470, 77, 296, 16, juce::Justification::centredRight);
    }
    panel (g, { 16.0f, 408.0f, 760.0f, 94.0f }, "CORRELATION BY FREQUENCY", MAIN);
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("grey = input   colour = output   (red = cancels in mono)", 300, 413, 466, 16, juce::Justification::centredRight);

    // nodes & phase
    panel (g, { 784.0f, 72.0f, 320.0f, 430.0f }, "NODES / PHASE", MAIN);
    {
        auto lr = juce::Rectangle<float> (800.0f, 340.0f, 290.0f, 44.0f);
        lcd (g, lr, C::amber);
        const bool alignOn = pv (IDs::align) > 0.5f, autoOn = pv (IDs::autoAlign) > 0.5f;
        g.setFont (font (8.5f)); g.setColour (C::dim);
        g.drawText ("L/R OFFSET", (int) lr.getX() + 8, (int) lr.getY() + 4, 80, 10, juce::Justification::centredLeft);
        g.drawText ("POLARITY", (int) lr.getX() + 118, (int) lr.getY() + 4, 70, 10, juce::Justification::centredLeft);
        g.drawText ("CONFIDENCE", (int) lr.getX() + 200, (int) lr.getY() + 4, 80, 10, juce::Justification::centredLeft);
        const float ms = autoOn ? proc.alignNow.load() : pv (IDs::alignMs);
        const bool flip = autoOn ? proc.polNow.load() < 0.0f : pv (IDs::flip) > 0.5f;
        const auto col = alignOn ? C::amber : C::dim;
        g.setFont (mono (15.0f)); g.setColour (col); g.drawText ((ms >= 0 ? "+" : "") + juce::String (ms, 2) + " ms", juce::Rectangle<float> (lr.getX() + 8, lr.getY() + 16, 104, 22), juce::Justification::centredLeft);
        status.draw (g, { lr.getX() + 116, lr.getY() + 16, 70, 22 }, flip ? "FLIP" : "NORM", flip && alignOn ? C::red : col, 1.6f, juce::Justification::centredLeft, false, 9.0f);
        const float conf = juce::jlimit (0.0f, 1.0f, proc.confNow.load());
        for (int k = 0; k < 10; ++k) drawLed (g, { lr.getX() + 206.0f + k * 8.0f, lr.getY() + 28.0f }, 2.4f, autoOn && alignOn ? C::green : C::dim, conf * 10.0f > (float) k ? 1.0f : 0.1f);
    }
    g.setFont (font (9.5f)); g.setColour (C::dim);
    g.drawFittedText ("Classic mono-bass filters just sum L+R - if the two sides are out of time or polarity the bass gets thinner. "
                      "PHASE ALIGN lines the left and right lows up first (AUTO follows it live), so the mono bass keeps its full weight.",
                      juce::Rectangle<int> (800, 394, 290, 100), juce::Justification::topLeft, 7);

    // output / monitor / auto focus
    panel (g, { 16.0f, 510.0f, 1088.0f, 158.0f }, "OUTPUT / LISTEN / AUTO FOCUS", MAIN);
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("LISTEN", 272, 584, 80, 12, juce::Justification::centredLeft);
    if (pv (IDs::autoTrim) > 0.5f)
    {
        g.setFont (mono (10.0f)); g.setColour (MAIN);
        g.drawText ("TRIM " + juce::String (proc.trimNow.load(), 1) + " dB", 400, 540, 110, 24, juce::Justification::centredLeft);
    }
    g.setColour (C::border); g.fillRect (664, 524, 1, 132);
    g.setColour (C::border); g.fillRect (884, 524, 1, 132);
    g.setFont (font (10.5f)); g.setColour (C::text.withAlpha (0.8f));
    g.drawText ("MONO COMPATIBILITY", 680, 530, 190, 14, juce::Justification::centredLeft);
    {
        g.setFont (font (8.5f)); g.setColour (C::dim);
        // how much level survives the mono sum (output): 10*log10(|M|^2 / (|M|^2+|S|^2)) relative to stereo
        const float sumLoss = 10.0f * std::log10 (juce::jmax (1e-6f, 0.5f * (1.0f + cOut)));
        const float sumLossIn = 10.0f * std::log10 (juce::jmax (1e-6f, 0.5f * (1.0f + cIn)));
        g.drawText ("MONO SUM LOSS  IN", 680, 554, 120, 12, juce::Justification::centredLeft);
        g.drawText ("OUT", 680, 576, 120, 12, juce::Justification::centredLeft);
        g.setFont (mono (11.0f));
        g.setColour (corrColour (cIn)); g.drawText (juce::String (sumLossIn, 1) + " dB", 790, 552, 80, 16, juce::Justification::centredRight);
        g.setColour (corrColour (cOut)); g.drawText (juce::String (sumLoss, 1) + " dB", 790, 574, 80, 16, juce::Justification::centredRight);
        g.setFont (font (8.5f)); g.setColour (C::dim);
        g.drawFittedText ("0 dB = nothing lost in mono. Below -3 dB parts of the mix cancel on mono systems.", juce::Rectangle<int> (680, 600, 190, 50), juce::Justification::topLeft, 3);
    }
    g.setFont (font (11.0f)); g.setColour (C::green.interpolatedWith (juce::Colours::white, 0.3f));
    g.drawText ("AUTO FOCUS", 900, 518, 160, 16, juce::Justification::centredLeft);
    auto sr = juce::Rectangle<float> (900.0f, 570.0f, 192.0f, 26.0f);
    lcd (g, sr, C::green);
    if (proc.isFocusing())
    {
        const float pr = proc.focusProgress();
        for (int k = 0; k < 20; ++k) drawLed (g, { sr.getX() + 9.0f + k * 9.2f, sr.getCentreY() }, 2.6f, C::green, pr * 20.0f > (float) k ? 1.0f : 0.1f);
    }
    else
        status.draw (g, sr.reduced (4.0f, 2.0f), proc.getFocusResult().isEmpty() ? juce::String ("READY") : juce::String ("APPLIED"), C::green, 2.0f, juce::Justification::centred, false, 10.0f);
    g.setFont (font (9.0f));
    g.setColour (proc.getFocusResult().isNotEmpty() && ! proc.isFocusing() ? C::green.withAlpha (0.85f) : C::dim);
    g.drawFittedText (proc.getFocusResult().isNotEmpty() && ! proc.isFocusing() ? proc.getFocusResult()
                      : juce::String ("Pick a goal, press AUTO FOCUS and play 5 s of the track."),
                      juce::Rectangle<int> (900, 602, 192, 56), juce::Justification::topLeft, 4);
}
