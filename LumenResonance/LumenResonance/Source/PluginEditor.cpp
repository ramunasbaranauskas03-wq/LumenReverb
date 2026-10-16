#include "PluginEditor.h"

using namespace lumen::led;
using Eng = lumen::res::ResonanceEngine;

namespace
{
constexpr int W = 1120, HH = 680;
const juce::Colour MAIN = C::magenta, SENS = C::amber, OUTC = C::cyan;
const juce::Colour BANDC[6] = { C::violet, C::blue, C::green, C::amber, C::orange, C::red };

template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}
float dispFreq (int d) { return 20.0f * std::pow (1000.0f, (d + 0.5f) / (float) Eng::kDisp); }
}

//==============================================================================
ResDisplay::ResDisplay (LumenResonanceProcessor& p) : proc (p)
{
    auto mk = [this] (const juce::String& id) -> std::unique_ptr<juce::ParameterAttachment>
    {
        if (auto* prm = proc.apvts.getParameter (id)) return std::make_unique<juce::ParameterAttachment> (*prm, [this] (float) { repaint(); }, nullptr);
        return {};
    };
    for (int i = 0; i < 6; ++i) { fAtt[i] = mk (IDs::bf (i)); gAtt[i] = mk (IDs::bg (i)); }
    lowAtt = mk (IDs::lowF); highAtt = mk (IDs::highF);
    std::fill (std::begin (sIn), std::end (sIn), -120.0f); std::fill (std::begin (sOut), std::end (sOut), -120.0f);
    setTooltip ("Amber curve = SENSITIVITY: drag the 6 nodes (up = catch more there, down = leave it alone). "
                "Drag the dashed lines to set the processing range. Double-click a node to reset it. Magenta = live reduction.");
}

void ResDisplay::update()
{
    const auto& e = proc.getEngine();
    for (int d = 0; d < Eng::kDisp; ++d)
    {
        gr[d] += (e.dispGr[(size_t) d] - gr[d]) * 0.5f;
        boost[d] += (e.dispBoost[(size_t) d] - boost[d]) * 0.3f;
        grHold[d] = juce::jmax (gr[d], grHold[d] - 0.25f);
        auto sm = [] (float& s, float v) { s += (v - s) * (v > s ? 0.5f : 0.12f); };
        sm (sIn[d], e.dispIn[(size_t) d]); sm (sOut[d], e.dispOut[(size_t) d]);
    }
    repaint();
}

int ResDisplay::hit (juce::Point<float> pt) const
{
    int best = -1; float bd = 13.0f;
    for (int i = 0; i < 6; ++i)
    {
        const float d = pt.getDistanceFrom ({ fToX (pv (IDs::bf (i))), sToY (pv (IDs::bg (i))) });
        if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0) return best;
    if (std::abs (pt.x - fToX (pv (IDs::lowF))) < 6.0f && pv (IDs::lowF) > 21.0f) return 6;
    if (std::abs (pt.x - fToX (pv (IDs::highF))) < 6.0f && pv (IDs::highF) < 19900.0f) return 7;
    if (pt.y < 30.0f) { if (pt.x < 30.0f) return 6; if (pt.x > (float) getWidth() - 30.0f) return 7; }
    return -1;
}
void ResDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = hit (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 6 ? juce::MouseCursor::LeftRightResizeCursor : h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}
void ResDisplay::mouseDown (const juce::MouseEvent& e)
{
    drag = hit (e.position);
    if (drag >= 0 && drag < 6) { fAtt[drag]->beginGesture(); gAtt[drag]->beginGesture(); }
    else if (drag == 6) lowAtt->beginGesture();
    else if (drag == 7) highAtt->beginGesture();
}
void ResDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    auto setV = [this] (juce::ParameterAttachment& a, const juce::String& id, float v)
    {
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id))) a.setValueAsPartOfGesture (rp->getNormalisableRange().snapToLegalValue (v));
    };
    const float f = xToF (e.position.x);
    if (drag < 6) { setV (*fAtt[drag], IDs::bf (drag), f); setV (*gAtt[drag], IDs::bg (drag), std::round (yToS (e.position.y) * 2.0f) / 2.0f); }
    else if (drag == 6) setV (*lowAtt, IDs::lowF, juce::jmin (f, pv (IDs::highF) / 1.5f));
    else setV (*highAtt, IDs::highF, juce::jmax (f, pv (IDs::lowF) * 1.5f));
}
void ResDisplay::mouseUp (const juce::MouseEvent&)
{
    if (drag >= 0 && drag < 6) { fAtt[drag]->endGesture(); gAtt[drag]->endGesture(); }
    else if (drag == 6) lowAtt->endGesture();
    else if (drag == 7) highAtt->endGesture();
    drag = -1;
}
void ResDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hit (e.position);
    if (h >= 0 && h < 6) { gAtt[h]->setValueAsCompleteGesture (0.0f); fAtt[h]->setValueAsCompleteGesture (bandDefaults()[h]); }
    else if (h == 6) lowAtt->setValueAsCompleteGesture (20.0f);
    else if (h == 7) highAtt->setValueAsCompleteGesture (20000.0f);
}

void ResDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, MAIN);
    const float w = r.getWidth(), h = r.getHeight(), top = 20.0f, bot = h - 20.0f;
    g.setFont (font (8.5f));
    for (float f : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = fToX (f);
        g.setColour (C::dim.withAlpha (0.16f)); g.fillRect (x, top, 1.0f, bot - top);
        g.setColour (C::dim.withAlpha (0.7f));
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), (int) x + 2, (int) h - 16, 30, 12, juce::Justification::centredLeft);
    }
    // spectra: dB -100..0 over the full height
    auto dbY = [&] (float db) { return bot - juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 100.0f) * (bot - top); };
    juce::Path pin, pout, pfill;
    for (int d = 0; d < Eng::kDisp; ++d)
    {
        const float x = fToX (dispFreq (d)), yi = dbY (sIn[d]), yo = dbY (sOut[d]);
        if (d == 0) { pin.startNewSubPath (x, yi); pout.startNewSubPath (x, yo); pfill.startNewSubPath (x, bot); pfill.lineTo (x, yo); }
        else { pin.lineTo (x, yi); pout.lineTo (x, yo); pfill.lineTo (x, yo); }
    }
    pfill.lineTo (w, bot); pfill.closeSubPath();
    juce::ColourGradient og (OUTC.withAlpha (0.20f), 0, top, OUTC.withAlpha (0.02f), 0, bot, false);
    g.setGradientFill (og); g.fillPath (pfill);
    g.setColour (C::text.withAlpha (0.35f)); g.strokePath (pin, juce::PathStrokeType (1.0f));
    g.setColour (OUTC.withAlpha (0.75f)); g.strokePath (pout, juce::PathStrokeType (1.1f));

    // processing range shading
    const float lx = fToX (pv (IDs::lowF)), hx = fToX (pv (IDs::highF));
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (0.0f, top, lx, bot - top); g.fillRect (hx, top, w - hx, bot - top);
    for (int k = 0; k < 2; ++k)
    {
        const float x = k == 0 ? lx : hx;
        const bool on = hover == 6 + k || drag == 6 + k;
        juce::Path dl; dl.startNewSubPath (x, top); dl.lineTo (x, bot);
        juce::Path dashed; const float dash[] { 4.0f, 3.0f };
        juce::PathStrokeType (on ? 1.6f : 1.0f).createDashedStroke (dashed, dl, dash, 2);
        g.setColour (C::text.withAlpha (on ? 0.9f : 0.45f)); g.fillPath (dashed);
        g.setFont (font (8.5f));
        g.drawText (k == 0 ? "RANGE " + hzText (pv (IDs::lowF)) : hzText (pv (IDs::highF)), (int) x + (k == 0 ? 4 : -84), (int) top + 2, 80, 11,
                    k == 0 ? juce::Justification::centredLeft : juce::Justification::centredRight);
    }

    // reduction: hangs from the top, 0..20 dB over 45 % of the height
    const float grH = (bot - top) * 0.45f;
    juce::Path rp, rl, hl;
    rp.startNewSubPath (0, top);
    for (int d = 0; d < Eng::kDisp; ++d)
    {
        const float x = fToX (dispFreq (d)), y = top + juce::jlimit (0.0f, 1.0f, gr[d] / 20.0f) * grH, yh = top + juce::jlimit (0.0f, 1.0f, grHold[d] / 20.0f) * grH;
        rp.lineTo (x, y);
        if (d == 0) { rl.startNewSubPath (x, y); hl.startNewSubPath (x, yh); } else { rl.lineTo (x, y); hl.lineTo (x, yh); }
    }
    rp.lineTo (w, top); rp.closeSubPath();
    juce::ColourGradient rg (MAIN.withAlpha (0.55f), 0, top, MAIN.withAlpha (0.12f), 0, top + grH, false);
    g.setGradientFill (rg); g.fillPath (rp);
    g.setColour (MAIN.withAlpha (0.35f)); g.strokePath (hl, juce::PathStrokeType (1.0f));
    glowPath (g, rl, MAIN, 1.6f, 0.9f);
    if (pv (IDs::recover) > 0.05f)
    {
        // RECOVER boost: green, rising from the bottom (0..6 dB over 25 % of the height)
        juce::Path bp, bl; const float bh = (bot - top) * 0.25f;
        bp.startNewSubPath (0, bot);
        for (int d = 0; d < Eng::kDisp; ++d)
        {
            const float x = fToX (dispFreq (d)), y = bot - juce::jlimit (0.0f, 1.0f, boost[d] / 6.0f) * bh;
            bp.lineTo (x, y); if (d == 0) bl.startNewSubPath (x, y); else bl.lineTo (x, y);
        }
        bp.lineTo (w, bot); bp.closeSubPath();
        juce::ColourGradient bg (C::green.withAlpha (0.08f), 0, bot - bh, C::green.withAlpha (0.4f), 0, bot, false);
        g.setGradientFill (bg); g.fillPath (bp);
        glowPath (g, bl, C::green, 1.3f, 0.7f);
    }

    // sensitivity curve
    lumen::res::Params p = proc.readParams();
    juce::Path sc;
    for (int px = 0; px <= (int) w; px += 2)
    {
        const float f = xToF ((float) px), y = sToY (juce::jlimit (-24.0f, 12.0f, lumen::res::sensDb (p, f)));
        if (px == 0) sc.startNewSubPath ((float) px, y); else sc.lineTo ((float) px, y);
    }
    g.setColour (SENS.withAlpha (0.25f)); g.fillRect (0.0f, sToY (0.0f), w, 1.0f);
    glowPath (g, sc, SENS, 1.6f, 0.8f);
    g.setFont (font (8.0f)); g.setColour (SENS.withAlpha (0.7f));
    g.drawText ("SENS 0", 4, (int) sToY (0.0f) - 11, 50, 10, juce::Justification::centredLeft);
    for (int i = 0; i < 6; ++i)
    {
        const juce::Point<float> c { fToX (p.band[i].freq), sToY (p.band[i].gainDb) };
        const bool on = hover == i || drag == i;
        const auto col = BANDC[i];
        g.setColour (col.withAlpha (on ? 0.35f : 0.15f)); g.fillEllipse (c.x - 12, c.y - 12, 24, 24);
        g.setColour (juce::Colour (0xff04060a)); g.fillEllipse (c.x - 7, c.y - 7, 14, 14);
        g.setColour (col); g.drawEllipse (c.x - 7, c.y - 7, 14, 14, on ? 2.2f : 1.4f);
        g.setFont (font (8.5f)); g.setColour (col.interpolatedWith (juce::Colours::white, 0.3f));
        g.drawText (i == 0 ? "LS" : i == 5 ? "HS" : juce::String (i + 1), juce::Rectangle<float> (c.x - 7, c.y - 7, 14, 14), juce::Justification::centred);
        if (on)
        {
            auto lb = juce::Rectangle<float> (c.x - 50, c.y + 14, 100, 16);
            g.setColour (juce::Colour (0xd004060a)); g.fillRoundedRectangle (lb, 3.0f);
            g.setColour (C::text); g.setFont (mono (9.5f));
            g.drawText (hzText (p.band[i].freq) + "  " + (p.band[i].gainDb >= 0 ? "+" : "") + juce::String (p.band[i].gainDb, 1), lb, juce::Justification::centred);
        }
    }
    g.setFont (font (8.5f));
    g.setColour (C::text.withAlpha (0.5f)); g.drawText ("IN", 8, (int) h - 30, 20, 11, juce::Justification::centredLeft);
    g.setColour (OUTC); g.drawText ("OUT", 26, (int) h - 30, 30, 11, juce::Justification::centredLeft);
    g.setColour (MAIN); g.drawText ("REDUCTION", 56, (int) h - 30, 70, 11, juce::Justification::centredLeft);
    g.setColour (SENS); g.drawText ("SENSITIVITY", 126, (int) h - 30, 80, 11, juce::Justification::centredLeft);
    if (pv (IDs::recover) > 0.05f) { g.setColour (C::green); g.drawText ("RECOVER", 206, (int) h - 30, 60, 11, juce::Justification::centredLeft); }
    g.setColour (MAIN.withAlpha (0.6f));
    for (float db : { 6.0f, 12.0f, 18.0f }) g.drawText ("-" + juce::String ((int) db), (int) w - 30, (int) (top + db / 20.0f * grH) - 6, 26, 11, juce::Justification::centredRight);
}

