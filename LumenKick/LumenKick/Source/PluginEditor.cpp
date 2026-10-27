#include "PluginEditor.h"

using namespace lumen::led;
using namespace lumen::kick2;

namespace
{
const juce::Colour cLayer[3] { juce::Colour (0xffffd166), juce::Colour (0xffff8c42), juce::Colour (0xff4fc3ff) };
const juce::Colour cAcc { 0xffff6b5b };
namespace Lay
{
constexpr int dispY = 60, dispH = 236, viewW = 800, panelY = 304, panelH = 340, panelW = 400, masterY = 652, masterH = 124;
inline int panelX (int k) { return 12 + k * 408; }
}
}

//==============================================================================
void KickView::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    g.setColour (C::field); g.fillRoundedRectangle (all, 6.0f);
    g.setColour (C::border); g.drawRoundedRectangle (all.reduced (0.5f), 6.0f, 1.0f);
    auto r = all.reduced (8).withTrimmedBottom (14).withTrimmedTop (14);
    const int n = rd.out.getNumSamples();
    if (n < 2) { g.setColour (C::dim); g.drawText ("press PLAY or play a note", r, juce::Justification::centred); return; }
    const double lenMs = 1000.0 * n / rd.fs;
    const double viewMs = std::max (300.0, std::ceil (lenMs / 100.0) * 100.0);
    auto xOf = [&] (double ms) { return r.getX() + r.getWidth() * (float) (ms / viewMs); };
    // grid
    g.setFont (mono (9.0f));
    const double step = viewMs > 1200 ? 200 : viewMs > 600 ? 100 : 50;
    for (double ms = 0; ms <= viewMs; ms += step)
    {
        g.setColour (C::border.withAlpha (0.35f)); g.drawVerticalLine ((int) xOf (ms), r.getY(), r.getBottom());
        g.setColour (C::dim); g.drawText (juce::String ((int) ms) + " ms", (int) xOf (ms) + 3, (int) r.getBottom() + 1, 60, 12, juce::Justification::centredLeft);
    }
    g.setColour (C::border.withAlpha (0.6f)); g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
    float pk = 0.05f; for (int c = 0; c < 2; ++c) pk = std::max (pk, rd.out.getMagnitude (c, 0, n));
    const float sc = r.getHeight() * 0.48f / pk;
    auto drawWave = [&] (const float* d, int len, juce::Colour col, bool fill)
    {
        juce::Path top, bot;
        const int W = (int) r.getWidth();
        for (int px = 0; px <= W; ++px)
        {
            const int a = (int) ((double) px / W * viewMs * 0.001 * rd.fs), b = (int) ((double) (px + 1) / W * viewMs * 0.001 * rd.fs);
            float mn = 0, mx = 0;
            for (int i = a; i < std::min (b, len); ++i) { mn = std::min (mn, d[i]); mx = std::max (mx, d[i]); }
            const float x = r.getX() + (float) px;
            if (px == 0) { top.startNewSubPath (x, r.getCentreY() - mx * sc); bot.startNewSubPath (x, r.getCentreY() - mn * sc); }
            else { top.lineTo (x, r.getCentreY() - mx * sc); bot.lineTo (x, r.getCentreY() - mn * sc); }
        }
        if (fill)
        {
            juce::Path f (top);
            for (int px = W; px >= 0; --px) { /* close along the bottom edge */ }
            juce::Path area; area.addPath (top);
            // build a closed shape: top edge forward, bottom edge back
            juce::Path closed;
            {
                juce::PathFlatteningIterator it1 (top), it2 (bot);
                std::vector<juce::Point<float>> tp, bp;
                while (it1.next()) tp.push_back ({ it1.x2, it1.y2 });
                while (it2.next()) bp.push_back ({ it2.x2, it2.y2 });
                if (! tp.empty() && ! bp.empty())
                {
                    closed.startNewSubPath (tp.front());
                    for (auto& p : tp) closed.lineTo (p);
                    for (auto it = bp.rbegin(); it != bp.rend(); ++it) closed.lineTo (*it);
                    closed.closeSubPath();
                }
            }
            g.setColour (col.withAlpha (0.28f)); g.fillPath (closed);
        }
        g.setColour (col.withAlpha (fill ? 0.85f : 1.0f));
        g.strokePath (top, juce::PathStrokeType (1.0f)); g.strokePath (bot, juce::PathStrokeType (1.0f));
    };
    for (int k = 2; k >= 0; --k)
        if (! rd.layers[(size_t) k].empty() && proc.P (IDs::L (k, "on")) > 0.5f)
            drawWave (rd.layers[(size_t) k].data(), (int) rd.layers[(size_t) k].size(), cLayer[k], true);
    drawWave (rd.out.getReadPointer (0), n, juce::Colours::white.withAlpha (0.8f), false);

    // sub pitch curve (log Hz on the right)
    if (! rd.lowHz.empty() && proc.P (IDs::L (2, "syn")) > -59.9f)
    {
        auto yHz = [&] (double hz) { return r.getBottom() - r.getHeight() * (float) (std::log10 (std::max (20.0, hz) / 20.0) / std::log10 (50.0)); };
        juce::Path pc;
        for (size_t ms = 0; ms < rd.lowHz.size() && ms <= (size_t) viewMs; ++ms)
        {
            const float x = xOf ((double) ms), y = yHz (rd.lowHz[ms]);
            if (ms == 0) pc.startNewSubPath (x, y); else pc.lineTo (x, y);
        }
        juce::Path dashed; const float dl[] { 4.0f, 3.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, pc, dl, 2);
        g.setColour (cLayer[2].withAlpha (0.9f)); g.fillPath (dashed);
        g.setFont (mono (9.0f));
        g.drawText ("SUB " + hzNote (rd.lowHz.back()), (int) r.getRight() - 120, (int) r.getY(), 118, 12, juce::Justification::centredRight);
    }
    // playhead
    const auto since = juce::Time::getMillisecondCounter() - proc.lastHitMs.load();
    if (proc.lastHitMs.load() != 0 && since < viewMs)
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f)); g.fillRect (xOf ((double) since), r.getY(), 1.5f, r.getHeight());
    }
    g.setFont (font (10.0f)); g.setColour (C::text.withAlpha (0.8f));
    g.drawText ("KICK  " + juce::String (juce::roundToInt (lenMs)) + " ms", 10, 2, 200, 14, juce::Justification::centredLeft);
    g.setColour (C::dim); g.drawText ("drag into your DAW", (int) all.getRight() - 160, 2, 150, 14, juce::Justification::centredRight);
    if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - msgTime < 6000)
    {
        g.setFont (font (11.0f));
        const float w = std::min (r.getWidth() - 20, (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), message) + 30.0f);
        auto b = juce::Rectangle<float> (r.getCentreX() - w * 0.5f, r.getBottom() - 30, w, 24);
        g.setColour (juce::Colour (0xee23272d)); g.fillRoundedRectangle (b, 5);
        g.setColour (cAcc); g.drawRoundedRectangle (b, 5, 1); g.drawText (message, b, juce::Justification::centred);
    }
}

