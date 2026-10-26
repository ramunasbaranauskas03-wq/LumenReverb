#include "PluginEditor.h"

using namespace lumen::led;

namespace
{
const juce::Colour cSub  { 0xff4fc3ff };    // SUB   - cyan
const juce::Colour cBody { 0xffe6a440 };    // BODY  - amber
const juce::Colour cAir  { 0xffa293ff };    // AIR   - violet

juce::String noteText (float hz)
{
    if (hz <= 0) return "--";
    int oct; float cents; const char* n = lumen::bass2::noteName (hz, oct, cents);
    return juce::String (n) + juce::String (oct) + " " + (cents >= 0 ? "+" : "") + juce::String (juce::roundToInt (cents)) + "c";
}
}

//==============================================================================
BassDisplay::BassDisplay (LumenBassProcessor& p, const BassSpectra& s) : proc (p), sp (s)
{
    setTooltip ("Drag the SUB | BODY and BODY | AIR lines to move the band splits");
}

int BassDisplay::hitLine (float x) const
{
    if (std::abs (x - fx (proc.P (IDs::x1))) < 7) return 1;
    if (std::abs (x - fx (proc.P (IDs::x2))) < 7) return 2;
    return 0;
}

void BassDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitLine ((float) e.x);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void BassDisplay::mouseDown (const juce::MouseEvent& e)
{
    drag = hitLine ((float) e.x);
    if (drag) if (auto* p = proc.apvts.getParameter (drag == 1 ? IDs::x1 : IDs::x2)) p->beginChangeGesture();
}

void BassDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (! drag) return;
    if (auto* p = proc.apvts.getParameter (drag == 1 ? IDs::x1 : IDs::x2))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) xf ((float) e.x)));
    repaint();
}

void BassDisplay::mouseUp (const juce::MouseEvent&)
{
    if (drag) if (auto* p = proc.apvts.getParameter (drag == 1 ? IDs::x1 : IDs::x2)) p->endChangeGesture();
    drag = 0;
}

