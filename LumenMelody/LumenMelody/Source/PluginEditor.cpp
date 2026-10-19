#include "PluginEditor.h"

using namespace lumen::led;
using namespace lumen::melody;

namespace
{
constexpr int W = 1120, HH = 760;
const juce::Colour MAIN = C::green, LEADC = C::magenta, BASSC = C::cyan, GLOB = C::amber;
template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}
}

//==============================================================================
PianoRoll::PianoRoll (LumenMelodyProcessor& p, int pt, juce::Colour c) : proc (p), part (pt), col (c)
{
    setTooltip ("Click = add a note.  Drag a note = move it (it stays in key).  Double-click = change length.  Right-click = delete");
}

int PianoRoll::pitchOf (const Note& n) const
{
    Player tmp; auto pp = proc.readPlay(); pp.source = Internal; tmp.setParams (pp);
    return tmp.pitchFor (n, n.step, part == 1);
}

int PianoRoll::relForPitch (int step, int pitch) const
{
    Player tmp; auto pp = proc.readPlay(); pp.source = Internal; tmp.setParams (pp);
    int best = 0, bd = 999;
    for (int r = -14; r <= 21; ++r) { Note n; n.step = step; n.rel = r; const int d = std::abs (tmp.pitchFor (n, step, part == 1) - pitch); if (d < bd) { bd = d; best = r; } }
    return best;
}

PianoRoll::Geo PianoRoll::geo() const
{
    Geo g; const auto pat = changed || dragIdx >= 0 ? work : proc.getPattern (part);
    g.steps = std::max (1, proc.readPlay().bars) * 16;
    int lo = 127, hi = 0;
    for (auto& n : pat) { const int p = pitchOf (n); lo = std::min (lo, p); hi = std::max (hi, p); }
    if (lo > hi) { const int c = part == 0 ? 69 : 33; lo = c - 6; hi = c + 6; }
    if (dragIdx >= 0 && fixedLo >= 0) { lo = fixedLo; hi = fixedHi; }
    else { lo -= 3; hi += 3; const int minSpan = part == 0 ? 20 : 14; if (hi - lo < minSpan) { const int m = (lo + hi) / 2; lo = m - minSpan / 2; hi = lo + minSpan; } }
    g.lo = lo; g.hi = hi;
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.x0 = r.getX() + 34; g.w = r.getWidth() - 36; g.y0 = r.getY() + (part == 0 ? 16.0f : 2.0f); g.h = r.getBottom() - g.y0;
    return g;
}