//==============================================================================
void GrMap::push (const float* gr, float depth)
{
    if (getWidth() <= 0) return;
    if (img.getWidth() != getWidth() || img.getHeight() != getHeight()) { img = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true); img.clear (img.getBounds(), juce::Colour (0xff020306)); }
    img.moveImageSection (0, 0, 1, 0, img.getWidth() - 1, img.getHeight());
    const int x = img.getWidth() - 1, hgt = img.getHeight();
    const float sc = 1.0f / juce::jmax (3.0f, depth);
    for (int y = 0; y < hgt; ++y)
    {
        const float f = 20.0f * std::pow (1000.0f, 1.0f - (y + 0.5f) / (float) hgt);
        const int d = juce::jlimit (0, Eng::kDisp - 1, (int) (std::log10 (f / 20.0f) / 3.0f * Eng::kDisp));
        const float v = juce::jlimit (0.0f, 1.0f, gr[d] * sc);
        const juce::Colour c = v < 0.5f ? juce::Colour (0xff020306).interpolatedWith (C::violet, v * 2.0f) : C::violet.interpolatedWith (C::magenta.interpolatedWith (juce::Colours::white, 0.3f), (v - 0.5f) * 2.0f);
        img.setPixelAt (x, y, c);
    }
    repaint();
}
void GrMap::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff020306)); g.fillRoundedRectangle (r, 4.0f);
    if (img.isValid()) g.drawImageAt (img, 0, 0);
    g.setColour (C::border); g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setFont (font (8.0f)); g.setColour (C::dim);
    g.drawText ("20k", 4, 2, 30, 10, juce::Justification::centredLeft);
    g.drawText ("20", 4, getHeight() - 12, 30, 10, juce::Justification::centredLeft);
}

