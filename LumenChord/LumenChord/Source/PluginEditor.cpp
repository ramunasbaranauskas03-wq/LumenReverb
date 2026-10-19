#include "PluginEditor.h"

using namespace lumen::led;
using namespace lumen::chord;

namespace
{
constexpr int W = 1120, HH = 680;
const juce::Colour MAIN = C::violet, PADC = C::magenta, ARPC = C::cyan, VOC = C::amber;
template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}
}

//==============================================================================
void PadButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const int len = (int) proc.apvts.getRawParameterValue (IDs::numSlots)->load();
    const bool active = idx < len;
    const bool now = proc.currentChordSlot.load() == idx && active;
    const auto s = proc.getSlot (idx);
    g.setColour (juce::Colour (0xff070a10)); g.fillRoundedRectangle (r, 8.0f);
    if (now) { g.setColour (PADC.withAlpha (0.3f)); g.fillRoundedRectangle (r, 8.0f); }
    juce::ColourGradient cg (PADC.withAlpha (active ? 0.18f : 0.04f), r.getX(), r.getY(), C::violet.withAlpha (active ? 0.08f : 0.02f), r.getX(), r.getBottom(), false);
    g.setGradientFill (cg); g.fillRoundedRectangle (r, 8.0f);
    g.setColour ((now ? juce::Colours::white : PADC).withAlpha (active ? (now ? 0.9f : 0.5f) : 0.15f)); g.drawRoundedRectangle (r, 8.0f, now ? 2.0f : 1.0f);
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText (juce::String (idx + 1) + "   " + juce::MidiMessage::getMidiNoteName (48 + idx, true, true, 3), r.reduced (8.0f, 6.0f), juce::Justification::topLeft);
    g.setFont (font (22.0f)); g.setColour ((active ? juce::Colours::white : C::dim).withAlpha (s.on ? 1.0f : 0.3f));
    g.drawText (name, r.withTrimmedTop (18.0f).withHeight (36.0f), juce::Justification::centred);
    g.setFont (font (12.0f)); g.setColour (PADC.interpolatedWith (juce::Colours::white, 0.3f).withAlpha (active ? 0.9f : 0.3f));
    g.drawText (roman, r.withTrimmedTop (56.0f).withHeight (18.0f), juce::Justification::centred);
    drawLed (g, { r.getCentreX(), r.getBottom() - 12.0f }, 3.4f, PADC, now ? 1.0f : 0.1f);
}
void PadButton::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) { proc.audition (idx); return; }
    auto s = proc.getSlot (idx);
    juce::PopupMenu m, deg, sh, q, inv, oct;
    static const char* rn[] { "I", "II", "III", "IV", "V", "VI", "VII" };
    for (int d = 0; d < 7; ++d) deg.addItem (100 + d, rn[d], true, s.degree == d);
    sh.addItem (200, "flat (borrowed)", true, s.shift == -1); sh.addItem (201, "natural", true, s.shift == 0); sh.addItem (202, "sharp", true, s.shift == 1);
    for (int k = 0; k < kQualities; ++k) q.addItem (300 + k, qualityNames()[k], true, s.quality == k);
    inv.addItem (399, "Global", true, s.inversion < 0); for (int k = 0; k < 4; ++k) inv.addItem (400 + k, juce::String (k == 0 ? "Root" : juce::String (k) + (k == 1 ? "st" : k == 2 ? "nd" : "rd")), true, s.inversion == k);
    for (int k = -1; k <= 1; ++k) oct.addItem (501 + k, k == 0 ? "0" : (k > 0 ? "+1" : "-1"), true, s.octave == k);
    m.addSubMenu ("Degree", deg); m.addSubMenu ("Flat / Sharp", sh); m.addSubMenu ("Quality", q); m.addSubMenu ("Inversion", inv); m.addSubMenu ("Octave", oct);
    m.addItem (600, s.on ? "Mute this slot (rest)" : "Unmute");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, s] (int r) mutable
    {
        if (r >= 100 && r < 107) s.degree = r - 100;
        else if (r >= 200 && r < 203) s.shift = r - 201;
        else if (r >= 300 && r < 300 + kQualities) s.quality = r - 300;
        else if (r == 399) s.inversion = -1;
        else if (r >= 400 && r < 404) s.inversion = r - 400;
        else if (r >= 500 && r < 503) s.octave = r - 501;
        else if (r == 600) s.on = ! s.on;
        else return;
        proc.setSlot (idx, s); proc.audition (idx);
    });
}

