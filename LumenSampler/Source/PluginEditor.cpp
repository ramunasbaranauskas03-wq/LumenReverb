#include "PluginEditor.h"
using namespace lumen::led;

static constexpr int W = 1180, HH = 912;
static const juce::Rectangle<int> kWave { 12, 58, W - 24, 210 };

// ------------------------------------------------------------------------------------------------ WaveView
static const char* kMarkerIds[] = { "start", "end", "loop_start", "loop_end" };

float WaveView::xOf (double n) const { auto r = getLocalBounds().toFloat().reduced (2, 0); return (float) (r.getX() + (n - v0) / (v1 - v0) * r.getWidth()); }
double WaveView::normOf (float x) const { auto r = getLocalBounds().toFloat().reduced (2, 0); return juce::jlimit (0.0, 1.0, v0 + (x - r.getX()) / r.getWidth() * (v1 - v0)); }

int WaveView::markerAt (float x) const
{
    if (proc.source() == nullptr) return -1;
    const bool loopOn = proc.apvts.getRawParameterValue (IDs::loop)->load() > 0.5f;
    int best = -1; float bd = 8.0f;
    for (int m = 0; m < 4; ++m)
    {
        if (m >= 2 && ! loopOn) continue;
        const float d = std::abs (xOf (proc.apvts.getRawParameterValue (kMarkerIds[m])->load()) - x);
        if (d < bd) { bd = d; best = m; }
    }
    return best;
}

void WaveView::rebuild()
{
    auto s = proc.source();
    const int w = juce::jmax (1, getWidth() - 4);
    for (int c = 0; c < 2; ++c) { mn[c].assign ((size_t) w, 0.0f); mx[c].assign ((size_t) w, 0.0f); }
    builtW = getWidth(); builtVersion = proc.sampleVersion.load(); dirty = false;
    if (s == nullptr) return;
    for (int c = 0; c < 2; ++c)
    {
        const float* d = s->data (0, c);
        for (int x = 0; x < w; ++x)
        {
            const int a = (int) ((v0 + (v1 - v0) * x / w) * s->length), b = juce::jmax (a + 1, (int) ((v0 + (v1 - v0) * (x + 1) / w) * s->length));
            float lo = 1e9f, hi = -1e9f;
            const int step = juce::jmax (1, (b - a) / 2048);
            for (int i = juce::jmax (0, a); i < juce::jmin (s->length, b); i += step) { lo = juce::jmin (lo, d[i]); hi = juce::jmax (hi, d[i]); }
            if (lo > hi) lo = hi = 0;
            mn[c][(size_t) x] = lo; mx[c][(size_t) x] = hi;
        }
    }
}

