#include "PluginEditor.h"

using namespace lumen::led;
using namespace lumen::riser;

namespace
{
constexpr int W = 1120, HH = 666;
const juce::Colour MAIN = C::orange, GEN = C::magenta, FXC = C::cyan, DROPC = C::amber;
template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}
float shapeCurve (float x, float c)
{
    if (c >= 0) return std::pow (x, 1.0f + 2.5f * c);
    return 1.0f - std::pow (1.0f - x, 1.0f - 2.5f * c);
}
}

//==============================================================================
WavDrag::WavDrag (LumenRiserProcessor& p, LumenRiserEditor& e, LumenRiserProcessor::RenderKind k, juce::String t, juce::Colour c)
    : proc (p), ed (e), kind (k), text (std::move (t)), col (c)
{
    setTooltip ("Drag into Ableton as an audio clip, rendered at the song tempo (the riser ends exactly on the drop). Click = save to Documents / Lumen Riser");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}
void WavDrag::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff0a0f16)); g.fillRoundedRectangle (r, 5.0f);
    if (isMouseOver()) { g.setColour (col.withAlpha (0.12f)); g.fillRoundedRectangle (r, 5.0f); }
    g.setColour (col.withAlpha (0.75f)); g.drawRoundedRectangle (r, 5.0f, 1.2f);
    // mini waveform glyph
    juce::Path w; const float x0 = r.getX() + 8, y0 = r.getCentreY(), ww = 26;
    for (int i = 0; i <= 26; ++i)
    {
        const float u = i / 26.0f;
        const float a = kind == LumenRiserProcessor::RenderRiser ? u : kind == LumenRiserProcessor::RenderImpact ? std::exp (-u * 4) : kind == LumenRiserProcessor::RenderDown ? 1 - u : (u < 0.6f ? u / 0.6f : std::exp (-(u - 0.6f) * 8));
        const float y = y0 + std::sin (u * 40.0f) * a * (r.getHeight() * 0.32f);
        if (i == 0) w.startNewSubPath (x0, y); else w.lineTo (x0 + u * ww, y);
    }
    g.setColour (col); g.strokePath (w, juce::PathStrokeType (1.2f));
    g.setFont (font (text.length() > 8 ? 9.5f : 10.5f)); g.setColour (col.interpolatedWith (juce::Colours::white, 0.25f));
    g.drawText (text, r.withTrimmedLeft (37.0f), juce::Justification::centredLeft);
}
void WavDrag::mouseDrag (const juce::MouseEvent& e)
{
    if (dragged || e.getDistanceFromDragStart() < 6) return;
    dragged = true;
    const auto f = proc.exportWav (kind);
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragged = false; });
    else dragged = false;
}
void WavDrag::mouseUp (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() >= 6) return;
    const auto f = proc.exportWav (kind);
    ed.showMessage (f.existsAsFile() ? "saved " + f.getFullPathName() : juce::String ("export failed"));
}

