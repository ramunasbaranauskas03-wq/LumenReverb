#include "PluginEditor.h"
using namespace lumen::led;
using namespace lumen::eq2;

static constexpr int W = 1240, HH = 780;
static juce::Colour bandColour (int b)
{
    static const juce::Colour c[12] { juce::Colour (0xff4fa3ff), juce::Colour (0xffe6a440), juce::Colour (0xff52c78c), juce::Colour (0xffa293ff), juce::Colour (0xffe6873f),
                                      juce::Colour (0xff35c9d6), juce::Colour (0xffe05a8a), juce::Colour (0xffc7d052), juce::Colour (0xff8f84ff), juce::Colour (0xff4fd1a5),
                                      juce::Colour (0xffd98c5f), juce::Colour (0xff6fb6ff) };
    return c[b % 12];
}
static const char* chanShort[] { "", "M", "S", "L", "R" };

// ================================================================================================ EqDisplay
EqDisplay::EqDisplay (LumenEQProcessor& p, LumenEQEditor& e, const EqSpectra& s) : proc (p), ed (e), sp (s)
{
    setTooltip ("Double-click to add a band.  Drag a band: frequency / gain (Shift = fine).  Mouse wheel on a band: Q.  "
                "Right-click a band: shape, slope, stereo placement, dynamic, solo, delete.  Alt-click a band: delete.");
}

juce::Point<float> EqDisplay::nodePos (int b) const
{
    const auto bp = proc.band (b);
    const float g = bp.hasGain() ? bp.gain * proc.P (IDs::scale) : 0.0f;
    return { fx (bp.freq), gy (g) };
}
int EqDisplay::hitNode (juce::Point<float> pt) const
{
    int best = -1; float bd = 12.0f;
    for (int b = 0; b < kBands; ++b) if (proc.P (IDs::used (b)) > 0.5f) { const float d = pt.getDistanceFrom (nodePos (b)); if (d < bd) { bd = d; best = b; } }
    return best;
}

void EqDisplay::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    const int h = hitNode (e.position); if (h != hover) hover = h;
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
    repaint();
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h < 0) { ed.select (-1); return; }
    if (e.mods.isAltDown()) { proc.removeBand (h); ed.select (-1); return; }
    ed.select (h);
    if (e.mods.isPopupMenu()) { showBandMenu (h); return; }
    drag = h; dragStart = e.position; startF = proc.P (IDs::freq (h)); startG = proc.P (IDs::gain (h));
    for (auto id : { IDs::freq (h), IDs::gain (h) }) proc.apvts.getParameter (id)->beginChangeGesture();
}
void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    const float fine = e.mods.isShiftDown() ? 0.15f : 1.0f;
    auto set = [&] (const juce::String& id, float v) { if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id))) rp->setValueNotifyingHost (rp->convertTo0to1 (v)); };
    const float x = dragStart.x + (e.position.x - dragStart.x) * fine, y = dragStart.y + (e.position.y - dragStart.y) * fine;
    set (IDs::freq (drag), (float) juce::jlimit (10.0, 30000.0, xf (x)));
    const auto bp = proc.band (drag);
    if (bp.hasGain())
    {
        const float sc = std::abs (proc.P (IDs::scale)) > 0.01f ? proc.P (IDs::scale) : 1.0f;
        set (IDs::gain (drag), juce::jlimit (-30.0f, 30.0f, yg (y) / sc));
    }
    mousePos = e.position;
    repaint();
}
void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (drag >= 0) for (auto id : { IDs::freq (drag), IDs::gain (drag) }) proc.apvts.getParameter (id)->endChangeGesture();
    drag = -1;
}
void EqDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h >= 0) { proc.setParam (IDs::on (h), proc.P (IDs::on (h)) > 0.5f ? 0.0f : 1.0f); return; }
    const float f = (float) xf (e.position.x);
    const int type = f < 35 ? LowCut : f > 16000 ? HighCut : Bell;
    const int b = proc.addBand (type, f, type == Bell ? yg (e.position.y) : 0.0f, type == Bell ? 1.0f : 0.71f);
    if (b >= 0) ed.select (b); else ed.showMessage ("All 24 bands are in use");
}
void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int h = hitNode (e.position); if (h < 0) h = ed.selected();
    if (h < 0) return;
    const float q = proc.P (IDs::q (h));
    proc.setParam (IDs::q (h), juce::jlimit (0.025f, 40.0f, q * std::pow (1.25f, w.deltaY * 4.0f)));
}