void WaveView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, C::cyan);
    auto s = proc.source();
    if (dirty || builtW != getWidth() || builtVersion != proc.sampleVersion.load()) rebuild();
    if (s == nullptr)
    {
        g.setColour (hover ? C::cyan : C::dim); g.setFont (font (17.0f));
        g.drawText ("DROP A SAMPLE HERE  (WAV / AIFF / FLAC / OGG / MP3)", r, juce::Justification::centred);
        g.setFont (font (11.5f, false)); g.setColour (C::dim);
        g.drawText ("played back bit-identical at the root note - no colouring, no resampling, no dither", r.withTrimmedTop (60), juce::Justification::centred);
        return;
    }
    auto pv = [this] (const char* id) { return (double) proc.apvts.getRawParameterValue (id)->load(); };
    const double st = pv ("start"), en = pv ("end"), ls = pv ("loop_start"), le = pv ("loop_end");
    const bool loopOn = pv ("loop") > 0.5, slice = (int) pv ("mode") == 2;
    // inactive area shading
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), r.getY(), juce::jmax (r.getX(), xOf (st)), r.getBottom()));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (r.getRight(), xOf (en)), r.getY(), r.getRight(), r.getBottom()));
    if (loopOn && ! slice)
    {
        g.setColour (C::green.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (ls), r.getY(), xOf (le), r.getBottom()));
    }
    // waveform, two lanes for stereo
    const int lanes = s->channels > 1 ? 2 : 1;
    const float laneH = (r.getHeight() - 20.0f) / lanes;
    for (int c = 0; c < lanes; ++c)
    {
        const float cy = r.getY() + 10.0f + laneH * (c + 0.5f), amp = laneH * 0.46f;
        g.setColour (C::border.withAlpha (0.6f)); g.drawHorizontalLine ((int) cy, r.getX(), r.getRight());
        juce::Path p;
        const int w = (int) mx[c].size();
        for (int x = 0; x < w; ++x) { const float X = 2.0f + (float) x; p.addRectangle (X, cy - mx[c][(size_t) x] * amp, 1.0f, juce::jmax (1.0f, (mx[c][(size_t) x] - mn[c][(size_t) x]) * amp)); }
        g.setColour (C::cyan.withAlpha (0.85f)); g.fillPath (p);
    }
    // slices
    if (slice)
    {
        const auto sl = proc.getSlices();
        g.setFont (mono (10.0f));
        for (size_t i = 0; i < sl.size(); ++i)
        {
            const float x = xOf ((double) sl[i] / s->length);
            if (x < r.getX() - 1 || x > r.getRight()) continue;
            g.setColour (C::amber.withAlpha (0.7f)); g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            g.setColour (C::amber); g.fillRect (x, r.getY(), 18.0f, 12.0f);
            g.setColour (C::field); g.drawText (juce::String ((int) i + 1), juce::Rectangle<float> (x, r.getY(), 18.0f, 12.0f), juce::Justification::centred);
        }
    }
    // markers
    auto marker = [&] (double n, juce::Colour col, const juce::String& t, bool top, int idx)
    {
        const float x = xOf (n);
        if (x < r.getX() - 2 || x > r.getRight() + 2) return;
        const bool hot = idx == hoverMarker || idx == dragMarker;
        g.setColour (col.withAlpha (hot ? 1.0f : 0.8f)); g.fillRect (x - (hot ? 1.0f : 0.5f), r.getY(), hot ? 2.0f : 1.0f, r.getHeight());
        auto tag = juce::Rectangle<float> (x - (idx % 2 == 0 ? 0.0f : 22.0f), top ? r.getY() : r.getBottom() - 13.0f, 22.0f, 13.0f);
        g.fillRect (tag); g.setColour (C::field); g.setFont (font (9.5f)); g.drawText (t, tag, juce::Justification::centred);
    };
    marker (st, C::cyan, "S", false, 0); marker (en, C::cyan, "E", false, 1);
    if (loopOn && ! slice) { marker (ls, C::green, "LS", true, 2); marker (le, C::green, "LE", true, 3); }
    // playheads
    const int k = proc.playCount.load();
    for (int i = 0; i < k; ++i)
    {
        const float x = xOf (proc.playPos[(size_t) i].load() / s->length);
        g.setColour (juce::Colours::white.withAlpha (0.85f)); g.drawVerticalLine ((int) x, r.getY() + 1, r.getBottom() - 1);
    }
    if (v1 - v0 < 0.999)
    {
        g.setColour (C::dim); g.setFont (mono (10.0f));
        g.drawText ("zoom x" + juce::String (1.0 / (v1 - v0), 1), r.reduced (6, 2), juce::Justification::topRight);
    }
}

void WaveView::mouseMove (const juce::MouseEvent& e)
{
    const int m = markerAt (e.position.x);
    if (m != hoverMarker) { hoverMarker = m; repaint(); }
    setMouseCursor (m >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    auto s = proc.source();
    if (s == nullptr) { ed.loadFile ({}); return; }
    dragMarker = markerAt (e.position.x);
    if (dragMarker >= 0) { if (auto* p = proc.apvts.getParameter (kMarkerIds[dragMarker])) p->beginChangeGesture(); return; }
    // click inside the wave = audition (slice under the mouse in slice mode, root note otherwise)
    int note = (int) proc.apvts.getRawParameterValue (IDs::root)->load();
    if ((int) proc.apvts.getRawParameterValue (IDs::mode)->load() == 2)
    {
        const auto sl = proc.getSlices(); const double pos = normOf (e.position.x) * s->length;
        int idx = 0; for (size_t i = 0; i < sl.size(); ++i) if (sl[i] <= pos) idx = (int) i;
        note = 36 + idx;
    }
    auditionNote = juce::jlimit (0, 127, note);
    proc.keyboardState.noteOn (1, auditionNote, 0.9f);
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    auto s = proc.source();
    if (dragMarker < 0 || s == nullptr) return;
    double n = normOf (e.position.x);
    if (! e.mods.isShiftDown())
    {
        const int zc = lumen::sampler::nearestZeroCrossing (*s, (int) std::lround (n * s->length), juce::jmax (64, (int) (s->rate * 0.004)));
        n = (double) zc / s->length;
    }
    auto pv = [this] (const char* id) { return (double) proc.apvts.getRawParameterValue (id)->load(); };
    const double minGap = 64.0 / s->length;
    if (dragMarker == 0) n = juce::jmin (n, pv ("end") - minGap);
    if (dragMarker == 1) n = juce::jmax (n, pv ("start") + minGap);
    if (dragMarker == 2) n = juce::jmin (n, pv ("loop_end") - minGap);
    if (dragMarker == 3) n = juce::jmax (n, pv ("loop_start") + minGap);
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (kMarkerIds[dragMarker]))) p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0.0, 1.0, n)));
    repaint();
}

