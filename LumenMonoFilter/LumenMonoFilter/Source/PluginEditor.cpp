#include "PluginEditor.h"
using namespace lumen::led;

static constexpr int W = 1180, HH = 724;
static const juce::Colour bandCol[4] { C::orange, C::amber, C::green, C::violet };
static const char* bandName[4] { "LOW", "LOW-MID", "HIGH-MID", "HIGH" };
static const juce::Colour monoCol = C::cyan;

// ================================================================================================ WidthGraph
WidthGraph::WidthGraph (LumenMonoFilterProcessor& p, const Spectra& s) : proc (p), sp (s)
{
    setTooltip ("Drag the blue MONO line to set where the bass becomes mono.  Drag a band's dot up / down for its width "
                "(100 % = original, 0 % = mono, 200 % = twice as wide).  Drag the thin lines to move the crossovers.  "
                "Click a band name to solo it.  Double-click = reset.  Mouse wheel = fine width.");
}

void WidthGraph::xo (float out[3]) const
{
    lumen::mf::Params q; q.x[0] = pv (IDs::x1); q.x[1] = pv (IDs::x2); q.x[2] = pv (IDs::x3);
    lumen::mf::orderedX (q, proc.sampleRateNow(), out);
}
float WidthGraph::bandCentre (int b) const
{
    float x[3]; xo (x);
    const float lo = b == 0 ? 20.0f : x[b - 1], hi = b == 3 ? 20000.0f : x[b];
    return std::sqrt (lo * hi);
}

juce::String WidthGraph::idOf (int h) const
{
    switch (h) { case HMono: return IDs::monoFreq; case HX1: return IDs::x1; case HX2: return IDs::x2; case HX3: return IDs::x3; default: break; }
    if (h >= HB1 && h <= HB4) return IDs::w (h - HB1);
    return {};
}

int WidthGraph::hit (juce::Point<float> p) const
{
    auto r = plot();
    if (p.y < r.getY())
    {
        float x[3]; xo (x);
        for (int b = 0; b < 4; ++b)
        {
            const float lo = fToX (b == 0 ? 20.0f : x[b - 1]), hi = fToX (b == 3 ? 20000.0f : x[b]);
            if (p.x >= lo && p.x < hi) return HLabel1 + b;
        }
        return -1;
    }
    for (int b = 0; b < 4; ++b)
        if (p.getDistanceFrom ({ fToX (bandCentre (b)), wToY (pv (IDs::w (b))) }) < 10.0f) return HB1 + b;
    if (pv (IDs::monoOn) > 0.5f && std::abs (p.x - fToX (pv (IDs::monoFreq))) < 7.0f) return HMono;
    float x[3]; xo (x);
    for (int k = 0; k < 3; ++k) if (std::abs (p.x - fToX (x[k])) < 5.0f) return HX1 + k;
    // anywhere else inside a band: vertical drag sets that band's width
    for (int b = 0; b < 4; ++b)
    {
        const float lo = fToX (b == 0 ? 20.0f : x[b - 1]), hi = fToX (b == 3 ? 20000.0f : x[b]);
        if (p.x >= lo && p.x < hi) return HB1 + b;
    }
    return -1;
}

void WidthGraph::setParamValue (const juce::String& id, float v)
{
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id))) rp->setValueNotifyingHost (rp->convertTo0to1 (v));
}

void WidthGraph::mouseMove (const juce::MouseEvent& e)
{
    const int h = hit (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h == HMono || (h >= HX1 && h <= HX3) ? juce::MouseCursor::LeftRightResizeCursor
                  : h >= HB1 && h <= HB4 ? juce::MouseCursor::UpDownResizeCursor
                  : h >= HLabel1 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void WidthGraph::mouseDown (const juce::MouseEvent& e)
{
    mouseMove (e);
    if (hover >= HLabel1)
    {
        const int b = hover - HLabel1, cur = (int) pv (IDs::monitor);
        proc.setParam (IDs::monitor, cur == MonB1 + b ? (float) MonStereo : (float) (MonB1 + b));
        repaint(); return;
    }
    drag = hover;
    if (drag >= 0) if (auto* prm = proc.apvts.getParameter (idOf (drag))) prm->beginChangeGesture();
    mouseDrag (e);
}

void WidthGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0 || drag >= HLabel1) return;
    if (drag == HMono || (drag >= HX1 && drag <= HX3)) setParamValue (idOf (drag), xToF (e.position.x));
    else setParamValue (idOf (drag), yToW (e.position.y));
    repaint();
}