void EqDisplay::showBandMenu (int b)
{
    juce::PopupMenu m, tm, sm, cm;
    const int type = (int) proc.P (IDs::type (b)), slope = (int) proc.P (IDs::slope (b)), chan = (int) proc.P (IDs::chan (b));
    for (int i = 0; i < kNumTypes; ++i) tm.addItem (100 + i, typeNames()[i], true, i == type);
    for (int i = 0; i < 8; ++i) sm.addItem (200 + i, slopeNames()[i], true, i == slope);
    for (int i = 0; i < kNumChans; ++i) cm.addItem (300 + i, chanNames()[i], true, i == chan);
    m.addSectionHeader ("BAND " + juce::String (b + 1));
    m.addSubMenu ("Shape", tm); m.addSubMenu ("Slope", sm); m.addSubMenu ("Stereo Placement", cm);
    const bool dyn = std::abs (proc.P (IDs::dyn (b))) > 0.05f;
    m.addItem (400, dyn ? "Make Static" : "Make Dynamic");
    m.addItem (401, "Detect from Side-chain", true, proc.P (IDs::sc (b)) > 0.5f);
    m.addSeparator();
    m.addItem (402, proc.P (IDs::on (b)) > 0.5f ? "Bypass Band" : "Enable Band");
    m.addItem (403, "Solo Band", true, proc.soloBand.load() == b);
    m.addItem (404, "Delete Band");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, b] (int r)
    {
        if (r >= 100 && r < 100 + kNumTypes) proc.setParam (IDs::type (b), (float) (r - 100));
        else if (r >= 200 && r < 208) proc.setParam (IDs::slope (b), (float) (r - 200));
        else if (r >= 300 && r < 300 + kNumChans) proc.setParam (IDs::chan (b), (float) (r - 300));
        else if (r == 400) proc.setParam (IDs::dyn (b), std::abs (proc.P (IDs::dyn (b))) > 0.05f ? 0.0f : -6.0f);
        else if (r == 401) proc.setParam (IDs::sc (b), proc.P (IDs::sc (b)) > 0.5f ? 0.0f : 1.0f);
        else if (r == 402) proc.setParam (IDs::on (b), proc.P (IDs::on (b)) > 0.5f ? 0.0f : 1.0f);
        else if (r == 403) proc.soloBand.store (proc.soloBand.load() == b ? -1 : b);
        else if (r == 404) { proc.removeBand (b); ed.select (-1); }
    });
}