void KickView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    juce::File f;
    const auto msg = proc.exportWav (f);
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
    message = msg; msgTime = juce::Time::getMillisecondCounter();
}

//==============================================================================
int SpecView::hit (float x) const
{
    if (std::abs (x - fx (proc.P (IDs::x1))) < 7) return 1;
    if (std::abs (x - fx (proc.P (IDs::x2))) < 7) return 2;
    return 0;
}
void SpecView::mouseMove (const juce::MouseEvent& e) { const int h = hit ((float) e.x); if (h != hover) { hover = h; repaint(); } setMouseCursor (h ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); }
void SpecView::mouseDown (const juce::MouseEvent& e) { drag = hit ((float) e.x); if (drag) if (auto* p = proc.apvts.getParameter (drag == 1 ? IDs::x1 : IDs::x2)) p->beginChangeGesture(); }
void SpecView::mouseDrag (const juce::MouseEvent& e)
{
    if (! drag) return;
    if (auto* p = proc.apvts.getParameter (drag == 1 ? IDs::x1 : IDs::x2)) p->setValueNotifyingHost (p->convertTo0to1 ((float) xf ((float) e.x)));
    repaint();
}
void SpecView::mouseUp (const juce::MouseEvent&) { if (drag) if (auto* p = proc.apvts.getParameter (drag == 1 ? IDs::x1 : IDs::x2)) p->endChangeGesture(); drag = 0; }