void BassDisplay::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    g.setColour (C::field); g.fillRoundedRectangle (all, 6.0f);
    g.setColour (C::border); g.drawRoundedRectangle (all.reduced (0.5f), 6.0f, 1.0f);
    auto r = plot();
    const double nyq = sp.fs * 0.5;
    const float x1 = fx (proc.x1Now.load()), x2 = fx (proc.x2Now.load());
    const float x1p = fx (proc.P (IDs::x1)), x2p = fx (proc.P (IDs::x2));

    // band zones
    struct Zone { float a, b; juce::Colour c; const char* name; };
    const Zone zones[] { { r.getX(), x1, cSub, "SUB" }, { x1, x2, cBody, "BODY" }, { x2, r.getRight(), cAir, "AIR" } };
    for (auto& z : zones) { g.setColour (z.c.withAlpha (0.045f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (z.a, r.getY(), z.b, r.getBottom())); }

    // grid
    g.setFont (mono (9.0f));
    for (double f : { 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0, 800.0, 900.0, 1000.0, 2000.0, 3000.0, 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0, 10000.0, 20000.0 })
    {
        const float x = fx (f); const int fi = juce::roundToInt (f);
        const bool major = fi == 20 || fi == 50 || fi == 100 || fi == 200 || fi == 500 || fi == 1000 || fi == 2000 || fi == 5000 || fi == 10000 || fi == 20000;
        g.setColour (C::border.withAlpha (major ? 0.6f : 0.25f)); g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        if (major) { g.setColour (C::dim); g.drawText (f >= 1000 ? juce::String (fi / 1000) + "k" : juce::String (fi), (int) x - 18, (int) r.getBottom() + 3, 36, 12, juce::Justification::centred); }
    }
    for (float db = -72; db <= 0; db += 12) { g.setColour (C::border.withAlpha (0.3f)); g.drawHorizontalLine ((int) sy (db), r.getX(), r.getRight()); }
    if (nyq < 20000) { g.setColour (C::bg.withAlpha (0.6f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (fx (nyq), r.getY(), r.getRight(), r.getBottom())); }

    // spectra (+4.5 dB/oct tilt so a balanced bass looks flat)
    auto spec = [&] (const float* s)
    {
        juce::Path p; bool st = false; const int nb = BassSpectra::kN / 2;
        for (float x = r.getX(); x <= std::min (r.getRight(), fx (nyq)); x += 2.0f)
        {
            const float b0 = (float) (xf (x - 1) * BassSpectra::kN / sp.fs), b1 = (float) (xf (x + 1) * BassSpectra::kN / sp.fs);
            float v;
            if (b1 - b0 < 1.0f) { const float bc = juce::jlimit (1.0f, (float) (nb - 2), 0.5f * (b0 + b1)); const int k = (int) bc; v = s[k] + (s[k + 1] - s[k]) * (bc - (float) k); }
            else { v = -200; for (int k = juce::jlimit (1, nb - 1, (int) b0); k <= juce::jlimit (1, nb - 1, (int) b1); ++k) v = std::max (v, s[k]); }
            v += 4.5f * (float) std::log2 (xf (x) / 1000.0);
            if (! st) { p.startNewSubPath (x, sy (v)); st = true; } else p.lineTo (x, sy (v));
        }
        return p;
    };
    {
        auto p = spec (sp.pre); g.setColour (C::text.withAlpha (0.25f)); g.strokePath (p, juce::PathStrokeType (1.0f));
    }
    {
        auto p = spec (sp.post);
        juce::Path f (p); f.lineTo (std::min (r.getRight(), fx (nyq)), r.getBottom()); f.lineTo (r.getX(), r.getBottom()); f.closeSubPath();
        for (auto& z : zones)
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (juce::Rectangle<float>::leftTopRightBottom (z.a, r.getY(), z.b, r.getBottom()).toNearestInt());
            g.setGradientFill (juce::ColourGradient (z.c.withAlpha (0.30f), 0, r.getY(), z.c.withAlpha (0.04f), 0, r.getBottom(), false)); g.fillPath (f);
            g.setColour (z.c.withAlpha (0.85f)); g.strokePath (p, juce::PathStrokeType (1.3f));
        }
    }

    // crossover handles
    for (int k = 1; k <= 2; ++k)
    {
        const float x = k == 1 ? x1p : x2p;
        const bool act = hover == k || drag == k;
        g.setColour (C::text.withAlpha (act ? 0.9f : 0.45f));
        g.fillRect (x - (act ? 1.0f : 0.5f), r.getY(), act ? 2.0f : 1.0f, r.getHeight());
        auto tag = juce::Rectangle<float> (x - 34, r.getY() + 4, 68, 16);
        g.setColour (juce::Colour (0xff23272d)); g.fillRoundedRectangle (tag, 3);
        g.setColour (act ? C::text : C::dim); g.drawRoundedRectangle (tag, 3, 1);
        g.setFont (mono (9.5f)); g.drawText (hzText (proc.P (k == 1 ? IDs::x1 : IDs::x2)), tag, juce::Justification::centred);
    }

    // band titles with live dynamics
    auto title = [&] (const Zone& z, const juce::String& info, bool on)
    {
        const float cx = 0.5f * (std::max (z.a, r.getX()) + z.b);
        g.setFont (font (11.0f)); g.setColour (on ? z.c : C::dim);
        g.drawText (z.name, (int) cx - 60, 4, 120, 13, juce::Justification::centred);
        g.setFont (mono (9.0f)); g.setColour (C::dim);
        g.drawText (info, (int) cx - 80, 15, 160, 11, juce::Justification::centred);
    };
    auto dbs = [] (float v) { return (v >= 0 ? "+" : "") + juce::String (v, 1) + " dB"; };
    title (zones[0], "glue " + dbs (proc.subGr.load()) + (proc.duckDb.load() < -0.1f ? "  duck " + dbs (proc.duckDb.load()) : juce::String()), proc.P (IDs::subOn) > 0.5f);
    title (zones[1], "squash " + dbs (proc.midSq.load()), proc.P ("mid_on") > 0.5f);
    title (zones[2], "squash " + dbs (proc.topSq.load()), proc.P ("top_on") > 0.5f);

    // compare / delta banner
    if (proc.P (IDs::compare) > 0.5f || proc.P (IDs::delta) > 0.5f)
    {
        const bool cmp = proc.P (IDs::compare) > 0.5f;
        auto b = juce::Rectangle<float> (r.getX() + 8, r.getY() + 26, cmp ? 270.0f : 210.0f, 20);
        g.setColour ((cmp ? C::green : C::red).withAlpha (0.18f)); g.fillRoundedRectangle (b, 4);
        g.setColour (cmp ? C::green : C::red); g.setFont (font (10.0f));
        g.drawText (cmp ? "COMPARE: original at the same loudness" : "DELTA: hearing only the change", b, juce::Justification::centred);
    }
    if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - msgTime < 7000)
    {
        g.setFont (font (11.0f));
        const float w = std::min (r.getWidth() - 20, (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), message) + 30.0f);
        auto b = juce::Rectangle<float> (r.getCentreX() - w * 0.5f, r.getBottom() - 34, w, 24);
        g.setColour (juce::Colour (0xee23272d)); g.fillRoundedRectangle (b, 5);
        g.setColour (C::cyan); g.drawRoundedRectangle (b, 5, 1); g.drawText (message, b, juce::Justification::centred);
    }
}