void PianoRoll::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff05080c)); g.fillRoundedRectangle (r, 5.0f);
    const auto gm = geo();
    const int rows = gm.hi - gm.lo + 1; const float rh = gm.h / (float) rows, sw = gm.w / (float) gm.steps;
    const auto pp = proc.readPlay();
    const auto& sc = lumen::chord::scales()[(size_t) juce::jlimit (0, (int) lumen::chord::scales().size() - 1, pp.scale)];
    auto inScale = [&] (int n) { const int pc = ((n - pp.key) % 12 + 12) % 12; for (int v : sc.iv) if (v == pc) return true; return false; };
    for (int k = 0; k < rows; ++k)
    {
        const int pitch = gm.hi - k; const float y = gm.y0 + k * rh;
        g.setColour (inScale (pitch) ? juce::Colour (0xff0b1018) : juce::Colour (0xff07090d)); g.fillRect (gm.x0, y, gm.w, rh);
        if (pitch % 12 == pp.key % 12) { g.setColour (col.withAlpha (0.08f)); g.fillRect (gm.x0, y, gm.w, rh); }
        if (pitch % 12 == 0 || rh > 9) { g.setFont (font (std::min (9.0f, rh + 1))); g.setColour (pitch % 12 == 0 ? C::text.withAlpha (0.7f) : C::dim.withAlpha (0.6f)); if (pitch % 12 == 0 || ! juce::MidiMessage::isMidiNoteBlack (pitch)) g.drawText (juce::MidiMessage::getMidiNoteName (pitch, true, true, 3), juce::Rectangle<float> (r.getX() + 2, y, 32, rh), juce::Justification::centredLeft); }
    }
    for (int s = 0; s <= gm.steps; ++s)
    {
        g.setColour (s % 16 == 0 ? C::dim.withAlpha (0.7f) : s % 4 == 0 ? C::border : juce::Colour (0xff0d121a));
        g.fillRect (gm.x0 + s * sw, gm.y0, s % 16 == 0 ? 1.4f : 1.0f, gm.h);
    }
    // chord names
    if (part == 0)
    {
        Player tmp; auto q = pp; q.source = Internal; tmp.setParams (q);
        const int per = std::max (1, (int) std::llround (pp.chordBeats * 4));
        for (int s = 0; s < gm.steps; s += per)
        {
            const int cd = tmp.chordAtStep (s);
            const int rootPc = (pp.key + lumen::chord::degreeSemis (sc, cd)) % 12;
            std::vector<int> notes { 0, lumen::chord::degreeSemis (sc, cd + 2) - lumen::chord::degreeSemis (sc, cd), lumen::chord::degreeSemis (sc, cd + 4) - lumen::chord::degreeSemis (sc, cd) };
            for (auto& v : notes) v += rootPc;
            g.setFont (font (10.0f)); g.setColour (MAIN.withAlpha (0.9f));
            const juce::String nm = pp.source == FollowInput ? juce::String ("from input") : lumen::chord::chordName (rootPc, notes, lumen::chord::useFlats (pp.key, sc.iv));
            g.drawText (nm, juce::Rectangle<float> (gm.x0 + s * sw + 4, r.getY() + 1, per * sw - 6, 14), juce::Justification::centredLeft);
            if (pp.source == FollowInput) break;
        }
    }
    // notes
    const auto pat = changed || dragIdx >= 0 ? work : proc.getPattern (part);
    for (size_t i = 0; i < pat.size(); ++i)
    {
        const auto& n = pat[i];
        const int p = pitchOf (n);
        if (p < gm.lo || p > gm.hi) continue;
        auto nr = juce::Rectangle<float> (gm.x0 + n.step * sw + 1, gm.y0 + (gm.hi - p) * rh + 1, std::max (3.0f, n.len * sw - 2), std::max (2.0f, rh - 2));
        const juce::Colour c = n.approach ? GLOB : col;
        g.setColour (c.withAlpha (0.25f + 0.65f * n.vel)); g.fillRoundedRectangle (nr, 2.0f);
        g.setColour (c.interpolatedWith (juce::Colours::white, 0.4f)); g.drawRoundedRectangle (nr, 2.0f, (int) i == dragIdx ? 1.6f : 0.8f);
    }
    const int64_t ps = proc.playStep.load();
    if (ps >= 0)
    {
        const float x = gm.x0 + (float) (ps % gm.steps) * sw;
        g.setColour (juce::Colours::white.withAlpha (0.12f)); g.fillRect (x, gm.y0, sw, gm.h);
        g.setColour (juce::Colours::white.withAlpha (0.8f)); g.fillRect (x, gm.y0, 1.2f, gm.h);
    }
    g.setColour (col.withAlpha (0.35f)); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
}

int PianoRoll::hit (juce::Point<float> pt) const
{
    const auto gm = geo(); const int rows = gm.hi - gm.lo + 1; const float rh = gm.h / (float) rows, sw = gm.w / (float) gm.steps;
    const auto& pat = work;
    for (int i = (int) pat.size() - 1; i >= 0; --i)
    {
        const auto& n = pat[(size_t) i]; const int p = pitchOf (n);
        auto nr = juce::Rectangle<float> (gm.x0 + n.step * sw, gm.y0 + (gm.hi - p) * rh, n.len * sw, rh);
        if (nr.expanded (1.0f, 1.0f).contains (pt)) return i;
    }
    return -1;
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    work = proc.getPattern (part); changed = false;
    const auto gm = geo(); fixedLo = gm.lo; fixedHi = gm.hi;
    const int rows = gm.hi - gm.lo + 1; const float rh = gm.h / (float) rows, sw = gm.w / (float) gm.steps;
    if (e.position.x < gm.x0 || e.position.y < gm.y0) return;
    const int idx = hit (e.position);
    if (e.mods.isPopupMenu()) { if (idx >= 0) { work.erase (work.begin() + idx); proc.setPattern (part, work); } return; }
    if (idx >= 0) { dragIdx = idx; return; }
    const int step = juce::jlimit (0, gm.steps - 1, (int) ((e.position.x - gm.x0) / sw));
    const int pitch = gm.hi - juce::jlimit (0, rows - 1, (int) ((e.position.y - gm.y0) / rh));
    Note n; n.step = step; n.len = 2; n.rel = relForPitch (step, pitch); n.vel = step % 4 == 0 ? 1.0f : 0.85f;
    work.push_back (n); dragIdx = (int) work.size() - 1; changed = true;
    proc.setPattern (part, work);
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIdx < 0 || dragIdx >= (int) work.size()) return;
    const auto gm = geo(); const int rows = gm.hi - gm.lo + 1; const float rh = gm.h / (float) rows, sw = gm.w / (float) gm.steps;
    auto& n = work[(size_t) dragIdx];
    const int step = juce::jlimit (0, gm.steps - n.len, (int) ((e.position.x - gm.x0) / sw));
    const int pitch = gm.hi - juce::jlimit (0, rows - 1, (int) ((e.position.y - gm.y0) / rh));
    const int rel = relForPitch (step, pitch);
    if (step != n.step || rel != n.rel) { n.step = step; n.rel = rel; n.approach = false; changed = true; repaint(); }
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    if (changed) { std::sort (work.begin(), work.end(), [] (const Note& a, const Note& b) { return a.step < b.step; }); proc.setPattern (part, work); }
    dragIdx = -1; changed = false; fixedLo = fixedHi = -1; repaint();
}

void PianoRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    work = proc.getPattern (part);
    const int idx = hit (e.position);
    if (idx < 0) return;
    static const int lens[] { 1, 2, 3, 4, 6, 8, 16 };
    auto& n = work[(size_t) idx];
    int k = 0; while (k < 7 && lens[k] <= n.len) ++k;      // next longer length, wraps to 1/16
    n.len = k >= 7 ? lens[0] : lens[k];
    proc.setPattern (part, work);
}

//==============================================================================
MidiDrag::MidiDrag (LumenMelodyProcessor& p, LumenMelodyEditor& e, LumenMelodyProcessor::ExportKind k, juce::String t, juce::Colour c)
    : proc (p), ed (e), kind (k), text (std::move (t)), col (c)
{
    setTooltip ("Drag into Ableton as a MIDI clip. Click = save to Documents / Lumen Melody Bass");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}
void MidiDrag::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff0a0f16)); g.fillRoundedRectangle (r, 5.0f);
    g.setColour (col.withAlpha (0.75f)); g.drawRoundedRectangle (r, 5.0f, 1.2f);
    g.setFont (font (10.5f)); g.setColour (col.interpolatedWith (juce::Colours::white, 0.2f));
    g.drawText (text, r, juce::Justification::centred);
}
void MidiDrag::mouseDrag (const juce::MouseEvent& e)
{
    if (dragged || e.getDistanceFromDragStart() < 6) return;
    dragged = true;
    const auto f = proc.exportMidi (kind);
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragged = false; });
    else dragged = false;
}
void MidiDrag::mouseUp (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() >= 6) return;
    const auto f = proc.exportMidi (kind);
    ed.showMessage (f.existsAsFile() ? "saved " + f.getFullPathName() : juce::String ("export failed"));
}

