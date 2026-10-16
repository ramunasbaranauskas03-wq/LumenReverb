#include "PluginEditor.h"

using namespace lumen::led;

namespace
{
constexpr int W = 1000, HH = 650;
const juce::Colour BC[3] = { C::magenta, C::amber, C::cyan };
const juce::Colour MAIN = C::orange;
const juce::Colour UPC = C::cyan, DOWNC = C::red;
const char* BN[3] = { "LOW", "MID", "HIGH" };

template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}
int rowY (int b) { return 100 + (2 - b) * 100; }     // HIGH on top, like OTT
}

//==============================================================================
BandMeter::BandMeter (LumenOTTProcessor& p, int b, juce::Colour c) : proc (p), band (b), colour (c)
{
    aDown = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter (IDs::dthr (b)), [this] (float) { repaint(); }, nullptr);
    aUp = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter (IDs::uthr (b)), [this] (float) { repaint(); }, nullptr);
    setTooltip ("Drag the cyan line (upward threshold) or the red line (downward threshold)");
}
void BandMeter::mouseMove (const juce::MouseEvent& e)
{
    const float xd = xOf (proc.apvts.getRawParameterValue (IDs::dthr (band))->load()), xu = xOf (proc.apvts.getRawParameterValue (IDs::uthr (band))->load());
    setMouseCursor (std::min (std::abs (e.x - xd), std::abs (e.x - xu)) < 8.0f ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}
void BandMeter::mouseDown (const juce::MouseEvent& e)
{
    const float xd = xOf (proc.apvts.getRawParameterValue (IDs::dthr (band))->load()), xu = xOf (proc.apvts.getRawParameterValue (IDs::uthr (band))->load());
    drag = std::abs (e.x - xd) <= std::abs (e.x - xu) ? 0 : 1;
    (drag == 0 ? aDown : aUp)->beginGesture();
    mouseDrag (e);
}
void BandMeter::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    const float v = dbOf ((float) e.x);
    if (drag == 0) aDown->setValueAsPartOfGesture (juce::jlimit (-60.0f, 0.0f, std::round (v * 10.0f) / 10.0f));
    else aUp->setValueAsPartOfGesture (juce::jlimit (-80.0f, 0.0f, std::round (v * 10.0f) / 10.0f));
}
void BandMeter::mouseUp (const juce::MouseEvent&) { if (drag >= 0) (drag == 0 ? aDown : aUp)->endGesture(); drag = -1; }

void BandMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, colour);
    const float dT = proc.apvts.getRawParameterValue (IDs::dthr (band))->load(), uT = proc.apvts.getRawParameterValue (IDs::uthr (band))->load();
    const float lock = proc.apvts.getRawParameterValue (IDs::lock)->load() > 0.5f ? proc.lockDb.load() : 0.0f;
    const float xd = xOf (dT + lock), xu = xOf (uT + lock);
    // zones
    g.setColour (UPC.withAlpha (0.08f)); g.fillRect (r.getX() + 4.0f, r.getY() + 3.0f, xu - 4.0f, r.getHeight() - 6.0f);
    g.setColour (DOWNC.withAlpha (0.08f)); g.fillRect (xd, r.getY() + 3.0f, r.getRight() - 4.0f - xd, r.getHeight() - 6.0f);
    // level bar (LED segments)
    const float lvDb = juce::Decibels::gainToDecibels (level, -80.0f);
    const int segs = (int) ((r.getWidth() - 8.0f) / 5.0f);
    const float sw = (r.getWidth() - 8.0f) / (float) segs;
    const float barY = r.getY() + 14.0f, barH = r.getHeight() * 0.38f;
    for (int i = 0; i < segs; ++i)
    {
        const float x = r.getX() + 4.0f + i * sw;
        const float db = dbOf (x + sw * 0.5f);
        const bool lit = db < lvDb;
        const juce::Colour c = db > dT + lock ? DOWNC : db < uT + lock ? UPC : colour;
        g.setColour (lit ? c : c.withAlpha (0.07f));
        g.fillRoundedRectangle (x + 0.6f, barY, sw - 1.2f, barH, 1.0f);
        if (lit) { g.setColour (c.withAlpha (0.2f)); g.fillRoundedRectangle (x, barY - 1.0f, sw, barH + 2.0f, 1.5f); }
    }
    // gain bars: boost (cyan, from the up threshold to the left) and cut (red)
    const float gy = barY + barH + 6.0f, gh = 7.0f;
    const float upW = juce::jlimit (0.0f, xu - 4.0f, up / 40.0f * (r.getWidth() - 8.0f));
    const float dnW = juce::jlimit (0.0f, r.getRight() - 4.0f - xd, -down / 40.0f * (r.getWidth() - 8.0f));
    g.setColour (UPC.withAlpha (0.85f)); g.fillRoundedRectangle (xu - upW, gy, upW, gh, 2.0f);
    g.setColour (DOWNC.withAlpha (0.85f)); g.fillRoundedRectangle (xd, gy, dnW, gh, 2.0f);
    // handles
    for (int k = 0; k < 2; ++k)
    {
        const float x = k == 0 ? xd : xu;
        juce::Path pth; pth.startNewSubPath (x, r.getY() + 3.0f); pth.lineTo (x, r.getBottom() - 3.0f);
        glowPath (g, pth, k == 0 ? DOWNC : UPC, 1.8f);
    }
    g.setFont (mono (9.5f));
    g.setColour (UPC.interpolatedWith (juce::Colours::white, 0.3f));
    g.drawText ("+" + juce::String (up, 1), juce::Rectangle<float> (r.getX() + 6.0f, r.getBottom() - 14.0f, 60.0f, 12.0f), juce::Justification::centredLeft);
    g.setColour (DOWNC.interpolatedWith (juce::Colours::white, 0.3f));
    g.drawText (juce::String (down, 1), juce::Rectangle<float> (r.getRight() - 66.0f, r.getBottom() - 14.0f, 60.0f, 12.0f), juce::Justification::centredRight);
    // threshold values next to their handles
    g.setFont (font (8.5f));
    g.setColour (UPC.withAlpha (0.75f));
    g.drawText (juce::String (uT, 1), juce::Rectangle<float> (xu - 46.0f, r.getY() + 2.0f, 42.0f, 10.0f), juce::Justification::centredRight);
    g.setColour (DOWNC.withAlpha (0.75f));
    g.drawText (juce::String (dT, 1), juce::Rectangle<float> (xd + 4.0f, r.getY() + 2.0f, 42.0f, 10.0f), juce::Justification::centredLeft);
}