void SpecView::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    g.setColour (C::field); g.fillRoundedRectangle (all, 6.0f);
    g.setColour (C::border); g.drawRoundedRectangle (all.reduced (0.5f), 6.0f, 1.0f);
    auto r = all.reduced (6);
    auto y = [&] (float db) { return r.getBottom() - 14 - (r.getHeight() - 30) * juce::jlimit (0.0f, 1.0f, (db + 72.0f) / 72.0f); };
    g.setFont (mono (8.5f));
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (C::border.withAlpha (0.35f)); g.drawVerticalLine ((int) fx (f), r.getY() + 16, r.getBottom() - 14);
        g.setColour (C::dim); g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) fx (f) - 14, (int) r.getBottom() - 12, 28, 10, juce::Justification::centred);
    }
    const float x1 = fx (proc.P (IDs::x1)), x2 = fx (proc.P (IDs::x2));
    g.setColour (cLayer[2].withAlpha (0.05f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), r.getY() + 16, x1, r.getBottom() - 14));
    g.setColour (cLayer[1].withAlpha (0.05f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x1, r.getY() + 16, x2, r.getBottom() - 14));
    g.setColour (cLayer[0].withAlpha (0.05f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x2, r.getY() + 16, r.getRight(), r.getBottom() - 14));
    auto curve = [&] (const std::vector<float>& s, juce::Colour c, float w)
    {
        if (s.empty()) return;
        juce::Path p; bool st = false;
        for (float x = r.getX(); x <= r.getRight(); x += 2)
        {
            const double f = xf (x); const int bin = juce::jlimit (1, (int) s.size() - 1, (int) (f * rd.specN / rd.fs));
            float v = s[(size_t) bin];
            const int b2 = juce::jlimit (1, (int) s.size() - 1, (int) (xf (x + 2) * rd.specN / rd.fs));
            for (int b = bin; b <= b2; ++b) v = std::max (v, s[(size_t) b]);
            if (! st) { p.startNewSubPath (x, y (v)); st = true; } else p.lineTo (x, y (v));
        }
        g.setColour (c); g.strokePath (p, juce::PathStrokeType (w));
    };
    for (int k = 0; k < 3; ++k) if (proc.P (IDs::L (k, "on")) > 0.5f) curve (rd.spec[(size_t) k], cLayer[k].withAlpha (0.8f), 1.2f);
    curve (rd.spec[3], juce::Colours::white.withAlpha (0.55f), 1.0f);
    for (int k = 1; k <= 2; ++k)
    {
        const float x = k == 1 ? x1 : x2; const bool act = hover == k || drag == k;
        g.setColour (C::text.withAlpha (act ? 0.95f : 0.5f)); g.fillRect (x - (act ? 1.0f : 0.5f), r.getY() + 16, act ? 2.0f : 1.0f, r.getHeight() - 30);
        g.setFont (mono (9.0f));
        const float v = proc.P (k == 1 ? IDs::x1 : IDs::x2);
        g.drawText (v < 1000 ? juce::String ((int) v) + " Hz" : juce::String (v / 1000.0f, 1) + " kHz", (int) x - 30, (int) r.getY() + 18, 60, 11, juce::Justification::centred);
    }
    g.setFont (font (10.0f));
    for (int k = 0; k < 3; ++k)
    {
        const float cx = k == 2 ? (r.getX() + x1) * 0.5f : k == 1 ? (x1 + x2) * 0.5f : (x2 + r.getRight()) * 0.5f;
        g.setColour (cLayer[k]); g.drawText (layerNames()[k], (int) cx - 30, (int) r.getY(), 60, 14, juce::Justification::centred);
    }
}