void EqDisplay::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    g.setColour (C::field); g.fillRoundedRectangle (all, 6.0f);
    g.setColour (C::border); g.drawRoundedRectangle (all.reduced (0.5f), 6.0f, 1.0f);
    auto r = plot();
    const double fs = proc.sampleRateNow(), nyq = fs * 0.5;
    // grid
    g.setFont (mono (9.0f));
    for (double f : { 20.0, 30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 800.0, 1000.0, 2000.0, 3000.0, 4000.0, 5000.0, 6000.0, 8000.0, 10000.0, 20000.0 })
    {
        const float x = fx (f); const auto fi = juce::roundToInt (f); const bool major = fi == 20 || fi == 50 || fi == 100 || fi == 200 || fi == 500 || fi == 1000 || fi == 2000 || fi == 5000 || fi == 10000 || fi == 20000;
        g.setColour (C::border.withAlpha (major ? 0.7f : 0.3f)); g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        if (major) { g.setColour (C::dim); g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) x - 18, (int) r.getBottom() + 3, 36, 12, juce::Justification::centred); }
    }
    for (float db = -range; db <= range + 0.01f; db += range / (range > 12 ? 6.0f : 4.0f))
    {
        const float y = gy (db);
        g.setColour (std::abs (db) < 0.01f ? C::dim.withAlpha (0.6f) : C::border.withAlpha (0.4f)); g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (C::dim); g.drawText ((db > 0 ? "+" : "") + juce::String ((int) std::round (db)), (int) r.getRight() + 2, (int) y - 6, 30, 12, juce::Justification::centredLeft);
    }
    if (nyq < 30000) { g.setColour (C::bg.withAlpha (0.6f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (fx (nyq), r.getY(), r.getRight(), r.getBottom())); }

    // analyzer
    auto spec = [&] (const float* s)
    {
        juce::Path p; bool st = false; const int nb = EqSpectra::kN / 2;
        for (float x = r.getX(); x <= std::min (r.getRight(), fx (nyq)); x += 2.0f)
        {
            const float b0 = (float) (xf (x - 1) * EqSpectra::kN / sp.fs), b1 = (float) (xf (x + 1) * EqSpectra::kN / sp.fs);
            float v;
            if (b1 - b0 < 1.0f) { const float bc = juce::jlimit (1.0f, (float) (nb - 2), 0.5f * (b0 + b1)); const int k = (int) bc; v = s[k] + (s[k + 1] - s[k]) * (bc - (float) k); }
            else { v = -200; for (int k = juce::jlimit (1, nb - 1, (int) b0); k <= juce::jlimit (1, nb - 1, (int) b1); ++k) v = std::max (v, s[k]); }
            // +4.5 dB / octave tilt (like a pink-noise reference: a balanced mix looks flat)
            v += 4.5f * (float) std::log2 (xf (x) / 1000.0);
            if (! st) { p.startNewSubPath (x, sy (v)); st = true; } else p.lineTo (x, sy (v));
        }
        return p;
    };
    if (showPre) { auto p = spec (sp.pre); g.setColour (C::text.withAlpha (0.22f)); g.strokePath (p, juce::PathStrokeType (1.0f)); }
    if (showPost)
    {
        auto p = spec (sp.post); juce::Path f (p); f.lineTo (std::min (r.getRight(), fx (nyq)), r.getBottom()); f.lineTo (r.getX(), r.getBottom()); f.closeSubPath();
        g.setGradientFill (juce::ColourGradient (C::cyan.withAlpha (0.20f), 0, r.getY(), C::cyan.withAlpha (0.04f), 0, r.getBottom(), false)); g.fillPath (f);
        g.setColour (C::cyan.withAlpha (0.45f)); g.strokePath (p, juce::PathStrokeType (1.0f));
    }
    if (showSc && proc.hasSidechainSignal())
    {
        auto p = spec (sp.sc); g.setColour (C::orange.withAlpha (0.7f)); g.strokePath (p, juce::PathStrokeType (1.2f));
        // collision zones: both loud at the same frequency
        const int nb = EqSpectra::kN / 2;
        for (float x = r.getX(); x <= std::min (r.getRight(), fx (8000)); x += 3.0f)
        {
            const int k = juce::jlimit (1, nb - 1, (int) (xf (x) * EqSpectra::kN / sp.fs));
            const float a = sp.pre[k], b = sp.sc[k];
            if (a > -60 && b > -60 && std::abs (a - b) < 9)
            {
                const float sev = juce::jlimit (0.0f, 1.0f, (std::min (a, b) + 60) / 40.0f);
                g.setColour (C::red.withAlpha (0.12f + 0.35f * sev)); g.fillRect (x, r.getBottom() - 8 - 30 * sev, 3.0f, 8 + 30 * sev);
            }
        }
    }

    // curves
    const auto bands = proc.bands(); const float scale = proc.P (IDs::scale);
    const int sel = ed.selected();
    for (int b = 0; b < kBands; ++b)
    {
        const auto& raw = bands[(size_t) b]; if (! raw.used) continue;
        const auto bp = scaled (raw, scale);
        const auto d = design (bp.type, fs, bp.freq, bp.gain, bp.q, bp.slope, false);
        const auto col = bandColour (b);
        juce::Path p, f; bool st = false;
        for (float x = r.getX(); x <= std::min (r.getRight(), fx (nyq)); x += 2.0f)
        {
            const float y = gy ((float) designMagDb (d, fs, xf (x)));
            if (! st) { p.startNewSubPath (x, y); f.startNewSubPath (x, gy (0)); st = true; }
            p.lineTo (x, y); f.lineTo (x, y);
        }
        f.lineTo (std::min (r.getRight(), fx (nyq)), gy (0)); f.closeSubPath();
        const float a = ! raw.on ? 0.04f : b == sel ? 0.22f : b == hover ? 0.16f : 0.08f;
        g.setColour (col.withAlpha (a)); g.fillPath (f);
        if (b == sel || b == hover) { g.setColour (col.withAlpha (raw.on ? 0.8f : 0.3f)); g.strokePath (p, juce::PathStrokeType (1.2f)); }
        // live dynamic curve
        const float dg = proc.dynGain[b].load();
        if (raw.isDynamic() && std::abs (dg) > 0.05f)
        {
            const auto dd = design (bp.type, fs, bp.freq, bp.gain + dg, bp.q, bp.slope, false);
            juce::Path q; bool s2 = false;
            for (float x = r.getX(); x <= std::min (r.getRight(), fx (nyq)); x += 3.0f) { const float y = gy ((float) designMagDb (dd, fs, xf (x))); if (! s2) { q.startNewSubPath (x, y); s2 = true; } else q.lineTo (x, y); }
            g.setColour (col.withAlpha (0.9f)); g.strokePath (q, juce::PathStrokeType (1.6f));
        }
    }
    // total response (stereo)
    {
        juce::Path p; bool st = false;
        StaticCurve curve; curve.set (bands, scale, fs, false);
        for (float x = r.getX(); x <= std::min (r.getRight(), fx (nyq)); x += 1.5f)
        {
            const auto m = curve.at (xf (x));
            const double pw = 0.5 * (m.h11 * m.h11 + m.h12 * m.h12 + m.h21 * m.h21 + m.h22 * m.h22);
            const float y = gy ((float) (10 * std::log10 (pw + 1e-12)));
            if (! st) { p.startNewSubPath (x, y); st = true; } else p.lineTo (x, y);
        }
        glowPath (g, p, C::text, 2.0f, 1.0f);
    }
    // nodes
    for (int b = 0; b < kBands; ++b)
    {
        const auto& raw = bands[(size_t) b]; if (! raw.used) continue;
        const auto pt = nodePos (b); const auto col = bandColour (b);
        const bool hot = b == hover || b == drag, isSel = b == sel;
        const float rad = hot || isSel ? 9.0f : 7.5f;
        if (raw.isDynamic())
        {
            // dynamic range bar from the static gain to gain + range
            const float y2 = gy ((raw.gain + raw.dynRange) * scale);
            g.setColour (col.withAlpha (0.35f)); g.fillRoundedRectangle (pt.x - 2.5f, std::min (pt.y, y2), 5.0f, std::abs (y2 - pt.y), 2.5f);
            const float yd = gy ((raw.gain + proc.dynGain[b].load()) * scale);
            g.setColour (col); g.fillEllipse (pt.x - 3.5f, yd - 3.5f, 7, 7);
        }
        g.setColour (col.withAlpha (raw.on ? (isSel ? 1.0f : 0.85f) : 0.3f)); g.fillEllipse (pt.x - rad, pt.y - rad, 2 * rad, 2 * rad);
        if (isSel) { g.setColour (C::text); g.drawEllipse (pt.x - rad - 2, pt.y - rad - 2, 2 * rad + 4, 2 * rad + 4, 1.4f); }
        g.setColour (C::bg); g.setFont (font (9.5f));
        g.drawText (juce::String (b + 1) + chanShort[raw.chan], juce::Rectangle<float> (pt.x - 12, pt.y - 7, 24, 14), juce::Justification::centred);
        if (proc.soloBand.load() == b) { g.setColour (C::amber); g.setFont (font (9.0f)); g.drawText ("SOLO", (int) pt.x - 20, (int) pt.y - 24, 40, 12, juce::Justification::centred); }
    }
    // read-out
    if (mousePos.x >= r.getX() && mousePos.y >= r.getY())
    {
        juce::String t;
        const int h = drag >= 0 ? drag : hover;
        if (h >= 0) { const auto bp = proc.band (h); t = juce::String (h + 1) + "  " + typeNames()[bp.type] + "   " + hzText (bp.freq) + " (" + noteName (bp.freq) + ")" + (bp.hasGain() ? "   " + juce::String (bp.gain, 1) + " dB" : juce::String()) + "   Q " + juce::String (bp.q, 2); }
        else t = hzText ((float) xf (mousePos.x)) + "  " + noteName ((float) xf (mousePos.x)) + "   " + juce::String (yg (mousePos.y), 1) + " dB";
        g.setColour (C::bg.withAlpha (0.85f)); g.fillRoundedRectangle (r.getX() + 8, r.getY() + 8, 330, 20, 4.0f);
        g.setColour (C::text); g.setFont (mono (10.5f)); g.drawText (t, (int) r.getX() + 14, (int) r.getY() + 8, 320, 20, juce::Justification::centredLeft);
    }
    if (proc.P (IDs::mode) > 0.5f) { g.setColour (C::amber); g.setFont (font (10.0f)); g.drawText ("LINEAR PHASE  " + juce::String (proc.getLatencySamples() * 1000.0 / fs, 0) + " ms", (int) r.getRight() - 230, (int) r.getY() + 8, 220, 16, juce::Justification::centredRight); }
    if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - msgTime < 9000)
    {
        auto m = juce::Rectangle<float> (r.getCentreX() - 330, r.getY() + 40, 660, 34);
        g.setColour (C::bg.withAlpha (0.92f)); g.fillRoundedRectangle (m, 6.0f); g.setColour (C::cyan); g.drawRoundedRectangle (m, 6.0f, 1.0f);
        g.setColour (C::text); g.setFont (font (12.0f)); g.drawFittedText (message, m.reduced (10, 2).toNearestInt(), juce::Justification::centred, 2);
    }
    bool any = false; for (auto& b : bands) any |= b.used;
    if (! any) { g.setColour (C::dim); g.setFont (font (14.0f, false)); g.drawText ("Double-click anywhere to add a band  -  or pick an EDM preset above", r.toNearestInt(), juce::Justification::centred); }
}

