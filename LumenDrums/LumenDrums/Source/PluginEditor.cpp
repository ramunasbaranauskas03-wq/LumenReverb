#include "PluginEditor.h"

using namespace lumen::led;
using namespace lumen::drums;

namespace
{
constexpr int W = 1180, HH = 760;
constexpr int GX = 150, GY = 162;              // step grid origin
const juce::Colour ACC = C::cyan, HOT = C::amber;
juce::Colour padColour (int p)
{
    static const juce::Colour c[kPads] { juce::Colour (0xff4fa3ff), juce::Colour (0xffe6a440), juce::Colour (0xffe6873f), juce::Colour (0xff52c78c), juce::Colour (0xff3fb8a0),
                                         juce::Colour (0xff8f84ff), juce::Colour (0xffd47fd0), juce::Colour (0xffc779ff), juce::Colour (0xff6f9bd8), juce::Colour (0xffe05a55) };
    return c[juce::jlimit (0, kPads - 1, p)];
}
template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}
const int kLenChoices[] { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 24, 32, 48, 64 };
}

//==============================================================================
bool StepGrid::cellAt (juce::Point<float> pt, int& pad, int& step) const
{
    const float cw = (float) getWidth() / kCols;
    pad = (int) (pt.y / kRowH); const int c = (int) (pt.x / cw);
    if (pad < 0 || pad >= kPads || c < 0 || c >= kCols) return false;
    step = page * kCols + c;
    return true;
}

void StepGrid::paint (juce::Graphics& g)
{
    const int pat = proc.currentPattern();
    const auto P = proc.getPattern (pat);
    const float cw = (float) getWidth() / kCols;
    const int64_t ps = proc.playStep.load();
    for (int t = 0; t < kPads; ++t)
    {
        const auto& tr = P.tr[(size_t) t];
        const bool sel = t == ed.selectedPad();
        const auto col = padColour (t);
        if (sel) { g.setColour (juce::Colours::white.withAlpha (0.035f)); g.fillRect (0.0f, (float) t * kRowH, (float) getWidth(), (float) kRowH); }
        const int cur = ps >= 0 ? (int) (ps % std::max (1, tr.length)) : -1;
        for (int c = 0; c < kCols; ++c)
        {
            const int k = page * kCols + c;
            auto r = juce::Rectangle<float> (c * cw, (float) t * kRowH, cw, (float) kRowH).reduced (2.0f, 3.0f);
            const bool inLen = k < tr.length;
            const bool beatCol = (k / 4) % 2 == 0;
            g.setColour (! inLen ? juce::Colour (0xff15171a) : beatCol ? juce::Colour (0xff23272d) : juce::Colour (0xff1d2025));
            g.fillRoundedRectangle (r, 3.0f);
            if (! inLen) continue;
            const auto& s = tr.s[(size_t) k];
            if (s.vel > 0)
            {
                const float v = (float) s.vel / 127.0f;
                const float a = s.prob >= 100 ? 1.0f : 0.35f + 0.65f * (float) s.prob / 100.0f;
                g.setColour (col.withAlpha (0.25f * a)); g.fillRoundedRectangle (r, 3.0f);
                auto fill = r.withTrimmedTop (r.getHeight() * (1.0f - (0.25f + 0.75f * v)));
                g.setColour (col.withAlpha ((0.55f + 0.45f * v) * a)); g.fillRoundedRectangle (fill, 3.0f);
                if (s.ratchet > 1)
                {
                    g.setColour (juce::Colour (0xff10141a));
                    for (int j = 1; j < s.ratchet; ++j) g.fillRect (r.getX() + r.getWidth() * (float) j / s.ratchet - 0.75f, r.getY() + 3.0f, 1.5f, r.getHeight() - 6.0f);
                }
                if (s.prob < 100) { g.setColour (C::text.withAlpha (0.9f)); g.setFont (font (8.5f)); g.drawText (juce::String ((int) s.prob) + "%", r.withTrimmedBottom (r.getHeight() - 12), juce::Justification::centred); }
            }
            if (k == cur) { g.setColour (juce::Colours::white.withAlpha (0.85f)); g.drawRoundedRectangle (r, 3.0f, 1.5f); }
        }
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    int pad, step; if (! cellAt (e.position, pad, step)) return;
    ed.selectPad (pad);
    const int pat = proc.currentPattern();
    if (step >= proc.getLength (pat, pad)) return;
    auto s = proc.getStep (pat, pad, step);
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m, pr, ra, ve;
        for (int v : { 100, 75, 50, 25, 10 }) pr.addItem (100 + v, juce::String (v) + " %", true, s.prob == v);
        for (int r = 1; r <= 4; ++r) ra.addItem (300 + r, r == 1 ? "Off" : "x" + juce::String (r), true, s.ratchet == r);
        ve.addItem (401, "Accent (127)"); ve.addItem (402, "Normal (100)"); ve.addItem (403, "Ghost (55)");
        m.addSubMenu ("Probability", pr); m.addSubMenu ("Ratchet (roll)", ra); m.addSubMenu ("Velocity", ve);
        m.addItem (500, s.vel > 0 ? "Delete hit" : "Add hit");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }), [this, pat, pad, step, s] (int r) mutable
        {
            if (r >= 100 && r <= 200) { s.prob = (uint8_t) (r - 100); if (s.vel == 0) s.vel = 100; }
            else if (r > 300 && r <= 304) { s.ratchet = (uint8_t) (r - 300); if (s.vel == 0) s.vel = 100; }
            else if (r == 401) s.vel = 127; else if (r == 402) s.vel = 100; else if (r == 403) s.vel = 55;
            else if (r == 500) s.vel = s.vel > 0 ? 0 : 100;
            else return;
            proc.setStep (pat, pad, step, s); repaint();
        });
        return;
    }
    if (e.mods.isShiftDown()) s.vel = s.vel == 127 ? 100 : 127;
    else if (s.vel > 0) { dragPad = -1; s.vel = 0; s.prob = 100; s.ratchet = 1; proc.setStep (pat, pad, step, s); repaint(); return; }
    else { s.vel = 100; proc.audition (pad, 0.8f); }
    proc.setStep (pat, pad, step, s);
    dragPad = pad; dragStep = step; dragStartY = e.position.y; dragStartVel = s.vel;
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragPad < 0) return;
    const int pat = proc.currentPattern();
    auto s = proc.getStep (pat, dragPad, dragStep);
    s.vel = (uint8_t) juce::jlimit (1, 127, dragStartVel + (int) ((dragStartY - e.position.y) * 2.0f));
    proc.setStep (pat, dragPad, dragStep, s);
    repaint();
}