void WaveView::mouseUp (const juce::MouseEvent&)
{
    if (dragMarker >= 0)
    {
        if (auto* p = proc.apvts.getParameter (kMarkerIds[dragMarker])) p->endChangeGesture();
        if (dragMarker < 2 && (int) proc.apvts.getRawParameterValue (IDs::sliceMode)->load() == 1) proc.rebuildSlices();
    }
    dragMarker = -1;
    if (auditionNote >= 0) { proc.keyboardState.noteOff (1, auditionNote, 0.0f); auditionNote = -1; }
}

void WaveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const double c = normOf (e.position.x), span = v1 - v0;
    const double ns = juce::jlimit (0.0005, 1.0, span * (w.deltaY > 0 ? 0.8 : 1.25));
    const double t = (c - v0) / span;
    v0 = juce::jlimit (0.0, 1.0 - ns, c - t * ns); v1 = v0 + ns;
    dirty = true; repaint();
}

void WaveView::filesDropped (const juce::StringArray& files, int, int) { hover = false; if (! files.isEmpty()) ed.loadFile (juce::File (files[0])); }

// ------------------------------------------------------------------------------------------------ SliceDrag
void SliceDrag::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1);
    const bool ok = proc.source() != nullptr && (wav || ! proc.getSlices().empty());
    g.setColour (isMouseOver() ? juce::Colour (0xff272c33) : juce::Colour (0xff20242a)); g.fillRoundedRectangle (r, 4);
    g.setColour (ok ? (wav ? C::cyan : C::amber).withAlpha (0.7f) : C::border); g.drawRoundedRectangle (r, 4, 1);
    g.setColour (ok ? C::text : C::dim); g.setFont (font (11.0f));
    g.drawText (wav ? "DRAG WAV (EDIT)" : "DRAG SLICE MIDI", r, juce::Justification::centred);
}
void SliceDrag::mouseDrag (const juce::MouseEvent& e)
{
    if (dragged || e.getDistanceFromDragStart() < 6) return;
    dragged = true;
    auto f = make();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragged = false; });
    else dragged = false;
}
void SliceDrag::mouseUp (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() >= 6) return;
    auto f = make();
    ed.showMessage (f.existsAsFile() ? "Saved " + f.getFullPathName() : juce::String ("Load a sample first"));
}

// ------------------------------------------------------------------------------------------------ Editor
LumenSamplerEditor::Knob& LumenSamplerEditor::knob (const juce::String& id, const juce::String& lbl, juce::Colour c, int x, int y, bool bip, int w)
{
    knobs.push_back (std::make_unique<Knob> (proc.apvts, id, lbl, c, bip));
    addAndMakeVisible (*knobs.back()); knobs.back()->setBounds (x, y, w, 70);
    return *knobs.back();
}
LumenSamplerEditor::Toggle& LumenSamplerEditor::toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r)
{
    toggles.push_back (std::make_unique<Toggle> (proc.apvts, id, txt, c));
    addAndMakeVisible (*toggles.back()); toggles.back()->setBounds (r);
    return *toggles.back();
}
LumenSamplerEditor::Combo& LumenSamplerEditor::combo (const juce::String& id, juce::Colour c, juce::Rectangle<int> r)
{
    combos.push_back (std::make_unique<Combo> (proc.apvts, id, c));
    addAndMakeVisible (*combos.back()); combos.back()->setBounds (r);
    return *combos.back();
}

