#include "PluginEditor.h"
using namespace lumen::led;

static constexpr int W = 980, HH = 620;
static const juce::Colour bandCol[3] { C::orange, C::cyan, C::violet };     // low, mid, high

// ================================================================================================ BandMeter
BandMeter::BandMeter (LumenOTTProcessor& p, int b, juce::Colour c) : proc (p), band (b), colour (c)
{
    setTooltip ("Drag the green handle = UPWARD threshold (quieter parts are lifted), the red handle = DOWNWARD threshold (louder parts are pushed down).  "
                "The white bar is the band level, the arrow is what OTT is doing right now.");
}

void BandMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (C::field); g.fillRoundedRectangle (r, 4.0f);
    const float up = proc.P (IDs::uthr (band)) + off, dn = proc.P (IDs::dthr (band)) + off;
    const float xu = xOf (up), xd = xOf (dn);
    auto inner = r.reduced (3.0f, 6.0f);
    // zones
    g.setColour (C::green.withAlpha (0.10f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (inner.getX(), inner.getY(), xu, inner.getBottom()));
    g.setColour (C::red.withAlpha (0.10f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xd, inner.getY(), inner.getRight(), inner.getBottom()));
    // scale
    g.setFont (mono (8.5f));
    for (int d = -60; d <= 0; d += 12) { const float x = xOf ((float) d); g.setColour (C::border.withAlpha (0.6f)); g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom()); }
    // level bar
    const float lx = xOf (juce::jlimit (-72.0f, 6.0f, level));
    auto bar = juce::Rectangle<float>::leftTopRightBottom (inner.getX(), inner.getCentreY() - 7, std::max (inner.getX(), lx), inner.getCentreY() + 7);
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.35f), bar.getX(), 0, colour.withAlpha (0.95f), bar.getRight() + 1, 0, false));
    g.fillRoundedRectangle (bar, 3.0f);
    // live gain arrow (from the level to where it ends up)
    if (std::abs (gain) > 0.1f && level > -70.0f)
    {
        const float tx = xOf (juce::jlimit (-72.0f, 6.0f, level + gain));
        const auto c = gain > 0 ? C::green : C::red;
        const float y = inner.getCentreY();
        g.setColour (c.withAlpha (0.9f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (lx, tx), y - 1.5f, std::max (lx, tx), y + 1.5f));
        juce::Path a; const float s = gain > 0 ? 1.0f : -1.0f;
        a.addTriangle (tx, y - 6, tx, y + 6, tx + 7 * s, y); g.fillPath (a);
    }
    // threshold handles
    auto handle = [&] (float x, juce::Colour c, bool hot, bool leftSide)
    {
        g.setColour (c.withAlpha (hot ? 1.0f : 0.85f)); g.fillRect (x - (hot ? 1.5f : 1.0f), inner.getY(), hot ? 3.0f : 2.0f, inner.getHeight());
        juce::Path t; const float w = hot ? 8 : 6;
        t.addTriangle (x, inner.getY() + 2, x + (leftSide ? -w : w), inner.getY() + 2, x, inner.getY() + 2 + w * 1.4f);
        g.fillPath (t);
    };
    handle (xu, C::green, hover == 0 || drag == 0, true);
    handle (xd, C::red, hover == 1 || drag == 1, false);
    g.setColour (C::dim); g.setFont (mono (9.5f));
    g.drawText (juce::String (level > -70 ? juce::String (level, 1) + " dB" : juce::String ("--")) + (std::abs (gain) > 0.05f ? "   " + juce::String (gain > 0 ? "+" : "") + juce::String (gain, 1) + " dB" : juce::String()),
                r.reduced (8, 3), juce::Justification::bottomRight);
}

void BandMeter::mouseMove (const juce::MouseEvent& e)
{
    const float xu = xOf (proc.P (IDs::uthr (band)) + off), xd = xOf (proc.P (IDs::dthr (band)) + off);
    int h = -1; if (std::abs (e.position.x - xu) < 9) h = 0; else if (std::abs (e.position.x - xd) < 9) h = 1;
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}
void BandMeter::mouseDown (const juce::MouseEvent& e)
{
    mouseMove (e); drag = hover;
    if (drag >= 0) proc.apvts.getParameter (drag == 0 ? IDs::uthr (band) : IDs::dthr (band))->beginChangeGesture();
}
void BandMeter::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    const auto id = drag == 0 ? IDs::uthr (band) : IDs::dthr (band);
    float v = dbOf (e.position.x) - off;
    if (drag == 0) v = std::min (v, proc.P (IDs::dthr (band)));      // upward threshold stays below the downward one
    else v = std::max (v, proc.P (IDs::uthr (band)));
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id))) rp->setValueNotifyingHost (rp->convertTo0to1 (v));
    repaint();
}
void BandMeter::mouseUp (const juce::MouseEvent&) { if (drag >= 0) proc.apvts.getParameter (drag == 0 ? IDs::uthr (band) : IDs::dthr (band))->endChangeGesture(); drag = -1; }

