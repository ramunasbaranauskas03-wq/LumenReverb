#include "PluginEditor.h"

using namespace lumen::led;

namespace
{
constexpr int W = 1120, HH = 680;
const juce::Colour MAIN = C::red, SUBC = C::orange, CLKC = C::amber, SMPC = C::cyan, MSTC = C::magenta;
const juce::Colour STEMC[4] = { C::red, C::amber, C::cyan, C::green };
template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}
void drawWave (juce::Graphics& g, const juce::AudioBuffer<float>& b, juce::Rectangle<float> r, juce::Colour col, int from = 0, int len = -1)
{
    if (b.getNumSamples() < 2) return;
    if (len < 0) len = b.getNumSamples() - from;
    const int w = std::max (1, (int) r.getWidth());
    juce::Path p; juce::Path q;
    for (int x = 0; x < w; ++x)
    {
        const int s0 = from + (int) ((double) x / w * len), s1 = std::max (s0 + 1, from + (int) ((double) (x + 1) / w * len));
        float mn = 0, mx = 0;
        for (int s = s0; s < std::min (s1, b.getNumSamples()); ++s) { const float v = b.getSample (0, s); mn = std::min (mn, v); mx = std::max (mx, v); }
        const float y0 = r.getCentreY() - mx * r.getHeight() * 0.48f, y1 = r.getCentreY() - mn * r.getHeight() * 0.48f;
        p.addRectangle (r.getX() + (float) x, y0, 1.0f, std::max (1.0f, y1 - y0));
    }
    g.setColour (col.withAlpha (0.75f)); g.fillPath (p);
}
}

//==============================================================================
void KickView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, MAIN);
    const auto& b = proc.getRender();
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const float lenSec = (float) (b.getNumSamples() / fs);
    // waveform
    g.setColour (MAIN.withAlpha (0.12f)); g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);
    drawWave (g, b, r.reduced (4.0f, 10.0f), MAIN.interpolatedWith (C::orange, 0.3f));
    // pitch curve (log 20 Hz .. 2 kHz, right scale)
    const auto p = proc.readParams();
    juce::Path pc;
    for (int x = 0; x < (int) r.getWidth(); x += 2)
    {
        const double t = (double) x / r.getWidth() * lenSec;
        const double f = lumen::kick::KickSynth::subFreq (p, t);
        const float y = r.getBottom() - 8.0f - (float) (std::log10 (f / 20.0) / 2.0) * (r.getHeight() - 16.0f);
        if (x == 0) pc.startNewSubPath ((float) x, y); else pc.lineTo ((float) x, y);
    }
    glowPath (g, pc, C::amber, 1.4f, 0.8f);
    g.setFont (font (8.5f)); g.setColour (C::amber.withAlpha (0.8f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f })
    {
        const float y = r.getBottom() - 8.0f - std::log10 (f / 20.0f) / 2.0f * (r.getHeight() - 16.0f);
        g.setColour (C::amber.withAlpha (0.12f)); g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
        g.setColour (C::amber.withAlpha (0.6f)); g.drawText (f >= 1000.0f ? "1k" : juce::String ((int) f), (int) r.getRight() - 32, (int) y - 11, 28, 10, juce::Justification::centredRight);
    }
    g.setColour (C::dim);
    for (int ms = 100; ms < lenSec * 1000; ms += 100)
    {
        const float x = (float) (ms / 1000.0 / lenSec) * r.getWidth();
        g.setColour (C::dim.withAlpha (0.15f)); g.fillRect (x, r.getY(), 1.0f, r.getHeight());
        g.setColour (C::dim); g.drawText (juce::String (ms) + "ms", (int) x + 2, (int) r.getBottom() - 12, 50, 10, juce::Justification::centredLeft);
    }
    g.setFont (font (9.0f)); g.setColour (C::text.withAlpha (0.7f));
    g.drawText ("WAVEFORM", 8, 6, 80, 12, juce::Justification::centredLeft);
    g.setColour (C::amber); g.drawText ("PITCH", 76, 6, 60, 12, juce::Justification::centredLeft);
    g.setColour (C::dim); g.drawText ("drag me into Ableton", (int) r.getRight() - 150, 6, 142, 12, juce::Justification::centredRight);
}
void KickView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    const auto f = proc.exportKick();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
    else dragging = false;
}