//==============================================================================
LumenKickEditor::Knob& LumenKickEditor::knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip, const juce::String& tip)
{
    auto k = std::make_unique<Knob> (proc.apvts, id, lbl, c, bip);
    k->setBounds (r); k->setTooltip (tip); addAndMakeVisible (*k);
    auto& ref = *k; owned.push_back (std::move (k)); return ref;
}
LumenKickEditor::Toggle& LumenKickEditor::toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r, bool round, const juce::String& tip)
{
    auto t = std::make_unique<Toggle> (proc.apvts, id, txt, c, round);
    t->setBounds (r); t->setTooltip (tip); addAndMakeVisible (*t);
    auto& ref = *t; owned.push_back (std::move (t)); return ref;
}
LumenKickEditor::Combo& LumenKickEditor::combo (const juce::String& id, juce::Colour c, juce::Rectangle<int> r, const juce::String& tip)
{
    auto cb = std::make_unique<Combo> (proc.apvts, id, c);
    cb->setBounds (r); cb->setTooltip (tip); addAndMakeVisible (*cb);
    auto& ref = *cb; owned.push_back (std::move (cb)); return ref;
}

void LumenKickEditor::chooseFile (std::function<void (const juce::File&)> then)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [then] (const juce::FileChooser& fc) { const auto f = fc.getResult(); if (f.existsAsFile()) then (f); });
}

