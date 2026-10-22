#include "PluginEditor.h"
using namespace lumen::led;
using namespace lumen::mixcheck;

static constexpr int W = 1240, HH = 860;

static juce::Colour catColour (Cat c)
{
    switch (c)
    {
        case Cat::Big: return C::amber;      case Cat::Car: return C::blue;     case Cat::Mobile: return C::violet;
        case Cat::Home: return C::green;     case Cat::Broadcast: return C::orange; case Cat::Studio: return C::cyan;
        case Cat::Streaming: return C::red;  case Cat::Custom: return C::text;  case Cat::Off: default: return C::dim;
    }
}
static const char* catName (Cat c)
{
    switch (c)
    {
        case Cat::Big: return "BIG SYSTEM"; case Cat::Car: return "CAR"; case Cat::Mobile: return "MOBILE"; case Cat::Home: return "HOME / PUBLIC";
        case Cat::Broadcast: return "BROADCAST"; case Cat::Studio: return "STUDIO CHECK"; case Cat::Streaming: return "STREAMING"; case Cat::Custom: return "YOUR IR"; case Cat::Off:
        default: return "HEADPHONES ONLY";
    }
}

// ------------------------------------------------------------------ simple line icons
static void drawIcon (juce::Graphics& g, int icon, juce::Rectangle<float> r, juce::Colour col)
{
    const float s = std::min (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (s, s);
    const float x = r.getX(), y = r.getY(), u = s / 24.0f;
    auto P = [&] (float a, float b) { return juce::Point<float> (x + a * u, y + b * u); };
    juce::Path p; const float lw = std::max (1.3f, 1.5f * u);
    auto rr = [&] (float a, float b, float w, float h, float c) { p.addRoundedRectangle (x + a * u, y + b * u, w * u, h * u, c * u); };
    auto circ = [&] (float cx, float cy, float rad) { p.addEllipse (x + (cx - rad) * u, y + (cy - rad) * u, 2 * rad * u, 2 * rad * u); };
    auto line = [&] (float a, float b, float c, float d) { p.startNewSubPath (P (a, b)); p.lineTo (P (c, d)); };
    switch (icon)
    {
        case IcMonitors: rr (3, 4, 7, 16, 1.5f); rr (14, 4, 7, 16, 1.5f); circ (6.5f, 14, 2.2f); circ (17.5f, 14, 2.2f); circ (6.5f, 8, 1.2f); circ (17.5f, 8, 1.2f); break;
        case IcClub: rr (2, 3, 8, 18, 1); rr (14, 3, 8, 18, 1); circ (6, 15, 2.8f); circ (18, 15, 2.8f); circ (6, 7.5f, 1.6f); circ (18, 7.5f, 1.6f); line (12, 2, 12, 22); break;
        case IcFestival: line (2, 21, 22, 21); line (4, 21, 4, 6); line (20, 21, 20, 6); line (2, 6, 22, 6); line (4, 6, 12, 2); line (20, 6, 12, 2); rr (8, 12, 8, 6, 1); break;
        case IcCar: case IcCarSub:
            p.startNewSubPath (P (2, 16)); p.lineTo (P (3, 11)); p.lineTo (P (7, 10)); p.lineTo (P (10, 6)); p.lineTo (P (17, 6)); p.lineTo (P (20, 10)); p.lineTo (P (22, 11)); p.lineTo (P (22, 16)); p.closeSubPath();
            circ (7, 17, 2.4f); circ (17, 17, 2.4f);
            if (icon == IcCarSub) { line (10, 13, 14, 13); line (12, 11, 12, 15); }
            break;
        case IcPhone: rr (7, 2, 10, 20, 2); line (10, 19, 14, 19); break;
        case IcPhoneStereo: rr (2, 7, 20, 10, 2); line (4.5f, 10, 4.5f, 14); line (19.5f, 10, 19.5f, 14); break;
        case IcReels: rr (7, 2, 10, 20, 2); p.addTriangle (P (10.5f, 9), P (10.5f, 15), P (15, 12)); break;
        case IcEarbuds: circ (7, 8, 3.5f); circ (17, 8, 3.5f); line (7, 11.5f, 7, 20); line (17, 11.5f, 17, 20); break;
        case IcPods: circ (7, 7, 3); circ (17, 7, 3); line (8.5f, 10, 8.5f, 18); line (15.5f, 10, 15.5f, 18); break;
        case IcHeadphones: p.addCentredArc (x + 12 * u, y + 13 * u, 8.5f * u, 8.5f * u, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true); rr (2, 13, 5, 8, 1.5f); rr (17, 13, 5, 8, 1.5f); break;
        case IcLaptop: rr (5, 5, 14, 10, 1); line (2, 18, 22, 18); line (5, 15, 3, 18); line (19, 15, 21, 18); break;
        case IcBtSpeaker: rr (2, 7, 20, 10, 5); circ (8, 12, 2.4f); circ (16, 12, 2.4f); break;
        case IcTv: rr (2, 4, 20, 13, 1); line (8, 21, 16, 21); line (12, 17, 12, 21); break;
        case IcRadio: rr (2, 7, 20, 13, 2); circ (8, 13.5f, 3.4f); line (14, 11, 19, 11); line (14, 14, 19, 14); line (5, 7, 15, 2); break;
        case IcFm: case IcAm:
            line (12, 9, 12, 22); line (7, 22, 17, 22);
            p.addCentredArc (x + 12 * u, y + 9 * u, 4 * u, 4 * u, 0, -0.9f, 0.9f, true);
            p.addCentredArc (x + 12 * u, y + 9 * u, 8 * u, 8 * u, 0, -0.9f, 0.9f, true);
            break;
        case IcCeiling: line (2, 4, 22, 4); circ (12, 9, 4); line (5, 15, 3, 19); line (12, 16, 12, 21); line (19, 15, 21, 19); break;
        case IcWall: rr (2, 3, 20, 18, 0.5f); line (2, 9, 22, 9); line (2, 15, 22, 15); line (9, 3, 9, 9); line (16, 9, 16, 15); line (8, 15, 8, 21); break;
        case IcCube: rr (6, 4, 12, 16, 1.5f); circ (12, 12, 3.6f); break;
        case IcNearfield: rr (6, 2, 12, 20, 1); circ (12, 15, 3.4f); circ (12, 7, 1.6f); break;
        case IcHifi: rr (2, 3, 6, 18, 1); rr (16, 3, 6, 18, 1); rr (9, 14, 6, 7, 1); circ (5, 15, 1.8f); circ (19, 15, 1.8f); break;
        case IcStream: circ (12, 12, 9.5f); p.addTriangle (P (10, 8), P (10, 16), P (16.5f, 12)); break;
        case IcIr: line (2, 12, 6, 12); line (6, 12, 7, 3); line (7, 3, 8, 18); line (8, 18, 10, 8); line (10, 8, 12, 15); line (12, 15, 15, 10); line (15, 10, 18, 13); line (18, 13, 22, 12); break;
        default: break;
    }
    g.setColour (col);
    g.strokePath (p, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ------------------------------------------------------------------ device response (for the display)
static float deviceResponseDb (const Device& d, double fs, double f)
{
    Biquad s[8]; double m = 1.0;
    int n = butterSections (s, fs, d.hp, d.hpOrder, true); for (int k = 0; k < n; ++k) m *= s[k].magnitude (fs, f);
    n = butterSections (s, fs, d.lp, d.lpOrder, false); for (int k = 0; k < n; ++k) m *= s[k].magnitude (fs, f);
    if (d.special == FM) { n = butterSections (s, fs, 15000, 8, false); for (int k = 0; k < n; ++k) m *= s[k].magnitude (fs, f); }
    for (auto& e : d.eq)
    {
        Biquad b; if (e.type == 0) b.bell (fs, e.f, e.g, e.q); else if (e.type == 1) b.lowShelf (fs, e.f, e.g, e.q); else b.highShelf (fs, e.f, e.g, e.q);
        m *= b.magnitude (fs, f);
    }
    return (float) (20.0 * std::log10 (m + 1e-9));
}

// ================================================================================================ DeviceTile
DeviceTile::DeviceTile (LumenMixCheckProcessor& p, int i) : proc (p), index (i)
{
    setTooltip (juce::String (devices()[(size_t) i].name) + "\n" + devices()[(size_t) i].listenFor);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
void DeviceTile::paint (juce::Graphics& g)
{
    const auto& d = devices()[(size_t) index];
    const bool sel = proc.activeDevice() == index, over = isMouseOver();
    const auto col = catColour (d.cat);
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (sel ? col.withAlpha (0.18f) : over ? juce::Colour (0xff272b32) : juce::Colour (0xff1f2328)); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (sel ? col : over ? C::dim : C::border); g.drawRoundedRectangle (r, 6.0f, sel ? 1.6f : 1.0f);
    drawIcon (g, d.icon, r.withHeight (r.getHeight() * 0.58f).reduced (4, 6).translated (0, 2), sel ? col : col.withAlpha (0.75f));
    g.setColour (sel ? C::text : C::text.withAlpha (0.8f)); g.setFont (font (10.0f));
    g.drawFittedText (d.shortName, r.removeFromBottom (20).toNearestInt().reduced (3, 0), juce::Justification::centred, 1, 0.8f);
}
void DeviceTile::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    proc.setParam (IDs::device, (float) index);
    if (proc.P (IDs::cycle) > 0.5f) proc.setParam (IDs::cycle, 0.0f);
}

// ================================================================================================ ResponseView
void ResponseView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, C::cyan);
    auto pl = r.reduced (10, 10).withTrimmedBottom (14).withTrimmedLeft (26);
    auto fx = [&] (double f) { return pl.getX() + pl.getWidth() * (float) (std::log10 (f / 20.0) / 3.0); };
    auto xf = [&] (float xx) { return 20.0 * std::pow (10.0, 3.0 * (xx - pl.getX()) / pl.getWidth()); };
    auto dy = [&] (float db) { return pl.getY() + pl.getHeight() * (1.0f - juce::jlimit (0.0f, 1.0f, (db + 42.0f) / 54.0f)); };
    auto sy = [&] (float db) { return pl.getBottom() - pl.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 90.0f); };
    g.setFont (mono (9.0f));
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float xx = fx (f); g.setColour (C::border.withAlpha (0.6f)); g.drawVerticalLine ((int) xx, pl.getY(), pl.getBottom());
        g.setColour (C::dim); g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) xx - 18, (int) pl.getBottom() + 2, 36, 12, juce::Justification::centred);
    }
    for (float db : { 12.0f, 0.0f, -12.0f, -24.0f, -36.0f })
    {
        const float yy = dy (db); g.setColour (juce::exactlyEqual (db, 0.0f) ? C::dim.withAlpha (0.5f) : C::border.withAlpha (0.5f)); g.drawHorizontalLine ((int) yy, pl.getX(), pl.getRight());
        g.setColour (C::dim); g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), (int) r.getX() + 4, (int) yy - 6, 28, 12, juce::Justification::centredLeft);
    }
    // live spectra: your mix (grey) and what the device plays (colour)
    const int dev = proc.activeDevice();
    const auto& d = devices()[(size_t) dev];
    const auto col = catColour (d.cat);
    auto spec = [&] (const float* s)
    {
        juce::Path p; bool st = false; const int nb = LiveSpectra::kN / 2;
        for (float xx = pl.getX(); xx <= pl.getRight(); xx += 2.0f)
        {
            const float b = (float) (xf (xx) * LiveSpectra::kN / sp.fs);
            float v;
            if (b < 2.0f) v = s[1];
            else { const float b1 = (float) (xf (xx + 2.0f) * LiveSpectra::kN / sp.fs); const int k0 = juce::jlimit (1, nb - 2, (int) b), k1 = juce::jlimit (k0, nb - 2, (int) b1);
                   if (k1 == k0) { const float fr = b - (float) k0; v = s[k0] + (s[k0 + 1] - s[k0]) * fr; } else { v = -200; for (int k = k0; k <= k1; ++k) v = std::max (v, s[k]); } }
            if (! st) { p.startNewSubPath (xx, sy (v)); st = true; } else p.lineTo (xx, sy (v));
        }
        return p;
    };
    auto pin = spec (sp.in), pout = spec (sp.out);
    juce::Path fillOut (pout); fillOut.lineTo (pl.getRight(), pl.getBottom()); fillOut.lineTo (pl.getX(), pl.getBottom()); fillOut.closeSubPath();
    g.setColour (col.withAlpha (0.12f)); g.fillPath (fillOut);
    g.setColour (col.withAlpha (0.55f)); g.strokePath (pout, juce::PathStrokeType (1.1f));
    g.setColour (C::text.withAlpha (0.28f)); g.strokePath (pin, juce::PathStrokeType (1.0f));
    // the device's frequency response
    if (dev != 0 && d.special != Custom)
    {
        juce::Path c; bool st = false;
        for (float xx = pl.getX(); xx <= pl.getRight(); xx += 1.5f)
        {
            const float yy = dy (deviceResponseDb (d, sp.fs, xf (xx)));
            if (! st) { c.startNewSubPath (xx, yy); st = true; } else c.lineTo (xx, yy);
        }
        glowPath (g, c, col.interpolatedWith (juce::Colours::white, 0.25f), 2.2f, 1.0f);
    }
    // headphone correction (dashed violet)
    if (proc.headphoneEqActive())
    {
        juce::Path c; bool st = false;
        for (float xx = pl.getX(); xx <= pl.getRight(); xx += 2.0f)
        {
            const float yy = dy ((float) proc.headphoneResponseDb (xf (xx)));
            if (! st) { c.startNewSubPath (xx, yy); st = true; } else c.lineTo (xx, yy);
        }
        juce::Path dashed; const float dl[] { 5.0f, 4.0f };
        juce::PathStrokeType (1.4f).createDashedStroke (dashed, c, dl, 2);
        g.setColour (C::violet.withAlpha (0.9f)); g.fillPath (dashed);
    }
    // legend
    g.setFont (font (9.5f, false));
    auto leg = [&] (float xx, juce::Colour c, const juce::String& t) { g.setColour (c); g.fillRect (xx, pl.getY() + 6, 12.0f, 3.0f); g.setColour (C::dim); g.drawText (t, (int) xx + 16, (int) pl.getY(), 140, 14, juce::Justification::centredLeft); };
    if (proc.headphoneEqActive()) leg (pl.getRight() - 560, C::violet, "headphone correction");
    leg (pl.getRight() - 420, col.interpolatedWith (juce::Colours::white, 0.25f), "device response");
    leg (pl.getRight() - 290, col, "what you hear");
    leg (pl.getRight() - 170, C::text.withAlpha (0.4f), "your mix");
}