//==============================================================================
LumenOTTEditor::LumenOTTEditor (LumenOTTProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    for (int b = 0; b < 3; ++b)
    {
        const int y = rowY (b);
        meters[b] = std::make_unique<BandMeter> (proc, b, BC[b]); addAndMakeVisible (*meters[b]); meters[b]->setBounds (128, y, 420, 84);
        auto* so = add<Toggle> (*this, owned, st, IDs::solo (b), "S", C::amber, true); so->setBounds (30, y + 50, 22, 22);
        auto* by = add<Toggle> (*this, owned, st, IDs::byp (b), "BYP", C::red, true); by->setBounds (60, y + 50, 22, 22);
        so->setTooltip ("Solo band"); by->setTooltip ("Bypass band (passes uncompressed)");
        add<Knob> (*this, owned, st, IDs::bin (b), "IN", BC[b], true)->setBounds (556, y - 4, 66, 92);
        add<Knob> (*this, owned, st, IDs::bout (b), "OUT", BC[b], true)->setBounds (622, y - 4, 66, 92);
    }
    add<Knob> (*this, owned, st, IDs::depth, "DEPTH", MAIN)->setBounds (722, 94, 248, 150);
    const char* gid[] { "time", "upward", "downward" };
    const char* glb[] { "TIME", "UPWARD", "DOWNWARD" };
    const juce::Colour gc[] { MAIN, UPC, DOWNC };
    for (int i = 0; i < 3; ++i) add<Knob> (*this, owned, st, gid[i], glb[i], gc[i])->setBounds (712 + i * 90, 262, 90, 104);

    add<Knob> (*this, owned, st, IDs::x1, "LOW / MID", C::magenta)->setBounds (26, 448, 86, 96);
    add<Knob> (*this, owned, st, IDs::x2, "MID / HIGH", C::cyan)->setBounds (112, 448, 86, 96);
    add<Knob> (*this, owned, st, IDs::in, "INPUT", C::text, true)->setBounds (26, 546, 86, 96);
    add<Knob> (*this, owned, st, IDs::out, "OUTPUT", C::text, true)->setBounds (112, 546, 86, 96);
    const char* oid[] { "knee", "link", "max_boost", "noise_floor" };
    const char* olb[] { "KNEE", "LINK", "MAX BOOST", "NOISE GUARD" };
    for (int i = 0; i < 4; ++i)
    {
        auto* k = add<Knob> (*this, owned, st, oid[i], olb[i], C::violet); k->setBounds (222 + (i % 2) * 86, 448 + (i / 2) * 98, 86, 96);
        if (i == 3) k->slider.setTooltip ("Upward compression fades out below this level - no more boosted hiss and room noise");
        if (i == 2) k->slider.setTooltip ("Limits how much quiet parts can be pushed up");
    }
    auto* sty = add<Combo> (*this, owned, st, IDs::style, MAIN); sty->setBounds (412, 460, 120, 24);
    sty->setTooltip ("Classic = OTT timing, Smack = faster attack / punchier, Smooth = slower and RMS (glue)");
    add<Combo> (*this, owned, st, IDs::detect, MAIN)->setBounds (412, 506, 120, 24);
    add<Combo> (*this, owned, st, IDs::os, MAIN)->setBounds (412, 552, 120, 24);
    auto* lk = add<Toggle> (*this, owned, st, IDs::lock, "LEVEL LOCK", C::green); lk->setBounds (548, 456, 132, 26);
    lk->setTooltip ("Thresholds follow the long-term input level: same OTT effect on quiet and loud sources");
    add<Toggle> (*this, owned, st, IDs::autoGain, "AUTO GAIN", C::green)->setBounds (548, 490, 132, 26);
    add<Toggle> (*this, owned, st, IDs::clip, "SOFT CLIP", C::amber)->setBounds (548, 524, 132, 26);

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setColour (juce::ComboBox::textColourId, MAIN);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (330, 18, 250, 28);
    for (auto* b : { &prevB, &nextB }) { b->setColour (juce::TextButton::buttonColourId, MAIN); addAndMakeVisible (*b); }
    prevB.setBounds (296, 18, 28, 28); nextB.setBounds (586, 18, 28, 28);
    prevB.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    for (auto* b : { &aB, &bB, &copyB }) addAndMakeVisible (*b);
    aB.setBounds (626, 18, 30, 28); bB.setBounds (658, 18, 30, 28); copyB.setBounds (692, 18, 40, 28);
    aB.onClick = [this] { proc.switchAB (0); };
    bB.onClick = [this] { proc.switchAB (1); };
    copyB.onClick = [this] { proc.copyAB(); };
    copyB.setColour (juce::TextButton::buttonColourId, C::dim);

    srcBox.addItemList ({ "Synth / Lead", "Bass / 808", "Drums", "Vocal", "Master / Bus" }, 1);
    srcBox.setSelectedId (1, juce::dontSendNotification);
    srcBox.setColour (juce::ComboBox::textColourId, C::cyan);
    addAndMakeVisible (srcBox); srcBox.setBounds (712, 456, 150, 26);
    assistB.setColour (juce::TextButton::buttonColourId, C::cyan);
    assistB.setTooltip ("Play the part: Smart Assist listens 5 s and sets OTT for this source (level-locked)");
    assistB.onClick = [this] { proc.startAssist ((LumenOTTProcessor::Source) (srcBox.getSelectedId() - 1)); };
    addAndMakeVisible (assistB); assistB.setBounds (870, 455, 102, 28);

    setSize (W, HH);
    startTimerHz (30);
}

LumenOTTEditor::~LumenOTTEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenOTTEditor::timerCallback()
{
    proc.finishAssistIfReady();
    if (proc.programVersion.load() != lastProgram) { lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    for (int b = 0; b < 3; ++b)
    {
        auto& m = *meters[b];
        const float lv = proc.bandLvl[b].load(), u = proc.bandUp[b].load(), d = proc.bandDown[b].load();
        m.level = lv > m.level ? lv : m.level * 0.85f;
        m.up += (u - m.up) * 0.35f; m.down += (d - m.down) * 0.35f;
        m.repaint();
    }
    aB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 0 ? MAIN : C::dim);
    bB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 1 ? MAIN : C::dim);
    assistB.setEnabled (! proc.isAssisting());
    repaint();
}

void LumenOTTEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (UPC.withAlpha (0.0f), 0, 64, DOWNC.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.25, UPC.withAlpha (0.75f)); line.addColour (0.75, DOWNC.withAlpha (0.75f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (MAIN.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (UPC, 20, 0, DOWNC, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 146.0f, 18.0f, 120.0f, 28.0f }, "OTT", MAIN, 2.4f, juce::Justification::centredLeft, false, 10.5f);
    }
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("IN", 748, 18, 20, 10, juce::Justification::centredLeft);
    g.drawText ("OUT", 748, 36, 24, 10, juce::Justification::centredLeft);
    ledMeter (g, { 774.0f, 18.0f, 210.0f, 9.0f }, proc.inPk.load(), proc.inPk.load(), false, 28);
    ledMeter (g, { 774.0f, 36.0f, 210.0f, 9.0f }, proc.outPk.load(), proc.outPk.load(), false, 28);

    panel (g, { 16.0f, 72.0f, 680.0f, 324.0f }, "BANDS", MAIN);
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("cyan = upward (quiet parts pushed up)   red = downward (loud parts pulled down)", 110, 77, 440, 16, juce::Justification::centredLeft);
    for (int b = 0; b < 3; ++b)
    {
        const int y = rowY (b);
        g.setFont (font (15.0f)); g.setColour (BC[b]);
        g.drawText (BN[b], 26, y + 4, 96, 20, juce::Justification::centredLeft);
        const float f0 = b == 0 ? 0.0f : pv (b == 1 ? IDs::x1 : IDs::x2), f1 = b == 2 ? 0.0f : pv (b == 0 ? IDs::x1 : IDs::x2);
        g.setFont (font (8.5f)); g.setColour (C::dim);
        auto hz = [] (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (f)); };
        g.drawText (b == 0 ? "< " + hz (f1) + " Hz" : b == 2 ? "> " + hz (f0) + " Hz" : hz (f0) + " - " + hz (f1) + " Hz", 26, y + 24, 100, 12, juce::Justification::centredLeft);
        g.drawText ("S", 30, y + 73, 22, 10, juce::Justification::centred);
        g.drawText ("BYP", 56, y + 73, 30, 10, juce::Justification::centred);
    }
    panel (g, { 704.0f, 72.0f, 280.0f, 324.0f }, "MACRO", MAIN);
    panel (g, { 16.0f, 404.0f, 680.0f, 236.0f }, "SETTINGS", C::violet);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("TIMING STYLE", 412, 446, 120, 12, juce::Justification::centredLeft);
    g.drawText ("DETECTION", 412, 492, 120, 12, juce::Justification::centredLeft);
    g.drawText ("OVERSAMPLING", 412, 538, 120, 12, juce::Justification::centredLeft);
    g.setFont (font (9.0f));
    if (pv (IDs::autoGain) > 0.5f) { g.setColour (C::green.withAlpha (0.85f)); g.drawText ("AUTO " + juce::String (proc.agDb.load(), 1) + " dB", 548, 560, 132, 14, juce::Justification::centredLeft); }
    if (pv (IDs::lock) > 0.5f) { g.setColour (C::green.withAlpha (0.85f)); g.drawText ("LOCK " + juce::String (proc.lockDb.load(), 1) + " dB", 548, 576, 132, 14, juce::Justification::centredLeft); }
    g.setColour (C::dim);
    g.drawFittedText ("DEPTH blends with a phase-matched dry signal: no comb filtering at any setting.", juce::Rectangle<int> (412, 596, 270, 36), juce::Justification::topLeft, 2);

    panel (g, { 704.0f, 404.0f, 280.0f, 236.0f }, "SMART ASSIST", C::cyan);
    auto sr = juce::Rectangle<float> (712.0f, 492.0f, 260.0f, 28.0f);
    lcd (g, sr, C::cyan);
    if (proc.isAssisting())
    {
        const float pr = proc.assistProgress();
        for (int k = 0; k < 26; ++k) drawLed (g, { sr.getX() + 10.0f + k * 9.6f, sr.getCentreY() }, 2.6f, C::cyan, pr * 26.0f > (float) k ? 1.0f : 0.1f);
    }
    else status.draw (g, sr.reduced (4.0f, 2.0f), proc.getAssistResult().isEmpty() ? juce::String ("READY") : juce::String ("SET"), C::cyan, 2.0f, juce::Justification::centred, false, 10.0f);
    g.setFont (font (9.5f));
    g.setColour (proc.getAssistResult().isNotEmpty() ? C::cyan.withAlpha (0.85f) : C::dim);
    g.drawFittedText (proc.getAssistResult().isNotEmpty() && ! proc.isAssisting() ? proc.getAssistResult()
                                                                                 : juce::String ("Pick the source, press ANALYZE and play 5 s. Depth, timing, band balance and Level Lock are set for you."),
                      juce::Rectangle<int> (714, 528, 258, 100), juce::Justification::topLeft, 6);
}