// ================================================================================================ Editor
LumenEQEditor::LumenEQEditor (LumenEQProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    fftBuf.assign ((size_t) EqSpectra::kN * 2, 0.0f); window.resize ((size_t) EqSpectra::kN);
    for (int i = 0; i < EqSpectra::kN; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) EqSpectra::kN);
    for (auto* a : { spectra.pre, spectra.post, spectra.sc }) std::fill (a, a + EqSpectra::kN / 2, -140.0f);

    // header
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) { proc.setCurrentProgram (i); select (-1); } };
    addAndMakeVisible (presetBox); presetBox.setBounds (200, 15, 270, 26);
    for (auto* b : { &prevB, &nextB, &abA, &abB, &abCopy, &preB, &postB, &scB, &matchB, &unmaskB, &soloB, &delB, &clearB }) { b->setColour (juce::TextButton::buttonColourId, C::cyan); addAndMakeVisible (*b); }
    prevB.setBounds (476, 15, 26, 26); nextB.setBounds (504, 15, 26, 26);
    prevB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + proc.getNumPrograms() - 1) % proc.getNumPrograms()); select (-1); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); select (-1); };
    abA.setBounds (546, 15, 30, 26); abB.setBounds (578, 15, 30, 26); abCopy.setBounds (610, 15, 52, 26);
    abA.onClick = [this] { proc.switchAB (0); }; abB.onClick = [this] { proc.switchAB (1); }; abCopy.onClick = [this] { proc.copyAB(); };
    auto mode = std::make_unique<Combo> (proc.apvts, IDs::mode, C::amber); mode->setBounds (680, 15, 140, 26);
    mode->setTooltip ("ZERO LATENCY: analog-matched filters (no cramping near 20 kHz), no delay.  LINEAR PHASE: no phase shift at all - for mastering and parallel tracks"); addAndMakeVisible (*mode); owned.push_back (std::move (mode));
    auto qual = std::make_unique<Combo> (proc.apvts, IDs::quality, C::amber); qual->setBounds (826, 15, 96, 26); qual->setTooltip ("Linear-phase resolution (higher = more accurate lows, more latency)");
    addAndMakeVisible (*qual); owned.push_back (std::move (qual));
    rangeBox.addItemList ({ "+- 3 dB", "+- 6 dB", "+- 12 dB", "+- 30 dB" }, 1); rangeBox.setSelectedId (3, juce::dontSendNotification);
    rangeBox.onChange = [this] { static const float rr[] { 3, 6, 12, 30 }; display->range = rr[juce::jlimit (0, 3, rangeBox.getSelectedId() - 1)]; display->repaint(); };
    addAndMakeVisible (rangeBox); rangeBox.setBounds (928, 15, 96, 26);
    preB.setBounds (1036, 15, 54, 26); postB.setBounds (1092, 15, 60, 26); scB.setBounds (1154, 15, 44, 26);
    for (auto* b : { &preB, &postB, &scB }) b->setClickingTogglesState (true);
    preB.setToggleState (true, juce::dontSendNotification); postB.setToggleState (true, juce::dontSendNotification); scB.setToggleState (true, juce::dontSendNotification);
    preB.setTooltip ("Analyzer: input spectrum"); postB.setTooltip ("Analyzer: output spectrum"); scB.setTooltip ("Analyzer: side-chain spectrum + collision zones (red)");
    preB.onClick = [this] { display->showPre = preB.getToggleState(); }; postB.onClick = [this] { display->showPost = postB.getToggleState(); }; scB.onClick = [this] { display->showSc = scB.getToggleState(); };

    display = std::make_unique<EqDisplay> (proc, *this, spectra); addAndMakeVisible (*display); display->setBounds (12, 62, 1216, 500);

    // global panel
    const int gx = 930, gy0 = 600;
    auto knob = [this] (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip) -> Knob&
    { auto k = std::make_unique<Knob> (proc.apvts, id, lbl, c, bip); k->setBounds (r); addAndMakeVisible (*k); auto* raw = k.get(); owned.push_back (std::move (k)); return *raw; };
    knob (IDs::scale, "GAIN SCALE", C::cyan, { gx + 0, gy0, 68, 84 }, true).setTooltip ("Scales every band's gain: 50 % = half the EQ, -100 % = inverted");
    knob (IDs::out, "OUTPUT", C::cyan, { gx + 67, gy0, 68, 84 }, true);
    knob (IDs::attack, "ATTACK", C::violet, { gx + 134, gy0, 64, 76 }, false).setTooltip ("Dynamic band attack time");
    knob (IDs::release, "RELEASE", C::violet, { gx + 200, gy0, 64, 76 }, false).setTooltip ("Dynamic band release time");
    auto ag = std::make_unique<Toggle> (proc.apvts, IDs::autoGain, "AUTO GAIN", C::green); ag->setBounds (gx + 8, gy0 + 92, 124, 26);
    ag->setTooltip ("Keeps the loudness the same when you EQ - judge the tone, not the volume"); addAndMakeVisible (*ag); owned.push_back (std::move (ag));
    matchB.setBounds (gx + 8, gy0 + 126, 124, 28); unmaskB.setBounds (gx + 138, gy0 + 126, 124, 28); clearB.setBounds (gx + 138, gy0 + 92, 124, 26);
    matchB.setTooltip ("EQ MATCH: make your track's tonal balance match a reference track (loaded file or side-chain)");
    unmaskB.setTooltip ("UNMASK: finds where the side-chain (kick, vocal) and this track fight and adds a dynamic cut that only works while the side-chain plays");
    clearB.onClick = [this] { proc.clearBands(); select (-1); };
    unmaskB.onClick = [this] { const auto m = proc.unmask(); showMessage (m); for (int b = lumen::eq2::kBands - 1; b >= 0; --b) if (proc.P (IDs::used (b)) > 0.5f && proc.P (IDs::sc (b)) > 0.5f) { select (b); break; } };
    matchB.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Match to the side-chain input (100 %)", proc.hasSidechainSignal());
        m.addItem (2, "Match to the side-chain input (50 %)", proc.hasSidechainSignal());
        m.addSeparator();
        m.addItem (3, "Load reference track...");
        m.addItem (4, "Match to the loaded reference (100 %)", proc.hasReference());
        m.addItem (5, "Match to the loaded reference (50 %)", proc.hasReference());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&matchB), [this] (int r)
        {
            if (r == 1 || r == 2) { showMessage (proc.eqMatch (true, r == 1 ? 1.0f : 0.5f, 10)); select (-1); }
            else if (r == 4 || r == 5) { showMessage (proc.eqMatch (false, r == 4 ? 1.0f : 0.5f, 10)); select (-1); }
            else if (r == 3)
            {
                chooser = std::make_unique<juce::FileChooser> ("Reference track", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                {
                    const auto f = fc.getResult(); if (! f.existsAsFile()) return;
                    juce::String err;
                    if (proc.loadMatchReference (f, err)) showMessage ("Reference loaded: " + f.getFileNameWithoutExtension() + "  -  play your track, then EQ MATCH -> Match to the loaded reference");
                    else showMessage ("Could not load: " + err);
                });
            }
        });
    };
    soloB.setClickingTogglesState (false);
    soloB.onClick = [this] { if (sel >= 0) proc.soloBand.store (proc.soloBand.load() == sel ? -1 : sel); };
    delB.onClick = [this] { if (sel >= 0) { proc.removeBand (sel); select (-1); } };
    delB.setColour (juce::TextButton::buttonColourId, C::red);

    setSize (W, HH);
    buildBandPanel();
    startTimerHz (30);
}

