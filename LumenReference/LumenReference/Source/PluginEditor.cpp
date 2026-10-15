#include "PluginEditor.h"

using namespace lumen::led;
using namespace lumen::ref;

namespace
{
constexpr int W = 1060, H = 660;
const juce::Colour MIXC = C::cyan, REFC = C::magenta, DIFFC = C::amber;
juce::String fmtDb (float v, int dec = 1) { return v <= -99.0f ? juce::String ("--") : juce::String (v, dec); }
juce::String fmtTime (double s) { const int m = (int) (s / 60.0); return juce::String (m) + ":" + juce::String ((int) s % 60).paddedLeft ('0', 2); }
}

LumenReferenceEditor::LumenReferenceEditor (LumenReferenceProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    for (int s = 0; s < kSlots; ++s)
    {
        slotBtn[s].setColour (juce::TextButton::buttonColourId, REFC);
        slotBtn[s].onClick = [this, s]
        {
            if (proc.getRef (s) == nullptr) { loadDialog (s); return; }
            setParam (IDs::slot, (float) s);
            proc.requestAnalysis (s);
        };
        slotBtn[s].setTooltip ("Click: select. Empty slot: load. Drag an audio file onto the window to load it into the selected slot.");
        addAndMakeVisible (slotBtn[s]);
    }
    loadBtn.setColour (juce::TextButton::buttonColourId, C::green);
    loadBtn.onClick = [this] { loadDialog (proc.activeSlot()); };
    addAndMakeVisible (loadBtn);
    abBtn.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    abBtn.setAlpha (0.0f);
    abBtn.onClick = [this] { setParam (IDs::ab, paramValue (IDs::ab) > 0.5f ? 0.0f : 1.0f); };
    abBtn.setTooltip ("Switch between your MIX and the REFERENCE (loudness matched)");
    addAndMakeVisible (abBtn);

    matchT = std::make_unique<Toggle> (st, IDs::match, "LEVEL MATCH", C::green);
    monoT  = std::make_unique<Toggle> (st, IDs::mono, "MONO", C::amber);
    previewT = std::make_unique<Toggle> (st, IDs::preview, "PREVIEW", REFC);
    previewT->setTooltip ("Play the reference even when the DAW is stopped");
    meqT = std::make_unique<Toggle> (st, IDs::meqOn, "MATCH EQ ON", DIFFC);
    syncC = std::make_unique<Combo> (st, IDs::sync, REFC);
    avgC = std::make_unique<Combo> (st, IDs::avg, MIXC);
    trimK = std::make_unique<Knob> (st, IDs::trim, "TRIM", REFC, true);
    amtK = std::make_unique<Knob> (st, IDs::meqAmt, "AMOUNT", DIFFC);
    for (juce::Component* c : std::initializer_list<juce::Component*> { matchT.get(), monoT.get(), previewT.get(), meqT.get(), syncC.get(), avgC.get(), trimK.get(), amtK.get() })
        addAndMakeVisible (*c);
    resetBtn.setColour (juce::TextButton::buttonColourId, MIXC);
    resetBtn.onClick = [this] { mixAcc.reset(); proc.resetMixMeters(); stHistory.clear(); mixProf = {}; };
    resetBtn.setTooltip ("Restart the mix analysis (do this when you jump to another part of the song)");
    captureBtn.setColour (juce::TextButton::buttonColourId, DIFFC);
    captureBtn.onClick = [this] { proc.captureMatchEq (mixProf, refProf); };
    captureBtn.setTooltip ("Fit an 8-band EQ that moves your mix's tonal balance toward the reference");
    zeroBtn.setColour (juce::TextButton::buttonColourId, C::dim.brighter (0.5f));
    zeroBtn.onClick = [this] { for (int b = 0; b < kMeqBands; ++b) setParam (IDs::meqGain (b), 0.0f); };
    for (auto* b : { &resetBtn, &captureBtn, &zeroBtn }) addAndMakeVisible (*b);

    mixAcc.prepare (proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0);
    tmpL.resize (1 << 16); tmpR.resize (1 << 16);
    setSize (W, H);
    startTimerHz (20);
}