//==============================================================================
LumenResonanceEditor::LumenResonanceEditor (LumenResonanceProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    display = std::make_unique<ResDisplay> (proc); addAndMakeVisible (*display); display->setBounds (26, 96, 780, 338);
    grmap = std::make_unique<GrMap>(); addAndMakeVisible (*grmap); grmap->setBounds (26, 466, 780, 56);

    struct K { const char* id; const char* lb; juce::Colour c; int x, y; const char* tip; };
    const K ks[] {
        { "depth", "DEPTH", MAIN, 836, 92, "Maximum reduction and overall strength" },
        { "recover", "RECOVER", C::green, 920, 92, "RECOVER: gently lifts masked dips (up to +6 dB) back towards the spectral envelope - suppress AND rebalance in one" },
        { "tone_keep", "TONE KEEP", C::cyan, 1004, 92, "TONE KEEP: tracks the note (f0) and protects the fundamental + first harmonics - the sound keeps its body and pitch" },
        { "sharpness", "SHARPNESS", MAIN, 836, 196, "Low = broad, smooth dips (tonal balance).  High = narrow notches on single resonances" },
        { "selectivity", "SELECTIVITY", MAIN, 920, 196, "Low = reduce everything sticking out a little (spectral compression).  High = only the strongest peaks" },
        { "resonance_focus", "FOCUS", C::amber, 1004, 196, "RESONANCE FOCUS: 0% reacts to every peak, 100% only to peaks that keep sticking out (room modes, ringing, whistle) - musical peaks pass" },
        { "attack", "ATTACK", C::violet, 836, 300, "How fast a resonance is caught" },
        { "release", "RELEASE", C::violet, 920, 300, "How fast the reduction lets go" },
        { "transient_protect", "PROTECT", C::green, 1004, 300, "TRANSIENT PROTECT: lets attacks pass untouched so drums, plucks and consonants keep their punch" },
    };
    for (auto& k : ks) { auto* kn = add<Knob> (*this, owned, st, k.id, k.lb, k.c); kn->setBounds (k.x, k.y, 84, 98); kn->slider.setTooltip (k.tip); }
    auto* md = add<Combo> (*this, owned, st, IDs::mode, MAIN); md->setBounds (836, 404, 108, 24);
    md->setTooltip ("Soft: gentle knee, very natural.  Hard: stronger, more immediate reduction");
    auto* rs = add<Combo> (*this, owned, st, IDs::res, MAIN); rs->setBounds (950, 404, 140, 24);
    rs->setTooltip ("Low Latency (21 ms) for tracking, Standard (43 ms), High Res (85 ms) for bass and mastering - sharper in the lows");
    auto* er = add<Toggle> (*this, owned, st, IDs::ear, "PERCEPTUAL (EAR WEIGHTED)", C::amber); er->setBounds (836, 434, 254, 24);
    er->setTooltip ("Detection follows the ear's sensitivity: harshness at 2-5 kHz counts more, deep lows less");

    auto* stc = add<Combo> (*this, owned, st, IDs::stereo, C::cyan); stc->setBounds (30, 574, 140, 24);
    stc->setTooltip ("Linked: same reduction on both channels.  Mid/Side: separate processing with its own amounts.  L/R: each channel on its own");
    msKnobs[0] = add<Knob> (*this, owned, st, IDs::midAmt, "MID AMT", C::cyan); msKnobs[0]->setBounds (176, 548, 76, 92);
    msKnobs[1] = add<Knob> (*this, owned, st, IDs::sideAmt, "SIDE AMT", C::cyan); msKnobs[1]->setBounds (252, 548, 76, 92);
    auto* kc = add<Combo> (*this, owned, st, IDs::sc, C::amber); kc->setBounds (30, 618, 140, 24);
    kc->setTooltip ("External Sidechain: the KEY decides what is reduced - e.g. vocal on the key, music on the input = the music makes room exactly where the vocal is");
    auto* dt = add<Toggle> (*this, owned, st, IDs::delta, "DELTA", C::orange); dt->setBounds (346, 556, 108, 24);
    dt->setTooltip ("Listen only to what is being removed");
    auto* ag = add<Toggle> (*this, owned, st, IDs::autoGain, "AUTO GAIN", C::green); ag->setBounds (346, 586, 108, 24);
    ag->setTooltip ("Makes up the level that the reduction takes away - compare fairly");
    add<Knob> (*this, owned, st, IDs::mix, "MIX", MAIN)->setBounds (462, 548, 76, 92);
    add<Knob> (*this, owned, st, IDs::out, "OUTPUT", MAIN, true)->setBounds (538, 548, 76, 92);

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
    copyB.setTooltip ("Copy the current setting to the other slot");
    copyB.setColour (juce::TextButton::buttonColourId, C::dim);

    srcBox.addItemList ({ "Vocal", "Instrument", "Drums", "Bass", "Mix / Master" }, 1);
    srcBox.setSelectedId (1, juce::dontSendNotification);
    srcBox.setColour (juce::ComboBox::textColourId, C::green);
    addAndMakeVisible (srcBox); srcBox.setBounds (650, 556, 116, 26);
    learnB.setColour (juce::TextButton::buttonColourId, C::green);
    learnB.setTooltip ("Play the track: SMART LEARN listens 5 s, finds where the resonances are and how narrow they are, "
                       "then sets depth, sharpness, range, timing and the sensitivity nodes for the chosen source");
    learnB.onClick = [this] { proc.startLearn ((LumenResonanceProcessor::Source) (srcBox.getSelectedId() - 1)); };
    addAndMakeVisible (learnB); learnB.setBounds (772, 555, 110, 28);

    setSize (W, HH);
    startTimerHz (30);
}