LumenEQEditor::~LumenEQEditor() { setLookAndFeel (nullptr); }

void LumenEQEditor::select (int b)
{
    sel = (b >= 0 && proc.P (IDs::used (b)) > 0.5f) ? b : -1;
    buildBandPanel(); display->repaint(); repaint();
}

void LumenEQEditor::buildBandPanel()
{
    if (builtFor == sel) return;
    builtFor = sel;
    for (auto& c : bandComps) removeChildComponent (c.get());
    bandComps.clear();
    soloB.setVisible (sel >= 0); delB.setVisible (sel >= 0);
    if (sel < 0) return;
    const int b = sel; const auto col = bandColour (b);
    const int py = 600;
    auto combo = [&] (const juce::String& id, juce::Rectangle<int> r) { auto c = std::make_unique<Combo> (proc.apvts, id, col); c->setBounds (r); addAndMakeVisible (*c); bandComps.push_back (std::move (c)); };
    auto knob = [&] (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip, const juce::String& tip)
    { auto k = std::make_unique<Knob> (proc.apvts, id, lbl, c, bip); k->setBounds (r); k->setTooltip (tip); addAndMakeVisible (*k); bandComps.push_back (std::move (k)); };
    auto toggle = [&] (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r, const juce::String& tip)
    { auto t = std::make_unique<Toggle> (proc.apvts, id, txt, c); t->setBounds (r); t->setTooltip (tip); addAndMakeVisible (*t); bandComps.push_back (std::move (t)); };
    combo (IDs::type (b), { 24, py + 6, 150, 26 });
    combo (IDs::slope (b), { 24, py + 38, 150, 26 });
    combo (IDs::chan (b), { 24, py + 70, 150, 26 });
    toggle (IDs::on (b), "BAND ON", col, { 24, py + 104, 150, 26 }, "Bypass / enable this band");
    soloB.setBounds (24, py + 136, 72, 26); delB.setBounds (102, py + 136, 72, 26);
    knob (IDs::freq (b), "FREQUENCY", col, { 190, py, 90, 104 }, false, "Frequency");
    knob (IDs::gain (b), "GAIN", col, { 286, py, 90, 104 }, true, "Gain");
    knob (IDs::q (b), "Q", col, { 382, py, 90, 104 }, false, "Bandwidth / resonance (also: mouse wheel on the band)");
    knob (IDs::dyn (b), "DYN RANGE", C::violet, { 500, py, 84, 98 }, true, "DYNAMIC EQ: how far the gain moves when the band's frequency gets louder than the threshold (negative = cut when loud, e.g. harshness / de-ess)");
    knob (IDs::thr (b), "THRESHOLD", C::violet, { 590, py, 84, 98 }, false, "Level (in this band) where the dynamic action starts");
    toggle (IDs::sc (b), "SIDE-CHAIN DETECT", C::orange, { 500, py + 110, 174, 26 }, "The band reacts to the side-chain input (e.g. bass cuts 60 Hz only while the kick plays)");
}