void WidthGraph::mouseUp (const juce::MouseEvent&)
{
    if (drag >= 0 && drag < HLabel1) if (auto* prm = proc.apvts.getParameter (idOf (drag))) prm->endChangeGesture();
    drag = -1;
}

void WidthGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hit (e.position);
    if (h < 0 || h >= HLabel1) return;
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (idOf (h))))
    { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->getDefaultValue()); rp->endChangeGesture(); }
}

void WidthGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int h = hit (e.position);
    if (h == HMono || (h >= HX1 && h <= HX3))
    {
        const auto id = idOf (h); proc.setParam (id, pv (id) * std::pow (2.0f, w.deltaY * 0.25f)); return;
    }
    if (h < HB1 || h > HB4) return;
    const auto id = idOf (h);
    proc.setParam (id, juce::jlimit (0.0f, 2.0f, pv (id) + (w.deltaY > 0 ? 0.02f : -0.02f) * (std::abs (w.deltaY) > 0.3f ? 3.0f : 1.0f)));
}

void WidthGraph::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    g.setColour (C::field); g.fillRoundedRectangle (all, 6.0f);
    g.setColour (C::border); g.drawRoundedRectangle (all.reduced (0.5f), 6.0f, 1.0f);
    auto r = plot();
    float x[3]; xo (x);
    const int monitor = (int) pv (IDs::monitor);

    // band tints + labels
    for (int b = 0; b < 4; ++b)
    {
        const float lo = fToX (b == 0 ? 20.0f : x[b - 1]), hi = fToX (b == 3 ? 20000.0f : x[b]);
        const bool solo = monitor == MonB1 + b;
        g.setColour (bandCol[b].withAlpha (solo ? 0.10f : 0.035f)); g.fillRect (lo, r.getY(), hi - lo, r.getHeight());
        auto lab = juce::Rectangle<float> (lo + 2, 4, hi - lo - 4, 22);
        if (hover == HLabel1 + b || solo) { g.setColour (bandCol[b].withAlpha (solo ? 0.25f : 0.10f)); g.fillRoundedRectangle (lab, 4.0f); }
        g.setColour (bandCol[b]); g.setFont (font (lab.getWidth() < 90 ? 9.5f : 11.0f));
        g.drawText (juce::String (bandName[b]) + "  " + juce::String (juce::roundToInt (pv (IDs::w (b)) * 100.0f)) + "%" + (solo ? "  SOLO" : ""), lab, juce::Justification::centred);
    }
    // grid
    g.setFont (mono (9.0f));
    for (float f : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const float xx = fToX (f);
        g.setColour (C::border.withAlpha (0.55f)); g.drawVerticalLine ((int) xx, r.getY(), r.getBottom());
        g.setColour (C::dim);
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) xx - 20, (int) r.getBottom() + 3, 40, 14, juce::Justification::centred);
    }
    for (float w : { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f })
    {
        const float yy = wToY (w);
        g.setColour (lumen::mf::same (w, 1.0f) ? C::dim.withAlpha (0.55f) : C::border.withAlpha (0.45f)); g.drawHorizontalLine ((int) yy, r.getX(), r.getRight());
        g.setColour (C::dim); g.drawText (juce::String ((int) (w * 100)) + "%", (int) r.getRight() - 40, (int) yy - 13, 38, 12, juce::Justification::centredRight);
    }

    // spectra: mid (grey), side out (violet fill), side in (dim line) -> you see what was removed
    {
        const int nb = Spectra::kN / 2;
        auto curve = [&] (const float* d) {
            juce::Path p; bool started = false;
            for (float xx = r.getX(); xx <= r.getRight(); xx += 2.0f)
            {
                const float f0 = xToF (xx - 1.0f), f1 = xToF (xx + 1.0f);
                const float b0 = (float) (f0 * Spectra::kN / sp.fs), b1 = (float) (f1 * Spectra::kN / sp.fs);
                float v = -200;
                if (b1 - b0 < 1.0f)
                {
                    // narrower than one bin: interpolate (smooth low end instead of stair steps)
                    const float bc = juce::jlimit (1.0f, (float) (nb - 2), 0.5f * (b0 + b1));
                    const int k = (int) bc; const float fr = bc - (float) k;
                    v = d[k] + (d[k + 1] - d[k]) * fr;
                }
                else { const int k0 = juce::jlimit (1, nb - 1, (int) b0), k1 = juce::jlimit (k0, nb - 1, (int) b1); for (int k = k0; k <= k1; ++k) v = std::max (v, d[k]); }
                const float yy = dbToY (v);
                if (! started) { p.startNewSubPath (xx, yy); started = true; } else p.lineTo (xx, yy);
            }
            return p;
        };
        auto fill = [&] (juce::Path p) { p.lineTo (r.getRight(), r.getBottom()); p.lineTo (r.getX(), r.getBottom()); p.closeSubPath(); return p; };
        auto pm = curve (sp.midOut), ps = curve (sp.sideOut), pi = curve (sp.sideIn);
        g.setColour (juce::Colour (0xff8796a8).withAlpha (0.10f)); g.fillPath (fill (pm));
        g.setColour (juce::Colour (0xff8796a8).withAlpha (0.35f)); g.strokePath (pm, juce::PathStrokeType (1.0f));
        g.setColour (C::magenta.withAlpha (0.16f)); g.fillPath (fill (ps));
        g.setColour (C::magenta.withAlpha (0.6f)); g.strokePath (ps, juce::PathStrokeType (1.1f));
        g.setColour (C::text.withAlpha (0.25f)); g.strokePath (pi, juce::PathStrokeType (0.8f, juce::PathStrokeType::curved), juce::AffineTransform());
    }

    // mono zone
    const bool monoOn = pv (IDs::monoOn) > 0.5f;
    const float mf = pv (IDs::monoFreq), mx = fToX (mf);
    if (monoOn)
    {
        g.setGradientFill (juce::ColourGradient (monoCol.withAlpha (0.16f), r.getX(), 0, monoCol.withAlpha (0.04f), mx, 0, false));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), r.getY(), mx, r.getBottom()));
    }

    // the width curve (what really happens to the side, incl. global width)
    {
        lumen::mf::Params q = proc.readParams();
        const double fs = proc.sampleRateNow();
        juce::Path c;
        bool firstPt = true;
        for (float xx = r.getX(); xx <= r.getRight(); xx += 1.5f)
        {
            const float w = (float) (lumen::mf::sideCurve (q, fs, xToF (xx)) * q.width);
            const float yy = wToY (juce::jlimit (0.0f, 2.0f, w));
            if (firstPt) { c.startNewSubPath (xx, yy); firstPt = false; } else c.lineTo (xx, yy);
        }
        glowPath (g, c, C::text, 2.0f, 0.95f);
    }

    // crossover lines
    for (int k = 0; k < 3; ++k)
    {
        const float xx = fToX (x[k]); const bool hot = hover == HX1 + k || drag == HX1 + k;
        g.setColour (C::text.withAlpha (hot ? 0.8f : 0.3f));
        const float dl[] { 4.0f, 3.0f }; g.drawDashedLine ({ xx, r.getY(), xx, r.getBottom() }, dl, 2, hot ? 1.6f : 1.0f);
        if (hot) { g.setFont (mono (10.0f)); g.drawText (hzText (x[k]), (int) xx - 40, (int) r.getBottom() - 30, 80, 14, juce::Justification::centred); }
    }
    // mono handle
    if (monoOn)
    {
        const bool hot = hover == HMono || drag == HMono;
        g.setColour (monoCol.withAlpha (hot ? 1.0f : 0.85f)); g.fillRect (mx - (hot ? 1.5f : 1.0f), r.getY(), hot ? 3.0f : 2.0f, r.getHeight());
        auto tag = juce::Rectangle<float> (mx + 6, r.getY() + 8, 112, 36);
        if (tag.getRight() > r.getRight() - 44) tag.setX (mx - 118);
        g.setColour (C::bg.withAlpha (0.85f)); g.fillRoundedRectangle (tag, 4.0f);
        g.setColour (monoCol); g.setFont (font (11.0f)); g.drawText ("MONO < " + hzText (mf), tag.withHeight (18).translated (0, 2), juce::Justification::centred);
        g.setColour (C::dim); g.setFont (font (9.5f, false));
        g.drawText (slopeNames()[(int) pv (IDs::monoSlope)] + (pv (IDs::lowWidth) > 0.005f ? ", bass " + juce::String (juce::roundToInt (pv (IDs::lowWidth) * 100)) + "%" : juce::String()),
                    tag.withTrimmedTop (18), juce::Justification::centred);
    }
    // band width dots
    for (int b = 0; b < 4; ++b)
    {
        const float cx = fToX (bandCentre (b)), cy = wToY (pv (IDs::w (b)));
        const bool hot = hover == HB1 + b || drag == HB1 + b;
        const float rad = hot ? 7.0f : 5.5f;
        g.setColour (bandCol[b].withAlpha (0.25f)); g.fillEllipse (cx - rad - 3, cy - rad - 3, 2 * rad + 6, 2 * rad + 6);
        g.setColour (bandCol[b]); g.fillEllipse (cx - rad, cy - rad, 2 * rad, 2 * rad);
        g.setColour (C::bg); g.fillEllipse (cx - 2, cy - 2, 4, 4);
    }
    // legend
    g.setColour (C::bg.withAlpha (0.8f)); g.fillRoundedRectangle (r.getX() + 2, r.getBottom() - 22, 300, 18, 4.0f);
    g.setFont (font (9.5f, false));
    auto leg = [&] (float xx, juce::Colour c, const juce::String& t) { g.setColour (c); g.fillRect (xx, r.getBottom() - 12, 10.0f, 3.0f); g.setColour (C::dim); g.drawText (t, (int) xx + 14, (int) r.getBottom() - 18, 90, 14, juce::Justification::centredLeft); };
    leg (r.getX() + 6, C::text, "width curve"); leg (r.getX() + 96, C::magenta, "side out"); leg (r.getX() + 172, C::text.withAlpha (0.35f), "side in"); leg (r.getX() + 236, juce::Colour (0xff8796a8), "mid");

    // message (AUTO result)
    if (msg.isNotEmpty() && juce::Time::getMillisecondCounter() - msgTime < 12000)
    {
        auto m = juce::Rectangle<float> (r.getCentreX() - 260, r.getY() + 54, 520, 40);
        g.setColour (C::bg.withAlpha (0.92f)); g.fillRoundedRectangle (m, 6.0f);
        g.setColour (monoCol.withAlpha (0.8f)); g.drawRoundedRectangle (m, 6.0f, 1.0f);
        g.setColour (C::text); g.setFont (font (12.0f)); g.drawFittedText ("AUTO:  " + msg, m.reduced (10, 2).toNearestInt(), juce::Justification::centred, 2);
    }
}