//==============================================================================
void Timeline::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, MAIN);
    auto in = r.reduced (10.0f, 8.0f);
    auto& st = proc.apvts;
    auto v = [&] (const juce::String& id) { return st.getRawParameterValue (id)->load(); };
    const int mode = (int) v (IDs::mode);
    const double P = periodBars ((int) v (IDs::period)), L = std::min (lengthBars ((int) v (IDs::length)), P);
    const float curve = v (IDs::curve);
    const double gapB = gapBeats ((int) v (IDs::gap)) / 4.0;   // in bars
    const float bw = in.getWidth() / (float) P;
    auto xOf = [&] (double bar) { return in.getX() + (float) bar * bw; };
    // bars
    for (int b = 0; b < (int) P; ++b)
    {
        auto cell = juce::Rectangle<float> (xOf (b) + 1, in.getY() + 14, bw - 2, in.getHeight() - 28);
        g.setColour (juce::Colour (0xff0a0e14)); g.fillRect (cell);
        if (P <= 32 || b % 4 == 0) { g.setFont (font (8.0f)); g.setColour (C::dim); g.drawText (juce::String (b + 1 + (int) v (IDs::offset)), juce::Rectangle<float> (xOf (b), in.getBottom() - 12, bw, 12), juce::Justification::centred); }
    }
    // build curve
    if (mode == AutoBars)
    {
        juce::Path fill; const float base = in.getBottom() - 14, top = in.getY() + 14;
        fill.startNewSubPath (xOf (P - L), base);
        for (int i = 0; i <= 120; ++i) { const float u = i / 120.0f; fill.lineTo (xOf (P - L + u * L), base - (base - top) * shapeCurve (u, curve)); }
        fill.lineTo (xOf (P), base); fill.closeSubPath();
        juce::ColourGradient cg (GEN.withAlpha (0.1f), xOf (P - L), 0, MAIN.withAlpha (0.65f), xOf (P), 0, false);
        g.setGradientFill (cg); g.fillPath (fill);
        juce::Path edge; edge.startNewSubPath (xOf (P - L), base);
        for (int i = 0; i <= 120; ++i) { const float u = i / 120.0f; edge.lineTo (xOf (P - L + u * L), base - (base - top) * shapeCurve (u, curve)); }
        glowPath (g, edge, MAIN, 1.6f);
        if (gapB > 0)
        {
            auto gr = juce::Rectangle<float> (xOf (P - gapB), top, std::max (3.0f, (float) gapB * bw), base - top);
            g.setColour (juce::Colour (0xff120406)); g.fillRect (gr);
            g.setColour (C::red.withAlpha (0.9f)); g.drawRect (gr, 1.2f);
            g.setFont (font (10.0f)); g.drawText ("GAP", juce::Rectangle<float> (xOf (P) - 86, in.getY() - 2, 34, 14), juce::Justification::centredRight);
        }
        g.setColour (DROPC); g.fillRect (xOf (P) - 2.0f, in.getY(), 3.0f, in.getHeight() - 12);
        g.setFont (font (10.0f)); g.drawText ("DROP", juce::Rectangle<float> (xOf (P) - 48, in.getY() - 2, 44, 14), juce::Justification::centredRight);
        g.setColour (MAIN); g.drawText ("BUILD " + juce::String (L, 0) + (L == 1 ? " BAR" : " BARS"), juce::Rectangle<float> (xOf (P - L) + 4, in.getY() - 2, 140, 14), juce::Justification::centredLeft);
        const float ph = proc.periodPhase.load();
        if (ph >= 0)
        {
            const float x = xOf (ph);
            g.setColour (juce::Colours::white.withAlpha (0.25f)); g.fillRect (x - 3, in.getY() + 12, 7.0f, in.getHeight() - 24);
            g.setColour (juce::Colours::white); g.fillRect (x - 0.5f, in.getY() + 12, 1.6f, in.getHeight() - 24);
        }
        else { g.setFont (font (9.0f)); g.setColour (C::dim); g.drawText ("press play in Ableton", in.withTrimmedTop (14).withHeight (14), juce::Justification::centredLeft); }
    }
    else
    {
        g.setFont (font (13.0f)); g.setColour (MAIN.withAlpha (0.85f));
        g.drawText (mode == ManualRise ? "MANUAL: automate the RISE knob up - snap it back down for the DROP"
                                       : "MIDI: hold any note = build (" + juce::String (L, 0) + " bars to full), release = DROP",
                    in, juce::Justification::centred);
    }
}

//==============================================================================
void ProgressOrb::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto c = r.getCentre(); const float rad = std::min (r.getWidth(), r.getHeight()) * 0.5f - 6.0f;
    const float pr = proc.progress.load();
    const int st = proc.state.load();
    g.setColour (juce::Colour (0xff05080c)); g.fillEllipse (r.withSizeKeepingCentre (rad * 2 + 8, rad * 2 + 8));
    g.setColour (C::border); g.drawEllipse (r.withSizeKeepingCentre (rad * 2 + 8, rad * 2 + 8), 1.0f);
    const int segs = 40;
    const float a0 = juce::MathConstants<float>::pi * 1.25f, span = juce::MathConstants<float>::pi * 1.5f;
    for (int i = 0; i < segs; ++i)
    {
        const float u = (i + 0.5f) / segs, a = -a0 + span * u + juce::MathConstants<float>::pi * 0.5f;
        const auto pt = c + juce::Point<float> (std::cos (a - juce::MathConstants<float>::pi * 0.5f), std::sin (a - juce::MathConstants<float>::pi * 0.5f)) * (rad - 6.0f);
        const bool on = u <= pr + 1e-4f && pr > 0;
        const juce::Colour col = GEN.interpolatedWith (MAIN, u).interpolatedWith (DROPC, std::max (0.0f, u - 0.7f) / 0.3f);
        drawLed (g, pt, 3.6f, col, on ? 1.0f : 0.06f);
    }
    if (flash > 0.01f) { g.setColour (DROPC.withAlpha (0.35f * flash)); g.fillEllipse (r.withSizeKeepingCentre (rad * 1.6f, rad * 1.6f)); }
    const juce::String big = st == 2 && pr <= 0 ? "DROP!" : juce::String (juce::roundToInt (pr * 100.0f)) + "%";
    dm.draw (g, r.withSizeKeepingCentre (rad * 1.3f, 30.0f), big, st == 2 && pr <= 0 ? DROPC : MAIN, 3.2f, juce::Justification::centred, false, 11.4f);
    const int dv = proc.stutterDiv.load();
    static const char* dn[] { "1/4", "1/8", "1/16", "1/32", "1/64" };
    const juce::String sub = st == 1 ? (dv >= 0 ? juce::String ("BUILD  ") + dn[juce::jlimit (0, 4, dv)] : juce::String ("BUILD")) : st == 2 ? juce::String ("IMPACT") : juce::String ("READY");
    g.setFont (font (10.0f)); g.setColour (st == 1 ? MAIN : st == 2 ? DROPC : C::dim);
    g.drawText (sub, r.withSizeKeepingCentre (rad * 1.4f, 14).translated (0, 26), juce::Justification::centred);
}