//==============================================================================
void SampleZone::rebuild()
{
    peaks.assign (240, 0.0f);
    const auto smp = proc.sampleFor (pad);
    const auto q = proc.padParams (pad);
    if (smp != nullptr)
    {
        const int n = smp->buf.getNumSamples();
        for (int i = 0; i < 240; ++i)
        {
            const int a = (int) ((double) i / 240 * n), b = juce::jmax (a + 1, (int) ((double) (i + 1) / 240 * n));
            peaks[(size_t) i] = smp->buf.getMagnitude (0, a, juce::jmin (b, n) - a);
        }
    }
    else
    {
        // render the synth hit offline for the picture (what you see is what plays)
        Voice v; v.prepare (24000.0);
        auto qq = q; qq.useSample = false; qq.pan = 0; qq.level = 1;
        v.start (pad, qq, 1.0f, nullptr, 1234u);
        const int len = pad == RKick && qq.model == 6 ? 48000 : pad == RRide || pad == RFx || pad == ROHat ? 36000 : 18000;
        std::vector<float> L ((size_t) len, 0.0f), R ((size_t) len, 0.0f);
        v.render (L.data(), R.data(), len);
        for (int i = 0; i < 240; ++i)
        {
            float m = 0; for (int k = i * len / 240; k < (i + 1) * len / 240; ++k) m = juce::jmax (m, std::abs (L[(size_t) k]));
            peaks[(size_t) i] = m;
        }
    }
    float mx = 0.001f; for (float p : peaks) mx = juce::jmax (mx, p);
    for (auto& p : peaks) p /= mx;
    dirty = false;
}