//==============================================================================
void SampleZone::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, SMPC);
    if (hot) { g.setColour (SMPC.withAlpha (0.2f)); g.fillRoundedRectangle (r, 4.0f); }
    auto s = proc.getSample();
    if (s == nullptr)
    {
        g.setFont (font (12.0f)); g.setColour (SMPC.withAlpha (0.8f));
        g.drawText ("DROP YOUR KICK HERE", r, juce::Justification::centred);
        g.setFont (font (9.0f)); g.setColour (C::dim);
        g.drawText ("WAV / AIFF / MP3 / FLAC", r.withTrimmedTop (r.getHeight() * 0.5f + 12.0f).withHeight (14.0f), juce::Justification::centred);
        return;
    }
    drawWave (g, s->buf, r.reduced (4.0f, 14.0f), SMPC);
    const float hp = proc.apvts.getRawParameterValue (IDs::smpHp)->load();
    g.setFont (font (9.5f)); g.setColour (SMPC);
    g.drawText (s->name + "   " + juce::String (s->buf.getNumSamples() / s->rate, 2) + " s", 6, 2, (int) r.getWidth() - 12, 12, juce::Justification::centredLeft);
    if (hp > 21.0f) { g.setColour (C::amber); g.drawText ("TOP ONLY (HP " + juce::String (juce::roundToInt (hp)) + " Hz)", 6, (int) r.getHeight() - 14, 200, 12, juce::Justification::centredLeft); }
}
void SampleZone::filesDropped (const juce::StringArray& files, int, int)
{
    hot = false;
    juce::String err;
    if (! files.isEmpty() && proc.loadSample (juce::File (files[0]), err)) { if (onMessage) onMessage ("loaded " + juce::File (files[0]).getFileName()); }
    else if (onMessage) onMessage (err);
    repaint();
}

//==============================================================================
void LoopView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, MAIN);
    if (hot) { g.setColour (MAIN.withAlpha (0.15f)); g.fillRoundedRectangle (r, 4.0f); }
    const auto& lp = proc.getLoop();
    if (lp.getNumSamples() < 2)
    {
        g.setFont (font (13.0f)); g.setColour (MAIN.withAlpha (0.85f));
        g.drawText ("DROP A DRUM LOOP HERE", r, juce::Justification::centred);
        g.setFont (font (9.5f)); g.setColour (C::dim);
        g.drawText ("it will be split into KICK / SNARE / HATS / PERC", r.withTrimmedTop (r.getHeight() * 0.5f + 12.0f).withHeight (14.0f), juce::Justification::centred);
        return;
    }
    drawWave (g, lp, r.reduced (4.0f, 16.0f), C::text.withAlpha (0.6f));
    if (auto sp = proc.getSplit())
        for (auto& o : sp->onsets)
        {
            const float x = r.getX() + 4.0f + (float) o.sample / (float) lp.getNumSamples() * (r.getWidth() - 8.0f);
            g.setColour (STEMC[o.cls].withAlpha (0.6f)); g.fillRect (x, r.getY() + 4.0f, 1.5f, r.getHeight() - 8.0f);
            for (int c = 0; c < 4; ++c) if ((o.classes >> c) & 1u) drawLed (g, { x + 1.0f, r.getY() + 8.0f + 9.0f * (float) c }, 2.6f, STEMC[c], 1.0f);
        }
    g.setFont (font (9.5f)); g.setColour (C::text.withAlpha (0.8f));
    g.drawText (proc.getLoopName(), 8, 2, 400, 12, juce::Justification::centredLeft);
    if (proc.isSplitting())
    {
        auto bar = r.withSizeKeepingCentre (r.getWidth() * 0.5f, 18.0f);
        g.setColour (juce::Colour (0xe0040608)); g.fillRoundedRectangle (bar.expanded (8.0f), 6.0f);
        const int segs = 30;
        for (int k = 0; k < segs; ++k) drawLed (g, { bar.getX() + 6.0f + k * (bar.getWidth() - 12.0f) / (segs - 1), bar.getCentreY() }, 3.0f, MAIN, proc.splitProgress() * segs > (float) k ? 1.0f : 0.1f);
    }
}
void LoopView::filesDropped (const juce::StringArray& files, int, int)
{
    hot = false;
    juce::String err;
    if (files.isEmpty() || ! proc.startSplit (juce::File (files[0]), err)) { if (onMessage) onMessage (err); }
    else if (onMessage) onMessage ("splitting " + juce::File (files[0]).getFileName() + "...");
    repaint();
}