// ================================================================================================ ReportView
static constexpr float kRowH = 22.0f, kHead = 26.0f;
int ReportView::rowAt (float y) const { return (int) ((y - kHead) / kRowH) + 1; }
void ReportView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (C::field); g.fillRoundedRectangle (r, 6.0f);
    const float cx[] { 10, 196, 252, 308, 364, 420, 476, 532 };
    g.setFont (font (9.5f)); g.setColour (C::dim);
    g.drawText ("DEVICE", (int) cx[0], 6, 180, 14, juce::Justification::centredLeft);
    for (int b = 0; b < DeviceReport::kBands; ++b) g.drawText (reportBandName (b), (int) cx[b + 1], 6, 52, 14, juce::Justification::centred);
    g.drawText ("DIST", (int) cx[6], 6, 52, 14, juce::Justification::centred);
    g.drawText ("VERDICT", (int) cx[7], 6, 200, 14, juce::Justification::centredLeft);
    if (data.empty())
    {
        g.setColour (C::dim); g.setFont (font (13.0f, false));
        g.drawFittedText ("Play the loudest part of your track (the drop) for at least 8 seconds,\nthen press ANALYZE.  MixCheck runs those seconds through every device\nand shows what each one does to your mix: which bands disappear, which get\nexaggerated, how much the speaker distorts and how loud streaming plays it.",
                          getLocalBounds().reduced (30, 60), juce::Justification::centredTop, 6);
        return;
    }
    const int sel = proc.activeDevice();
    for (int d = 1; d < (int) data.size(); ++d)
    {
        const float y = kHead + (float) (d - 1) * kRowH;
        const auto& dv = devices()[(size_t) d]; const auto& rp = data[(size_t) d];
        if (d == sel) { g.setColour (catColour (dv.cat).withAlpha (0.14f)); g.fillRect (2.0f, y, r.getWidth() - 4, kRowH); }
        else if (d & 1) { g.setColour (juce::Colours::white.withAlpha (0.02f)); g.fillRect (2.0f, y, r.getWidth() - 4, kRowH); }
        g.setColour (catColour (dv.cat)); g.fillRoundedRectangle (cx[0], y + 7, 3, kRowH - 14, 1.5f);
        g.setColour (C::text); g.setFont (font (11.0f)); g.drawText (dv.name, (int) cx[0] + 9, (int) y, 176, (int) kRowH, juce::Justification::centredLeft);
        if (! rp.valid) { g.setColour (C::dim); g.setFont (font (10.0f, false)); g.drawText ("load an IR first", (int) cx[1], (int) y, 300, (int) kRowH, juce::Justification::centredLeft); continue; }
        for (int b = 0; b < DeviceReport::kBands; ++b)
        {
            const float v = rp.bandDb[b];
            const auto c = v < -18 ? C::red : v < -6 ? C::orange : v > 6 ? C::blue : v > 3 ? C::cyan : C::green;
            g.setColour (c.withAlpha (0.18f + 0.5f * juce::jlimit (0.0f, 1.0f, std::abs (v) / 24.0f))); g.fillRoundedRectangle (cx[b + 1] + 2, y + 3, 48, kRowH - 6, 3.0f);
            g.setColour (C::text); g.setFont (mono (10.0f));
            g.drawText (v <= -59.5f ? juce::String ("gone") : (v >= 0.5f ? "+" : "") + juce::String (juce::roundToInt (v)), (int) cx[b + 1], (int) y, 52, (int) kRowH, juce::Justification::centred);
        }
        g.setColour (rp.distortionPct > 6 ? C::orange : C::dim); g.setFont (mono (10.0f));
        g.drawText (rp.distortionPct > 0.05f ? juce::String (rp.distortionPct, 1) + "%" : juce::String ("-"), (int) cx[6], (int) y, 52, (int) kRowH, juce::Justification::centred);
        g.setColour (rp.verdict == "translates well" ? C::green : C::text.withAlpha (0.85f)); g.setFont (font (10.0f, false));
        g.drawFittedText (rp.verdict, juce::Rectangle<int> ((int) cx[7], (int) y, getWidth() - (int) cx[7] - 6, (int) kRowH), juce::Justification::centredLeft, 1, 0.8f);
    }
}
void ReportView::mouseUp (const juce::MouseEvent& e)
{
    const int d = rowAt (e.position.y);
    if (! data.empty() && d >= 1 && d < (int) data.size()) { proc.setParam (IDs::device, (float) d); proc.setParam (IDs::cycle, 0.0f); repaint(); }
}