void SampleZone::checkParams()
{
    const auto q = proc.padParams (pad);
    const float sig = q.model * 1000.0f + q.tune * 13.0f + q.decay * 7.0f + q.tone * 3.0f + q.snap * 11.0f + q.drive * 5.0f + (q.useSample ? 50000.0f : 0.0f) + q.start * 17.0f;
    if (std::abs (sig - lastSig) > 1e-4f) { lastSig = sig; dirty = true; repaint(); }
}

void SampleZone::paint (juce::Graphics& g)
{
    if (dirty) rebuild();
    auto r = getLocalBounds().toFloat();
    lcd (g, r, ACC);
    const auto col = padColour (pad);
    auto a = r.reduced (8.0f, 18.0f);
    juce::Path wave;
    const int N = (int) peaks.size();
    for (int i = 0; i < N; ++i) { const float x = a.getX() + a.getWidth() * i / (N - 1), y = a.getCentreY() - peaks[(size_t) i] * a.getHeight() * 0.5f; if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y); }
    for (int i = N - 1; i >= 0; --i) wave.lineTo (a.getX() + a.getWidth() * i / (N - 1), a.getCentreY() + peaks[(size_t) i] * a.getHeight() * 0.5f);
    wave.closeSubPath();
    g.setColour (col.withAlpha (0.35f)); g.fillPath (wave);
    g.setColour (col); g.strokePath (wave, juce::PathStrokeType (1.0f));
    const auto name = proc.sampleName (pad);
    g.setFont (font (10.0f)); g.setColour (C::text.withAlpha (0.85f));
    g.drawText (name.isNotEmpty() ? "SAMPLE: " + name : "SYNTH: " + modelNames (pad)[(int) proc.apvts.getRawParameterValue (IDs::pad (pad, "model"))->load()],
                r.reduced (8.0f, 3.0f).withHeight (14.0f), juce::Justification::centredLeft);
    g.setColour (C::dim); g.setFont (font (8.5f));
    g.drawText (name.isNotEmpty() ? "drop another file to replace, X = back to synth" : "drop your WAV here", r.reduced (8.0f, 3.0f).withTrimmedTop (r.getHeight() - 20.0f), juce::Justification::centredLeft);
    if (hover) { g.setColour (ACC.withAlpha (0.15f)); g.fillRoundedRectangle (r, 4.0f); g.setColour (ACC); g.drawRoundedRectangle (r.reduced (1.0f), 4.0f, 1.5f); }
}

void SampleZone::filesDropped (const juce::StringArray& files, int, int)
{
    hover = false;
    juce::String err;
    if (! files.isEmpty() && proc.loadSample (pad, juce::File (files[0]), err)) { ed.showMessage ("loaded " + juce::File (files[0]).getFileName() + " on " + padNames()[pad]); proc.audition (pad); }
    else ed.showMessage (err.isNotEmpty() ? err : juce::String ("could not load the file"));
    refresh();
}
void SampleZone::mouseDown (const juce::MouseEvent&) { proc.audition (pad); }

//==============================================================================
juce::File DragOut::make() { return isWav ? proc.exportWav (ed.exportBars()) : proc.exportMidi (ed.exportBars()); }
void DragOut::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff23272d)); g.fillRoundedRectangle (r, 4.0f);
    g.setColour ((isWav ? HOT : ACC).withAlpha (isMouseOver() ? 0.9f : 0.55f)); g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (C::text); g.setFont (font (10.5f));
    g.drawText (text, r, juce::Justification::centred);
}
void DragOut::mouseDrag (const juce::MouseEvent& e)
{
    if (dragged || e.getDistanceFromDragStart() < 6) return;
    dragged = true;
    const auto f = make();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragged = false; });
    else dragged = false;
}
void DragOut::mouseUp (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() >= 6) return;
    const auto f = make();
    ed.showMessage (f.existsAsFile() ? "saved " + f.getFullPathName() : juce::String ("export failed"));
}