//==============================================================================
StemRow::StemRow (LumenKickProcessor& p, int c) : proc (p), cls (c)
{
    setTooltip ("Drag this row into Ableton to get the " + juce::String (lumen::split::className (c)) + " stem on its own track");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    playB.setColour (juce::TextButton::buttonColourId, STEMC[c]);
    playB.onClick = [this] { proc.previewStem (proc.previewing() == cls ? -1 : cls); };
    addAndMakeVisible (playB);
    toSampleB.setColour (juce::TextButton::buttonColourId, SMPC);
    toSampleB.setTooltip ("Put the loudest hit of this stem into the SAMPLE layer of the kick designer");
    toSampleB.onClick = [this] { proc.stemToSampleLayer (cls); };
    addAndMakeVisible (toSampleB);
}
void StemRow::resized()
{
    playB.setBounds (getWidth() - 270, getHeight() / 2 - 14, 70, 28);
    toSampleB.setBounds (getWidth() - 190, getHeight() / 2 - 14, 180, 28);
}
void StemRow::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto col = STEMC[cls];
    g.setColour (juce::Colour (0xff070a0f)); g.fillRoundedRectangle (r, 5.0f);
    g.setColour (col.withAlpha (0.35f)); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    drawLed (g, { 16.0f, r.getCentreY() }, 4.0f, col, proc.previewing() == cls ? 1.0f : 0.25f);
    g.setFont (font (12.0f)); g.setColour (col.interpolatedWith (juce::Colours::white, 0.3f));
    g.drawText (lumen::split::className (cls), 30, 0, 140, (int) r.getHeight(), juce::Justification::centredLeft);
    auto wr = juce::Rectangle<float> (170.0f, 6.0f, r.getWidth() - 450.0f, r.getHeight() - 12.0f);
    g.setColour (juce::Colour (0xff030407)); g.fillRoundedRectangle (wr, 3.0f);
    if (auto sp = proc.getSplit())
    {
        drawWave (g, sp->stems[cls], wr.reduced (2.0f, 2.0f), col);
        const double rms = [&] { double s = 0; const auto& b = sp->stems[cls]; for (int i = 0; i < b.getNumSamples(); i += 4) s += (double) b.getSample (0, i) * b.getSample (0, i); return 10 * std::log10 (s / std::max (1, b.getNumSamples() / 4) + 1e-12); }();
        g.setFont (mono (9.0f)); g.setColour (C::dim);
        g.drawText (juce::String (rms, 1) + " dB RMS", (int) wr.getRight() - 90, (int) wr.getY() + 2, 86, 10, juce::Justification::centredRight);
        g.setFont (font (8.5f));
        g.drawText ("DRAG ROW -> ABLETON", (int) wr.getRight() + 6, (int) r.getHeight() - 14, 120, 10, juce::Justification::centredLeft);
    }
    else { g.setFont (font (9.0f)); g.setColour (C::dim); g.drawText ("not split yet", wr, juce::Justification::centred); }
}
void StemRow::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 6) return;
    const auto f = proc.stemFile (cls);
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