// ================================================================================================ Editor
LumenMixCheckEditor::Knob& LumenMixCheckEditor::knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip)
{
    auto k = std::make_unique<Knob> (proc.apvts, id, lbl, c, bip); k->setBounds (r); addAndMakeVisible (*k);
    auto* raw = k.get(); owned.push_back (std::move (k)); return *raw;
}
LumenMixCheckEditor::Toggle& LumenMixCheckEditor::toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r)
{
    auto k = std::make_unique<Toggle> (proc.apvts, id, txt, c); k->setBounds (r); addAndMakeVisible (*k);
    auto* raw = k.get(); owned.push_back (std::move (k)); return *raw;
}

LumenMixCheckEditor::LumenMixCheckEditor (LumenMixCheckProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    fftBuf.assign ((size_t) LiveSpectra::kN * 2, 0.0f); window.resize ((size_t) LiveSpectra::kN);
    for (int i = 0; i < LiveSpectra::kN; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) LiveSpectra::kN);
    std::fill (std::begin (spectra.in), std::end (spectra.in), -140.0f); std::fill (std::begin (spectra.out), std::end (spectra.out), -140.0f);

    // device grid
    for (int i = 0; i < numDevices(); ++i)
    {
        auto t = std::make_unique<DeviceTile> (proc, i);
        t->setBounds (20 + (i % 4) * 122, 92 + (i / 4) * 64, 118, 60);
        addAndMakeVisible (*t); tiles.push_back (std::move (t));
    }
    // controls
    const int ky = 584;
    knob (IDs::room, "ROOM", C::green, { 22, ky, 80, 92 }).setTooltip ("How much of the space (club hall, car cabin, living room) you hear");
    knob (IDs::noise, "NOISE", C::orange, { 116, ky, 80, 92 }).setTooltip ("Real-world noise: crowd in the club, road in the car, street in the shop.  Does your track still cut through?");
    knob (IDs::drive, "SPEAKER DIST", C::red, { 210, ky, 80, 92 }).setTooltip ("How hard the small speakers are pushed (100 % = typical)");
    knob (IDs::volume, "LISTEN LEVEL", C::cyan, { 304, ky, 80, 92 }).setTooltip ("How loud the listener plays it: quiet listening loses bass and air (equal-loudness), loud listening pushes small speakers into distortion");
    knob (IDs::out, "OUTPUT", C::cyan, { 410, ky, 80, 92 }, true);
    toggle (IDs::headphones, "VIRTUAL SPEAKERS", C::violet, { 22, 688, 152, 26 }).setTooltip ("For headphone producers: speaker devices are placed in front of you (interaural delay, head shadow, early reflections) instead of inside your head - like listening to real speakers");
    toggle (IDs::levelMatch, "LEVEL MATCH", C::green, { 180, 688, 130, 26 }).setTooltip ("Every device plays as loud as your mix, so you judge the tone, not the volume");
    toggle (IDs::renderBypass, "BYPASS ON EXPORT", C::amber, { 316, 688, 180, 26 }).setTooltip ("MixCheck turns itself off while the DAW renders / exports: it can never end up in your master file");
    auto cyc = std::make_unique<Combo> (proc.apvts, IDs::cycle, C::cyan); cyc->setBounds (76, 728, 150, 26);
    cyc->setTooltip ("Switches devices automatically in time with your song while it plays: club, car, phone, earbuds, laptop, speaker, radio, next room, cube");
    addAndMakeVisible (*cyc); owned.push_back (std::move (cyc));
    for (auto* b : { &loadIrB, &resetB, &tabListen, &tabReport, &analyzeB }) { b->setColour (juce::TextButton::buttonColourId, C::cyan); addAndMakeVisible (*b); }
    loadIrB.setBounds (236, 728, 120, 26); resetB.setBounds (362, 728, 134, 26);
    loadIrB.setTooltip ("Load your own impulse response (WAV / AIFF): your car, your club, a friend's speaker");
    resetB.setTooltip ("Restart the loudness measurement (also happens automatically when playback starts)");
    resetB.onClick = [this] { proc.resetLoudness(); };
    loadIrB.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load impulse response", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult(); if (! f.existsAsFile()) return;
            juce::String err;
            if (proc.loadIR (f, err)) { proc.setParam (IDs::device, (float) (numDevices() - 1)); irMsg = "IR: " + proc.irName(); }
            else irMsg = "IR failed: " + err;
            repaint();
        });
    };
    // your headphones
    {
        auto hp = std::make_unique<Combo> (proc.apvts, IDs::hpModel, C::violet); hp->setBounds (22, 806, 236, 26);
        hp->setTooltip ("Pick your headphones: MixCheck removes their colouration so you hear a neutral reference.  "
                        "Curves are averages of published measurements - load your own ParametricEQ.txt (AutoEQ / Sonarworks export) for your exact pair.");
        addAndMakeVisible (*hp); owned.push_back (std::move (hp));
        knob (IDs::hpAmount, "CORRECTION", C::violet, { 410, 778, 80, 70 }).setTooltip ("How much of the headphone correction is applied");
        loadEqB.setColour (juce::TextButton::buttonColourId, C::violet); addAndMakeVisible (loadEqB); loadEqB.setBounds (264, 806, 136, 26);
        loadEqB.setTooltip ("Load an exact correction for your own pair: AutoEQ 'ParametricEQ.txt' / Equalizer APO format");
        loadEqB.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Load headphone EQ (ParametricEQ.txt)", juce::File(), "*.txt");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult(); if (! f.existsAsFile()) return;
                juce::String err;
                if (proc.loadHeadphoneEq (f, err)) { proc.setParam (IDs::hpModel, (float) customHeadphoneIndex()); hpMsg = "Loaded: " + f.getFileNameWithoutExtension(); }
                else hpMsg = err;
                proc.viewDirty.store (true); repaint();
            });
        };
    }
    // right side
    tabListen.setBounds (524, 66, 150, 28); tabReport.setBounds (678, 66, 210, 28);
    tabListen.onClick = [this] { showTab (0); }; tabReport.onClick = [this] { showTab (1); };
    response = std::make_unique<ResponseView> (proc, spectra); addAndMakeVisible (*response); response->setBounds (532, 184, 688, 340);
    report = std::make_unique<ReportView> (proc); addChildComponent (*report); report->setBounds (532, 150, 688, 690);
    analyzeB.setBounds (532, 112, 240, 30); analyzeB.onClick = [this] { proc.startAnalyzeAll(); };
    showTab (0);
    setSize (W, HH);
    startTimerHz (30);
}