//==============================================================================
LumenDrumsEditor::LumenDrumsEditor (LumenDrumsProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    // ---- pattern row
    for (int i = 0; i < kPatterns; ++i)
    {
        auto* b = patBtns.add (new juce::TextButton (patternNames()[i]));
        b->setColour (juce::TextButton::buttonColourId, ACC);
        b->setTooltip ("Pattern " + patternNames()[i] + " (" + sectionNames()[i] + " after SONG).  Automate the PATTERN parameter or play C0-G0 to switch.  Right-click: copy the current pattern here");
        b->setBounds (16 + i * 42, 82, 38, 30);
        b->onClick = [this, i] { if (juce::ModifierKeys::currentModifiers.isPopupMenu()) return; proc.selectPattern (i); };
        b->addMouseListener (this, false);
        addAndMakeVisible (b);
    }
    auto* gen = add<Combo> (*this, owned, st, IDs::genre, ACC); gen->setBounds (370, 84, 150, 26); gen->setTooltip ("Genre for GENERATE / SONG");
    auto* cx = add<Knob> (*this, owned, st, IDs::complexity, "", ACC); cx->setBounds (524, 76, 40, 42); cx->slider.setTooltip ("Complexity: 0 = minimal, 100 = busy (ghost notes, rolls)");
    struct B { juce::TextButton* b; int x, w; const char* tip; std::function<void()> fn; };
    const B bs[] {
        { &genB, 568, 92, "New groove for the current pattern in the chosen genre", [this] { proc.generate(); } },
        { &songB, 664, 82, "Whole song in one click: A intro, B groove, C drop, D break, E 4-bar build roll, F/G with fills, H variation  (+ the genre's kit)", [this] { proc.generateSong(); } },
        { &fillB, 750, 52, "Turns the last beat into a fill (snare roll, tom pickups)", [this] { proc.fill(); } },
        { &varyB, 806, 52, "Variation: moves the optional hits, keeps kick and backbeat", [this] { proc.variation(); } },
        { &clearB, 862, 56, "Clear the current pattern", [this] { proc.clearPattern(); } } };
    for (auto& b : bs) { b.b->setBounds (b.x, 83, b.w, 28); b.b->setTooltip (b.tip); b.b->onClick = b.fn; b.b->setColour (juce::TextButton::buttonColourId, b.b == &genB || b.b == &songB ? ACC : C::dim.brighter (0.4f)); addAndMakeVisible (*b.b); }
    auto* sw = add<Knob> (*this, owned, st, IDs::swing, "SWING", HOT); sw->setBounds (930, 70, 46, 52); sw->slider.setTooltip ("Swing on every 2nd 16th");
    auto* hu = add<Knob> (*this, owned, st, IDs::humanize, "HUMAN", HOT); hu->setBounds (982, 70, 46, 52); hu->slider.setTooltip ("Humanize: small velocity variations");
    auto* sq = add<Toggle> (*this, owned, st, IDs::seqOn, "SEQUENCER", ACC); sq->setBounds (1040, 84, 124, 26);
    sq->setTooltip ("On: plays the pattern with Ableton.  Off: Lumen Drums is a playable drum rack (MIDI C1 = kick, D1 = snare, D#1 = clap, F#1 = closed hat, A#1 = open hat ...)");

    // ---- grid
    grid = std::make_unique<StepGrid> (proc, *this); addAndMakeVisible (*grid);
    grid->setBounds (GX, GY, 16 * 40, kPads * StepGrid::kRowH);
    for (int pg = 0; pg < 4; ++pg)
    {
        auto* b = pageBtns.add (new juce::TextButton (juce::String (pg * 16 + 1) + "-" + juce::String (pg * 16 + 16)));
        b->setColour (juce::TextButton::buttonColourId, ACC); b->setBounds (GX + pg * 70, 138, 66, 20);
        b->onClick = [this, pg] { grid->page = pg; grid->repaint(); };
        b->setTooltip ("Steps page (tracks can be up to 64 steps long)");
        addAndMakeVisible (b);
    }
    for (int t = 0; t < kPads; ++t)
    {
        const int y = GY + t * StepGrid::kRowH + 4;
        auto* nb = nameBtns.add (new juce::TextButton (padNames()[t]));
        nb->setColour (juce::TextButton::buttonColourId, padColour (t)); nb->setBounds (16, y, 76, 30);
        nb->onClick = [this, t] { selectPad (t); proc.audition (t); };
        nb->setTooltip ("Select + hear the pad");
        addAndMakeVisible (nb);
        add<Toggle> (*this, owned, st, IDs::pad (t, "mute"), "M", C::red)->setBounds (96, y + 3, 24, 24);
        add<Toggle> (*this, owned, st, IDs::pad (t, "solo"), "S", HOT)->setBounds (122, y + 3, 24, 24);
        auto* lb = lenBoxes.add (new juce::ComboBox());
        for (int i = 0; i < (int) std::size (kLenChoices); ++i) lb->addItem (juce::String (kLenChoices[i]), kLenChoices[i]);
        lb->setTooltip ("Track length in steps: different lengths = polyrhythm (e.g. 12 against 16)");
        lb->setBounds (GX + 16 * 40 + 8, y + 3, 54, 24);
        lb->onChange = [this, t, lb] { if (lb->getSelectedId() > 0) proc.setLength (proc.currentPattern(), t, lb->getSelectedId()); };
        addAndMakeVisible (lb);
        auto* db = diceBtns.add (new juce::TextButton ("DICE"));
        db->setColour (juce::TextButton::buttonColourId, padColour (t)); db->setBounds (GX + 16 * 40 + 66, y + 2, 46, 26);
        db->onClick = [this, t] { proc.generateTrack (t); };
        db->setTooltip ("New groove for this track only");
        addAndMakeVisible (db);
    }

    // ---- master / export column
    const int mx = 920;
    auto* gl = add<Knob> (*this, owned, st, IDs::glue, "GLUE", ACC); gl->setBounds (mx, 166, 74, 84); gl->slider.setTooltip ("Bus glue compressor");
    auto* cl = add<Knob> (*this, owned, st, IDs::clip, "CLIP", HOT); cl->setBounds (mx + 80, 166, 74, 84); cl->slider.setTooltip ("Drive into a soft clipper: louder, punchier transients");
    auto* vo = add<Knob> (*this, owned, st, IDs::volume, "VOLUME", C::text); vo->setBounds (mx + 160, 166, 74, 84);
    barsBox.addItem ("1 bar", 1); barsBox.addItem ("2 bars", 2); barsBox.addItem ("4 bars", 3); barsBox.addItem ("8 bars", 4);
    barsBox.setSelectedId (3, juce::dontSendNotification); barsBox.setBounds (mx, 386, 234, 24);
    addAndMakeVisible (barsBox);
    auto dm = std::make_unique<DragOut> (proc, *this, false, "DRAG MIDI"); dm->setBounds (mx, 416, 114, 34); addAndMakeVisible (*dm); owned.push_back (std::move (dm));
    auto dw = std::make_unique<DragOut> (proc, *this, true, "DRAG WAV"); dw->setBounds (mx + 120, 416, 114, 34); addAndMakeVisible (*dw); owned.push_back (std::move (dw));

    // ---- pad editor
    zone = std::make_unique<SampleZone> (proc, *this); addAndMakeVisible (*zone); zone->setBounds (126, 592, 282, 150);
    clearSmp.setBounds (382, 594, 24, 20); clearSmp.setColour (juce::TextButton::buttonColourId, C::red);
    clearSmp.setTooltip ("Remove the sample (back to the synth sound)");
    clearSmp.onClick = [this] { proc.clearSample (selPad); zone->refresh(); };
    addAndMakeVisible (clearSmp);

    // ---- header
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setColour (juce::ComboBox::textColourId, C::text);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (420, 18, 300, 28);
    for (auto* b : { &prevB, &nextB }) { b->setColour (juce::TextButton::buttonColourId, ACC); addAndMakeVisible (*b); }
    prevB.setBounds (386, 18, 28, 28); nextB.setBounds (726, 18, 28, 28);
    prevB.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    for (auto* b : { &aB, &bB, &copyB }) { b->setColour (juce::TextButton::buttonColourId, ACC); addAndMakeVisible (*b); }
    aB.setBounds (766, 18, 30, 28); bB.setBounds (798, 18, 30, 28); copyB.setBounds (832, 18, 40, 28);
    aB.onClick = [this] { proc.switchAB (0); };
    bB.onClick = [this] { proc.switchAB (1); };
    copyB.onClick = [this] { proc.copyAB(); };

    selectPad (0);
    setSize (W, HH);
    startTimerHz (30);
}