//==============================================================================
void StepLane::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff04060a)); g.fillRoundedRectangle (r, 4.0f);
    const float w = r.getWidth() / 16.0f;
    for (int i = 0; i < 16; ++i)
    {
        auto c = juce::Rectangle<float> (r.getX() + i * w, r.getY(), w, r.getHeight()).reduced (2.0f, 4.0f);
        g.setColour ((i / 4) % 2 == 0 ? juce::Colour (0xff0b0f16) : juce::Colour (0xff080b10)); g.fillRoundedRectangle (c, 2.0f);
        const float v = proc.getStep (i);
        if (v > 0.01f)
        {
            auto bar = c.withTrimmedTop (c.getHeight() * (1.0f - v));
            juce::ColourGradient cg (ARPC.interpolatedWith (juce::Colours::white, 0.2f), 0, bar.getY(), ARPC.darker (0.6f), 0, bar.getBottom(), false);
            g.setGradientFill (cg); g.fillRoundedRectangle (bar, 2.0f);
        }
        else { g.setColour (C::dim.withAlpha (0.4f)); g.drawText ("-", c, juce::Justification::centred); }
        if (proc.getRatchet (i)) { g.setColour (C::amber); g.setFont (font (9.0f)); g.drawText ("x2", c.withHeight (12.0f), juce::Justification::centred); }
    }
}
void StepLane::set (const juce::MouseEvent& e)
{
    const int i = juce::jlimit (0, 15, (int) (e.position.x / ((float) getWidth() / 16.0f)));
    const float v = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - 4.0f) / ((float) getHeight() - 8.0f));
    proc.setStep (i, v < 0.08f ? 0.0f : v);
    repaint();
}
void StepLane::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { proc.toggleRatchet (juce::jlimit (0, 15, (int) (e.position.x / ((float) getWidth() / 16.0f)))); repaint(); return; }
    set (e);
}
void StepLane::mouseDrag (const juce::MouseEvent& e) { if (! e.mods.isPopupMenu()) set (e); }

//==============================================================================
void Keyboard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const int lo = 24, hi = 96;                   // C1 .. C7
    int whites = 0; for (int n = lo; n <= hi; ++n) if (! juce::MidiMessage::isMidiNoteBlack (n)) ++whites;
    const float ww = r.getWidth() / (float) whites;
    const int key = (int) proc.apvts.getRawParameterValue (IDs::key)->load();
    const auto& sc = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, (int) proc.apvts.getRawParameterValue (IDs::scale)->load())];
    auto inScale = [&] (int n) { const int pc = ((n - key) % 12 + 12) % 12; for (int v : sc.iv) if (v == pc) return true; return false; };
    int wi = 0;
    std::vector<std::pair<float, int>> blacks;
    for (int n = lo; n <= hi; ++n)
    {
        if (juce::MidiMessage::isMidiNoteBlack (n)) { blacks.push_back ({ r.getX() + wi * ww - ww * 0.32f, n }); continue; }
        auto k = juce::Rectangle<float> (r.getX() + wi * ww, r.getY(), ww - 1.0f, r.getHeight());
        const bool on = proc.sounding[(size_t) n].load(), ch = proc.inChord[(size_t) n].load();
        g.setColour (on ? PADC : ch ? ARPC.withAlpha (0.55f).interpolatedWith (juce::Colours::white, 0.35f) : inScale (n) ? juce::Colour (0xffd9dee8) : juce::Colour (0xff8a909c)); g.fillRoundedRectangle (k, 2.0f);
        if (on) { g.setColour (PADC.withAlpha (0.4f)); g.fillRoundedRectangle (k.expanded (1.0f), 3.0f); }
        if (n % 12 == 0) { g.setColour (juce::Colours::black.withAlpha (0.6f)); g.setFont (font (8.0f)); g.drawText ("C" + juce::String (n / 12 - 2), k.withTrimmedTop (k.getHeight() - 12.0f), juce::Justification::centred); }
        ++wi;
    }
    for (auto& b : blacks)
    {
        auto k = juce::Rectangle<float> (b.first, r.getY(), ww * 0.64f, r.getHeight() * 0.62f);
        const bool on = proc.sounding[(size_t) b.second].load(), ch = proc.inChord[(size_t) b.second].load();
        g.setColour (on ? PADC : ch ? ARPC.darker (0.3f) : inScale (b.second) ? juce::Colour (0xff3a404c) : juce::Colour (0xff10131a)); g.fillRoundedRectangle (k, 2.0f);
    }
}