//==============================================================================
LumenRiserEditor::LumenRiserEditor (LumenRiserProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    struct K { const char* id; const char* lb; int x, y; juce::Colour c; const char* tip; };
    auto knob = [&] (const K& k, int sz = 64) { auto* kn = add<Knob> (*this, owned, st, k.id, k.lb, k.c); kn->setBounds (k.x, k.y, sz, sz + 14); kn->slider.setTooltip (k.tip); return kn; };
    auto combo = [&] (const char* id, int x, int y, int w, juce::Colour c, const char* tip) { auto* cb = add<Combo> (*this, owned, st, id, c); cb->setBounds (x, y, w, 24); cb->setTooltip (tip); return cb; };
    auto toggle = [&] (const char* id, const char* t, int x, int y, int w, juce::Colour c, const char* tip) { auto* tg = add<Toggle> (*this, owned, st, id, t, c); tg->setBounds (x, y, w, 24); tg->setTooltip (tip); return tg; };
    auto label = [&] (const char* t, int x, int y, int w) { auto* l = add<juce::Label> (*this, owned, juce::String(), t); l->setFont (font (8.5f)); l->setColour (juce::Label::textColourId, C::dim); l->setBounds (x, y, w, 12); l->setInterceptsMouseClicks (false, false); };

    // ---------------- BUILD
    orb = std::make_unique<ProgressOrb> (proc); addAndMakeVisible (*orb); orb->setBounds (30, 96, 166, 166);
    knob ({ "rise", "RISE", 206, 92, MAIN, "Manual mode: automate this from 0 to 100% across the build, then snap it back to 0 for the DROP" }, 70);
    knob ({ "curve", "CURVE", 206, 180, MAIN, "Shape of the build: slow start (tension saved for the end) or fast start" }, 64);
    timeline = std::make_unique<Timeline> (proc); addAndMakeVisible (*timeline); timeline->setBounds (290, 84, 584, 112);
    label ("TRIGGER", 290, 202, 100); combo ("mode", 290, 214, 168, MAIN, "AUTO: builds by itself before every drop.  MANUAL: you automate RISE.  MIDI: hold a note to build, release to drop");
    label ("BUILD LENGTH", 464, 202, 90); combo ("length", 464, 214, 84, MAIN, "How long the build lasts");
    label ("DROP EVERY", 554, 202, 90); combo ("period", 554, 214, 84, MAIN, "AUTO: how often the drop comes (the build fills the last bars before it)");
    {
        auto* o = add<Knob> (*this, owned, st, IDs::offset, "OFFSET", MAIN); o->setBounds (642, 198, 44, 58);
        o->slider.setTooltip ("AUTO: shifts the drop by whole bars so it lands on your drop's bar 1");
    }
    label ("GAP BEFORE DROP", 692, 202, 100); combo ("gap", 692, 214, 90, C::red, "Silence right before the drop - the classic EDM 'breath' that makes the drop hit harder");
    label ("KEY", 788, 202, 60); combo ("key", 788, 214, 86, GEN, "Song key: the synth riser, Shepard tone, impact and downlifter are tuned to it");
    previewB.setColour (juce::TextButton::buttonColourId, MAIN); previewB.setBounds (888, 86, 104, 30);
    previewB.setTooltip ("Hear the whole build + drop now (at the song tempo, also when Ableton is stopped)");
    previewB.onClick = [this] { proc.preview(); };
    dropB.setColour (juce::TextButton::buttonColourId, DROPC); dropB.setBounds (998, 86, 98, 30);
    dropB.setTooltip ("Fire the impact / downlifter now");
    dropB.onClick = [this] { proc.dropNow(); };
    for (auto* b : { &previewB, &dropB }) addAndMakeVisible (*b);
    struct D { LumenRiserProcessor::RenderKind k; const char* t; int x, y; juce::Colour c; };
    const D ds[] { { LumenRiserProcessor::RenderRiser, "RISER", 888, 140, MAIN }, { LumenRiserProcessor::RenderImpact, "IMPACT", 998, 140, DROPC },
                   { LumenRiserProcessor::RenderDown, "DOWNLIFTER", 888, 176, C::violet }, { LumenRiserProcessor::RenderFull, "RISER+HIT", 998, 176, GEN } };
    for (auto& d : ds) { auto w = std::make_unique<WavDrag> (proc, *this, d.k, d.t, d.c); addAndMakeVisible (*w); w->setBounds (d.x, d.y, 104 - (d.x > 900 ? 6 : 0), 30); owned.push_back (std::move (w)); }

    // ---------------- GENERATOR (y 278..)
    const int gy = 300;
    toggle ("noise_on", "NOISE", 24, gy, 150, GEN, "Filtered white / pink noise sweep");
    knob ({ "noise_level", "LEVEL", 24, gy + 30, GEN, "Noise level" });
    knob ({ "noise_res", "RESO", 92, gy + 30, GEN, "Filter resonance (more = whistling sweep)" });
    combo ("noise_filter", 24, gy + 118, 74, GEN, "Sweep type"); combo ("noise_type", 102, gy + 118, 72, GEN, "Noise colour");

    toggle ("synth_on", "SUPERSAW", 190, gy, 210, GEN, "7-voice supersaw stack that rises in pitch and opens its filter");
    knob ({ "synth_level", "LEVEL", 190, gy + 30, GEN, "Synth level" });
    knob ({ "synth_pitch", "PITCH", 258, gy + 30, GEN, "How far the pitch rises (semitones; 12 = one octave)" });
    knob ({ "synth_detune", "DETUNE", 326, gy + 30, GEN, "Supersaw width (spreads more as the build rises)" });
    combo ("synth_stack", 190, gy + 118, 126, GEN, "Notes of the riser chord");
    {
        auto* o = add<Knob> (*this, owned, st, IDs::synthOct, "OCT", GEN); o->setBounds (326, gy + 106, 48, 52); o->slider.setTooltip ("Synth octave");
    }

    toggle ("shepard_on", "SHEPARD", 416, gy, 140, GEN, "Shepard tone: a sound that seems to rise forever - endless tension, in key");
    knob ({ "shepard_level", "LEVEL", 416, gy + 30, GEN, "Shepard level" });
    knob ({ "shepard_speed", "SPEED", 484, gy + 30, GEN, "Rise speed (accelerates with the build)" });

    toggle ("impact_on", "IMPACT", 572, gy, 140, DROPC, "Sub boom + crack + air on the drop, tuned to the key");
    knob ({ "impact_level", "LEVEL", 572, gy + 30, DROPC, "Impact level" });
    knob ({ "impact_decay", "DECAY", 640, gy + 30, DROPC, "Impact length" });

    toggle ("down_on", "DOWNLIFTER", 728, gy, 140, C::violet, "Falling sweep after the drop");
    knob ({ "down_level", "LEVEL", 728, gy + 30, C::violet, "Downlifter level" });
    combo ("down_length", 796, gy + 50, 72, C::violet, "Downlifter length");

    knob ({ "space", "SPACE", 886, gy + 4, C::blue, "Reverb on the generator (grows with the build)" });
    knob ({ "size", "SIZE", 954, gy + 4, C::blue, "Reverb size / length" });
    knob ({ "gen_level", "GEN LEVEL", 1022, gy + 4, GEN, "Level of everything the generator adds" });
    toggle ("tail_cut", "CUT TAIL AT DROP", 886, gy + 98, 204, C::blue, "Stops the reverb wash right on the drop for a clean, punchy drop");

    // ---------------- TRANSITION FX (y 500..)
    const int fy = 516;
    toggle ("fx_on", "FX ON YOUR TRACK", 24, fy, 170, FXC, "Processes the audio on this track (put Lumen Riser on your music bus / group)");
    combo ("fx_filter", 24, fy + 34, 170, FXC, "Filter that sweeps with the build");
    knob ({ "fx_amount", "FILTER", 210, fy - 4, FXC, "How far the filter sweeps" });
    knob ({ "fx_res", "RESO", 278, fy - 4, FXC, "Filter resonance" });
    knob ({ "stutter", "STUTTER", 360, fy - 4, FXC, "Rhythmic gate that speeds up 1/4 -> 1/8 -> 1/16 -> 1/32 (snare-roll effect)" });
    combo ("roll", 350, fy + 84, 84, FXC, "How fast the stutter goes at the end");
    knob ({ "fx_reverb", "WASH", 446, fy - 4, FXC, "Reverb wash that grows with the build" });
    knob ({ "width", "WIDTH", 514, fy - 4, FXC, "Stereo widening with the build" });
    knob ({ "fx_mix", "MIX", 582, fy - 4, FXC, "Dry / processed" });
    toggle ("level_match", "LEVEL MATCH", 24, fy + 70, 170, C::green, "Keeps the loudness steady while the filter cuts - no volume dip during the build");
    toggle ("club_safe", "CLUB SAFE (MONO <150 Hz)", 24, fy + 104, 220, C::green, "Bass below 150 Hz stays mono - riser and wash never smear the low end on a club system");
    knob ({ "output", "OUTPUT", 1022, fy - 4, C::text, "Output level" });

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setColour (juce::ComboBox::textColourId, MAIN);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (382, 18, 320, 28);
    for (auto* b : { &prevB, &nextB }) { b->setColour (juce::TextButton::buttonColourId, MAIN); addAndMakeVisible (*b); }
    prevB.setBounds (348, 18, 28, 28); nextB.setBounds (708, 18, 28, 28);
    prevB.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    for (auto* b : { &aB, &bB, &copyB }) addAndMakeVisible (*b);
    aB.setBounds (748, 18, 30, 28); bB.setBounds (780, 18, 30, 28); copyB.setBounds (814, 18, 40, 28);
    aB.onClick = [this] { proc.switchAB (0); };
    bB.onClick = [this] { proc.switchAB (1); };
    copyB.onClick = [this] { proc.copyAB(); };
    copyB.setColour (juce::TextButton::buttonColourId, C::dim);

    setSize (W, HH);
    startTimerHz (30);
}