// ================================================================================================ StereoScope
void StereoScope::update()
{
    const int n = 1536, pos = proc.ringPos.load (std::memory_order_acquire);
    pts.resize ((size_t) n);
    float pk = 1e-4f;
    for (int i = 0; i < n; ++i)
    {
        const int q = (pos - n + i) & (LumenMonoFilterProcessor::kRing - 1);
        const float l = proc.ring[2][q], r = proc.ring[3][q];
        const float side = (r - l) * 0.70710678f, mid = (l + r) * 0.70710678f;   // goniometer: up = mono, sideways = stereo
        pts[(size_t) i] = { side, mid };
        pk = std::max (pk, std::max (std::abs (side), std::abs (mid)));
    }
    const float target = 0.9f / std::max (0.02f, pk);
    scale = target < scale ? target : scale + (target - scale) * 0.04f;
    corr += (proc.corrOut.load() - corr) * 0.25f; corrLow += (proc.corrLow.load() - corrLow) * 0.25f;
    width += (proc.widthOut.load() - width) * 0.2f; widthIn += (proc.widthIn.load() - widthIn) * 0.2f;
    loss += (proc.monoLoss.load() - loss) * 0.2f; bal += (proc.balanceOut.load() - bal) * 0.2f;
    repaint();
}

void StereoScope::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    auto sq = all.removeFromTop (all.getWidth() * 0.66f).reduced (all.getWidth() * 0.17f, 4);
    const float s = std::min (sq.getWidth(), sq.getHeight());
    sq = sq.withSizeKeepingCentre (s, s);
    const auto c = sq.getCentre(); const float R = s * 0.5f;
    g.setColour (C::field); g.fillEllipse (sq);
    g.setColour (C::border); g.drawEllipse (sq, 1.0f);
    g.setColour (C::border.withAlpha (0.8f));
    g.drawLine (c.x, c.y - R, c.x, c.y + R, 1.0f); g.drawLine (c.x - R, c.y, c.x + R, c.y, 1.0f);
    const float d = R * 0.7071f;
    g.drawLine (c.x - d, c.y - d, c.x + d, c.y + d, 0.8f); g.drawLine (c.x - d, c.y + d, c.x + d, c.y - d, 0.8f);
    g.setColour (C::dim); g.setFont (font (9.5f));
    g.drawText ("M", (int) c.x - 8, (int) (c.y - R) - 2, 16, 12, juce::Justification::centred);
    g.drawText ("L", (int) (c.x - d) - 14, (int) (c.y - d) - 12, 12, 12, juce::Justification::centred);
    g.drawText ("R", (int) (c.x + d) + 2, (int) (c.y - d) - 12, 12, 12, juce::Justification::centred);
    g.drawText ("+S", (int) (c.x + R) - 2, (int) c.y - 14, 18, 12, juce::Justification::centred);
    g.drawText ("-S", (int) (c.x - R) - 16, (int) c.y - 14, 18, 12, juce::Justification::centred);
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const float px = c.x + juce::jlimit (-1.0f, 1.0f, pts[i].x * scale) * R, py = c.y - juce::jlimit (-1.0f, 1.0f, pts[i].y * scale) * R;
        const float a = 0.15f + 0.6f * (float) i / (float) pts.size();
        g.setColour (C::cyan.withAlpha (a)); g.fillRect (px - 0.7f, py - 0.7f, 1.4f, 1.4f);
    }
    // level meters beside the scope
    ledMeter (g, { all.getX() + 6, sq.getY(), 7, sq.getHeight() }, lvlL, lvlL, true, 0);
    ledMeter (g, { all.getRight() - 13, sq.getY(), 7, sq.getHeight() }, lvlR, lvlR, true, 0);

    // correlation bars
    auto bar = [&] (juce::Rectangle<float> b, float v, const juce::String& label)
    {
        auto lab = b.removeFromTop (13);
        g.setColour (C::dim); g.setFont (font (9.5f)); g.drawText (label, lab, juce::Justification::centredLeft);
        g.setColour (v < 0 ? C::red : C::text); g.setFont (mono (10.5f)); g.drawText ((v >= 0 ? "+" : "") + juce::String (v, 2), lab, juce::Justification::centredRight);
        g.setColour (C::field); g.fillRoundedRectangle (b, 3.0f);
        g.setColour (C::border); g.drawVerticalLine ((int) b.getCentreX(), b.getY(), b.getBottom());
        const float t = juce::jlimit (-1.0f, 1.0f, v), x0 = b.getCentreX(), x1 = b.getCentreX() + t * b.getWidth() * 0.5f;
        const auto col = t < 0 ? C::red : t < 0.3f ? C::amber : C::green;
        g.setColour (col.withAlpha (0.85f)); g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (std::min (x0, x1), b.getY() + 2, std::max (x0, x1), b.getBottom() - 2), 2.0f);
        g.setColour (C::dim); g.setFont (font (8.5f, false));
        g.drawText ("-1", b.reduced (2, 0), juce::Justification::centredLeft); g.drawText ("+1", b.reduced (2, 0), juce::Justification::centredRight);
    };
    all.removeFromTop (4);
    bar (all.removeFromTop (30).reduced (10, 0), corr, "CORRELATION (output)");
    all.removeFromTop (5);
    bar (all.removeFromTop (30).reduced (10, 0), corrLow, "LOW END CORRELATION (< mono freq)");
    all.removeFromTop (8);
    auto row = all.removeFromTop (34).reduced (10, 0);
    auto stat = [&] (juce::Rectangle<float> b, const juce::String& k, const juce::String& v, juce::Colour col)
    {
        g.setColour (C::field); g.fillRoundedRectangle (b.reduced (2, 0), 4.0f);
        g.setColour (C::dim); g.setFont (font (8.5f)); g.drawText (k, b.removeFromTop (14).translated (0, 2), juce::Justification::centred);
        g.setColour (col); g.setFont (mono (11.5f)); g.drawText (v, b, juce::Justification::centred);
    };
    const float cw = row.getWidth() / 3.0f;
    stat (row.removeFromLeft (cw), "WIDTH", juce::String (juce::roundToInt (width * 100)) + "%", C::magenta);
    stat (row.removeFromLeft (cw), "MONO LOSS", juce::String (loss, 1) + " dB", loss < -3.5f ? C::red : loss < -1.5f ? C::amber : C::green);
    const int bp = juce::roundToInt (std::abs (bal) * 100);
    stat (row, "BALANCE", bp < 3 ? juce::String ("C") : juce::String (bp) + (bal < 0 ? " L" : " R"), C::text);
}