//==============================================================================
void ScopeView::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    g.setColour (C::field); g.fillRoundedRectangle (all, 6.0f);
    g.setColour (C::border); g.drawRoundedRectangle (all.reduced (0.5f), 6.0f, 1.0f);
    g.setFont (font (10.0f)); g.setColour (C::text.withAlpha (0.85f)); g.drawText ("WAVEFORM", 12, 6, 100, 14, juce::Justification::centredLeft);
    g.setFont (mono (9.0f)); g.setColour (C::dim); g.drawText ("grey = in  |  colour = out", (int) all.getRight() - 172, 6, 160, 14, juce::Justification::centredRight);

    auto sc = all.reduced (10).withTrimmedTop (18).withHeight (all.getHeight() * 0.56f);
    g.setColour (C::border.withAlpha (0.5f)); g.drawHorizontalLine ((int) sc.getCentreY(), sc.getX(), sc.getRight());
    float pk = 0.05f; for (float v : sp.scopePre) pk = std::max (pk, std::abs (v)); for (float v : sp.scopePost) pk = std::max (pk, std::abs (v));
    auto wave = [&] (const float* s)
    {
        juce::Path p;
        for (int i = 0; i < 1024; ++i)
        {
            const float x = sc.getX() + sc.getWidth() * (float) i / 1023.0f, y = sc.getCentreY() - sc.getHeight() * 0.46f * s[i] / pk;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };
    g.setColour (C::text.withAlpha (0.25f)); g.strokePath (wave (sp.scopePre), juce::PathStrokeType (1.2f));
    glowPath (g, wave (sp.scopePost), cSub, 1.6f);

    // tuner
    auto t = all.reduced (10).withTop (sc.getBottom() + 10);
    const float bh = proc.bassHz.load(), kh = proc.kickHz.load();
    auto row = [&] (int i, const char* lbl, float hz, juce::Colour c)
    {
        auto rr = t.withHeight (22).translated (0, (float) i * 26);
        g.setColour (C::dim); g.setFont (font (10.0f)); g.drawText (lbl, rr.withWidth (50), juce::Justification::centredLeft);
        g.setColour (hz > 0 ? c : C::dim); g.setFont (font (15.0f)); g.drawText (noteText (hz), rr.withTrimmedLeft (52).withWidth (110), juce::Justification::centredLeft);
        g.setFont (mono (10.0f)); g.setColour (C::dim); g.drawText (hz > 0 ? juce::String (hz, 1) + " Hz" : juce::String ("--"), rr.withTrimmedLeft (160), juce::Justification::centredLeft);
    };
    row (0, "BASS", bh, cSub);
    row (1, "KICK", kh, C::orange);
    auto st = t.withHeight (22).translated (0, 54);
    g.setFont (font (10.0f));
    if (bh > 0 && kh > 0)
    {
        const float semis = 12.0f * std::log2 (bh / kh);
        int iv = ((int) std::lround (semis) % 12 + 12) % 12;
        const bool good = iv == 0 || iv == 7 || iv == 5;
        const int shown = iv > 6 ? 12 - iv : iv;
        g.setColour (good ? C::green : C::amber);
        g.drawText (good ? (iv == 0 ? "KICK & BASS IN KEY (same note)" : "KICK & BASS IN KEY (5th / 4th)") : "KICK vs BASS: " + juce::String (shown) + (shown == 1 ? " semitone" : " semitones") + " apart - retune the kick?", st, juce::Justification::centredLeft);
    }
    else { g.setColour (C::dim); g.drawText (kh > 0 ? "play the bass" : "route the kick to the side-chain to compare", st, juce::Justification::centredLeft); }
}