//==============================================================================
LumenMelodyEditor::LumenMelodyEditor (LumenMelodyProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    auto knob = [&] (const char* id, const char* lb, int x, int y, juce::Colour c, const char* tip, int sz = 60)
    { auto* kn = add<Knob> (*this, owned, st, id, lb, c); kn->setBounds (x, y, sz, sz + 14); kn->slider.setTooltip (tip); return kn; };
    auto combo = [&] (const char* id, int x, int y, int w, juce::Colour c, const char* tip) { auto* cb = add<Combo> (*this, owned, st, id, c); cb->setBounds (x, y, w, 24); cb->setTooltip (tip); return cb; };
    auto toggle = [&] (const char* id, const char* t, int x, int y, int w, juce::Colour c, const char* tip) { auto* tg = add<Toggle> (*this, owned, st, id, t, c); tg->setBounds (x, y, w, 24); tg->setTooltip (tip); return tg; };
    auto label = [&] (const char* t, int x, int y, int w) { auto* l = add<juce::Label> (*this, owned, juce::String(), t); l->setFont (font (8.5f)); l->setColour (juce::Label::textColourId, C::dim); l->setBounds (x - 2, y, w, 12); l->setInterceptsMouseClicks (false, false); };

    // SONG row
    label ("KEY", 30, 84, 60);           combo ("key", 30, 96, 70, MAIN, "Key");
    label ("SCALE", 106, 84, 80);        combo ("scale", 106, 96, 140, MAIN, "Scale");
    label ("CHORDS FROM", 252, 84, 120); combo ("chord_source", 252, 96, 220, MAIN, "Built-in progression, or follow the chords you send in (Lumen Chord on another track -> MIDI To this track, or play chords on your keyboard)");
    label ("PROGRESSION", 478, 84, 120); combo ("progression", 478, 96, 210, MAIN, "Chord progression (scale degrees). Use Major scale for the 'Major' ones");
    label ("CHORD LENGTH", 694, 84, 90); combo ("chord_length", 694, 96, 86, MAIN, "Length of each chord");
    label ("PATTERN", 786, 84, 90);      combo ("pattern_length", 786, 96, 80, MAIN, "Loop length of the hook and bassline");
    label ("HARMONY", 872, 84, 90);      combo ("harmony", 872, 96, 120, MAIN, "FOLLOW CHORDS: the hook moves with every chord (sequence, like most EDM hooks).  KEEP LINE: the melody stays put and only snaps to the chord on strong beats");
    playB.setClickingTogglesState (true); playB.setColour (juce::TextButton::buttonColourId, MAIN); playB.setBounds (1000, 94, 92, 28);
    playB.setTooltip ("Play the loop without starting Ableton");
    playB.onClick = [this] { proc.setPreviewPlaying (playB.getToggleState()); };
    addAndMakeVisible (playB);

    leadRoll = std::make_unique<PianoRoll> (proc, 0, LEADC); addAndMakeVisible (*leadRoll); leadRoll->setBounds (24, 160, 1072, 196);
    bassRoll = std::make_unique<PianoRoll> (proc, 1, BASSC); addAndMakeVisible (*bassRoll); bassRoll->setBounds (24, 362, 1072, 98);

    // LEAD
    const int ly = 488;
    toggle ("lead_on", "LEAD", 26, ly, 90, LEADC, "Lead / hook on MIDI channel 1");
    combo ("lead_style", 122, ly, 170, LEADC, "Hook style (rhythm + phrasing)");
    genLead.setColour (juce::TextButton::buttonColourId, LEADC); genLead.setBounds (298, ly - 1, 130, 26);
    genLead.setTooltip ("New hook (press again for another). Knobs below reshape the current one");
    genLead.onClick = [this] { proc.generate (0, true); };
    addAndMakeVisible (genLead);
    knob ("lead_catchy", "CATCHY", 26, ly + 34, LEADC, "Catchiness: more repetition and fewer different notes = easier to sing / remember");
    knob ("lead_density", "DENSITY", 92, ly + 34, LEADC, "How many notes");
    knob ("lead_range", "RANGE", 158, ly + 34, LEADC, "Range of the melody (scale steps)");
    knob ("lead_octave", "OCTAVE", 224, ly + 34, LEADC, "Octave");
    knob ("lead_gate", "GATE", 290, ly + 34, LEADC, "Note length (short = plucky)");
    label ("CONTOUR", 360, ly + 36, 70); combo ("lead_contour", 360, ly + 48, 68, LEADC, "Shape of the phrase");
    label ("HOOK", 360, ly + 78, 70);    combo ("hook_length", 360, ly + 90, 68, LEADC, "Motif length: the hook repeats and the last repeat answers it (A A A B)");
    combo ("lead_sound", 26, ly + 126, 110, C::orange, "Preview sound for the lead");
    knob ("lead_level", "", 140, ly + 114, C::orange, "Lead preview level", 40);

    // BASS
    const int bx = 456;
    toggle ("bass_on", "BASS", bx, ly, 90, BASSC, "Bassline on MIDI channel 2");
    combo ("bass_style", bx + 96, ly, 200, BASSC, "Bassline groove");
    genBass.setColour (juce::TextButton::buttonColourId, BASSC); genBass.setBounds (bx + 302, ly - 1, 130, 26);
    genBass.onClick = [this] { proc.generate (1, true); };
    addAndMakeVisible (genBass);
    knob ("bass_variation", "VARIATION", bx, ly + 34, BASSC, "Octave jumps, fifths and ghost notes");
    knob ("bass_octave", "OCTAVE", bx + 66, ly + 34, BASSC, "Octave");
    knob ("bass_gate", "GATE", bx + 132, ly + 34, BASSC, "Note length");
    toggle ("bass_approach", "APPROACH NOTES", bx + 204, ly + 52, 150, BASSC, "A passing note on the last 16th that leads into the next chord (only when the chord changes)");
    combo ("bass_sound", bx, ly + 126, 130, C::orange, "Preview sound for the bass");
    knob ("bass_level", "", bx + 134, ly + 114, C::orange, "Bass preview level", 40);

    // GLOBAL
    const int gx = 908;
    knob ("swing", "SWING", gx, ly - 6, GLOB, "Swing (delays every 2nd 16th)");
    knob ("humanize", "HUMANIZE", gx + 66, ly - 6, GLOB, "Velocity variation");
    knob ("velocity", "VELOCITY", gx, ly + 74, GLOB, "Overall velocity");
    toggle ("preview_sound", "SOUND", gx + 72, ly + 98, 112, C::orange, "Built-in preview sounds. Turn off when your own synths play the MIDI");
    dice.setColour (juce::TextButton::buttonColourId, MAIN); dice.setBounds (gx, ly + 166, 184, 28);
    dice.onClick = [this] { proc.generate (2, true); };
    addAndMakeVisible (dice);

    // drag
    struct D { LumenMelodyProcessor::ExportKind k; const char* t; int x; juce::Colour c; };
    const D ds[] { { LumenMelodyProcessor::ExportLead, "DRAG LEAD MIDI", 26, LEADC }, { LumenMelodyProcessor::ExportBass, "DRAG BASS MIDI", 196, BASSC }, { LumenMelodyProcessor::ExportBoth, "DRAG BOTH", 366, MAIN } };
    for (auto& d : ds) { auto w = std::make_unique<MidiDrag> (proc, *this, d.k, d.t, d.c); addAndMakeVisible (*w); w->setBounds (d.x, 712, 162, 30); owned.push_back (std::move (w)); }

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setColour (juce::ComboBox::textColourId, MAIN);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (412, 18, 300, 28);
    for (auto* b : { &prevB, &nextB }) { b->setColour (juce::TextButton::buttonColourId, MAIN); addAndMakeVisible (*b); }
    prevB.setBounds (378, 18, 28, 28); nextB.setBounds (718, 18, 28, 28);
    prevB.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    for (auto* b : { &aB, &bB, &copyB }) addAndMakeVisible (*b);
    aB.setBounds (756, 18, 30, 28); bB.setBounds (788, 18, 30, 28); copyB.setBounds (822, 18, 40, 28);
    aB.onClick = [this] { proc.switchAB (0); };
    bB.onClick = [this] { proc.switchAB (1); };
    copyB.onClick = [this] { proc.copyAB(); };
    copyB.setColour (juce::TextButton::buttonColourId, C::dim);

    setSize (W, HH);
    startTimerHz (30);
}