LumenKickEditor::LumenKickEditor (LumenKickProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (W, H);

    // ---- header
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (186, 14, 240, 26);
    for (auto* b : { &prevB, &nextB, &abA, &abB, &abCopy, &playB, &loadKickB, &exportB }) { b->setColour (juce::TextButton::buttonColourId, cAcc); addAndMakeVisible (*b); }
    prevB.setBounds (430, 14, 28, 26); nextB.setBounds (460, 14, 28, 26);
    auto step = [this] (int d) { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + d + n) % n); proc.audition(); };
    prevB.onClick = [step] { step (-1); }; nextB.onClick = [step] { step (1); };
    abA.setBounds (498, 14, 30, 26); abB.setBounds (530, 14, 30, 26); abCopy.setBounds (562, 14, 50, 26);
    abA.onClick = [this] { proc.switchAB (0); }; abB.onClick = [this] { proc.switchAB (1); }; abCopy.onClick = [this] { proc.copyAB(); };
    playB.setBounds (622, 14, 70, 26); playB.onClick = [this] { proc.audition(); };
    playB.setTooltip ("Play the kick (or play MIDI notes - with Key: Sub the sub follows the note)");
    loadKickB.setBounds (700, 14, 160, 26);
    loadKickB.setTooltip ("Load a kick sample: it is split into TOP / MID / LOW (the three add up to the original exactly). Or drop a file on the waveform");
    loadKickB.onClick = [this] { chooseFile ([this] (const juce::File& f) { showMessage (proc.loadKick (f)); proc.audition(); }); };
    exportB.setBounds (868, 14, 100, 26);
    exportB.setTooltip ("Save the kick as a 24-bit WAV in Documents / Lumen Kick (or drag the waveform into your DAW)");
    exportB.onClick = [this] { juce::File f; showMessage (proc.exportWav (f)); };
    combo (IDs::root, cLayer[2], { 978, 14, 72, 26 }, "Root note: the sub's pitch (when Key Track is off, or for PLAY)");
    combo (IDs::keyTrack, cLayer[2], { 1056, 14, 172, 26 }, "Key Track: Sub = the sub follows the MIDI note (tuned kicks), All = all layers follow");

    view = std::make_unique<KickView> (proc, rd); addAndMakeVisible (*view); view->setBounds (12, Lay::dispY, Lay::viewW, Lay::dispH);
    spec = std::make_unique<SpecView> (proc, rd); addAndMakeVisible (*spec); spec->setBounds (12 + Lay::viewW + 8, Lay::dispY, W - 24 - Lay::viewW - 8, Lay::dispH);

    // ---- layer panels
    for (int k = 0; k < 3; ++k)
    {
        const int x = Lay::panelX (k), y = Lay::panelY;
        const auto c = cLayer[k];
        toggle (IDs::L (k, "on"), "", c, { x + 8, y + 5, 18, 18 }, true, layerNames()[k] + " on / off");
        toggle (IDs::L (k, "filt"), "BAND", c, { x + 230, y + 4, 66, 20 }, false, "On: this layer plays only its band of the sample (TOP / MID / LOW split). Off: the full sample");
        toggle (IDs::L (k, "solo"), "S", c, { x + 302, y + 4, 26, 20 }, false, "Solo");
        toggle (IDs::L (k, "inv"), juce::String (juce::CharPointer_UTF8 ("\xc3\x98")), c, { x + 332, y + 4, 26, 20 }, false, "Phase invert (180°) - fix a layer that cancels the others");
        auto& nb = smpName[(size_t) k]; nb.setColour (juce::TextButton::buttonColourId, c); addAndMakeVisible (nb);
        nb.setBounds (x + 10, y + 30, 268, 24);
        nb.setTooltip ("Load a sample into this layer (or drop a file on this panel)");
        nb.onClick = [this, k] { chooseFile ([this, k] (const juce::File& f) { showMessage (proc.loadLayer (k, f)); proc.audition(); }); };
        auto mk = [&] (juce::TextButton& b, const char* t, int bx, std::function<void()> fn, const char* tip)
        { b.setButtonText (t); b.setColour (juce::TextButton::buttonColourId, c); b.setBounds (x + bx, y + 30, 32, 24); b.onClick = fn; b.setTooltip (tip); addAndMakeVisible (b); };
        mk (smpPrev[(size_t) k], "<", 282, [this, k] { const auto m = proc.stepLayer (k, -1); if (m.isNotEmpty()) showMessage (m); proc.audition(); }, "Previous sample in the same folder");
        mk (smpNext[(size_t) k], ">", 318, [this, k] { const auto m = proc.stepLayer (k, 1); if (m.isNotEmpty()) showMessage (m); proc.audition(); }, "Next sample in the same folder");
        mk (smpClear[(size_t) k], "X", 354, [this, k] { proc.clearLayer (k); }, "Remove the sample from this layer");

        const int kw = 64, kh = 76;
        knob (IDs::L (k, "smp"), "LEVEL", c, { x + 6, y + 70, kw, kh }, false, "Sample level");
        knob (IDs::L (k, "spitch"), "PITCH", c, { x + 72, y + 70, kw, kh }, true, "Sample pitch (semitones)");
        knob (IDs::L (k, "sstart"), "START", c, { x + 138, y + 70, kw, kh }, false, "Skip the start of the sample");
        knob (IDs::L (k, "nudge"), "NUDGE", c, { x + 204, y + 70, kw, kh }, false, "Delay this layer (align transients and phase between layers)");

        const int sy = y + 164;
        knob (IDs::L (k, "syn"), "LEVEL", c, { x + 6, sy, kw, kh }, false, "Synth level (Off = sample only)");
        if (k == Top)
        {
            combo (IDs::L (0, "type"), c, { x + 76, sy + 22, 100, 24 }, "Click type: Click (sine burst), Noise, Snap (909-style), Tick (very short)");
            knob (IDs::L (0, "tone"), "TONE", c, { x + 186, sy, kw, kh }, false, "Click brightness");
            knob (IDs::L (0, "tdec"), "LENGTH", c, { x + 252, sy, kw, kh }, false, "Click length");
        }
        else if (k == Mid)
        {
            knob (IDs::L (1, "freq"), "FREQ", c, { x + 72, sy, kw, kh }, false, "Body frequency (where the punch sits)");
            knob (IDs::L (1, "start"), "SWEEP", c, { x + 138, sy, kw, kh }, false, "How far the body pitch drops from (semitones)");
            knob (IDs::L (1, "pdec"), "SPEED", c, { x + 204, sy, kw, kh }, false, "How fast the body pitch drops");
            knob (IDs::L (1, "wave"), "WAVE", c, { x + 270, sy, kw, kh }, false, "Sine -> triangle -> square: more harmonics, more knock");
            knob (IDs::L (1, "drive"), "DRIVE", c, { x + 336, sy, kw - 4, kh }, false, "Body saturation");
        }
        else
        {
            knob (IDs::L (2, "start"), "SWEEP", c, { x + 72, sy, kw, kh }, false, "Pitch drop (semitones) - the classic kick 'boom'");
            knob (IDs::L (2, "pdec"), "SPEED", c, { x + 138, sy, kw, kh }, false, "How fast the sub reaches its note: short = punchy, long = zappy / 808");
            knob (IDs::L (2, "drive"), "DRIVE", c, { x + 204, sy, kw, kh }, false, "Sub saturation (audible on small speakers)");
        }
        const int ey = y + 258;
        knob (IDs::L (k, "att"), "ATTACK", c, { x + 6, ey, kw, kh }, false, "Fade-in of this layer");
        knob (IDs::L (k, "dec"), "DECAY", c, { x + 72, ey, kw, kh }, false, "Length of this layer");
        knob (IDs::L (k, "curve"), "CURVE", c, { x + 138, ey, kw, kh }, false, "Decay shape: low = holds then drops (punchy), high = fast fade");
    }

    // ---- master
    {
        const int x = 12, y = Lay::masterY + 36, kw = 78, kh = 88;
        combo (IDs::drvType, cAcc, { x + 10, y - 4, 120, 24 }, "Master drive type");
        knob (IDs::drvAmt, "DRIVE", cAcc, { x + 30, y + 24, kw, kh - 24 }, false, "Master drive (2x oversampled)");
        int cx = x + 140;
        auto add = [&] (const juce::String& id, const char* l, bool bip, const char* tip) { knob (id, l, cAcc, { cx, y, kw, kh }, bip, tip); cx += kw + 6; };
        add (IDs::eqLow, "LOW", true, "Low shelf (70 Hz)");
        add (IDs::eqMidF, "MID FREQ", false, "Mid bell frequency");
        add (IDs::eqMidG, "MID", true, "Mid bell gain (cut the boxiness around 300-500 Hz)");
        add (IDs::eqHigh, "HIGH", true, "High shelf (7 kHz)");
        add (IDs::velSens, "VELOCITY", false, "How much MIDI velocity changes the level");
        add (IDs::pitch, "PITCH", true, "Global pitch (all layers)");
        add (IDs::x1, "LOW / MID", false, "Split frequency between LOW and MID (also draggable on the spectrum)");
        add (IDs::x2, "MID / TOP", false, "Split frequency between MID and TOP");
        add (IDs::out, "OUTPUT", true, "Output level");
        toggle (IDs::clip, "CLIPPER", cAcc, { cx + 4, y + 30, 100, 26 }, false, "Soft clipper at the output - loud and never above 0 dBFS");
    }
    startTimerHz (30);
}