LumenMixCheckEditor::~LumenMixCheckEditor() { setLookAndFeel (nullptr); }

void LumenMixCheckEditor::showTab (int t)
{
    tab = t;
    response->setVisible (t == 0); report->setVisible (t == 1); analyzeB.setVisible (t == 1);
    tabListen.setToggleState (t == 0, juce::dontSendNotification); tabReport.setToggleState (t == 1, juce::dontSendNotification);
    if (t == 1) report->refresh();
    repaint();
}

void LumenMixCheckEditor::updateSpectra()
{
    const int N = LiveSpectra::kN, pos = proc.ringPos.load (std::memory_order_acquire);
    spectra.fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    double e[2][DeviceReport::kBands] {};
    for (int which = 0; which < 2; ++which)
    {
        std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int q = (pos - N + i) & (LumenMixCheckProcessor::kRing - 1);
            fftBuf[(size_t) i] = 0.5f * (proc.ring[which * 2][q] + proc.ring[which * 2 + 1][q]) * window[(size_t) i];
        }
        fft.performFrequencyOnlyForwardTransform (fftBuf.data());
        float* dst = which == 0 ? spectra.in : spectra.out;
        const float norm = 4.0f / (float) N;
        for (int k = 1; k < N / 2; ++k)
        {
            const float mag = fftBuf[(size_t) k] * norm;
            const float db = 20.0f * std::log10 (mag + 1e-9f);
            dst[k] = db > dst[k] ? dst[k] + (db - dst[k]) * 0.5f : dst[k] + (db - dst[k]) * 0.15f;
            const float f = (float) (k * spectra.fs / N);
            for (int b = 0; b < DeviceReport::kBands; ++b) if (f >= reportBandEdge (b) && f < reportBandEdge (b + 1)) { e[which][b] += (double) mag * mag; break; }
        }
    }
    // loudness-weighted normalisation (like LEVEL MATCH): K-weighting per band
    static const double kw[DeviceReport::kBands] { 0.25, 0.85, 1.0, 1.55, 1.6 };
    double tin = 0, tout = 0; for (int b = 0; b < DeviceReport::kBands; ++b) { tin += e[0][b] * kw[b]; tout += e[1][b] * kw[b]; }
    const double norm = 10 * std::log10 ((tout + 1e-20) / (tin + 1e-20));
    for (int b = 0; b < DeviceReport::kBands; ++b)
    {
        const float v = (float) juce::jlimit (-40.0, 20.0, 10 * std::log10 ((e[1][b] + 1e-20) / (e[0][b] + 1e-20)) - norm);
        spectra.bandDb[b] = e[0][b] > 1e-10 ? v : 0.0f;
        bars[b] += (spectra.bandDb[b] - bars[b]) * 0.15f;
    }
}