// ================================================================================================ Editor
LumenOTTEditor::Knob& LumenOTTEditor::knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip)
{
    auto k = std::make_unique<Knob> (proc.apvts, id, lbl, c, bip); k->setBounds (r); addAndMakeVisible (*k);
    auto* raw = k.get(); owned.push_back (std::move (k)); return *raw;
}
LumenOTTEditor::Toggle& LumenOTTEditor::toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r)
{
    auto k = std::make_unique<Toggle> (proc.apvts, id, txt, c); k->setBounds (r); addAndMakeVisible (*k);
    auto* raw = k.get(); owned.push_back (std::move (k)); return *raw;
}

LumenOTTEditor::LumenOTTEditor (LumenOTTProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    // header
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (250, 15, 250, 26);
    for (auto* b : { &prevB, &nextB, &abA, &abB, &abCopy }) { b->setColour (juce::TextButton::buttonColourId, C::cyan); addAndMakeVisible (*b); }
    prevB.setBounds (506, 15, 26, 26); nextB.setBounds (534, 15, 26, 26);
    prevB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + proc.getNumPrograms() - 1) % proc.getNumPrograms()); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    abA.setBounds (580, 15, 30, 26); abB.setBounds (612, 15, 30, 26); abCopy.setBounds (644, 15, 52, 26);
    abA.onClick = [this] { proc.switchAB (0); }; abB.onClick = [this] { proc.switchAB (1); }; abCopy.onClick = [this] { proc.copyAB(); };
    auto st = std::make_unique<Combo> (proc.apvts, IDs::style, C::cyan); st->setBounds (720, 15, 110, 26); st->setTooltip ("CLEAN: lookahead + RMS, the expensive sound.  CLASSIC: the original OTT timing.  SMASH: fast and gritty");
    addAndMakeVisible (*st); owned.push_back (std::move (st));
    auto osb = std::make_unique<Combo> (proc.apvts, IDs::os, C::dim); osb->setBounds (836, 15, 70, 26); osb->setTooltip ("Oversampling"); addAndMakeVisible (*osb); owned.push_back (std::move (osb));

    // left: the big ones
    knob (IDs::depth, "DEPTH", C::cyan, { 30, 84, 150, 160 });
    knob (IDs::time, "TIME", C::cyan, { 22, 262, 76, 84 });
    knob (IDs::in, "IN GAIN", C::cyan, { 110, 262, 76, 84 }, true);
    knob (IDs::upward, "UPWARD", C::green, { 22, 362, 76, 84 });
    knob (IDs::downward, "DOWNWARD", C::red, { 110, 362, 76, 84 });
    knob (IDs::out, "OUT GAIN", C::cyan, { 66, 462, 76, 84 }, true);
    // bands (HIGH on top like the original)
    const char* names[3] { "LOW", "MID", "HIGH" };
    for (int b = 2; b >= 0; --b)
    {
        const int row = 2 - b, y = 78 + row * 132;
        knob (IDs::gain (b), juce::String (names[b]) + " GAIN", bandCol[b], { 222, y + 16, 80, 92 }, true);
        meters[b] = std::make_unique<BandMeter> (proc, b, bandCol[b]);
        addAndMakeVisible (*meters[b]); meters[b]->setBounds (316, y + 26, 470, 72);
        toggle (IDs::solo (b), "S", C::amber, { 794, y + 34, 28, 26 }).setTooltip ("Solo this band");
        toggle (IDs::byp (b), "B", C::dim, { 794, y + 66, 28, 26 }).setTooltip ("Bypass this band");
    }
    knob (IDs::x2, "MID / HIGH", C::dim, { 846, 140, 70, 82 });
    knob (IDs::x1, "LOW / MID", C::dim, { 846, 272, 70, 82 });
    // bottom extras
    const int by = 512;
    knob (IDs::maxBoost, "MAX BOOST", C::green, { 222, by, 70, 82 });
    knob (IDs::floor, "NOISE GUARD", C::green, { 296, by, 70, 82 });
    knob (IDs::link, "STEREO LINK", C::dim, { 370, by, 70, 82 });
    toggle (IDs::adaptive, "ADAPTIVE", C::cyan, { 456, by + 2, 116, 26 }).setTooltip ("Thresholds follow the level of the sound, so OTT works the same on quiet and loud material (recommended)");
    toggle (IDs::autoGain, "AUTO GAIN", C::amber, { 456, by + 34, 116, 26 }).setTooltip ("Keeps the output as loud as the input - judge the sound, not the volume");
    toggle (IDs::clip, "SOFT CLIP", C::red, { 580, by + 2, 116, 26 });
    setSize (W, HH);
    startTimerHz (30);
}