//==============================================================================
LumenKickEditor::LumenKickEditor (LumenKickProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    const char* tabs[] { "KICK DESIGNER", "DRUM SPLIT" };
    for (int i = 0; i < 2; ++i) { tabB[i].setButtonText (tabs[i]); tabB[i].onClick = [this, i] { setTab (i); }; addAndMakeVisible (tabB[i]); tabB[i].setBounds (16 + i * 156, 72, 150, 28); }

    // ---------------- DESIGN ----------------
    kickView = std::make_unique<KickView> (proc); addAndMakeVisible (*kickView); kickView->setBounds (26, 136, 620, 214); designComps.push_back (kickView.get());
    for (auto* b : { &playB, &exportB }) { addAndMakeVisible (*b); designComps.push_back (b); }
    playB.setColour (juce::TextButton::buttonColourId, MAIN); playB.setBounds (420, 108, 70, 24); playB.onClick = [this] { proc.audition(); };
    playB.setTooltip ("Play the kick (or play MIDI notes - C3 = the tuned pitch)");
    exportB.setColour (juce::TextButton::buttonColourId, C::dim); exportB.setBounds (496, 108, 150, 24);
    exportB.onClick = [this] { const auto f = proc.exportKick(); showMessage (f.existsAsFile() ? "saved " + f.getFullPathName() : juce::String ("export failed")); };

    sampleZone = std::make_unique<SampleZone> (proc); addAndMakeVisible (*sampleZone); sampleZone->setBounds (674, 136, 420, 112); designComps.push_back (sampleZone.get());
    sampleZone->onMessage = [this] (const juce::String& m) { showMessage (m); };
    for (auto* b : { &loadB, &clearB, &cloneB }) { addAndMakeVisible (*b); designComps.push_back (b); }
    loadB.setColour (juce::TextButton::buttonColourId, SMPC); loadB.setBounds (850, 106, 70, 24);
    loadB.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a kick sample", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) { juce::String err; showMessage (proc.loadSample (fc.getResult(), err) ? "loaded " + fc.getResult().getFileName() : err); }
        });
    };
    clearB.setColour (juce::TextButton::buttonColourId, C::dim); clearB.setBounds (924, 106, 60, 24); clearB.onClick = [this] { proc.clearSample(); };
    cloneB.setColour (juce::TextButton::buttonColourId, C::green); cloneB.setBounds (988, 106, 106, 24);
    cloneB.setTooltip ("KICK CLONE: measures your sample's pitch sweep and envelope, rebuilds its sub with the synth and keeps only the sample's top");
    cloneB.onClick = [this] { showMessage (proc.cloneFromSample()); };

    struct K { const char* id; const char* lb; juce::Colour c; int x, y, w, h; bool bi; const char* tip; };
    const K ks[] {
        { "sample_level", "LEVEL", SMPC, 674, 262, 82, 92, false, "Sample level" },
        { "sample_pitch", "PITCH", SMPC, 758, 262, 82, 92, true, "Sample pitch (semitones)" },
        { "sample_start", "START", SMPC, 842, 262, 82, 92, false, "Skip the start of the sample" },
        { "sample_decay", "LENGTH", SMPC, 926, 262, 82, 92, false, "Shorten the sample (Full = as is)" },
        { "sample_hp", "HIGH-PASS", SMPC, 1010, 262, 82, 92, false, "Use only the top of the sample - the synth provides the sub" },
        { "tune", "TUNE", SUBC, 30, 412, 104, 116, false, "Fundamental of the kick at C3 (with KEY TRACK on, MIDI notes transpose it)" },
        { "pitch_amount", "PITCH", SUBC, 140, 400, 80, 90, false, "How far above the fundamental the sweep starts (semitones)" },
        { "pitch_time", "P.TIME", SUBC, 222, 400, 80, 90, false, "How fast the pitch falls" },
        { "pitch_curve", "P.CURVE", SUBC, 304, 400, 80, 90, false, "Shape of the pitch fall" },
        { "shape", "SHAPE", SUBC, 386, 400, 80, 90, false, "Sine -> rounded square: more harmonics, more knock on small speakers" },
        { "start_phase", "PHASE", SUBC, 468, 400, 80, 90, false, "Start phase: 0 = soft start, 90 = hard click" },
        { "attack", "ATTACK", SUBC, 140, 494, 80, 90, false, "Attack" },
        { "hold", "HOLD", SUBC, 222, 494, 80, 90, false, "Full-level time before the decay" },
        { "decay", "DECAY", SUBC, 304, 494, 80, 90, false, "Decay to -60 dB" },
        { "decay_curve", "D.CURVE", SUBC, 386, 494, 80, 90, false, "Decay shape" },
        { "sub_level", "LEVEL", SUBC, 468, 494, 80, 90, false, "Sub level" },
        { "click_level", "LEVEL", CLKC, 572, 432, 76, 90, false, "Click level" },
        { "click_decay", "DECAY", CLKC, 650, 432, 76, 90, false, "Click length" },
        { "click_tone", "TONE", CLKC, 572, 526, 76, 90, false, "Click colour" },
        { "drive", "DRIVE", MSTC, 760, 410, 84, 96, false, "Saturation (alias-free ADAA)" },
        { "punch", "PUNCH", MSTC, 846, 410, 84, 96, false, "Mid-low punch (160 Hz)" },
        { "low", "LOW", MSTC, 932, 410, 84, 96, true, "Low shelf (70 Hz)" },
        { "high_cut", "HIGH CUT", MSTC, 1018, 410, 84, 96, false, "High cut" },
        { "velocity", "VELOCITY", MSTC, 760, 512, 84, 96, false, "Velocity sensitivity" },
        { "out", "OUTPUT", MSTC, 846, 512, 84, 96, true, "Output" },
    };
    for (auto& k : ks) { auto* kn = add<Knob> (*this, owned, st, k.id, k.lb, k.c, k.bi); kn->setBounds (k.x, k.y, k.w, k.h); kn->slider.setTooltip (k.tip); designComps.push_back (kn); }
    auto addT = [&] (const juce::String& id, const juce::String& t, juce::Colour c, juce::Rectangle<int> b, const juce::String& tip)
    { auto* tg = add<Toggle> (*this, owned, st, id, t, c); tg->setBounds (b); tg->setTooltip (tip); designComps.push_back (tg); };
    addT (IDs::smpOn, "ON", SMPC, { 674, 362, 60, 22 }, "Sample layer on / off");
    addT (IDs::smpTrack, "KEY TRACK", SMPC, { 740, 362, 110, 22 }, "The sample follows the MIDI note");
    addT (IDs::keyTrack, "KEY TRACK", SUBC, { 30, 536, 104, 22 }, "MIDI notes transpose the kick (C3 = TUNE)");
    addT (IDs::clickOn, "ON", CLKC, { 652, 536, 72, 22 }, "Click layer on / off");
    addT (IDs::clip, "CLIP", MSTC, { 932, 548, 84, 22 }, "Soft clipper on the output (loud and safe)");
    auto* ct = add<Combo> (*this, owned, st, IDs::clickType, CLKC); ct->setBounds (572, 404, 154, 24); designComps.push_back (ct);

    // ---------------- SPLIT ----------------
    loopView = std::make_unique<LoopView> (proc); addAndMakeVisible (*loopView); loopView->setBounds (26, 140, 876, 150); splitComps.push_back (loopView.get());
    loopView->onMessage = [this] (const juce::String& m) { showMessage (m); };
    for (auto* b : { &loadLoopB, &splitB, &folderB }) { addAndMakeVisible (*b); splitComps.push_back (b); }
    loadLoopB.setColour (juce::TextButton::buttonColourId, MAIN); loadLoopB.setBounds (26, 108, 110, 26);
    loadLoopB.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a drum loop", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) { juce::String err; showMessage (proc.startSplit (fc.getResult(), err) ? "splitting " + fc.getResult().getFileName() + "..." : err); }
        });
    };
    splitB.setColour (juce::TextButton::buttonColourId, C::green); splitB.setBounds (140, 108, 90, 26);
    splitB.setTooltip ("Split again with the current SENSITIVITY / SEPARATION");
    splitB.onClick = [this]
    {
        if (proc.getLoop().getNumSamples() < 2) { showMessage ("load a drum loop first"); return; }
        // re-split the loaded loop: write it to a temp file and split that
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (proc.getLoopName() + ".wav");
        LumenKickProcessor::writeWav (tmp, proc.getLoop(), proc.getLoopRate());
        juce::String err; showMessage (proc.startSplit (tmp, err) ? juce::String ("splitting...") : err);
    };
    folderB.setColour (juce::TextButton::buttonColourId, C::dim); folderB.setBounds (792, 108, 110, 26);
    folderB.onClick = [this] { auto d = proc.splitFolder(); if (d.isDirectory()) d.revealToUser(); else LumenKickProcessor::userFolder ("Splits").revealToUser(); };
    for (int c = 0; c < lumen::split::kClasses; ++c)
    {
        stems[c] = std::make_unique<StemRow> (proc, c); addAndMakeVisible (*stems[c]);
        stems[c]->setBounds (26, 304 + c * 76, 1068, 68); splitComps.push_back (stems[c].get());
    }
    {
        auto* k1 = add<Knob> (*this, owned, st, IDs::splitSens, "SENSITIVITY", MAIN); k1->setBounds (912, 140, 90, 104);
        k1->slider.setTooltip ("More = also finds quiet ghost notes and fast hats");
        auto* k2 = add<Knob> (*this, owned, st, IDs::splitSep, "SEPARATION", MAIN); k2->setBounds (1004, 140, 90, 104);
        k2->slider.setTooltip ("More = cleaner stems with harder edges, less = smoother stems with a bit more bleed");
        splitComps.push_back (k1); splitComps.push_back (k2);
    }

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setColour (juce::ComboBox::textColourId, MAIN);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (412, 18, 270, 28);
    for (auto* b : { &prevB, &nextB }) { b->setColour (juce::TextButton::buttonColourId, MAIN); addAndMakeVisible (*b); }
    prevB.setBounds (378, 18, 28, 28); nextB.setBounds (688, 18, 28, 28);
    prevB.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    for (auto* b : { &aB, &bB, &copyB }) addAndMakeVisible (*b);
    aB.setBounds (730, 18, 30, 28); bB.setBounds (762, 18, 30, 28); copyB.setBounds (796, 18, 40, 28);
    aB.onClick = [this] { proc.switchAB (0); };
    bB.onClick = [this] { proc.switchAB (1); };
    copyB.onClick = [this] { proc.copyAB(); };
    copyB.setColour (juce::TextButton::buttonColourId, C::dim);

    setSize (W, HH);
    setTab (0);
    startTimerHz (30);
}