//==============================================================================
LumenBassEditor::Knob& LumenBassEditor::knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip, const juce::String& tip)
{
    auto k = std::make_unique<Knob> (proc.apvts, id, lbl, c, bip);
    k->setBounds (r); k->setTooltip (tip); addAndMakeVisible (*k);
    auto& ref = *k; owned.push_back (std::move (k)); return ref;
}
LumenBassEditor::Toggle& LumenBassEditor::toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r, bool round, const juce::String& tip)
{
    auto t = std::make_unique<Toggle> (proc.apvts, id, txt, c, round);
    t->setBounds (r); t->setTooltip (tip); addAndMakeVisible (*t);
    auto& ref = *t; owned.push_back (std::move (t)); return ref;
}
LumenBassEditor::Combo& LumenBassEditor::combo (const juce::String& id, juce::Colour c, juce::Rectangle<int> r)
{
    auto cb = std::make_unique<Combo> (proc.apvts, id, c);
    cb->setBounds (r); addAndMakeVisible (*cb);
    auto& ref = *cb; owned.push_back (std::move (cb)); return ref;
}

// layout constants
namespace L
{
constexpr int top = 64, dispH = 330, panY = top + dispH + 10, panH = LumenBassEditor::H - panY - 12;
constexpr int subX = 12, subW = 300, bodyX = 320, bandW = 330, airX = 658, gX = 996, gW = 192;
constexpr int kw = 92, kh = 98;
}