// ================================================================================================ Editor
LumenMonoFilterEditor::Knob& LumenMonoFilterEditor::knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip)
{
    auto k = std::make_unique<Knob> (proc.apvts, id, lbl, c, bip); k->setBounds (r); addAndMakeVisible (*k);
    auto* raw = k.get(); owned.push_back (std::move (k)); return *raw;
}
LumenMonoFilterEditor::Toggle& LumenMonoFilterEditor::toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r, bool round)
{
    auto k = std::make_unique<Toggle> (proc.apvts, id, txt, c, round); k->setBounds (r); addAndMakeVisible (*k);
    auto* raw = k.get(); owned.push_back (std::move (k)); return *raw;
}
LumenMonoFilterEditor::Combo& LumenMonoFilterEditor::combo (const juce::String& id, juce::Colour c, juce::Rectangle<int> r)
{
    auto k = std::make_unique<Combo> (proc.apvts, id, c); k->setBounds (r); addAndMakeVisible (*k);
    auto* raw = k.get(); owned.push_back (std::move (k)); return *raw;
}

LumenMonoFilterEditor::LumenMonoFilterEditor (LumenMonoFilterProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    fftBuf.assign ((size_t) Spectra::kN * 2, 0.0f); window.resize ((size_t) Spectra::kN);
    for (int i = 0; i < Spectra::kN; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) Spectra::kN);
    for (auto* a : { spectra.midOut, spectra.sideOut, spectra.sideIn }) std::fill (a, a + Spectra::kN / 2, -140.0f);

    // ---- header
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (300, 15, 260, 26);
    for (auto* b : { &prevB, &nextB, &abA, &abB, &abCopy, &autoB }) { b->setColour (juce::TextButton::buttonColourId, C::cyan); addAndMakeVisible (*b); }
    prevB.setBounds (566, 15, 26, 26); nextB.setBounds (594, 15, 26, 26);
    prevB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + proc.getNumPrograms() - 1) % proc.getNumPrograms()); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    abA.setBounds (636, 15, 30, 26); abB.setBounds (668, 15, 30, 26); abCopy.setBounds (700, 15, 52, 26);
    abA.onClick = [this] { proc.switchAB (0); }; abB.onClick = [this] { proc.switchAB (1); }; abCopy.onClick = [this] { proc.copyAB(); };
    abA.setTooltip ("A/B compare two settings"); abCopy.setTooltip ("Copy this setting to the other slot");
    combo (IDs::mode, C::cyan, { 960, 15, 140, 26 }).setTooltip ("ZERO LATENCY: the mid (mono sum) is never touched, no delay.  LINEAR PHASE: no phase shift on the side at all (for mastering), ~90 ms latency (the DAW compensates)");

    // ---- graph + scope
    graph = std::make_unique<WidthGraph> (proc, spectra); addAndMakeVisible (*graph); graph->setBounds (12, 66, 796, 330);
    scope = std::make_unique<StereoScope> (proc); addAndMakeVisible (*scope); scope->setBounds (820, 92, 348, 334);

    // ---- monitor strip
    const char* monTxt[9] { "STEREO", "MONO", "SIDE", "MONO LOWS", "LOW", "LOW-MID", "HIGH-MID", "HIGH", "DELTA" };
    const char* monTip[9] { "Normal listening", "Mono check: L + R summed (what clubs / phones hear)", "Only the stereo part (L - R)",
                            "Only what is below the MONO frequency", "Solo the LOW band", "Solo the LOW-MID band", "Solo the HIGH-MID band", "Solo the HIGH band",
                            "Only what MonoFilter changes (output - input)" };
    int mxp = 12;
    for (int i = 0; i < 9; ++i)
    {
        auto& b = mon[i]; b.setButtonText (monTxt[i]); b.setTooltip (monTip[i]);
        b.setColour (juce::TextButton::buttonColourId, i == 0 ? C::cyan : i < 4 ? C::cyan : i < 8 ? bandCol[i - 4] : C::red);
        const int w = i == 3 ? 96 : i >= 4 && i <= 7 ? 78 : 74;
        b.setBounds (mxp + 62, 404, w, 26); mxp += w + 4;
        b.onClick = [this, i] { proc.setParam (IDs::monitor, (float) i); };
        addAndMakeVisible (b);
    }

    // ---- MONO MAKER
    const int py = 444;
    toggle (IDs::monoOn, "", monoCol, { 312, py + 4, 24, 20 }, true).setTooltip ("Mono maker on / off");
    knob (IDs::monoFreq, "MONO BELOW", monoCol, { 22, py + 30, 96, 108 }).setTooltip ("Everything below this frequency is folded to mono (club / vinyl / phone safe)");
    knob (IDs::lowWidth, "BASS WIDTH", monoCol, { 122, py + 40, 76, 90 }).setTooltip ("0 % = bass fully mono, 50 % = half the original bass width kept");
    combo (IDs::monoSlope, monoCol, { 208, py + 40, 126, 26 }).setTooltip ("How sharp the mono / stereo transition is");
    autoB.setBounds (208, py + 74, 126, 34);
    autoB.setTooltip ("Play the loudest part of your track and press AUTO: MonoFilter listens for 6 seconds and sets MONO BELOW and PHASE ALIGN for you");
    autoB.onClick = [this] { if (! proc.isLearning()) proc.startLearn(); };
    toggle (IDs::align, "PHASE ALIGN", C::green, { 22, py + 160, 124, 26 }).setTooltip ("Aligns left / right bass in time and polarity BEFORE folding to mono -> no bass lost when the two sides were out of phase (detuned layers, spaced mics, stereo 808s)");
    toggle (IDs::alignAuto, "AUTO TRACK", C::green, { 22, py + 192, 124, 26 }).setTooltip ("Tracks the delay and polarity continuously (otherwise use ALIGN TIME)");
    knob (IDs::alignMs, "ALIGN TIME", C::green, { 152, py + 150, 70, 84 }, true).setTooltip ("Manual delay of the right bass vs the left (when AUTO TRACK is off)");

    // ---- STEREO IMAGER
    const int ix = 362;
    for (int b = 0; b < 4; ++b) knob (IDs::w (b), bandName[b], bandCol[b], { ix + 8 + b * 76, py + 34, 72, 86 }).setTooltip ("Width of this band: 0 % mono, 100 % untouched, 200 % twice as wide");
    knob (IDs::width, "WIDTH", C::magenta, { ix + 318, py + 26, 100, 112 }).setTooltip ("Global width on top of everything (0 % = full mono)");
    knob (IDs::x1, "X-OVER 1", C::dim, { ix + 46, py + 150, 66, 80 });
    knob (IDs::x2, "X-OVER 2", C::dim, { ix + 122, py + 150, 66, 80 });
    knob (IDs::x3, "X-OVER 3", C::dim, { ix + 198, py + 150, 66, 80 });

    // ---- STEREOIZE
    const int sx = 798;
    knob (IDs::stAmt, "AMOUNT", C::magenta, { sx + 8, py + 34, 76, 90 }).setTooltip ("Makes mono sounds wide.  Added to the SIDE only, so it disappears completely in mono: 100 % mono safe");
    knob (IDs::stFreq, "FROM", C::magenta, { sx + 88, py + 40, 68, 82 }).setTooltip ("Stereoize only above this frequency (bass stays mono)");
    combo (IDs::stMode, C::magenta, { sx + 10, py + 152, 140, 26 }).setTooltip ("DIFFUSE: smooth, natural (allpass decorrelation).  HAAS: classic short delay widening");
    knob (IDs::stTime, "HAAS TIME", C::magenta, { sx + 44, py + 182, 66, 56 });

    // ---- OUTPUT
    const int ox = 974;
    knob (IDs::balance, "BALANCE", C::text, { ox + 6, py + 34, 62, 78 }, true);
    knob (IDs::mix, "MIX", C::cyan, { ox + 68, py + 34, 62, 78 });
    knob (IDs::out, "OUTPUT", C::cyan, { ox + 130, py + 34, 62, 78 }, true);
    combo (IDs::source, C::text, { ox + 10, py + 128, 176, 24 }).setTooltip ("Input: stereo, swapped, one channel only or summed to mono");
    combo (IDs::polarity, C::text, { ox + 10, py + 158, 176, 24 }).setTooltip ("Flip the polarity of one channel");
    toggle (IDs::autoGain, "AUTO GAIN", C::amber, { ox + 10, py + 190, 176, 26 }).setTooltip ("Keeps the loudness the same when the width changes, so you judge the sound, not the volume");

    setSize (W, HH);
    startTimerHz (30);
}