LumenReferenceEditor::~LumenReferenceEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenReferenceEditor::setParam (const juce::String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
}

void LumenReferenceEditor::loadDialog (int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Load reference track", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, slot] (const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile()) { proc.loadReference (slot, fc.getResult()); setParam (IDs::slot, (float) slot); }
    });
}

bool LumenReferenceEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) for (auto ext : { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg", ".m4a" }) if (f.endsWithIgnoreCase (ext)) return true;
    return false;
}
void LumenReferenceEditor::filesDropped (const juce::StringArray& files, int, int)
{
    int slot = proc.activeSlot();
    for (auto& f : files)
    {
        proc.loadReference (slot, juce::File (f));
        slot = (slot + 1) % kSlots;
    }
}

void LumenReferenceEditor::resized()
{
    for (int s = 0; s < kSlots; ++s) slotBtn[s].setBounds (290 + s * 98, 10, 92, 44);
    loadBtn.setBounds (684, 18, 58, 28);
    abBtn.setBounds (752, 8, 178, 50);
    matchT->setBounds (940, 8, 104, 22);
    monoT->setBounds (940, 34, 104, 22);
    syncC->setBounds (858, 96, 120, 22);
    previewT->setBounds (858, 124, 120, 22);
    trimK->setBounds (982, 86, 56, 68);
    avgC->setBounds (500, 172, 76, 20);
    resetBtn.setBounds (582, 172, 66, 20);
    meqT->setBounds (760, 504, 120, 22);
    amtK->setBounds (970, 494, 68, 76);
    captureBtn.setBounds (888, 504, 76, 22);
    zeroBtn.setBounds (888, 530, 76, 20);
}

//==============================================================================
void LumenReferenceEditor::timerCallback()
{
    // feed the mix analysis
    const int got = proc.pullMix (tmpL.data(), tmpR.data(), (int) tmpL.size());
    const float avgIdx = paramValue (IDs::avg);
    if (std::abs (avgIdx - lastAvg) > 0.1f) { const float taus[] = { 1.0f, 4.0f, 10.0f, 0.0f }; mixAcc.setTimeConstant (taus[juce::jlimit (0, 3, (int) avgIdx)]); lastAvg = avgIdx; }
    if (got > 0) mixAcc.push (tmpL.data(), tmpR.data(), got);
    if (proc.hostPlaying.load() && (tick % 2) == 0 && proc.mixS.load() > -70.0f) stHistory.push_back (proc.mixS.load());
    if (stHistory.size() > 20000) stHistory.erase (stHistory.begin(), stHistory.begin() + 5000);

    if ((tick % 5) == 0)
    {
        mixAcc.computeSpectral (mixProf);
        mixProf.lufsI = proc.mixI.load(); mixProf.lufsS = proc.mixS.load(); mixProf.truePeakDb = proc.mixTp.load();
        mixProf.plr = mixProf.truePeakDb - mixProf.lufsI;
        mixProf.lra = loudnessRange (stHistory);
        mixProf.valid = mixProf.valid && mixProf.lufsI > -70.0f;
    }
    if (proc.refVersion.load() != lastRefVersion || (tick % 10) == 0)
    {
        lastRefVersion = proc.refVersion.load();
        refProf = proc.getRefProfile (proc.activeSlot());
        for (int s = 0; s < kSlots; ++s)
        {
            auto d = proc.getRef (s);
            slotBtn[s].setButtonText (d != nullptr ? d->name.substring (0, 12) : (proc.isBusy (s) ? "LOADING..." : "+ LOAD"));
            slotBtn[s].setColour (juce::TextButton::buttonColourId, s == proc.activeSlot() ? REFC : C::dim.brighter (0.4f));
        }
    }
    if ((tick % 20) == 0) advice = advise (mixProf, refProf, 6);
    const float l = proc.outPeakL.load(), r = proc.outPeakR.load();
    outL = juce::jmax (l, outL * 0.8f); outR = juce::jmax (r, outR * 0.8f);
    ++tick;
    repaint();
}

//==============================================================================
void LumenReferenceEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto a = waveArea();
    if (! a.contains (e.position)) return;
    const float s = paramValue (IDs::loopS), en = paramValue (IDs::loopE);
    const float xs = a.getX() + s * a.getWidth(), xe = a.getX() + en * a.getWidth();
    if (std::abs (e.position.x - xs) < 7.0f) dragHandle = 0;
    else if (std::abs (e.position.x - xe) < 7.0f) dragHandle = 1;
    else if (e.mods.isShiftDown()) { dragHandle = 2; setParam (IDs::loopS, (e.position.x - a.getX()) / a.getWidth()); setParam (IDs::loopE, (e.position.x - a.getX()) / a.getWidth()); }
    else { proc.seekRequest.store (juce::jlimit (0.0, 1.0, (double) ((e.position.x - a.getX()) / a.getWidth()))); dragHandle = -1; }
}
void LumenReferenceEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto a = waveArea();
    const float t = juce::jlimit (0.0f, 1.0f, (e.position.x - a.getX()) / a.getWidth());
    if (dragHandle == 0) setParam (IDs::loopS, juce::jmin (t, paramValue (IDs::loopE) - 0.01f));
    else if (dragHandle == 1) setParam (IDs::loopE, juce::jmax (t, paramValue (IDs::loopS) + 0.01f));
    else if (dragHandle == 2) setParam (IDs::loopE, juce::jmax (t, paramValue (IDs::loopS) + 0.01f));
}
void LumenReferenceEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragHandle >= 0) proc.requestAnalysis (proc.activeSlot());
    dragHandle = -1;
}
void LumenReferenceEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto a = waveArea();
    const float xs = a.getX() + paramValue (IDs::loopS) * a.getWidth(), xe = a.getX() + paramValue (IDs::loopE) * a.getWidth();
    const bool near = a.contains (e.position) && (std::abs (e.position.x - xs) < 7.0f || std::abs (e.position.x - xe) < 7.0f);
    setMouseCursor (near ? juce::MouseCursor::LeftRightResizeCursor : a.contains (e.position) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

//==============================================================================
void LumenReferenceEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, H * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    paintHeader (g);
    paintWave (g);
    paintSpectrum (g);
    paintLoudness (g);
    paintStereo (g);
    paintBands (g);
    paintAdvice (g);
    paintMatch (g);
}