LumenBassEditor::LumenBassEditor (LumenBassProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (W, H);
    fftBuf.assign ((size_t) BassSpectra::kN * 2, 0.0f); window.resize ((size_t) BassSpectra::kN);
    for (int i = 0; i < BassSpectra::kN; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (BassSpectra::kN - 1));
    std::fill (std::begin (spectra.pre), std::end (spectra.pre), -120.0f);
    std::fill (std::begin (spectra.post), std::end (spectra.post), -120.0f);

    // ---- header
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    presetBox.setColour (juce::ComboBox::textColourId, cSub);
    addAndMakeVisible (presetBox); presetBox.setBounds (210, 15, 290, 26);
    for (auto* b : { &prevB, &nextB, &abA, &abB, &abCopy, &learnB }) { b->setColour (juce::TextButton::buttonColourId, cSub); addAndMakeVisible (*b); }
    prevB.setBounds (506, 15, 28, 26); nextB.setBounds (536, 15, 28, 26);
    auto step = [this] (int d) { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + d + n) % n); };
    prevB.onClick = [step] { step (-1); }; nextB.onClick = [step] { step (1); };
    abA.setClickingTogglesState (false); abB.setClickingTogglesState (false);
    abA.setBounds (580, 15, 30, 26); abB.setBounds (612, 15, 30, 26); abCopy.setBounds (644, 15, 52, 26);
    abA.onClick = [this] { proc.switchAB (0); }; abB.onClick = [this] { proc.switchAB (1); }; abCopy.onClick = [this] { proc.copyAB(); };
    combo (IDs::quality, C::amber, { 712, 15, 118, 26 }).setTooltip ("Oversampling: higher = cleaner distortion (less aliasing), more CPU");
    learnB.setBounds (840, 15, 80, 26);
    learnB.setTooltip ("Play the bass for 4 seconds: Lumen Bass finds its note and sets the split, glue, harmonics and sub generator");
    learnB.onClick = [this] { proc.startLearn(); };
    toggle (IDs::compare, "COMPARE", C::green, { 928, 15, 124, 26 }, false, "Hear the original at the SAME loudness as the processed sound - an honest before/after");
    toggle (IDs::delta, "DELTA", C::red, { 1058, 15, 128, 26 }, false, "Hear only what Lumen Bass adds or removes");

    display = std::make_unique<BassDisplay> (proc, spectra);
    addAndMakeVisible (*display); display->setBounds (12, L::top, 856, L::dispH);
    scope = std::make_unique<ScopeView> (proc, spectra);
    addAndMakeVisible (*scope); scope->setBounds (876, L::top, 312, L::dispH);

    // ---- SUB
    {
        const int x = L::subX, y = L::panY;
        toggle (IDs::subOn, "", cSub, { x + 8, y + 5, 18, 18 }, true, "Sub band on / off");
        toggle (IDs::subSolo, "S", cSub, { x + L::subW - 34, y + 5, 26, 20 }, false, "Solo the sub band");
        toggle (IDs::subMono, "MONO SUB", cSub, { x + 10, y + 34, 130, 24 }, false, "Makes everything below the split mono - tight and club / vinyl safe");
        knob (IDs::subTight, "TIGHT", cSub, { x + 8, y + 66, L::kw, L::kh }, false, "Glue compressor for the sub: every note hits the same level");
        knob (IDs::subPhone, "PHONE", cSub, { x + 104, y + 66, L::kw, L::kh }, false, "Adds harmonics of the sub so the bass is heard on phones, laptops and small speakers");
        knob (IDs::subChar, "CHARACTER", cSub, { x + 200, y + 66, L::kw, L::kh }, false, "Phone harmonics colour: Warm (2nd/4th) to Edgy (3rd)");
        knob (IDs::subGen, "SUB GEN", cSub, { x + 8, y + 206, L::kw, L::kh }, false, "Generates a clean sine sub that follows the bass - fixes weak or thin subs");
        knob (IDs::subLevel, "LEVEL", cSub, { x + 104, y + 206, L::kw, L::kh }, true, "Sub band level");
        combo (IDs::subGenMode, cSub, { x + 200, y + 242, 92, 24 }).setTooltip ("Sub generator: Unison = same note, Octave Down = one octave lower");
    }
    // ---- BODY / AIR
    auto bandPanel = [this] (const char* b, int x, juce::Colour c, const juce::String& name)
    {
        const int y = L::panY;
        toggle (bandId (b, "on"), "", c, { x + 8, y + 5, 18, 18 }, true, name + " band on / off");
        toggle (bandId (b, "solo"), "S", c, { x + L::bandW - 34, y + 5, 26, 20 }, false, "Solo the " + name.toLowerCase() + " band");
        combo (bandId (b, "type"), c, { x + 10, y + 34, 130, 24 }).setTooltip ("Drive type: Tube (warm, even), Tape (soft), Fuzz (aggressive), Fold (growl), Rectify (octave-up grit), Crush (bit / rate)");
        knob (bandId (b, "drive"), "DRIVE", c, { x + 8, y + 66, L::kw + 10, L::kh + 10 }, false, "Distortion amount - auto-levelled, so you hear the colour, not just loudness");
        knob (bandId (b, "tone"), "TONE", c, { x + 120, y + 66, L::kw, L::kh }, true, "Dark / bright tilt after the drive");
        knob (bandId (b, "mix"), "DRIVE MIX", c, { x + 224, y + 66, L::kw, L::kh }, false, "Parallel drive: blend clean and driven");
        knob (bandId (b, "squash"), "SQUASH", c, { x + 8, y + 206, L::kw, L::kh }, false, "Upward + downward compression (OTT-style): denser, louder, more detail");
        knob (bandId (b, "width"), "WIDTH", c, { x + 120, y + 206, L::kw, L::kh }, false, "Stereo width of this band (0 % = mono)");
        knob (bandId (b, "level"), "LEVEL", c, { x + 224, y + 206, L::kw, L::kh }, true, name + " band level");
    };
    bandPanel ("mid", L::bodyX, cBody, "Body");
    bandPanel ("top", L::airX, cAir, "Air");
    // ---- GLOBAL
    {
        const int x = L::gX, y = L::panY, kw = 86;
        knob (IDs::input, "INPUT", C::cyan, { x + 6, y + 30, kw, 84 }, true, "Input gain - drives everything harder");
        knob (IDs::output, "OUTPUT", C::cyan, { x + 98, y + 30, kw, 84 }, true, "Output gain");
        knob (IDs::mix, "MIX", C::cyan, { x + 6, y + 120, kw, 84 }, false, "Dry / wet for the whole plugin (phase-coherent)");
        knob (IDs::duck, "KICK DUCK", C::orange, { x + 6, y + 210, kw, 84 }, false, "Ducks the sub when the kick hits. Route the kick to the side-chain input");
        knob (IDs::duckRel, "RELEASE", C::orange, { x + 98, y + 210, kw, 84 }, false, "How fast the bass comes back after the kick");
        toggle (IDs::duckMid, "DUCK BODY", C::orange, { x + 6, y + 302, 88, 24 }, false, "Duck the body band too, not only the sub");
        toggle (IDs::clip, "SOFT CLIP", C::green, { x + 98, y + 302, 88, 24 }, false, "Oversampled soft clipper at the output - catches peaks without harshness");
    }
    startTimerHz (30);
}