LumenMelodyEditor::~LumenMelodyEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenMelodyEditor::timerCallback()
{
    if (proc.programVersion.load() != lastProgram) { lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    outPk = juce::jmax (proc.outPeak.load(), outPk * 0.85f);
    aB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 0 ? MAIN : C::dim);
    bB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 1 ? MAIN : C::dim);
    leadRoll->repaint(); bassRoll->repaint();
    repaint (0, 0, W, 72);
}

void LumenMelodyEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (LEADC.withAlpha (0.0f), 0, 64, BASSC.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.25, LEADC.withAlpha (0.75f)); line.addColour (0.75, BASSC.withAlpha (0.75f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (MAIN.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (LEADC, 20, 0, BASSC, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 146.0f, 18.0f, 220.0f, 28.0f }, "MELODY / BASS", MAIN, 1.85f, juce::Justification::centredLeft, false, 10.5f);
    }
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("OUT", 874, 24, 24, 10, juce::Justification::centredLeft);
    ledMeter (g, { 900.0f, 23.0f, 204.0f, 10.0f }, outPk, outPk, false, 26);

    panel (g, { 16.0f, 72.0f, 1088.0f, 60.0f }, "", MAIN);
    panel (g, { 16.0f, 140.0f, 1088.0f, 328.0f }, "", LEADC);
    g.setFont (font (9.0f)); g.setColour (LEADC); g.drawText ("LEAD  ch 1", 30, 143, 120, 14, juce::Justification::centredLeft);
    g.setColour (BASSC); g.drawText ("BASS  ch 2", 30, 448, 120, 14, juce::Justification::centredLeft);
    g.setColour (C::dim); g.drawText ("click = add   drag = move   double-click = length   right-click = delete", 600, 448, 496, 14, juce::Justification::centredRight);
    panel (g, { 16.0f, 476.0f, 432.0f, 228.0f }, "", LEADC, pv (IDs::leadOn) > 0.5f);
    panel (g, { 446.0f, 476.0f, 452.0f, 228.0f }, "", BASSC, pv (IDs::bassOn) > 0.5f);
    panel (g, { 896.0f, 476.0f, 208.0f, 228.0f }, "", GLOB);
    g.setColour (C::green.withAlpha (0.85f)); g.setFont (font (9.0f));
    g.drawFittedText (msg, juce::Rectangle<int> (540, 712, 560, 14), juce::Justification::centredLeft, 1);
    g.setColour (C::dim); g.setFont (font (8.5f));
    g.drawFittedText ("Ableton: synth track -> MIDI From: this track, 'Ch. 1' (lead) / 'Ch. 2' (bass). Turn SOUND off.", juce::Rectangle<int> (540, 728, 560, 14), juce::Justification::centredLeft, 1);
}