LumenMonoFilterEditor::~LumenMonoFilterEditor() { setLookAndFeel (nullptr); }

void LumenMonoFilterEditor::updateSpectra()
{
    const int N = Spectra::kN, pos = proc.ringPos.load (std::memory_order_acquire);
    spectra.fs = proc.sampleRateNow();
    auto spectrum = [&] (int chA, int chB, float sign, float* dst)
    {
        std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int q = (pos - N + i) & (LumenMonoFilterProcessor::kRing - 1);
            fftBuf[(size_t) i] = 0.5f * (proc.ring[chA][q] + sign * proc.ring[chB][q]) * window[(size_t) i];
        }
        fft.performFrequencyOnlyForwardTransform (fftBuf.data());
        const float norm = 4.0f / (float) N;
        for (int k = 1; k < N / 2; ++k)
        {
            const float db = 20.0f * std::log10 (fftBuf[(size_t) k] * norm + 1e-9f);
            dst[k] = db > dst[k] ? dst[k] + (db - dst[k]) * 0.6f : dst[k] + (db - dst[k]) * 0.18f;
        }
    };
    spectrum (2, 3, 1.0f, spectra.midOut);
    spectrum (2, 3, -1.0f, spectra.sideOut);
    spectrum (0, 1, -1.0f, spectra.sideIn);
}