LumenOTTEditor::~LumenOTTEditor() { setLookAndFeel (nullptr); }

void LumenOTTEditor::timerCallback()
{
    for (int b = 0; b < 3; ++b)
    {
        auto& m = *meters[b];
        const float l = proc.bandLvl[b].load(), gn = proc.bandGain[b].load();
        m.level = l > m.level ? l : m.level - 1.2f;
        m.gain += (gn - m.gain) * 0.35f;
        m.off = proc.bandOff[b].load();
        m.repaint();
    }
    inM = std::max (proc.inPk.load(), inM * 0.85f); outM = std::max (proc.outPk.load(), outM * 0.85f);
    if (lastProgram != proc.programVersion.load() + proc.getCurrentProgram() * 1000)
    { lastProgram = proc.programVersion.load() + proc.getCurrentProgram() * 1000; presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    abA.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification); abB.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    repaint (920, 60, 60, 540); repaint (590, 480, 380, 120);
}

void LumenOTTEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    g.setColour (C::panelTop); g.fillRect (0, 0, W, 56);
    g.setColour (C::border); g.drawHorizontalLine (56, 0, (float) W);
    g.setColour (C::text); g.setFont (font (24.0f)); g.drawText ("LUMEN", 18, 12, 100, 32, juce::Justification::centredLeft);
    g.setColour (C::cyan); g.drawText ("OTT", 108, 12, 80, 32, juce::Justification::centredLeft);
    g.setColour (C::dim); g.setFont (font (9.5f, false)); g.drawText ("v2", 160, 24, 30, 14, juce::Justification::centredLeft);
    panel (g, { 12, 66, 190, 540 }, "MAIN", C::cyan, true);
    panel (g, { 210, 66, 622, 408 }, "BANDS", C::cyan, true);
    panel (g, { 840, 66, 82, 408 }, "X-OVER", C::dim, true);
    panel (g, { 210, 482, 760, 124 }, "SETTINGS", C::dim, true);
    // band labels
    const char* names[3] { "LOW", "MID", "HIGH" };
    for (int b = 2; b >= 0; --b)
    {
        const int y = 78 + (2 - b) * 132;
        g.setColour (bandCol[b]); g.setFont (font (11.0f)); g.drawText (names[b], 318, y + 8, 80, 14, juce::Justification::centredLeft);
        g.setColour (C::dim); g.setFont (font (9.5f, false));
        g.drawText ("up " + juce::String (pv (IDs::uthr (b)), 1) + "   down " + juce::String (pv (IDs::dthr (b)), 1) + " dB" + (pv (IDs::adaptive) > 0.5f ? "   (adaptive " + juce::String (meters[b]->off >= 0 ? "+" : "") + juce::String (meters[b]->off, 1) + ")" : juce::String()),
                    500, y + 8, 286, 14, juce::Justification::centredRight);
    }
    // in / out meters
    panel (g, { 930, 66, 40, 408 }, "", C::cyan, true);
    ledMeter (g, { 936, 94, 12, 370 }, inM, inM, true, 0);
    ledMeter (g, { 952, 94, 12, 370 }, outM, outM, true, 0);
    g.setColour (C::dim); g.setFont (font (8.5f)); g.drawText ("IN OUT", 930, 70, 40, 14, juce::Justification::centred);
    // info
    g.setColour (C::dim); g.setFont (font (10.0f, false));
    juce::String info = "Green = quiet parts lifted, red = loud parts pushed down.  DEPTH blends it in (100 % = full OTT).";
    if (pv (IDs::autoGain) > 0.5f) info << "   Auto gain " << juce::String (proc.agDb.load(), 1) << " dB";
    g.drawFittedText (info, juce::Rectangle<int> (712, 512, 248, 86), juce::Justification::topLeft, 6, 0.9f);
}