void LumenMixCheckEditor::timerCallback()
{
    updateSpectra();
    if (proc.viewDirty.exchange (false)) proc.refreshHeadphoneView();
    if (tab == 0) { response->repaint(); repaint (524, 100, 704, 84); repaint (524, 528, 704, 320); }
    for (auto& t : tiles) t->repaint();
    if (proc.reportsVersion() != lastRepVersion) { lastRepVersion = proc.reportsVersion(); report->refresh(); }
    if (proc.isAnalyzing()) analyzeB.setButtonText ("ANALYZING... " + juce::String (juce::roundToInt (proc.analyzeProgress() * 100)) + "%");
    else analyzeB.setButtonText (proc.capturedSeconds() < 2.0f ? juce::String ("PLAY YOUR TRACK FIRST") : "ANALYZE LAST " + juce::String (juce::roundToInt (std::min (8.0f, proc.capturedSeconds()))) + " SECONDS");
    if (tab == 1 && proc.activeDevice() != lastDevice) report->repaint();
    lastDevice = proc.activeDevice();
    repaint (0, 0, W, 60); repaint (524, 100, 704, 50); repaint (12, 776, 500, 80);
}

void LumenMixCheckEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    g.setColour (C::panelTop); g.fillRect (0, 0, W, 56);
    g.setColour (C::border); g.drawHorizontalLine (56, 0, (float) W);
    g.setColour (C::text); g.setFont (font (24.0f)); g.drawText ("LUMEN", 18, 12, 100, 32, juce::Justification::centredLeft);
    g.setColour (C::cyan); g.drawText ("MIXCHECK", 108, 12, 160, 32, juce::Justification::centredLeft);
    g.setColour (C::dim); g.setFont (font (10.5f, false)); g.drawText ("hear your mix on every system", 238, 20, 220, 18, juce::Justification::centredLeft);
    // export safety chip
    {
        const bool safe = pv (IDs::renderBypass) > 0.5f;
        auto chip = juce::Rectangle<float> (W - 430.0f, 14, 412, 28);
        g.setColour ((safe ? C::green : C::red).withAlpha (0.14f)); g.fillRoundedRectangle (chip, 6.0f);
        g.setColour (safe ? C::green : C::red); g.drawRoundedRectangle (chip, 6.0f, 1.0f);
        g.setFont (font (11.0f));
        g.drawText (safe ? "MONITORING ONLY  -  switches itself off when you export" : "WARNING: BYPASS ON EXPORT is off - this WILL be in your export",
                    chip, juce::Justification::centred);
    }
    panel (g, { 12, 66, 500, 476 }, "LISTEN ON", C::cyan, true);
    if (pv (IDs::cycle) > 0.5f) { g.setColour (C::cyan); g.setFont (font (10.0f)); g.drawText ("CYCLING WITH THE SONG", 300, 70, 200, 14, juce::Justification::centredRight); }
    panel (g, { 12, 550, 500, 218 }, "CONTROLS", C::cyan, true);
    g.setColour (C::dim); g.setFont (font (10.0f)); g.drawText ("CYCLE", 22, 728, 52, 26, juce::Justification::centredLeft);
    if (irMsg.isNotEmpty()) { g.setColour (C::dim); g.setFont (font (9.5f, false)); g.drawText (irMsg, 236, 756, 260, 12, juce::Justification::centredLeft); }

    // your headphones
    {
        const int m = juce::jlimit (0, numHeadphones() - 1, (int) pv (IDs::hpModel));
        panel (g, { 12, 776, 500, 76 }, "YOUR HEADPHONES", C::violet, m != 0);
        g.setColour (C::dim); g.setFont (font (9.5f, false));
        juce::String t = hpMsg.isNotEmpty() ? hpMsg : juce::String (headphoneModels()[(size_t) m].note);
        if (m == customHeadphoneIndex() && proc.customEqName().isNotEmpty() && hpMsg.isEmpty()) t = "Custom: " + proc.customEqName();
        g.drawFittedText (t, juce::Rectangle<int> (22, 834, 384, 16), juce::Justification::centredLeft, 1, 0.8f);
        if (m != 0) { g.setColour (C::violet); g.setFont (mono (9.5f)); g.drawText ("pre " + juce::String (proc.headphonePreampDb(), 1) + " dB", 150, 778, 120, 18, juce::Justification::centredRight); }
    }
    // right
    g.setColour (C::panelTop); g.fillRoundedRectangle (524, 100, 704, 748, 6.0f);
    g.setColour (C::border); g.drawRoundedRectangle (524.5f, 100.5f, 703, 747, 6.0f, 1.0f);
    const int dev = proc.activeDevice(); const auto& d = devices()[(size_t) dev]; const auto col = catColour (d.cat);
    if (tab == 0)
    {
        drawIcon (g, d.icon, { 536, 110, 44, 44 }, col);
        g.setColour (C::text); g.setFont (font (20.0f)); g.drawText (d.name, 592, 108, 420, 26, juce::Justification::centredLeft);
        g.setColour (col); g.setFont (font (10.0f)); g.drawText (catName (d.cat), 592, 134, 200, 14, juce::Justification::centredLeft);
        if (d.special == Custom) { g.setColour (C::dim); g.drawText (proc.irName().isEmpty() ? juce::String ("no IR loaded - press LOAD IR") : "IR: " + proc.irName(), 700, 134, 300, 14, juce::Justification::centredLeft); }
        g.setColour (C::text.withAlpha (0.75f)); g.setFont (font (11.0f, false));
        g.drawFittedText (juce::String ("LISTEN FOR:  ") + d.listenFor, juce::Rectangle<int> (592, 150, 628, 30), juce::Justification::centredLeft, 2);

        // what it does to your mix (live)
        panel (g, { 532, 532, 336, 308 }, "WHAT IT DOES TO YOUR MIX", col, true);
        for (int b = 0; b < DeviceReport::kBands; ++b)
        {
            const float y = 566.0f + (float) b * 52.0f, x0 = 600, w = 250, zx = x0 + w * (40.0f / 60.0f);
            g.setColour (C::dim); g.setFont (font (10.5f)); g.drawText (reportBandName (b), 544, (int) y, 52, 26, juce::Justification::centredLeft);
            g.setColour (C::field); g.fillRoundedRectangle (x0, y + 4, w, 18, 3.0f);
            g.setColour (C::border); g.drawVerticalLine ((int) zx, y + 2, y + 24);
            const float v = dev == 0 ? 0.0f : bars[b], vx = x0 + w * (juce::jlimit (-40.0f, 20.0f, v) + 40.0f) / 60.0f;
            const auto c = v < -18 ? C::red : v < -6 ? C::orange : v > 6 ? C::blue : v > 3 ? C::cyan : C::green;
            g.setColour (c.withAlpha (0.85f)); g.fillRoundedRectangle (std::min (zx, vx), y + 7, std::max (2.0f, std::abs (vx - zx)), 12, 2.0f);
            g.setColour (C::text); g.setFont (mono (10.0f));
            g.drawText ((v > 0 ? "+" : "") + juce::String (v, 1) + " dB", (int) x0, (int) y + 22, (int) w, 14, juce::Justification::centredRight);
        }
        // meters
        panel (g, { 876, 532, 344, 308 }, "LOUDNESS & STATUS", C::cyan, true);
        auto stat = [&] (int i, const juce::String& k, const juce::String& v, juce::Colour c)
        {
            auto b = juce::Rectangle<float> (886.0f + (float) (i % 3) * 110.0f, 564.0f + (float) (i / 3) * 80.0f, 104, 72);
            g.setColour (C::field); g.fillRoundedRectangle (b, 5.0f);
            g.setColour (C::dim); g.setFont (font (9.0f)); g.drawText (k, b.removeFromTop (24), juce::Justification::centred);
            g.setColour (c); g.setFont (mono (16.0f)); g.drawText (v, b.withTrimmedBottom (14), juce::Justification::centred);
        };
        auto lu = [] (float v) { return v < -69.0f ? juce::String ("--") : juce::String (v, 1); };
        stat (0, "MIX LUFS (S)", lu (proc.inLufsS.load()), C::text);
        stat (1, "MIX LUFS (INT)", lu (proc.inLufsI.load()), C::text);
        stat (2, "MIX TRUE PEAK", proc.inTp.load() < -99 ? juce::String ("--") : juce::String (proc.inTp.load(), 1), proc.inTp.load() > -1.0f ? C::red : C::text);
        const float sg = proc.streamGainDb.load();
        stat (3, d.special == Stream ? "PLATFORM GAIN" : "LEVEL MATCH", d.special == Stream ? (d.target >= 0 ? juce::String ("none") : (sg > 0 ? "+" : "") + juce::String (sg, 1)) : (pv (IDs::levelMatch) > 0.5f ? juce::String (proc.levelMatchDb.load(), 1) : juce::String ("off")),
              d.special == Stream && sg < -3 ? C::orange : C::cyan);
        stat (4, "DEVICE LIMITER", d.limRatio > 0 ? juce::String (proc.limGr.load(), 1) : juce::String ("--"), proc.limGr.load() < -6 ? C::orange : C::text);
        stat (5, "CORRELATION", juce::String (proc.corrOut.load(), 2), proc.corrOut.load() < 0 ? C::red : C::green);
        g.setColour (C::dim); g.setFont (font (9.5f, false));
        juce::String note;
        if (d.special == Stream && d.target < 0) note = sg < -0.5f ? juce::String (d.shortName) + " plays your track " + juce::String (-sg, 1) + " dB QUIETER than you master it"
                                                    : sg > 0.5f ? juce::String (d.shortName) + " turns your track UP " + juce::String (sg, 1) + " dB" : juce::String (d.shortName) + " plays it at your level";
        else if (proc.renderBypassed.load()) note = "Rendering: MixCheck is bypassed";
        else note = "Values in dB / LUFS.  Loudness resets when playback starts.";
        g.drawFittedText (note, juce::Rectangle<int> (886, 734, 324, 96), juce::Justification::centredLeft, 4);
    }
    else
    {
        g.setColour (C::dim); g.setFont (font (10.5f, false));
        g.drawFittedText ("Band change of every device vs. your mix (level matched).  Red = disappears, blue = exaggerated.  Click a row to listen.",
                          juce::Rectangle<int> (784, 108, 436, 38), juce::Justification::centredLeft, 2);
    }
}