//==============================================================================
void MidiDrag::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff0a0f16)); g.fillRoundedRectangle (r, 5.0f);
    g.setColour (C::green.withAlpha (0.7f)); g.drawRoundedRectangle (r, 5.0f, 1.2f);
    g.setFont (font (11.0f)); g.setColour (C::green);
    g.drawText ("DRAG MIDI", r, juce::Justification::centred);
}
void MidiDrag::mouseDrag (const juce::MouseEvent& e)
{
    if (dragged || e.getDistanceFromDragStart() < 6) return;
    dragged = true;
    const auto f = proc.exportMidi (0);
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragged = false; });
    else dragged = false;
}
void MidiDrag::mouseUp (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() >= 6) return;
    const auto f = proc.exportMidi (0);
    ed.showMessage (f.existsAsFile() ? "saved " + f.getFullPathName() : juce::String ("export failed"));
}

//==============================================================================
LumenChordEditor::LumenChordEditor (LumenChordProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    // key & mode row
    struct Cb { const char* id; int x, w; juce::Colour c; const char* tip; };
    const Cb cbs[] {
        { "key", 30, 80, MAIN, "Key" }, { "scale", 116, 170, MAIN, "Scale / mode" }, { "chord_type", 292, 150, PADC, "Chord type for all in-key chords" },
        { "mode", 448, 220, C::green, "ONE-FINGER: any key plays its scale chord.  PADS: C2..G2 trigger the 8 slots (other keys pass through).  AUTO: the progression plays with the song.  THROUGH: play your own chords (for the arp)" } };
    for (auto& c : cbs) { auto* cb = add<Combo> (*this, owned, st, c.id, c.c); cb->setBounds (c.x, 92, c.w, 28); cb->setTooltip (c.tip); }
    auto* snd = add<Toggle> (*this, owned, st, IDs::sound, "PREVIEW SOUND", C::orange); snd->setBounds (690, 94, 130, 24);
    snd->setTooltip ("Built-in sound so you hear it right away. Turn it off when another track plays the MIDI (MIDI From -> Lumen Chord)");
    auto* stp = add<Combo> (*this, owned, st, IDs::soundType, C::orange); stp->setBounds (826, 92, 110, 28);
    auto* slv = add<Knob> (*this, owned, st, IDs::soundLvl, "", C::orange); slv->setBounds (944, 80, 50, 50); slv->slider.setTooltip ("Preview sound level");
    slv->setBounds (950, 84, 44, 44);

    for (int i = 0; i < 8; ++i) { pads[i] = std::make_unique<PadButton> (proc, i); addAndMakeVisible (*pads[i]); pads[i]->setBounds (26 + i * 134, 164, 130, 104); }
    auto* len = add<Knob> (*this, owned, st, IDs::numSlots, "LENGTH", PADC); len->setBounds (26, 272, 70, 62); len->slider.setTooltip ("How many slots the progression uses");
    auto* bars = add<Combo> (*this, owned, st, IDs::bars, PADC); bars->setBounds (104, 290, 110, 26); bars->setTooltip ("Chord length in AUTO mode (and in the MIDI export)");
    moodBox.addItemList ({ "Euphoric", "Emotional", "Dark", "Epic", "Chill", "Surprise me" }, 1);
    moodBox.setSelectedId (1, juce::dontSendNotification); moodBox.setColour (juce::ComboBox::textColourId, C::green);
    addAndMakeVisible (moodBox); moodBox.setBounds (680, 290, 140, 26);
    genB.setColour (juce::TextButton::buttonColourId, C::green); genB.setBounds (826, 289, 110, 28);
    genB.setTooltip ("Writes a new progression for the mood in your key (press again for another)");
    genB.onClick = [this] { proc.generate ((LumenChordProcessor::Mood) (moodBox.getSelectedId() - 1)); refreshPads(); };
    addAndMakeVisible (genB);
    drag = std::make_unique<MidiDrag> (proc, *this); addAndMakeVisible (*drag); drag->setBounds (944, 288, 150, 30);

    keys = std::make_unique<Keyboard> (proc); addAndMakeVisible (*keys); keys->setBounds (26, 352, 1068, 56);

    // voicing
    auto* inv = add<Combo> (*this, owned, st, IDs::inversion, VOC); inv->setBounds (30, 456, 120, 26); inv->setTooltip ("Inversion");
    auto* spr = add<Combo> (*this, owned, st, IDs::spread, VOC); spr->setBounds (156, 456, 140, 26); spr->setTooltip ("Close / open (drop-2) / wide voicing");
    auto* vl = add<Toggle> (*this, owned, st, IDs::voiceLead, "VOICE LEADING", VOC); vl->setBounds (302, 456, 126, 26);
    vl->setTooltip ("Each chord moves to the inversion closest to the previous one - smooth like a pianist");
    struct K { const char* id; const char* lb; int x; const char* tip; };
    const K vk[] { { "octave", "OCTAVE", 30, "Octave" }, { "strum", "STRUM", 112, "Strum: notes one after another (guitar / harp feel)" },
                   { "humanize", "HUMANIZE", 194, "Small timing and velocity variations" }, { "velocity", "VELOCITY", 276, "Velocity (when input velocity is off, and in AUTO)" } };
    for (auto& k : vk) { auto* kn = add<Knob> (*this, owned, st, k.id, k.lb, VOC); kn->setBounds (k.x, 494, 78, 92); kn->slider.setTooltip (k.tip); }
    auto* bs = add<Toggle> (*this, owned, st, IDs::bass, "BASS NOTE", VOC); bs->setBounds (30, 600, 120, 24); bs->setTooltip ("Adds the root an octave below");
    auto* iv = add<Toggle> (*this, owned, st, IDs::inVel, "INPUT VELOCITY", VOC); iv->setBounds (156, 600, 140, 24);

    // arp
    auto* ao = add<Toggle> (*this, owned, st, IDs::arpOn, "ARP", ARPC); ao->setBounds (456, 456, 80, 26);
    auto* am = add<Combo> (*this, owned, st, IDs::arpMode, ARPC); am->setBounds (542, 456, 150, 26);
    auto* ar = add<Combo> (*this, owned, st, IDs::arpRate, ARPC); ar->setBounds (698, 456, 90, 26);
    auto* la = add<Toggle> (*this, owned, st, IDs::latch, "LATCH", ARPC); la->setBounds (794, 456, 90, 26); la->setTooltip ("Keep arpeggiating after you let go");
    const K ak[] { { "arp_octaves", "OCTAVES", 890, "Octave range" }, { "arp_gate", "GATE", 960, "Note length" }, { "arp_swing", "SWING", 1030, "Swing" } };
    for (auto& k : ak) { auto* kn = add<Knob> (*this, owned, st, k.id, k.lb, ARPC); kn->setBounds (k.x, 440, 68, 82); kn->slider.setTooltip (k.tip); }
    lane = std::make_unique<StepLane> (proc); addAndMakeVisible (*lane); lane->setBounds (456, 528, 640, 128);

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setColour (juce::ComboBox::textColourId, MAIN);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) { proc.setCurrentProgram (i); refreshPads(); } };
    addAndMakeVisible (presetBox); presetBox.setBounds (412, 18, 270, 28);
    for (auto* b : { &prevB, &nextB }) { b->setColour (juce::TextButton::buttonColourId, MAIN); addAndMakeVisible (*b); }
    prevB.setBounds (378, 18, 28, 28); nextB.setBounds (688, 18, 28, 28);
    prevB.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); refreshPads(); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); refreshPads(); };
    for (auto* b : { &aB, &bB, &copyB }) addAndMakeVisible (*b);
    aB.setBounds (730, 18, 30, 28); bB.setBounds (762, 18, 30, 28); copyB.setBounds (796, 18, 40, 28);
    aB.onClick = [this] { proc.switchAB (0); refreshPads(); };
    bB.onClick = [this] { proc.switchAB (1); refreshPads(); };
    copyB.onClick = [this] { proc.copyAB(); };
    copyB.setColour (juce::TextButton::buttonColourId, C::dim);

    setSize (W, HH);
    refreshPads();
    startTimerHz (30);
}