LumenKickEditor::~LumenKickEditor() { setLookAndFeel (nullptr); }

int LumenKickEditor::dropTarget (int x, int y) const
{
    if (y >= Lay::dispY && y < Lay::dispY + Lay::dispH) return 3;
    if (y >= Lay::panelY && y < Lay::panelY + Lay::panelH)
        for (int k = 0; k < 3; ++k) if (x >= Lay::panelX (k) && x < Lay::panelX (k) + Lay::panelW) return k;
    return -1;
}

void LumenKickEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropX = -1; repaint();
    if (files.isEmpty()) return;
    const juce::File f (files[0]);
    const int t = dropTarget (x, y);
    if (t == 3 || t < 0) showMessage (proc.loadKick (f));
    else showMessage (proc.loadLayer (t, f));
    proc.audition();
}

void LumenKickEditor::rerender()
{
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const auto p = proc.readParams();
    Engine::renderHit (p, proc.samplePointers(), fs, p.root, rd.out, &rd.layers);
    rd.fs = fs;
    // spectra
    juce::dsp::FFT fft (13); const int N = 8192; rd.specN = N;
    std::vector<float> buf ((size_t) N * 2);
    auto spectrum = [&] (const float* d, int len, std::vector<float>& outDb)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < std::min (len, N); ++i) buf[(size_t) i] = d[i];
        fft.performFrequencyOnlyForwardTransform (buf.data());
        outDb.assign ((size_t) N / 2, -120.0f);
        for (int b = 1; b < N / 2; ++b) outDb[(size_t) b] = 20.0f * std::log10 (buf[(size_t) b] * 0.02f + 1e-9f);
    };
    for (int k = 0; k < 3; ++k) spectrum (rd.layers[(size_t) k].data(), (int) rd.layers[(size_t) k].size(), rd.spec[(size_t) k]);
    spectrum (rd.out.getReadPointer (0), rd.out.getNumSamples(), rd.spec[3]);
    // normalise: loudest bin of the master at -6 dB
    float mx = -120; for (float v : rd.spec[3]) mx = std::max (mx, v);
    for (auto& sp : rd.spec) for (auto& v : sp) v -= mx + 6.0f;
    // sub pitch curve
    Engine e; e.prepare (fs, 64); e.setParams (p);
    rd.lowHz.clear();
    const int lenMs = (int) (1000.0 * rd.out.getNumSamples() / fs);
    for (int ms = 0; ms <= lenMs; ++ms) rd.lowHz.push_back ((float) e.lowFreqAt (ms));
}