LumenSamplerEditor::LumenSamplerEditor (LumenSamplerProcessor& p)
    : AudioProcessorEditor (&p), proc (p), wave (p, *this), sliceDrag (p, *this, false), wavDrag (p, *this, true),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (wave); wave.setBounds (kWave);

    // header
    const char* tabNames[] = { "CLASSIC", "ONE-SHOT", "SLICE" };
    for (int i = 0; i < 3; ++i)
    {
        auto& t = modeTabs[i];
        t.setButtonText (tabNames[i]); t.getProperties().set ("tab", true); t.setClickingTogglesState (false);
        t.setColour (juce::TextButton::buttonColourId, C::cyan);
        t.onClick = [this, i] { proc.setParam (IDs::mode, (float) i); };
        addAndMakeVisible (t); t.setBounds (300 + i * 92, 14, 88, 28);
    }
    for (auto* b : { &loadBtn, &rootBtn, &abA, &abB, &abCopy }) { b->setColour (juce::TextButton::buttonColourId, C::cyan); addAndMakeVisible (*b); }
    loadBtn.setBounds (588, 14, 70, 28); rootBtn.setBounds (664, 14, 92, 28);
    abA.setBounds (W - 250, 14, 32, 28); abB.setBounds (W - 214, 14, 32, 28); abCopy.setBounds (W - 178, 14, 52, 28);
    abA.setClickingTogglesState (false); abB.setClickingTogglesState (false);
    loadBtn.onClick = [this] { loadFile ({}); };
    rootBtn.setTooltip ("Set the root note to the detected pitch of the sample");
    rootBtn.onClick = [this] { if (proc.detectedRoot() > 0) { proc.applyDetectedRoot(); showMessage ("Root set to " + juce::MidiMessage::getMidiNoteName ((int) std::lround (proc.detectedRoot()), true, true, 3)); } else showMessage ("No clear pitch in this sample - set the root by hand"); };
    abA.onClick = [this] { proc.switchAB (0); };
    abB.onClick = [this] { proc.switchAB (1); };
    abCopy.onClick = [this] { proc.copyAB(); showMessage (juce::String ("Copied ") + (proc.getABSlot() == 0 ? "A to B" : "B to A")); };

    // ---- row 1 (y 318)
    const int y1 = 278, ky = y1 + 30;
    // SAMPLE
    boxes.push_back ({ { 12, (float) y1, 456, 206 }, "SAMPLE", C::cyan, {} });
    knob (IDs::root, "ROOT", C::cyan, 22, ky); knob (IDs::tune, "TUNE", C::cyan, 84, ky, true); knob (IDs::fine, "FINE", C::cyan, 146, ky, true);
    knob (IDs::sampleBpm, "SAMPLE BPM", C::violet, 208, ky, false, 66);
    combo (IDs::warp, C::violet, { 282, ky + 6, 120, 24 });
    toggle (IDs::reverse, "REVERSE", C::orange, { 282, ky + 38, 120, 24 });
    toggle (IDs::loop, "LOOP", C::green, { 22, ky + 84, 84, 26 });
    combo (IDs::loopMode, C::green, { 112, ky + 85, 104, 24 });
    knob (IDs::xfade, "X-FADE", C::green, 224, ky + 74);

    // AMP
    boxes.push_back ({ { 476, (float) y1, 330, 206 }, "AMP", C::cyan, {} });
    knob (IDs::attack, "ATTACK", C::cyan, 488, ky); knob (IDs::decay, "DECAY", C::cyan, 550, ky); knob (IDs::sustain, "SUSTAIN", C::cyan, 612, ky); knob (IDs::release, "RELEASE", C::cyan, 674, ky);
    knob (IDs::velSens, "VEL > VOL", C::cyan, 736, ky);
    // FILTER
    boxes.push_back ({ { 814, (float) y1, 354, 206 }, "FILTER", C::amber, IDs::fOn });
    toggles.push_back (std::make_unique<Toggle> (proc.apvts, IDs::fOn, "", C::amber, true)); addAndMakeVisible (*toggles.back()); toggles.back()->setBounds (1140, y1 + 3, 22, 22);
    combo (IDs::fType, C::amber, { 826, ky + 6, 90, 24 });
    knob (IDs::cutoff, "CUTOFF", C::amber, 922, ky); knob (IDs::res, "RESO", C::amber, 984, ky); knob (IDs::fEnv, "ENV AMT", C::amber, 1046, ky, true); knob (IDs::keyTrack, "KEY", C::amber, 1106, ky);
    knob (IDs::fA, "F.ATK", C::amber, 826, ky + 84); knob (IDs::fD, "F.DEC", C::amber, 888, ky + 84); knob (IDs::fS, "F.SUS", C::amber, 950, ky + 84); knob (IDs::fR, "F.REL", C::amber, 1012, ky + 84);
    knob (IDs::velCut, "VEL > CUT", C::amber, 1074, ky + 84, true);
    // AMP second row: output
    knob (IDs::volume, "VOLUME", C::cyan, 488, ky + 84); knob (IDs::pan, "PAN", C::cyan, 550, ky + 84, true); knob (IDs::width, "WIDTH", C::cyan, 612, ky + 84);
    knob (IDs::bend, "BEND", C::cyan, 674, ky + 84); knob (IDs::glide, "GLIDE", C::cyan, 736, ky + 84);

    // ---- row 2 (y 532)
    const int y2 = 492, ky2 = y2 + 30;
    for (int l = 0; l < 2; ++l)
    {
        const int x = 12 + l * 296;
        boxes.push_back ({ { (float) x, (float) y2, 288, 162 }, "LFO " + juce::String (l + 1), l == 0 ? C::violet : C::magenta, {} });
        const auto col = l == 0 ? C::violet : C::magenta;
        combo (IDs::lfo (l, "shape"), col, { x + 10, ky2 + 4, 82, 24 });
        toggle (IDs::lfo (l, "sync"), "SYNC", col, { x + 10, ky2 + 34, 82, 24 });
        combo (IDs::lfo (l, "div"), col, { x + 10, ky2 + 64, 82, 24 });
        knob (IDs::lfo (l, "rate"), "RATE", col, x + 98, ky2, false, 46);
        knob (IDs::lfo (l, "pitch"), "PITCH", col, x + 144, ky2, true, 46);
        knob (IDs::lfo (l, "filter"), "FILTER", col, x + 190, ky2, true, 46);
        knob (IDs::lfo (l, "vol"), "VOL", col, x + 236, ky2, false, 46);
    }
    // VOICE
    boxes.push_back ({ { 604, (float) y2, 220, 162 }, "VOICE", C::cyan, {} });
    knob (IDs::poly, "VOICES", C::cyan, 616, ky2);
    toggle (IDs::legato, "MONO / LEGATO", C::cyan, { 684, ky2 + 10, 128, 26 });
    // SLICE
    boxes.push_back ({ { 832, (float) y2, 336, 162 }, "SLICE", C::amber, {} });
    combo (IDs::sliceMode, C::amber, { 844, ky2 + 6, 110, 24 });
    knob (IDs::sliceSens, "SENS", C::amber, 962, ky2);
    combo (IDs::sliceGrid, C::amber, { 1030, ky2 + 6, 70, 24 });
    addAndMakeVisible (sliceDrag); sliceDrag.setBounds (844, ky2 + 82, 312, 34);

    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, C::cyan.withAlpha (0.7f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, C::cyan.withAlpha (0.25f));
    toggle (IDs::choke, "CHOKE", C::amber, { 1030, ky2 + 40, 70, 24 });
    addAndMakeVisible (wavDrag); wavDrag.setBounds (616, ky2 + 82, 196, 34);

    // ---- row 3 (y 662): the extras
    const int y3 = 662, ky3 = y3 + 30;
    boxes.push_back ({ { 12, (float) y3, 142, 152 }, "PITCH ENV", C::orange, {} });
    knob (IDs::pEnvAmt, "AMOUNT", C::orange, 22, ky3, true); knob (IDs::pEnvDecay, "DECAY", C::orange, 84, ky3);
    boxes.push_back ({ { 162, (float) y3, 206, 152 }, "UNISON", C::violet, {} });
    knob (IDs::uniVoices, "VOICES", C::violet, 172, ky3); knob (IDs::uniDetune, "DETUNE", C::violet, 234, ky3); knob (IDs::uniSpread, "SPREAD", C::violet, 296, ky3);
    boxes.push_back ({ { 376, (float) y3, 166, 152 }, "CHORD", C::green, {} });
    combo (IDs::chord, C::green, { 388, ky3 + 6, 142, 24 });
    boxes.push_back ({ { 550, (float) y3, 206, 152 }, "HUMANIZE", C::magenta, {} });
    knob (IDs::humStart, "START", C::magenta, 560, ky3); knob (IDs::humPitch, "PITCH", C::magenta, 622, ky3); knob (IDs::humVol, "VOLUME", C::magenta, 684, ky3);
    boxes.push_back ({ { 764, (float) y3, 142, 152 }, "FADES", C::cyan, {} });
    knob (IDs::fadeIn, "FADE IN", C::cyan, 774, ky3); knob (IDs::fadeOut, "FADE OUT", C::cyan, 836, ky3);
    boxes.push_back ({ { 914, (float) y3, 254, 152 }, "COLOR", C::red, {} });
    knob (IDs::drive, "DRIVE", C::red, 924, ky3);
    combo (IDs::vintage, C::red, { 992, ky3 + 6, 164, 24 });
    toggle (IDs::normalize, "NORMALIZE", C::red, { 992, ky3 + 40, 164, 24 });

    keyboard.setLowestVisibleKey (24); keyboard.setKeyWidth (17.0f);
    addAndMakeVisible (keyboard); keyboard.setBounds (12, 822, W - 24, 80);

    setSize (W, HH);
    startTimerHz (30);
}