void LumenMonoFilterEditor::timerCallback()
{
    updateSpectra();
    scope->lvlL = std::max (proc.outPk.load(), scope->lvlL * 0.85f); scope->lvlR = scope->lvlL;
    scope->update();
    graph->repaint();
    if (proc.finishLearnIfReady()) graph->showMessage (proc.lastAutoText());
    if (proc.isLearning()) { autoB.setButtonText ("LISTENING " + juce::String (juce::roundToInt (proc.learnProgress() * 100)) + "%"); autoB.setToggleState (true, juce::dontSendNotification); }
    else if (wasLearning || autoB.getToggleState()) { autoB.setButtonText ("AUTO"); autoB.setToggleState (false, juce::dontSendNotification); }
    wasLearning = proc.isLearning();
    const int monitor = (int) pv (IDs::monitor);
    for (int i = 0; i < 9; ++i) mon[i].setToggleState (i == monitor, juce::dontSendNotification);
    if (lastProgram != proc.programVersion.load() + proc.getCurrentProgram() * 1000)
    { lastProgram = proc.programVersion.load() + proc.getCurrentProgram() * 1000; presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    abA.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification); abB.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    repaint (0, 0, W, 60); repaint (12, 640, 340, 80); repaint (962, 640, 210, 80);
}

void LumenMonoFilterEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    g.setColour (C::panelTop); g.fillRect (0, 0, W, 56);
    g.setColour (C::border); g.drawHorizontalLine (56, 0, (float) W);
    g.setColour (C::text); g.setFont (font (24.0f)); g.drawText ("LUMEN", 18, 12, 100, 32, juce::Justification::centredLeft);
    g.setColour (C::cyan); g.drawText ("MONOFILTER", 108, 12, 170, 32, juce::Justification::centredLeft);
    g.setColour (C::dim); g.setFont (font (9.5f, false)); g.drawText ("v2", 262, 24, 30, 14, juce::Justification::centredLeft);
    // latency read-out
    const int lat = proc.getLatencySamples();
    g.setFont (font (10.0f, false)); g.setColour (C::dim);
    g.drawText (lat == 0 ? juce::String ("0 ms latency") : juce::String (lat * 1000.0 / proc.sampleRateNow(), 1) + " ms latency", 790, 15, 160, 26, juce::Justification::centredRight);

    // scope panel
    panel (g, { 816, 66, 356, 364 }, "STEREO FIELD", C::cyan, true);
    // monitor label
    g.setColour (C::dim); g.setFont (font (10.0f)); g.drawText ("LISTEN", 14, 404, 60, 26, juce::Justification::centredLeft);

    const int py = 444;
    const bool monoOn = pv (IDs::monoOn) > 0.5f;
    panel (g, { 12, (float) py, 340, 268 }, "MONO MAKER", monoCol, monoOn);
    panel (g, { 360, (float) py, 430, 268 }, "STEREO IMAGER", C::magenta, true);
    panel (g, { 798, (float) py, 168, 268 }, "STEREOIZE", C::magenta, pv (IDs::stAmt) > 0.001f);
    panel (g, { 974, (float) py, 198, 268 }, "OUTPUT", C::cyan, true);
    // align read-out
    if (pv (IDs::align) > 0.5f)
    {
        g.setColour (C::green); g.setFont (mono (10.5f));
        const juce::String t = pv (IDs::alignAuto) > 0.5f
            ? "R " + juce::String (proc.alignMs.load(), 2) + " ms" + (proc.alignPol.load() < 0 ? "  POL FLIP" : "") + "\nconfidence " + juce::String (juce::roundToInt (proc.alignConf.load() * 100)) + "%"
            : juce::String ("manual ") + juce::String (pv (IDs::alignMs), 2) + " ms";
        g.drawFittedText (t, { 228, py + 168, 118, 40 }, juce::Justification::centredLeft, 2);
    }
    else { g.setColour (C::dim); g.setFont (font (9.5f, false)); g.drawFittedText ("phase align off", { 228, py + 168, 118, 40 }, juce::Justification::centredLeft, 2); }
    g.setColour (C::dim); g.setFont (font (9.5f, false));
    g.drawFittedText ("Crossovers: drag the dashed lines in the display too", { 640, py + 170, 140, 50 }, juce::Justification::centredLeft, 3);
    if (pv (IDs::autoGain) > 0.5f)
    { g.setColour (C::amber); g.setFont (mono (10.0f)); g.drawText ("auto " + juce::String (proc.autoGainDb.load(), 1) + " dB", 984, py + 222, 176, 16, juce::Justification::centred); }
}