LumenRiserEditor::~LumenRiserEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenRiserEditor::timerCallback()
{
    if (proc.programVersion.load() != lastProgram) { lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    const int d = proc.drops.load();
    if (d != lastDrops) { lastDrops = d; orb->flash = 1.0f; }
    orb->flash *= 0.88f;
    outPk = juce::jmax (proc.outPeak.load(), outPk * 0.85f);
    aB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 0 ? MAIN : C::dim);
    bB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 1 ? MAIN : C::dim);
    orb->repaint(); timeline->repaint();
    repaint (0, 0, W, 72);
}

void LumenRiserEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (GEN.withAlpha (0.0f), 0, 64, DROPC.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.25, GEN.withAlpha (0.75f)); line.addColour (0.75, MAIN.withAlpha (0.75f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (MAIN.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (GEN, 20, 0, MAIN, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 146.0f, 18.0f, 190.0f, 28.0f }, "RISER", MAIN, 1.85f, juce::Justification::centredLeft, false, 10.5f);
    }
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("OUT", 866, 24, 24, 10, juce::Justification::centredLeft);
    ledMeter (g, { 892.0f, 23.0f, 212.0f, 10.0f }, outPk, outPk, false, 28);

    panel (g, { 16.0f, 72.0f, 1088.0f, 200.0f }, "BUILD & DROP", MAIN);
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("DRAG INTO ABLETON (WAV, SONG TEMPO)", 888, 124, 210, 12, juce::Justification::centredLeft);
    g.setColour (C::green.withAlpha (0.85f));
    g.drawFittedText (msg, juce::Rectangle<int> (888, 212, 210, 52), juce::Justification::topLeft, 4);
    panel (g, { 16.0f, 280.0f, 1088.0f, 210.0f }, "GENERATOR", GEN);
    panel (g, { 16.0f, 498.0f, 1088.0f, 160.0f }, "TRANSITION FX", FXC, pv (IDs::fxOn) > 0.5f);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawFittedText ("Ableton: put Lumen Riser on your music group (or on an empty audio track for the generator only). "
                      "AUTO mode follows the song: set DROP EVERY + BUILD LENGTH so the drop lands on bar 1 of your drop (use OFFSET).",
                      juce::Rectangle<int> (660, 556, 340, 60), juce::Justification::topLeft, 4);
}