void LumenReferenceEditor::paintHeader (juce::Graphics& g)
{
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (MIXC.withAlpha (0.0f), 0, 64, REFC.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.3, MIXC.withAlpha (0.7f)); line.addColour (0.7, REFC.withAlpha (0.7f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);

    juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
    juce::Path lp; ga.createPath (lp);
    g.setColour (MIXC.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
    juce::ColourGradient lg (MIXC, 20, 0, REFC, 140, 0, false);
    g.setGradientFill (lg); g.fillPath (lp);
    logo.draw (g, { 146.0f, 18.0f, 140.0f, 28.0f }, "REFERENCE", REFC, 1.85f, juce::Justification::centredLeft, false, 10.5f);

    // slot LEDs
    for (int s = 0; s < kSlots; ++s)
        drawLed (g, { 300.0f + s * 98.0f, 16.0f }, 2.6f, REFC, s == proc.activeSlot() ? 1.0f : (proc.getRef (s) != nullptr ? 0.25f : 0.0f));

    // A/B switch
    const bool ref = paramValue (IDs::ab) > 0.5f;
    auto ab = juce::Rectangle<float> (752.0f, 8.0f, 178.0f, 50.0f);
    lcd (g, ab, ref ? REFC : MIXC);
    auto half = ab.reduced (4.0f);
    auto left = half.removeFromLeft (half.getWidth() * 0.5f);
    for (int k = 0; k < 2; ++k)
    {
        auto r = k == 0 ? left : half;
        const bool on = (k == 1) == ref;
        const auto col = k == 0 ? MIXC : REFC;
        if (on) { g.setColour (col.withAlpha (0.18f)); g.fillRoundedRectangle (r, 4.0f); g.setColour (col.withAlpha (0.7f)); g.drawRoundedRectangle (r, 4.0f, 1.0f); }
        drawLed (g, { r.getX() + 12.0f, r.getCentreY() }, 4.0f, col, on ? 1.0f : 0.0f);
        g.setFont (font (16.0f)); g.setColour (on ? col.interpolatedWith (juce::Colours::white, 0.3f) : C::dim);
        g.drawText (k == 0 ? "MIX" : "REF", r.withTrimmedLeft (18.0f), juce::Justification::centred);
    }
    if (ref)
    {
        g.setFont (font (9.0f)); g.setColour (C::red);
        g.drawText ("TURN OFF BEFORE EXPORT", 740, 0, 200, 9, juce::Justification::centred);
    }
}

void LumenReferenceEditor::paintWave (juce::Graphics& g)
{
    panel (g, { 16.0f, 72.0f, 1028.0f, 88.0f }, "REFERENCE TRACK", REFC, proc.getRef (proc.activeSlot()) != nullptr);
    const auto a = waveArea();
    lcd (g, a, REFC);
    auto d = proc.getRef (proc.activeSlot());
    if (d == nullptr)
    {
        g.setFont (font (12.0f)); g.setColour (REFC.withAlpha (0.7f));
        g.drawText (proc.isBusy (proc.activeSlot()) ? "LOADING + ANALYSING..." : "DRAG A REFERENCE TRACK HERE (WAV / MP3 / FLAC / AIFF) OR CLICK LOAD", a, juce::Justification::centred);
        const auto err = proc.getError (proc.activeSlot());
        if (err.isNotEmpty()) { g.setColour (C::red); g.drawText (err, a.translated (0, 16), juce::Justification::centred); }
        return;
    }
    const float ls = paramValue (IDs::loopS), le = paramValue (IDs::loopE);
    const int cols = (int) d->ovMax.size();
    const float pos = (float) (proc.refPosSec.load() / juce::jmax (0.001, d->lengthSeconds()));
    // LED-column waveform
    const int bars = 205;
    const float bw = a.getWidth() / bars;
    for (int b = 0; b < bars; ++b)
    {
        float mx = 0.0f;
        const int c0 = b * cols / bars, c1 = juce::jmax (c0 + 1, (b + 1) * cols / bars);
        for (int c = c0; c < c1; ++c) mx = juce::jmax (mx, juce::jmax (d->ovMax[(size_t) c], -d->ovMin[(size_t) c]));
        const float t = (b + 0.5f) / bars;
        const bool inLoop = t >= ls && t <= le;
        const bool played = t <= pos;
        const float h = juce::jlimit (1.0f, a.getHeight() - 6.0f, mx * (a.getHeight() - 6.0f));
        auto r = juce::Rectangle<float> (a.getX() + b * bw + 0.6f, a.getCentreY() - h * 0.5f, bw - 1.2f, h);
        g.setColour ((inLoop ? (played ? REFC.interpolatedWith (juce::Colours::white, 0.3f) : REFC) : C::dim).withAlpha (inLoop ? 0.85f : 0.35f));
        g.fillRect (r);
    }
    // loop handles
    for (float t : { ls, le })
    {
        const float x = a.getX() + t * a.getWidth();
        g.setColour (C::amber); g.fillRect (x - 1.0f, a.getY(), 2.0f, a.getHeight());
        drawLed (g, { x, a.getY() + 4.0f }, 3.0f, C::amber, 1.0f);
    }
    const float px = a.getX() + pos * a.getWidth();
    g.setColour (juce::Colours::white); g.fillRect (px - 0.5f, a.getY(), 1.5f, a.getHeight());
    g.setFont (font (9.5f)); g.setColour (REFC.withAlpha (0.8f));
    g.drawText (d->name.toUpperCase() + "   " + fmtTime (proc.refPosSec.load()) + " / " + fmtTime (d->lengthSeconds())
                + (proc.isBusy (proc.activeSlot()) ? "   ANALYSING..." : "   LOOP " + fmtTime (ls * d->lengthSeconds()) + " - " + fmtTime (le * d->lengthSeconds())),
                (int) a.getX() + 150, (int) a.getY() - 20, 420, 14, juce::Justification::centredLeft);
    g.setColour (C::dim);
    g.drawText ("DRAG AMBER HANDLES = LOOP   CLICK = JUMP   SHIFT+DRAG = NEW LOOP", 470, (int) a.getY() - 20, 376, 14, juce::Justification::centredRight);
}

void LumenReferenceEditor::paintSpectrum (juce::Graphics& g)
{
    panel (g, { 16.0f, 168.0f, 644.0f, 300.0f }, "TONAL BALANCE", MIXC);
    g.setFont (font (9.5f));
    g.setColour (MIXC); g.drawText ("MIX", 150, 173, 40, 14, juce::Justification::centredLeft);
    g.setColour (REFC); g.drawText ("REFERENCE", 190, 173, 80, 14, juce::Justification::centredLeft);
    g.setColour (DIFFC); g.drawText ("DIFFERENCE", 270, 173, 90, 14, juce::Justification::centredLeft);
    g.setColour (C::dim); g.drawText ("AVG", 470, 173, 28, 14, juce::Justification::centredRight);
    auto top = juce::Rectangle<float> (26.0f, 198.0f, 624.0f, 180.0f), bot = juce::Rectangle<float> (26.0f, 384.0f, 624.0f, 76.0f);
    lcd (g, top, MIXC); lcd (g, bot, DIFFC);
    auto xOf = [&] (juce::Rectangle<float> r, float f) { return r.getX() + 8.0f + (r.getWidth() - 16.0f) * std::log (f / 20.0f) / std::log (1000.0f); };
    g.setFont (font (8.5f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        g.setColour (C::border.withAlpha (0.8f));
        g.drawVerticalLine ((int) xOf (top, f), top.getY() + 4, top.getBottom() - 4);
        g.drawVerticalLine ((int) xOf (bot, f), bot.getY() + 4, bot.getBottom() - 4);
        g.setColour (C::dim);
        g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String ((int) f), (int) xOf (top, f) - 15, (int) top.getBottom() - 14, 30, 12, juce::Justification::centred);
    }
    // band separators
    for (int b = 1; b < kBands; ++b) { g.setColour (C::dim.withAlpha (0.25f)); g.drawVerticalLine ((int) xOf (top, bandEdges()[(size_t) b]), top.getY(), top.getBottom()); }
    auto yTop = [&] (float db) { return top.getCentreY() - 8.0f - juce::jlimit (-24.0f, 12.0f, db) * (top.getHeight() - 30.0f) / 36.0f + (top.getHeight() - 30.0f) * 6.0f / 36.0f; };
    auto curve = [&] (const Profile& pr, juce::Colour col, bool fill)
    {
        if (! pr.valid && std::abs (pr.spec[30]) < 1.0e-9f) return;
        juce::Path p;
        for (int i = 0; i < kPoints; ++i)
        {
            const float x = xOf (top, pointFreq (i)), y = yTop (pr.spec[i]);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        if (fill)
        {
            juce::Path f (p); f.lineTo (xOf (top, 20000.0f), top.getBottom()); f.lineTo (xOf (top, 20.0f), top.getBottom()); f.closeSubPath();
            juce::ColourGradient gr (col.withAlpha (0.22f), 0, top.getY(), col.withAlpha (0.0f), 0, top.getBottom(), false);
            g.setGradientFill (gr); g.fillPath (f);
        }
        glowPath (g, p, col, 1.6f);
    };
    curve (refProf, REFC, true);
    curve (mixProf, MIXC, false);
    // difference (mix - ref), LED dots
    g.setColour (DIFFC.withAlpha (0.35f)); g.drawHorizontalLine ((int) bot.getCentreY(), bot.getX() + 6, bot.getRight() - 6);
    if (mixProf.valid && refProf.valid)
    {
        juce::Path p;
        for (int i = 0; i < kPoints; ++i)
        {
            const float d = juce::jlimit (-9.0f, 9.0f, mixProf.spec[i] - refProf.spec[i]);
            const float x = xOf (bot, pointFreq (i)), y = bot.getCentreY() - d * (bot.getHeight() * 0.42f) / 9.0f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            if (i % 2 == 0) drawLed (g, { x, y }, 1.8f, std::abs (d) > 3.0f ? C::red : DIFFC, 0.9f);
        }
        glowPath (g, p, DIFFC, 1.2f, 0.7f);
    }
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("+9 dB MIX LOUDER", (int) bot.getX() + 8, (int) bot.getY() + 2, 160, 12, juce::Justification::centredLeft);
    g.drawText ("-9 dB MIX QUIETER", (int) bot.getX() + 8, (int) bot.getBottom() - 14, 160, 12, juce::Justification::centredLeft);
}

void LumenReferenceEditor::paintLoudness (juce::Graphics& g)
{
    panel (g, { 668.0f, 168.0f, 376.0f, 150.0f }, "LOUDNESS & DYNAMICS", C::green);
    const int x0 = 680, y0 = 194;
    g.setFont (font (10.0f)); g.setColour (C::dim);
    g.drawText ("", x0, y0, 100, 14, juce::Justification::centredLeft);
    g.setColour (MIXC); g.drawText ("MIX", x0 + 110, y0, 70, 14, juce::Justification::centredRight);
    g.setColour (REFC); g.drawText ("REF", x0 + 190, y0, 70, 14, juce::Justification::centredRight);
    g.setColour (C::amber); g.drawText ("DIFF", x0 + 270, y0, 80, 14, juce::Justification::centredRight);
    struct Row { const char* n; float m, r; const char* unit; };
    const Row rows[] = { { "INTEGRATED", mixProf.lufsI, refProf.lufsI, "LUFS" }, { "SHORT-TERM", proc.mixS.load(), refProf.lufsS, "LUFS" },
                         { "TRUE PEAK", mixProf.truePeakDb, refProf.truePeakDb, "dBTP" }, { "PLR (PUNCH)", mixProf.lufsI > -70 ? mixProf.plr : -100.0f, refProf.valid ? refProf.plr : -100.0f, "dB" },
                         { "LRA (RANGE)", mixProf.lra > 0 ? mixProf.lra : -100.0f, refProf.valid ? refProf.lra : -100.0f, "LU" } };
    for (int i = 0; i < 5; ++i)
    {
        const int y = y0 + 16 + i * 16;
        g.setFont (font (10.0f)); g.setColour (C::text.withAlpha (0.75f)); g.drawText (rows[i].n, x0, y, 110, 16, juce::Justification::centredLeft);
        g.setFont (mono (12.0f));
        g.setColour (MIXC); g.drawText (fmtDb (rows[i].m), x0 + 110, y, 70, 16, juce::Justification::centredRight);
        g.setColour (REFC); g.drawText (fmtDb (rows[i].r), x0 + 190, y, 70, 16, juce::Justification::centredRight);
        const bool both = rows[i].m > -99.0f && rows[i].r > -99.0f;
        const float d = rows[i].m - rows[i].r;
        g.setColour (both ? (std::abs (d) > 2.0f ? C::red : std::abs (d) > 1.0f ? C::amber : C::green) : C::dim);
        g.drawText (both ? (d > 0 ? "+" : "") + juce::String (d, 1) + " " + rows[i].unit : juce::String ("--"), x0 + 260, y, 90, 16, juce::Justification::centredRight);
    }
    const float gdb = proc.refGainDb.load();
    gainMatrix.draw (g, { 680.0f, 296.0f, 352.0f, 18.0f }, "REF GAIN " + juce::String (gdb > 0 ? "+" : "") + juce::String (gdb, 1) + " DB" + (paramValue (IDs::match) > 0.5f ? "  MATCHED" : ""),
                     C::green, 1.9f, juce::Justification::centredLeft, false, 10.0f);
    ledMeter (g, { 960.0f, 298.0f, 74.0f, 6.0f }, outL, outL, false, 12);
    ledMeter (g, { 960.0f, 307.0f, 74.0f, 6.0f }, outR, outR, false, 12);
}

void LumenReferenceEditor::paintStereo (juce::Graphics& g)
{
    panel (g, { 668.0f, 326.0f, 376.0f, 142.0f }, "STEREO WIDTH  &  MONO SAFETY", C::violet);
    for (int b = 0; b < kBands; ++b)
    {
        const float y = 352.0f + b * 18.5f;
        g.setFont (font (9.0f)); g.setColour (C::text.withAlpha (0.7f));
        g.drawText (bandNames()[b], 680, (int) y, 70, 16, juce::Justification::centredLeft);
        auto bar = [&] (float w, float yy, juce::Colour col)
        {
            const int segs = 22;
            for (int k = 0; k < segs; ++k)
            {
                const float t = (k + 0.5f) / segs * 120.0f;
                drawLed (g, { 752.0f + k * 9.0f, yy }, 1.9f, col, w >= t ? 1.0f : 0.07f);
            }
        };
        bar (mixProf.bandWidth[b], y + 4.0f, MIXC);
        bar (refProf.bandWidth[b], y + 11.0f, REFC);
        const float c = mixProf.valid ? mixProf.bandCorr[b] : 1.0f;
        const auto col = c < 0.0f ? C::red : c < 0.5f ? C::amber : C::green;
        drawLed (g, { 960.0f, y + 8.0f }, 3.4f, col, mixProf.valid ? 1.0f : 0.1f);
        g.setFont (mono (10.5f)); g.setColour (col);
        g.drawText (mixProf.valid ? juce::String (c, 2) : juce::String ("--"), 968, (int) y + 1, 60, 14, juce::Justification::centredRight);
    }
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("WIDTH  (MIX / REF)", 752, 452, 200, 12, juce::Justification::centredLeft);
    g.drawText ("CORRELATION", 940, 452, 96, 12, juce::Justification::centredRight);
}

void LumenReferenceEditor::paintBands (juce::Graphics& g)
{
    panel (g, { 16.0f, 476.0f, 316.0f, 168.0f }, "BAND BALANCE  (MIX vs REF)", DIFFC);
    for (int b = 0; b < kBands; ++b)
    {
        const float x = 44.0f + b * 50.0f;
        const float d = mixProf.valid && refProf.valid ? mixProf.bandDb[b] - refProf.bandDb[b] : 0.0f;
        const int segs = 7;
        for (int k = -segs; k <= segs; ++k)
        {
            if (k == 0) continue;
            const float y = 560.0f - k * 7.5f;
            const float thr = (std::abs (k) - 0.5f) * 1.0f;
            const bool lit = (k > 0 && d > thr) || (k < 0 && -d > thr);
            drawLed (g, { x, y }, 2.6f, std::abs (k) > 4 ? C::red : std::abs (k) > 2 ? C::amber : C::green, lit ? 1.0f : 0.07f);
        }
        g.setColour (C::dim); g.fillRect (x - 8.0f, 559.5f, 16.0f, 1.0f);
        g.setFont (mono (10.0f)); g.setColour (std::abs (d) > 2.0f ? C::red : C::text);
        g.drawText (mixProf.valid && refProf.valid ? (d > 0 ? "+" : "") + juce::String (d, 1) : juce::String ("--"), (int) x - 24, 616, 48, 12, juce::Justification::centred);
        g.setFont (font (8.0f)); g.setColour (C::dim);
        g.drawText (bandNames()[b], (int) x - 25, 628, 50, 12, juce::Justification::centred);
    }
    g.setFont (font (8.0f)); g.setColour (C::dim);
    g.drawText ("+7", 20, 504, 20, 10, juce::Justification::centredLeft);
    g.drawText ("-7", 20, 607, 20, 10, juce::Justification::centredLeft);
}

void LumenReferenceEditor::paintAdvice (juce::Graphics& g)
{
    panel (g, { 340.0f, 476.0f, 412.0f, 168.0f }, "SMART ADVICE", C::green);
    float y = 500.0f;
    for (int i = 0; i < advice.size(); ++i)
    {
        drawLed (g, { 356.0f, y + 8.0f }, 2.8f, i == 0 ? C::amber : C::green, 1.0f);
        g.setFont (font (10.5f, false)); g.setColour (C::text.withAlpha (0.9f));
        juce::AttributedString as; as.append (advice[i], font (10.5f, false), C::text.withAlpha (0.9f)); as.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout tl; tl.createLayout (as, 380.0f);
        tl.draw (g, { 366.0f, y, 380.0f, tl.getHeight() });
        y += juce::jmax (16.0f, tl.getHeight() + 6.0f);
        if (y > 630.0f) break;
    }
}

void LumenReferenceEditor::paintMatch (juce::Graphics& g)
{
    panel (g, { 760.0f, 476.0f, 284.0f, 168.0f }, "MATCH EQ", DIFFC, paramValue (IDs::meqOn) > 0.5f);
    auto a = juce::Rectangle<float> (770.0f, 582.0f, 264.0f, 54.0f);
    lcd (g, a, DIFFC);
    lumen::Biquad bq[kMeqBands];
    const float amt = paramValue (IDs::meqAmt);
    for (int b = 0; b < kMeqBands; ++b) bq[b].set (lumen::meqType (b, kMeqBands), kMeqFreq[b], paramValue (IDs::meqGain (b)) * amt, 1.0, 48000.0);
    g.setColour (DIFFC.withAlpha (0.3f)); g.drawHorizontalLine ((int) a.getCentreY(), a.getX() + 4, a.getRight() - 4);
    juce::Path p;
    for (int i = 0; i <= 100; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, i / 100.0);
        double db = 0.0; for (auto& q : bq) db += q.magnitudeDb (f, 48000.0);
        const float x = a.getX() + 6 + (a.getWidth() - 12) * i / 100.0f, y = a.getCentreY() - (float) juce::jlimit (-12.0, 12.0, db) * (a.getHeight() * 0.42f) / 12.0f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    glowPath (g, p, DIFFC, 1.5f, paramValue (IDs::meqOn) > 0.5f ? 1.0f : 0.4f);
    for (int b = 0; b < kMeqBands; ++b)
    {
        const float x = a.getX() + 6 + (a.getWidth() - 12) * std::log (kMeqFreq[b] / 20.0f) / std::log (1000.0f);
        const float gv = paramValue (IDs::meqGain (b)) * amt;
        drawLed (g, { x, a.getCentreY() - gv * (a.getHeight() * 0.42f) / 12.0f }, 2.6f, juce::Colours::white.interpolatedWith (DIFFC, 0.4f), 1.0f);
    }
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawFittedText ("CAPTURE fits an EQ that moves your mix toward the reference. Bypass it or copy the moves to your own EQ.",
                      juce::Rectangle<int> (770, 532, 112, 46), juce::Justification::topLeft, 4);
}