LumenDrumsEditor::~LumenDrumsEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenDrumsEditor::selectPad (int p)
{
    if (p == selPad && ! padOwned.empty()) return;
    selPad = juce::jlimit (0, kPads - 1, p);
    for (int i = 0; i < kPads; ++i) nameBtns[i]->setToggleState (i == selPad, juce::dontSendNotification);
    buildPadEditor();
    zone->pad = selPad; zone->refresh();
    grid->repaint(); repaint();
}

void LumenDrumsEditor::buildPadEditor()
{
    padOwned.clear();
    auto& st = proc.apvts;
    const auto col = padColour (selPad);
    auto* m = add<Combo> (*this, padOwned, st, IDs::pad (selPad, "model"), col); m->setBounds (26, 600, 92, 26); m->setTooltip ("Synth model for this pad (ignored while a sample is loaded)");
    auto* ch = add<Combo> (*this, padOwned, st, IDs::pad (selPad, "choke"), col); ch->setBounds (26, 652, 92, 24); ch->setTooltip ("Choke group: pads in the same group cut each other (open hat stops when the closed hat plays)");
    auto* ou = add<Combo> (*this, padOwned, st, IDs::pad (selPad, "out"), col); ou->setBounds (26, 698, 92, 24); ou->setTooltip ("Output: Main, or an extra output -> its own Ableton track (enable the outputs in Ableton)");
    struct K { const char* id; const char* lb; const char* tip; };
    const K ks[] { { "tune", "TUNE", "Pitch in semitones" }, { "decay", "DECAY", "Length of the hit" }, { "tone", "TONE", "Left = darker (low-pass), right = thinner (high-pass)" },
                   { "snap", "SNAP", "Attack / click" }, { "drive", "DRIVE", "Saturation" }, { "level", "LEVEL", "Volume" }, { "pan", "PAN", "Pan" }, { "start", "START", "Sample start (samples only)" } };
    for (int i = 0; i < 8; ++i)
    {
        const bool bip = juce::String (ks[i].id) == "tune" || juce::String (ks[i].id) == "tone" || juce::String (ks[i].id) == "pan";
        auto* k = add<Knob> (*this, padOwned, st, IDs::pad (selPad, ks[i].id), ks[i].lb, col, bip);
        k->setBounds (420 + i * 72, 604, 70, 82); k->slider.setTooltip (ks[i].tip);
    }
    auto* rv = add<Toggle> (*this, padOwned, st, IDs::pad (selPad, "rev"), "REVERSE", col); rv->setBounds (420, 704, 110, 26);
    rv->setTooltip ("Play the sample backwards");
}