LumenResonanceEditor::~LumenResonanceEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenResonanceEditor::timerCallback()
{
    proc.finishLearnIfReady();
    if (proc.programVersion.load() != lastProgram) { lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    const auto& e = proc.getEngine();
    const int fc = e.frameCounter.load (std::memory_order_acquire);
    if (fc != lastFrame)
    {
        lastFrame = fc;
        display->update();
        grmap->push (display->gr, pv (IDs::depth));
    }
    inPk = juce::jmax (proc.inPeak.load(), inPk * 0.85f); outPk = juce::jmax (proc.outPeak.load(), outPk * 0.85f);
    avgRed += (e.avgReduction() - avgRed) * 0.2f;
    const bool ms = (int) pv (IDs::stereo) == 1;
    for (auto* k : msKnobs) k->setAlpha (ms ? 1.0f : 0.35f);
    aB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 0 ? MAIN : C::dim);
    bB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 1 ? MAIN : C::dim);
    learnB.setEnabled (! proc.isLearning());
    repaint();
}

void LumenResonanceEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (C::magenta.withAlpha (0.0f), 0, 64, C::amber.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.25, C::magenta.withAlpha (0.75f)); line.addColour (0.75, C::amber.withAlpha (0.75f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (MAIN.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (C::magenta, 20, 0, C::amber, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 146.0f, 18.0f, 220.0f, 28.0f }, "RESONANCE", MAIN, 1.85f, juce::Justification::centredLeft, false, 10.5f);
    }
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("IN", 852, 14, 24, 10, juce::Justification::centredLeft);
    g.drawText ("OUT", 852, 36, 24, 10, juce::Justification::centredLeft);
    ledMeter (g, { 878.0f, 13.0f, 226.0f, 9.0f }, inPk, inPk, false, 30);
    ledMeter (g, { 878.0f, 35.0f, 226.0f, 9.0f }, outPk, outPk, false, 30);

    panel (g, { 16.0f, 72.0f, 800.0f, 372.0f }, "RESONANCE MAP", MAIN);
    g.setFont (font (9.0f));
    g.setColour (C::dim);
    g.drawText ("latency " + juce::String (proc.getLatencySamples()) + " smp / " + juce::String (proc.getLatencySamples() / juce::jmax (1.0, proc.getEngine().getFs()) * 1000.0, 1) + " ms",
                560, 77, 246, 16, juce::Justification::centredRight);
    if (pv (IDs::sc) > 0.5f)
    {
        g.setColour (proc.scConnected.load() ? C::amber : C::red);
        g.drawText (proc.scConnected.load() ? "KEY: EXTERNAL" : "KEY: NO SIDECHAIN CONNECTED", 330, 77, 220, 16, juce::Justification::centredRight);
    }
    panel (g, { 16.0f, 450.0f, 800.0f, 80.0f }, "REDUCTION HISTORY", MAIN);

    panel (g, { 824.0f, 72.0f, 280.0f, 458.0f }, "SUPPRESSOR", MAIN);
    {
        auto lr = juce::Rectangle<float> (836.0f, 464.0f, 256.0f, 58.0f);
        lcd (g, lr, MAIN);
        g.setFont (font (8.5f)); g.setColour (C::dim);
        g.drawText ("LEVEL CHANGE", (int) lr.getX() + 8, (int) lr.getY() + 4, 140, 10, juce::Justification::centredLeft);
        grNum.draw (g, { lr.getX() + 6, lr.getY() + 16, 130, 28 }, (avgRed > 0.05f ? "-" : "+") + juce::String (std::abs (avgRed), 1) + " DB", MAIN, 2.0f, juce::Justification::centredLeft, false, 11.0f);
        const float f0 = proc.getEngine().lastF0.load(), cf = proc.getEngine().lastConf.load();
        g.setFont (font (8.5f)); g.setColour (C::dim);
        g.drawText ("NOTE f0", (int) lr.getX() + 150, (int) lr.getY() + 4, 60, 10, juce::Justification::centredLeft);
        g.setFont (mono (11.0f)); g.setColour (cf > 0.2f && pv (IDs::toneKeep) > 0.01f ? C::cyan : C::dim);
        g.drawText (cf > 0.2f ? hzText (f0) : juce::String ("--"), (int) lr.getX() + 150, (int) lr.getY() + 18, 100, 16, juce::Justification::centredLeft);
        if (pv (IDs::autoGain) > 0.5f)
        {
            g.setFont (mono (10.0f)); g.setColour (C::green);
            g.drawText ("AUTO +" + juce::String (proc.getEngine().autoGainDb(), 1) + " dB", (int) lr.getX() + 150, (int) lr.getY() + 38, 100, 14, juce::Justification::centredLeft);
        }
        if (pv (IDs::delta) > 0.5f) { g.setFont (font (10.0f)); g.setColour (C::orange); g.drawText ("DELTA", (int) lr.getX() + 8, (int) lr.getY() + 44, 58, 12, juce::Justification::centredLeft); }
    }

    panel (g, { 16.0f, 536.0f, 1088.0f, 132.0f }, "STEREO / KEY / OUTPUT", MAIN);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("STEREO", 30, 560, 100, 12, juce::Justification::centredLeft);
    g.drawText ("KEY", 30, 604, 100, 12, juce::Justification::centredLeft);
    g.setColour (C::border); g.fillRect (632, 548, 1, 108);
    g.setFont (font (11.0f)); g.setColour (C::green.interpolatedWith (juce::Colours::white, 0.3f));
    g.drawText ("SMART LEARN", 650, 540, 160, 14, juce::Justification::centredLeft);
    auto sr = juce::Rectangle<float> (890.0f, 555.0f, 202.0f, 28.0f);
    lcd (g, sr, C::green);
    if (proc.isLearning())
    {
        const float pr = proc.learnProgress();
        for (int k = 0; k < 21; ++k) drawLed (g, { sr.getX() + 9.0f + k * 9.2f, sr.getCentreY() }, 2.6f, C::green, pr * 21.0f > (float) k ? 1.0f : 0.1f);
    }
    else
        status.draw (g, sr.reduced (4.0f, 2.0f), proc.getLearnResult().isEmpty() ? juce::String ("READY") : juce::String ("APPLIED"), C::green, 2.0f, juce::Justification::centred, false, 10.0f);
    g.setFont (font (9.5f));
    const bool hasRes = proc.getLearnResult().isNotEmpty() && ! proc.isLearning();
    g.setColour (hasRes ? C::green.withAlpha (0.85f) : C::dim);
    g.drawFittedText (hasRes ? proc.getLearnResult() : juce::String ("Pick the source, press SMART LEARN and play 5 s. It finds the resonant zones and places the sensitivity nodes on them."),
                      juce::Rectangle<int> (650, 592, 442, 54), juce::Justification::topLeft, 3);
}