void LumenKickEditor::timerCallback()
{
    proc.collectGarbage();
    proc.resplitIfNeeded();
    // re-render the preview when anything changed
    std::vector<float> snap;
    for (auto* ap : proc.getParameters()) snap.push_back (ap->getValue());
    if (snap != lastSnap || proc.sampleVersion.load() != lastSampleVersion) { lastSnap = snap; lastSampleVersion = proc.sampleVersion.load(); dirty = true; }
    if (dirty && juce::Time::getMillisecondCounter() - lastRender > 60) { rerender(); dirty = false; lastRender = juce::Time::getMillisecondCounter(); spec->repaint(); }
    const int v = proc.programVersion.load() + proc.getCurrentProgram() * 100000;
    if (v != lastVersion) { lastVersion = v; presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    abA.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification); abB.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    for (int k = 0; k < 3; ++k)
    {
        const auto s = proc.layerSample (k);
        smpName[(size_t) k].setButtonText (s != nullptr ? s->name : juce::String ("+ load / drop a sample"));
    }
    meter = std::max (proc.outPk.load(), meter * 0.85f);
    view->repaint();
    repaint (W - 40, Lay::masterY, 30, Lay::masterH);
}

void LumenKickEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    g.setColour (C::panelTop); g.fillRect (0, 0, W, 52);
    g.setColour (C::border); g.drawHorizontalLine (52, 0, (float) W);
    g.setColour (C::text); g.setFont (font (22.0f)); g.drawText ("LUMEN", 16, 10, 90, 32, juce::Justification::centredLeft);
    g.setColour (cAcc); g.drawText ("KICK", 96, 10, 70, 32, juce::Justification::centredLeft);
    g.setColour (C::dim); g.setFont (font (9.5f, false)); g.drawText ("v2", 152, 22, 20, 14, juce::Justification::centredLeft);

    for (int k = 0; k < 3; ++k)
    {
        const int x = Lay::panelX (k), y = Lay::panelY;
        const bool on = proc.P (IDs::L (k, "on")) > 0.5f;
        panel (g, juce::Rectangle<float> ((float) x, (float) y, (float) Lay::panelW, (float) Lay::panelH), "", cLayer[k], on);
        g.setColour (on ? cLayer[k] : C::dim); g.setFont (font (13.0f));
        const char* sub[] { "CLICK", "BODY / PUNCH", "SUB" };
        g.drawText (layerNames()[k] + "  " + sub[k], x + 32, y + 4, 190, 20, juce::Justification::centredLeft);
        g.setFont (font (9.5f)); g.setColour (C::dim);
        g.drawText ("SAMPLE", x + 280, y + 70, 110, 12, juce::Justification::centredLeft);
        g.drawText ("SYNTH", x + 10, y + 150, 100, 12, juce::Justification::centredLeft);
        g.drawText ("ENVELOPE", x + 10, y + 244, 100, 12, juce::Justification::centredLeft);
        g.setColour (C::border); g.drawHorizontalLine (y + 156, (float) x + 60, (float) x + Lay::panelW - 10); g.drawHorizontalLine (y + 250, (float) x + 76, (float) x + Lay::panelW - 10);
        if (const auto s = proc.layerSample (k))
        {
            g.setFont (mono (9.5f)); g.setColour (C::text.withAlpha (0.75f));
            const double lenMs = 1000.0 * s->full.getNumSamples() / s->rate;
            g.drawText (juce::String (lenMs, 0) + " ms", x + 280, y + 88, 110, 12, juce::Justification::centredLeft);
            if (k == Low && s->lowHz > 0) { g.setColour (cLayer[2]); g.drawText (hzNote (s->lowHz), x + 280, y + 104, 110, 12, juce::Justification::centredLeft); }
            if (proc.P (IDs::L (k, "filt")) > 0.5f)
            {
                g.setColour (C::dim);
                g.drawText (k == Low ? "< " + juce::String ((int) proc.P (IDs::x1)) + " Hz" : k == Mid ? juce::String ((int) proc.P (IDs::x1)) + " - " + juce::String ((int) proc.P (IDs::x2)) + " Hz" : "> " + juce::String ((int) proc.P (IDs::x2)) + " Hz",
                            x + 280, y + 120, 110, 12, juce::Justification::centredLeft);
            }
        }
        if (k == Low)
        {
            g.setFont (mono (9.5f)); g.setColour (C::dim);
            const int note = proc.P (IDs::keyTrack) > 0.5f && proc.lastNote.load() >= 0 ? proc.lastNote.load() : kRootBase + (int) proc.P (IDs::root);
            g.drawText ("note " + noteText (note) + " (" + juce::String (midiHz (note), 1) + " Hz)", x + 276, y + 186, 120, 12, juce::Justification::centredLeft);
        }
        if (dropX >= 0 && dropTarget (dropX, dropY) == k)
        {
            g.setColour (cLayer[k].withAlpha (0.15f)); g.fillRoundedRectangle ((float) x, (float) y, (float) Lay::panelW, (float) Lay::panelH, 6.0f);
            g.setColour (cLayer[k]); g.setFont (font (14.0f)); g.drawText ("DROP: replace " + layerNames()[k], x, y + Lay::panelH / 2 - 10, Lay::panelW, 20, juce::Justification::centred);
        }
    }
    if (dropX >= 0 && dropTarget (dropX, dropY) == 3)
    {
        g.setColour (cAcc.withAlpha (0.15f)); g.fillRoundedRectangle (12.0f, (float) Lay::dispY, (float) Lay::viewW, (float) Lay::dispH, 6.0f);
    }
    panel (g, juce::Rectangle<float> (12.0f, (float) Lay::masterY, (float) (W - 24), (float) Lay::masterH), "MASTER", cAcc);
    ledMeter (g, { (float) W - 34, (float) Lay::masterY + 30, 10, (float) Lay::masterH - 44 }, meter, meter, true, 0);
}