void LumenDrumsEditor::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < kPatterns; ++i)
        if (e.eventComponent == patBtns[i] && e.mods.isPopupMenu())
        {
            proc.copyPatternTo (i);
            showMessage ("pattern " + patternNames()[proc.currentPattern()] + " copied to " + patternNames()[i]);
        }
}

void LumenDrumsEditor::timerCallback()
{
    if (proc.programVersion.load() != lastProgram)
    {
        lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
        zone->refresh();
    }
    const int pat = proc.currentPattern();
    if (pat != lastPattern || proc.patternVersion.load() != lastPatVersion)
    {
        lastPattern = pat; lastPatVersion = proc.patternVersion.load();
        for (int i = 0; i < kPatterns; ++i) patBtns[i]->setToggleState (i == pat, juce::dontSendNotification);
        for (int t = 0; t < kPads; ++t) lenBoxes[t]->setSelectedId (proc.getLength (pat, t), juce::dontSendNotification);
        grid->repaint();
    }
    for (int i = 0; i < 4; ++i) pageBtns[i]->setToggleState (i == grid->page, juce::dontSendNotification);
    for (int t = 0; t < kPads; ++t)
    {
        const float f = proc.padFlash[(size_t) t].exchange (0.0f);
        flash[(size_t) t] = juce::jmax (f, flash[(size_t) t] * 0.8f);
    }
    zone->checkParams();
    outPk = juce::jmax (proc.outPeak.load(), outPk * 0.85f);
    aB.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification); bB.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    if (proc.playStep.load() >= 0) grid->repaint();
    repaint (0, 0, W, 66);
    repaint (GX - 6, GY, 6, kPads * StepGrid::kRowH);
    repaint (910, 250, 260, 130);
}

void LumenDrumsEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    g.setColour (juce::Colour (0xff111316)); g.fillRect (0, 0, W, 64);
    g.setColour (C::border); g.fillRect (0, 64, W, 1);
    g.setColour (C::text); g.setFont (juce::Font (juce::FontOptions (25.0f, juce::Font::bold)).withExtraKerningFactor (0.12f));
    g.drawText ("LUMEN", 22, 16, 120, 32, juce::Justification::centredLeft);
    g.setColour (ACC); g.setFont (juce::Font (juce::FontOptions (25.0f, juce::Font::plain)).withExtraKerningFactor (0.12f));
    g.drawText ("DRUMS", 132, 16, 130, 32, juce::Justification::centredLeft);
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("OUTPUT", 900, 10, 80, 12, juce::Justification::centredLeft);
    ledMeter (g, { 900.0f, 28.0f, 264.0f, 8.0f }, outPk, outPk, false, 30);
    g.drawText (juce::String (juce::roundToInt (proc.hostBpm.load())) + " BPM", 900, 42, 120, 14, juce::Justification::centredLeft);

    panel (g, { 8.0f, 72.0f, 1164.0f, 50.0f }, "", ACC);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("PATTERN", 16, 72, 100, 11, juce::Justification::centredLeft);
    g.drawText ("GENERATOR", 370, 72, 100, 11, juce::Justification::centredLeft);
    panel (g, { 8.0f, 130.0f, 900.0f, 424.0f }, "", ACC);
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("STEPS", 16, 140, 120, 14, juce::Justification::centredLeft);
    g.drawText ("LENGTH", GX + 16 * 40 + 8, 140, 60, 14, juce::Justification::centredLeft);
    // trigger flashes next to the grid
    for (int t = 0; t < kPads; ++t)
    {
        const float f = flash[(size_t) t];
        g.setColour (f > 0.02f ? padColour (t).withAlpha (0.3f + 0.7f * f) : C::track);
        g.fillRoundedRectangle ((float) GX - 5.0f, (float) (GY + t * StepGrid::kRowH + 8), 3.0f, 22.0f, 1.5f);
    }
    panel (g, { 912.0f, 130.0f, 260.0f, 424.0f }, "BUS / EXPORT", ACC);
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("GLUE GAIN REDUCTION", 920, 264, 240, 12, juce::Justification::centredLeft);
    const float grDb = juce::Decibels::gainToDecibels (proc.glueGr.load());
    g.setColour (C::field); g.fillRoundedRectangle (920.0f, 280.0f, 234.0f, 8.0f, 2.0f);
    g.setColour (HOT); g.fillRoundedRectangle (920.0f, 280.0f, 234.0f * juce::jlimit (0.0f, 1.0f, -grDb / 12.0f), 8.0f, 2.0f);
    g.setColour (C::dim); g.drawText (juce::String (grDb, 1) + " dB", 920, 292, 100, 12, juce::Justification::centredLeft);
    g.drawText ("EXPORT LENGTH", 920, 372, 200, 12, juce::Justification::centredLeft);
    g.setFont (font (8.5f));
    g.drawFittedText ("Ableton: drag MIDI onto a Drum Rack track or WAV onto an audio track. For separate tracks per pad: set OUTPUT on the pad, then on new audio tracks choose Audio From: Lumen Drums -> Out 2..5.",
                      juce::Rectangle<int> (920, 458, 240, 90), juce::Justification::topLeft, 7);
    // pad editor
    const auto col = padColour (selPad);
    panel (g, { 8.0f, 562.0f, 1164.0f, 190.0f }, padNames()[selPad] + "  PAD", col);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("MODEL", 26, 588, 90, 11, juce::Justification::centredLeft);
    g.drawText ("CHOKE", 26, 640, 90, 11, juce::Justification::centredLeft);
    g.drawText ("OUTPUT", 26, 686, 90, 11, juce::Justification::centredLeft);
    g.setColour (C::green.withAlpha (0.85f)); g.setFont (font (9.0f));
    g.drawFittedText (msg, juce::Rectangle<int> (560, 708, 600, 30), juce::Justification::centredLeft, 2);
}