void LumenEQEditor::updateSpectra()
{
    const int N = EqSpectra::kN, pos = proc.ringPos.load (std::memory_order_acquire);
    spectra.fs = proc.sampleRateNow();
    float* dst[3] { spectra.pre, spectra.post, spectra.sc };
    for (int w = 0; w < 3; ++w)
    {
        if (w == 2 && ! proc.hasSidechainSignal()) continue;
        std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
        for (int i = 0; i < N; ++i) fftBuf[(size_t) i] = proc.ring[w][(pos - N + i) & (LumenEQProcessor::kRing - 1)] * window[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (fftBuf.data());
        const float norm = 4.0f / (float) N;
        for (int k = 1; k < N / 2; ++k)
        {
            const float db = 20.0f * std::log10 (fftBuf[(size_t) k] * norm + 1e-9f);
            dst[w][k] = db > dst[w][k] ? dst[w][k] + (db - dst[w][k]) * 0.5f : dst[w][k] + (db - dst[w][k]) * 0.12f;
        }
    }
}

void LumenEQEditor::timerCallback()
{
    updateSpectra();
    if (sel >= 0 && proc.P (IDs::used (sel)) < 0.5f) select (-1);
    inM = std::max (proc.inPk.load(), inM * 0.85f); outM = std::max (proc.outPk.load(), outM * 0.85f);
    if (lastProgram != proc.programVersion.load() + proc.getCurrentProgram() * 1000)
    { lastProgram = proc.programVersion.load() + proc.getCurrentProgram() * 1000; presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    abA.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification); abB.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    soloB.setToggleState (sel >= 0 && proc.soloBand.load() == sel, juce::dontSendNotification);
    display->repaint();
    repaint (0, 0, W, 60); repaint (0, 566, W, HH - 566);
}

void LumenEQEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    g.setColour (C::panelTop); g.fillRect (0, 0, W, 56);
    g.setColour (C::border); g.drawHorizontalLine (56, 0, (float) W);
    g.setColour (C::text); g.setFont (font (24.0f)); g.drawText ("LUMEN", 18, 12, 100, 32, juce::Justification::centredLeft);
    g.setColour (C::cyan); g.drawText ("EQ", 108, 12, 50, 32, juce::Justification::centredLeft);
    g.setColour (C::dim); g.setFont (font (9.5f, false)); g.drawText ("v2", 144, 24, 30, 14, juce::Justification::centredLeft);

    // band panel
    const bool has = sel >= 0;
    const auto col = has ? bandColour (sel) : C::dim;
    panel (g, { 12, 570, 906, 200 }, has ? "BAND " + juce::String (sel + 1) : "BAND", col, has);
    if (! has)
    {
        g.setColour (C::dim); g.setFont (font (12.0f, false));
        g.drawFittedText ("Click a band in the display to edit it.  Double-click the display to add one (up to 24).\n"
                          "Every band: 8 shapes, 6 - 96 dB/oct, Stereo / Mid / Side / Left / Right, static or DYNAMIC (also from the side-chain).",
                          juce::Rectangle<int> (30, 610, 860, 80), juce::Justification::centredLeft, 4);
    }
    else
    {
        const auto bp = proc.band (sel);
        g.setColour (C::dim); g.setFont (font (10.0f, false));
        g.drawText (noteName (bp.freq), 190, 706, 90, 14, juce::Justification::centred);
        if (bp.isDynamic())
        {
            g.setColour (C::violet); g.setFont (mono (11.0f));
            g.drawText ("dynamic now " + juce::String (proc.dynGain[sel].load(), 1) + " dB", 690, 610, 210, 18, juce::Justification::centredLeft);
        }
        g.setColour (C::dim); g.setFont (font (10.0f, false));
        g.drawFittedText (bp.type == LowCut || bp.type == HighCut ? "Slope sets the steepness; Q above 0.71 adds a resonant bump at the corner."
                        : bp.hasGain() ? "Make it dynamic with DYN RANGE: e.g. -6 dB at 3 kHz only when the lead gets harsh."
                                        : "Notch / band pass: frequency and Q.",
                          juce::Rectangle<int> (690, 640, 216, 80), juce::Justification::topLeft, 5);
    }
    panel (g, { 926, 570, 302, 200 }, "GLOBAL", C::cyan, true);
    ledMeter (g, { 1203, 600, 7, 150 }, inM, inM, true, 0); ledMeter (g, { 1214, 600, 7, 150 }, outM, outM, true, 0);
    if (pv (IDs::autoGain) > 0.5f) { g.setColour (C::green); g.setFont (mono (10.0f)); g.drawText ("auto " + juce::String (proc.autoGainNow.load(), 1) + " dB", 936, 756, 130, 12, juce::Justification::centredLeft); }
}