LumenKickEditor::~LumenKickEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenKickEditor::setTab (int t)
{
    tab = t;
    for (auto* c : designComps) c->setVisible (t == 0);
    for (auto* c : splitComps) c->setVisible (t == 1);
    for (int i = 0; i < 2; ++i) tabB[i].setColour (juce::TextButton::buttonColourId, i == t ? MAIN : C::dim);
    repaint();
}

void LumenKickEditor::timerCallback()
{
    proc.finishSplitIfReady();
    if (proc.programVersion.load() != lastProgram)
    {
        lastProgram = proc.programVersion.load();
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
        if (! proc.isSplitting() && proc.getSplit() != nullptr && tab == 1) msg = proc.getSplitMessage();
    }
    outPk = juce::jmax (proc.outPeak.load(), outPk * 0.85f);
    aB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 0 ? MAIN : C::dim);
    bB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 1 ? MAIN : C::dim);
    for (int c = 0; c < lumen::split::kClasses; ++c)
    {
        stems[c]->playB.setButtonText (proc.previewing() == c ? "STOP" : "PLAY");
        stems[c]->playB.setEnabled (proc.getSplit() != nullptr);
        stems[c]->toSampleB.setEnabled (proc.getSplit() != nullptr);
    }
    splitB.setEnabled (! proc.isSplitting());
    repaint();
}

void LumenKickEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (C::red.withAlpha (0.0f), 0, 64, C::amber.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.25, C::red.withAlpha (0.75f)); line.addColour (0.75, C::amber.withAlpha (0.75f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (MAIN.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (C::red, 20, 0, C::amber, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 146.0f, 18.0f, 220.0f, 28.0f }, "KICK DESIGNER", MAIN, 1.85f, juce::Justification::centredLeft, false, 10.5f);
    }
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("OUT", 852, 24, 24, 10, juce::Justification::centredLeft);
    ledMeter (g, { 878.0f, 23.0f, 226.0f, 10.0f }, outPk, outPk, false, 30);
    // message line
    g.setFont (font (9.5f)); g.setColour (C::green.withAlpha (0.85f));
    g.drawFittedText (msg, juce::Rectangle<int> (330, 74, 774, 22), juce::Justification::centredRight, 1);

    if (tab == 0)
    {
        panel (g, { 16.0f, 104.0f, 640.0f, 256.0f }, "KICK", MAIN);
        {
            const auto p = proc.readParams();
            noteDisp.draw (g, { 120.0f, 106.0f, 280.0f, 24.0f }, noteName (p.tuneHz).toUpperCase() + "  " + juce::String (p.tuneHz, 1) + "HZ", C::amber, 1.6f, juce::Justification::centredLeft, false, 9.0f);
        }
        panel (g, { 664.0f, 104.0f, 440.0f, 284.0f }, "SAMPLE", SMPC, pv (IDs::smpOn) > 0.5f);
        panel (g, { 16.0f, 368.0f, 540.0f, 300.0f }, "SUB", SUBC);
        panel (g, { 564.0f, 396.0f, 172.0f, 272.0f }, "CLICK", CLKC, pv (IDs::clickOn) > 0.5f);
        panel (g, { 744.0f, 396.0f, 360.0f, 272.0f }, "MASTER", MSTC);
        g.setFont (font (9.0f)); g.setColour (C::dim);
        g.drawFittedText ("MIDI: C3 plays the kick at TUNE; with KEY TRACK other notes transpose it.", juce::Rectangle<int> (30, 600, 512, 30), juce::Justification::topLeft, 2);
        g.drawFittedText ("Drop a kick into SAMPLE, press KICK CLONE: the synth rebuilds its sub, the sample keeps the top.", juce::Rectangle<int> (30, 626, 512, 30), juce::Justification::topLeft, 2);
        g.drawFittedText ("Drag the kick view into Ableton to get the WAV.", juce::Rectangle<int> (760, 620, 330, 30), juce::Justification::topLeft, 2);
    }
    else
    {
        panel (g, { 16.0f, 104.0f, 1088.0f, 192.0f }, "DRUM LOOP", MAIN);
        g.setFont (font (9.0f)); g.setColour (C::dim);
        const float sens = pv (IDs::splitSens), sep = pv (IDs::splitSep);
        g.drawText ("drop a loop or press LOAD LOOP - the split runs in the background", 240, 112, 540, 18, juce::Justification::centredLeft);
        (void) sens; (void) sep;
        g.setFont (font (9.0f)); g.setColour (C::dim);
        g.drawFittedText ("Stems are saved to Documents / Lumen Kick Designer / Splits. Drag a row into Ableton: each stem lands on its own track. "
                          "Kick + Snare + Hats + Perc together = exactly the original loop.", juce::Rectangle<int> (26, 614, 1068, 30), juce::Justification::centred, 2);
    }
}