LumenBassEditor::~LumenBassEditor() { setLookAndFeel (nullptr); }

void LumenBassEditor::updateSpectra()
{
    const int N = BassSpectra::kN, pos = proc.ringPos.load (std::memory_order_acquire);
    spectra.fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    float* dst[2] { spectra.pre, spectra.post };
    for (int w = 0; w < 2; ++w)
    {
        std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
        for (int i = 0; i < N; ++i) fftBuf[(size_t) i] = proc.ring[w][(pos - N + i) & (LumenBassProcessor::kRing - 1)] * window[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (fftBuf.data());
        const float norm = 4.0f / (float) N;
        for (int k = 1; k < N / 2; ++k)
        {
            const float db = 20.0f * std::log10 (fftBuf[(size_t) k] * norm + 1e-9f);
            dst[w][k] = db > dst[w][k] ? dst[w][k] + (db - dst[w][k]) * 0.5f : dst[w][k] + (db - dst[w][k]) * 0.12f;
        }
    }
    // scope: ~4 bass periods (or 40 ms), triggered on a rising zero crossing of the output
    const float bh = proc.bassHz.load();
    const int span = juce::jlimit (256, 4096, (int) (bh > 0 ? 4.0 * spectra.fs / bh : 0.04 * spectra.fs));
    int start = pos - span - 1;
    for (int k = 0; k < 2048; ++k)
    {
        const int i = start - k;
        const float a = proc.ring[1][(i - 1) & (LumenBassProcessor::kRing - 1)], b = proc.ring[1][i & (LumenBassProcessor::kRing - 1)];
        if (a <= 0 && b > 0) { start = i; break; }
    }
    for (int i = 0; i < 1024; ++i)
    {
        const int idx = start + (int) ((float) i * (float) span / 1024.0f);
        spectra.scopePre[i]  = proc.ring[0][idx & (LumenBassProcessor::kRing - 1)];
        spectra.scopePost[i] = proc.ring[1][idx & (LumenBassProcessor::kRing - 1)];
    }
}

void LumenBassEditor::timerCallback()
{
    updateSpectra();
    if (++tick % 3 == 0)
    {
        proc.bassHz.store (proc.detectPitch (0));
        proc.kickHz.store (proc.scActive.load() ? proc.detectPitch (2) : 0.0f);
    }
    inM = std::max (proc.inPk.load(), inM * 0.85f); outM = std::max (proc.outPk.load(), outM * 0.85f);
    const int v = proc.programVersion.load() + proc.getCurrentProgram() * 100000;
    if (v != lastVersion) { lastVersion = v; presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    abA.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification); abB.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    if (proc.learnReady()) { display->message = proc.learn(); display->msgTime = juce::Time::getMillisecondCounter(); }
    const bool lr = proc.learnRunning();
    learnB.setButtonText (lr ? "LISTEN " + juce::String (juce::roundToInt (proc.learnProgress() * 100)) + "%" : "LEARN");
    learnB.setToggleState (lr, juce::dontSendNotification);
    display->repaint(); scope->repaint();
    repaint (L::subX, L::panY, W - L::subX, L::panH);
}

void LumenBassEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    g.setColour (C::panelTop); g.fillRect (0, 0, W, 56);
    g.setColour (C::border); g.drawHorizontalLine (56, 0, (float) W);
    g.setColour (C::text); g.setFont (font (24.0f)); g.drawText ("LUMEN", 18, 12, 100, 32, juce::Justification::centredLeft);
    g.setColour (cSub); g.drawText ("BASS", 108, 12, 70, 32, juce::Justification::centredLeft);
    g.setColour (C::dim); g.setFont (font (9.5f, false)); g.drawText ("v2", 176, 24, 30, 14, juce::Justification::centredLeft);

    auto pnl = [&] (int x, int w, const juce::String& t, juce::Colour c, bool on, const juce::String& info)
    {
        panel (g, juce::Rectangle<float> ((float) x, (float) L::panY, (float) w, (float) L::panH), "", c, on);
        g.setColour (on ? c : C::dim); g.setFont (font (12.0f)); g.drawText (t, x + 32, L::panY + 4, 120, 20, juce::Justification::centredLeft);
        g.setColour (C::dim); g.setFont (mono (9.5f)); g.drawText (info, x + 120, L::panY + 4, w - 160, 20, juce::Justification::centredRight);
    };
    auto dbs = [] (float v) { return (v >= 0 ? "+" : "") + juce::String (v, 1) + " dB"; };
    pnl (L::subX, L::subW, "SUB", cSub, proc.P (IDs::subOn) > 0.5f, "< " + hzText (proc.P (IDs::x1)) + "   glue " + dbs (proc.subGr.load()));
    pnl (L::bodyX, L::bandW, "BODY", cBody, proc.P ("mid_on") > 0.5f, hzText (proc.P (IDs::x1)) + " - " + hzText (proc.P (IDs::x2)) + "   sq " + dbs (proc.midSq.load()));
    pnl (L::airX, L::bandW, "AIR", cAir, proc.P ("top_on") > 0.5f, "> " + hzText (proc.P (IDs::x2)) + "   sq " + dbs (proc.topSq.load()));
    pnl (L::gX, L::gW, "GLOBAL", C::cyan, true, "");
    g.setFont (font (9.5f, false)); g.setColour (C::dim);
    const int hy = L::panY + L::panH - 26;
    g.drawText ("Mono weight below the split. PHONE = small speakers", L::subX + 12, hy, L::subW - 24, 14, juce::Justification::centredLeft);
    g.drawText ("The growl: drive, squash and width of the mid range", L::bodyX + 12, hy, L::bandW - 24, 14, juce::Justification::centredLeft);
    g.drawText ("Fizz and bite on top - widen it, keep the sub mono", L::airX + 12, hy, L::bandW - 24, 14, juce::Justification::centredLeft);

    // global readouts + meters
    const int x = L::gX, y = L::panY;
    ledMeter (g, { (float) x + 112, (float) y + 124, 8, 76 }, inM, inM, true, 0);
    ledMeter (g, { (float) x + 126, (float) y + 124, 8, 76 }, outM, outM, true, 0);
    g.setColour (C::dim); g.setFont (font (8.5f));
    g.drawText ("IN", x + 106, y + 202, 20, 10, juce::Justification::centred); g.drawText ("OUT", x + 120, y + 202, 22, 10, juce::Justification::centred);
    g.setFont (mono (9.5f));
    const float corr = proc.lowCorr.load(), ph = proc.phoneDb.load();
    g.setColour (C::dim); g.drawText ("LOW MONO", x + 142, y + 124, 50, 12, juce::Justification::centredLeft);
    g.setColour (corr > 0.9f ? C::green : corr > 0.5f ? C::amber : C::red); g.drawText (juce::String (corr, 2), x + 142, y + 137, 50, 12, juce::Justification::centredLeft);
    g.setColour (C::dim); g.drawText ("PHONE", x + 142, y + 160, 50, 12, juce::Justification::centredLeft);
    g.setColour (ph > 0.5f ? C::green : C::text); g.drawText ((ph >= 0 ? "+" : "") + juce::String (ph, 1) + "dB", x + 142, y + 173, 50, 12, juce::Justification::centredLeft);
    const bool sc = proc.scActive.load();
    g.setColour (sc ? C::orange : C::dim); g.setFont (font (9.0f));
    g.drawText (sc ? "KICK SIDE-CHAIN: ON" : "SIDE-CHAIN: no signal", x + 6, y + 334, L::gW - 12, 14, juce::Justification::centred);
    g.setColour (C::dim); g.setFont (font (8.5f, false));
    g.drawText ("latency " + juce::String (proc.getLatencySamples()) + " smp", x + 6, y + 350, L::gW - 12, 12, juce::Justification::centred);
}