LumenChordEditor::~LumenChordEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenChordEditor::refreshPads()
{
    for (int i = 0; i < 8; ++i) { pads[i]->name = proc.slotName (i); pads[i]->roman = proc.slotRoman (i); pads[i]->repaint(); }
}

void LumenChordEditor::timerCallback()
{
    if (proc.programVersion.load() != lastProgram) { lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); refreshPads(); }
    // key / scale / type changes rename the pads
    const int h = (int) pv (IDs::key) * 1000 + (int) pv (IDs::scale) * 100 + (int) pv (IDs::type) * 10 + (int) pv (IDs::spread) + (int) pv (IDs::octave) * 7 + (int) pv (IDs::inversion) * 3;
    if (h != lastHash) { lastHash = h; refreshPads(); }
    outPk = juce::jmax (proc.outPeak.load(), outPk * 0.85f);
    aB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 0 ? MAIN : C::dim);
    bB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 1 ? MAIN : C::dim);
    for (auto& p : pads) p->repaint();
    keys->repaint(); lane->repaint();
    repaint (0, 0, W, 72);
}

void LumenChordEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (C::violet.withAlpha (0.0f), 0, 64, C::magenta.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.25, C::violet.withAlpha (0.75f)); line.addColour (0.75, C::magenta.withAlpha (0.75f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (MAIN.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (C::violet, 20, 0, C::magenta, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 146.0f, 18.0f, 220.0f, 28.0f }, "CHORD / ARP", MAIN, 1.85f, juce::Justification::centredLeft, false, 10.5f);
    }
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("OUT", 852, 24, 24, 10, juce::Justification::centredLeft);
    ledMeter (g, { 878.0f, 23.0f, 226.0f, 10.0f }, outPk, outPk, false, 30);

    panel (g, { 16.0f, 72.0f, 1088.0f, 60.0f }, "KEY / MODE", MAIN);
    panel (g, { 16.0f, 140.0f, 1088.0f, 276.0f }, "PROGRESSION", PADC);
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("CHORD LENGTH", 104, 276, 110, 12, juce::Justification::centredLeft);
    g.drawText ("click a pad to hear it, right-click to change it", 230, 296, 300, 14, juce::Justification::centredLeft);
    g.setColour (C::green.withAlpha (0.85f));
    g.drawFittedText (msg, juce::Rectangle<int> (400, 144, 700, 16), juce::Justification::centredRight, 1);
    panel (g, { 16.0f, 424.0f, 424.0f, 244.0f }, "VOICING", VOC);
    panel (g, { 446.0f, 424.0f, 658.0f, 244.0f }, "ARPEGGIATOR", ARPC, pv (IDs::arpOn) > 0.5f);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawFittedText ("Ableton: put Lumen Chord on a MIDI track. On your synth's track set MIDI From -> this track -> Lumen Chord, and turn PREVIEW SOUND off.",
                      juce::Rectangle<int> (30, 630, 400, 32), juce::Justification::topLeft, 2);
}