LumenSamplerEditor::~LumenSamplerEditor() { setLookAndFeel (nullptr); }

void LumenSamplerEditor::loadFile (const juce::File& f)
{
    if (f == juce::File())
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        { if (fc.getResult().existsAsFile()) loadFile (fc.getResult()); });
        return;
    }
    juce::String err;
    if (proc.loadFile (f, err))
    {
        juce::String m = "Loaded " + f.getFileName();
        if (proc.detectedRoot() > 0) m << "  -  root " << juce::MidiMessage::getMidiNoteName ((int) std::lround (proc.detectedRoot()), true, true, 3);
        if (proc.detectedBpm() > 0) m << "  -  " << juce::String (proc.detectedBpm(), 1) << " bpm";
        showMessage (m);
    }
    else showMessage (err);
    wave.refresh();
}

void LumenSamplerEditor::timerCallback()
{
    const float pk = proc.outPeak.load();
    meter = juce::jmax (pk, meter * 0.86f); hold = juce::jmax (pk, hold - 0.01f);
    const int mode = (int) pv (IDs::mode);
    if (mode != lastMode) { lastMode = mode; for (int i = 0; i < 3; ++i) modeTabs[i].setToggleState (i == mode, juce::dontSendNotification); }
    abA.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification); abB.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    if (proc.sampleVersion.load() != lastVersion) { lastVersion = proc.sampleVersion.load(); wave.refresh(); }
    wave.repaint();
    if (msg.isNotEmpty() && juce::Time::getMillisecondCounter() - msgTime > 6000) msg.clear();
    repaint (0, 0, W, 56);
    repaint (boxes[0].r.toNearestInt());
}

void LumenSamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    // header
    g.setColour (C::panelTop); g.fillRect (0, 0, W, 54);
    g.setColour (C::border); g.drawHorizontalLine (54, 0, (float) W);
    g.setColour (C::text); g.setFont (font (22.0f)); g.drawText ("LUMEN", 16, 12, 90, 30, juce::Justification::centredLeft);
    g.setColour (C::cyan); g.drawText ("SAMPLER", 98, 12, 140, 30, juce::Justification::centredLeft);
    g.setColour (C::dim); g.setFont (font (9.5f, false)); g.drawText ("BIT-EXACT ENGINE", 200, 22, 100, 14, juce::Justification::centredLeft);
    // info line
    auto info = juce::Rectangle<float> (764, 14, (float) (W - 250 - 772), 28);
    lcd (g, info, C::cyan);
    g.setFont (mono (10.5f));
    if (msg.isNotEmpty()) { g.setColour (C::amber); g.drawFittedText (msg, info.reduced (6, 0).toNearestInt(), juce::Justification::centredLeft, 2, 0.8f); }
    else { g.setColour (C::text.withAlpha (0.85f)); g.drawFittedText (proc.sampleInfo(), info.reduced (6, 0).toNearestInt(), juce::Justification::centredLeft, 2, 0.8f); }
    ledMeter (g, { (float) W - 116, 16, 100, 8 }, meter, hold, false, 0);
    ledMeter (g, { (float) W - 116, 30, 100, 8 }, meter, hold, false, 0);
    g.setColour (C::dim); g.setFont (font (9.0f, false)); g.drawText ("OUT", W - 150, 16, 30, 22, juce::Justification::centredLeft);

    for (auto& b : boxes) panel (g, b.r, b.title, b.col, b.onId.isEmpty() || pv (b.onId) > 0.5f);

    // SAMPLE panel read-outs
    auto& sb = boxes[0].r;
    auto ro = juce::Rectangle<float> (sb.getX() + 300, sb.getY() + 110, 146, 84);
    lcd (g, ro, C::cyan);
    g.setFont (mono (10.5f));
    auto line = [&] (int i, const juce::String& k, const juce::String& v, juce::Colour c)
    {
        auto rr = ro.reduced (8, 6).withHeight (18).translated (0, i * 18.0f);
        g.setColour (C::dim); g.drawText (k, rr, juce::Justification::centredLeft);
        g.setColour (c); g.drawText (v, rr, juce::Justification::centredRight);
    };
    const int root = (int) pv (IDs::root);
    line (0, "ROOT", juce::MidiMessage::getMidiNoteName (root, true, true, 3), C::cyan);
    juce::String det = "--";
    if (proc.detectedRoot() > 0)
    {
        const int n = (int) std::lround (proc.detectedRoot()); const int ct = (int) std::lround ((proc.detectedRoot() - n) * 100);
        det = juce::MidiMessage::getMidiNoteName (n, true, true, 3) + (ct >= 0 ? " +" : " ") + juce::String (ct) + "c";
    }
    line (1, "DETECTED", det, C::text);
    line (2, "TEMPO", proc.detectedBpm() > 0 ? juce::String (proc.detectedBpm(), 1) + " bpm" : juce::String ("--"), C::violet);
    auto q = proc.readParams();
    const bool exact = q.tune == 0 && q.fine == 0 && q.warp == 0 && ! q.fOn && q.volume == 1.0f && q.pan == 0 && q.width == 1.0f && q.pEnvAmt == 0 && q.uni == 1 && q.chordN == 1
                    && q.humStart == 0 && q.humPitch == 0 && q.humVol == 0 && q.fadeIn == 0 && q.fadeOut == 0 && pv (IDs::drive) < 1e-5f && (int) pv (IDs::vintage) == 0 && ! (pv (IDs::normalize) > 0.5f)
                    && q.A == 0 && q.S == 1.0f && q.velSens == 0 && q.lfo[0].vol == 0 && q.lfo[1].vol == 0 && q.lfo[0].pitch == 0 && q.lfo[1].pitch == 0;
    line (3, "AT ROOT", exact ? "BIT-EXACT" : "processed", exact ? C::green : C::amber);

    // hints
    g.setColour (C::dim); g.setFont (font (10.0f, false));
    g.drawFittedText ("Voices = 1 for mono. Glide in mono/legato. Sustain pedal + bend.", juce::Rectangle<int> (684, 562, 132, 40), juce::Justification::topLeft, 3);
    g.drawFittedText ("Slice 1 = C1, then up chromatically. Click the wave to audition.", juce::Rectangle<int> (1106, 522, 56, 76), juce::Justification::topLeft, 6, 0.8f);
    // chord read-out
    {
        const auto iv = chordIntervals ((int) pv (IDs::chord));
        juce::String t; const int root = (int) pv (IDs::root);
        for (int x : iv) t << juce::MidiMessage::getMidiNoteName (root + x, true, false, 3) << "  ";
        auto cr = juce::Rectangle<float> (388, 662 + 70, 142, 70);
        lcd (g, cr, C::green);
        g.setColour (iv.size() > 1 ? C::green : C::dim); g.setFont (mono (11.0f));
        g.drawFittedText (iv.size() > 1 ? t.trim() : juce::String ("one note per key"), cr.reduced (6).toNearestInt(), juce::Justification::centred, 2);
        g.setColour (C::dim); g.setFont (font (9.0f, false));
        g.drawText ("on root key", cr.withTrimmedTop (52), juce::Justification::centred);
    }
    // normalize read-out
    if (proc.source() != nullptr)
    {
        g.setColour (C::dim); g.setFont (mono (10.0f));
        g.drawText ("norm gain " + juce::String (proc.normalizeGainDb(), 1) + " dB", juce::Rectangle<float> (992, 662 + 104, 164, 16), juce::Justification::centredLeft);
    }
}
