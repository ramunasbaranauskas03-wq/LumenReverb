#include "PluginEditor.h"
#include <complex>

using namespace lumen::led;

namespace
{
constexpr int W = 900, HH = 560;
const juce::Colour FC = C::amber, RC = C::magenta, LC = C::blue, EC = C::green, KC = C::cyan;

template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}

float magDb (int type, float f, float fc, float res)
{
    using cd = std::complex<double>;
    const double w = f / fc;
    const cd s (0.0, w);
    const double r = juce::jlimit (0.0f, 1.0f, res);
    const double ks = 2.0 - 1.96 * std::pow (r, 0.7);
    const double k = 3.95 * r * (0.75 + 0.25 * r);
    const cd den = s * s + ks * s + 1.0;
    cd H;
    switch (type)
    {
        case 0:  H = (1.0 + 0.55 * k) / (std::pow (1.0 + s, 4.0) + k); break;
        case 1:  H = 1.0 / den; break;
        case 2:  H = 1.0 / (den * den); break;
        case 3:  H = s * s / den; break;
        case 4:  H = (s * s / den) * (s * s / den); break;
        case 5:  H = ks * s / den; break;
        case 6:  H = (s * s + 1.0) / den; break;
        case 7: case 8:
        {
            const double g = 0.15 + 0.83 * std::sqrt (r);
            const cd e = std::exp (cd (0.0, -2.0 * 3.141592653589793 * (double) f / std::max (25.0, (double) fc)));
            H = (type == 7 ? 1.0 / (1.0 - g * e) : 1.0 / (1.0 + g * e)) * std::sqrt (1.0 - g * g) * 1.4;
            break;
        }
        case 9:
        {
            static const double F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2100, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
            const double pos = juce::jlimit (0.0, 1.0, std::log2 (fc / 20.0) / std::log2 (1000.0)) * 4.0;
            const int a = std::min (3, (int) pos); const double fr = pos - a;
            const double q = 4.0 + 16.0 * r;
            H = 0.0;
            for (int i = 0; i < 3; ++i)
            {
                const double fi = F[a][i] + (F[a + 1][i] - F[a][i]) * fr;
                const cd si (0.0, f / fi);
                H += (si / q) / (si * si + si / q + 1.0) * (i == 0 ? 1.0 : i == 1 ? 0.7 : 0.45) * 1.8;
            }
            break;
        }
        default:
        {
            static const double spread[4] = { 0.55, 0.85, 1.3, 2.1 };
            cd ap = 1.0;
            for (double sp : spread) { const cd si (0.0, f / (fc * sp)); ap *= (1.0 - si) / (1.0 + si); }
            H = 0.6 * (1.0 + ap / (1.0 - 0.92 * r * ap));
            break;
        }
    }
    return (float) (20.0 * std::log10 (std::abs (H) + 1e-9));
}
juce::String noteName (float hz)
{
    const float midi = 69.0f + 12.0f * std::log2 (std::max (hz, 1.0f) / 440.0f);
    const int n = (int) std::lround (midi);
    const int cents = (int) std::lround ((midi - n) * 100.0f);
    return noteNames()[(n % 12 + 12) % 12] + juce::String (n / 12 - 1) + (cents == 0 ? juce::String() : (cents > 0 ? " +" : " ") + juce::String (cents));
}
}

LumenFilterEditor::LumenFilterEditor (LumenFilterProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    add<Combo> (*this, owned, st, IDs::type, FC)->setBounds (396, 76, 170, 22);
    const char* ids[] = { "cutoff", "res", "drive", "spread", "mix", "out" };
    const char* lbl[] = { "CUTOFF", "RESONANCE", "DRIVE", "SPREAD", "MIX", "OUTPUT" };
    for (int i = 0; i < 6; ++i) add<Knob> (*this, owned, st, ids[i], lbl[i], FC, i == 3)->setBounds (26 + i * 90, 246, 90, 82);

    add<Knob> (*this, owned, st, IDs::rise, "", RC)->setBounds (624, 98, 220, 190);
    add<Combo> (*this, owned, st, IDs::riseMode, RC)->setBounds (660, 296, 148, 24);

    add<Toggle> (*this, owned, st, IDs::lfoOn, "", LC, true)->setBounds (22, 345, 20, 20);
    add<Combo> (*this, owned, st, IDs::lfoShape, LC)->setBounds (110, 346, 110, 20);
    add<Toggle> (*this, owned, st, IDs::lfoSync, "SYNC", LC)->setBounds (226, 345, 66, 22);
    rateK = add<Knob> (*this, owned, st, IDs::lfoRate, "RATE", LC); rateK->setBounds (272, 372, 58, 80);
    divC = add<Combo> (*this, owned, st, IDs::lfoDiv, LC); divC->setBounds (296, 346, 76, 20);
    add<Knob> (*this, owned, st, IDs::lfoDepth, "DEPTH", LC, true)->setBounds (330, 372, 58, 80);
    add<Knob> (*this, owned, st, IDs::lfoStereo, "STEREO", LC)->setBounds (386, 372, 58, 80);

    add<Toggle> (*this, owned, st, IDs::envOn, "", EC, true)->setBounds (460, 345, 20, 20);
    add<Combo> (*this, owned, st, IDs::envSrc, EC)->setBounds (590, 346, 106, 20);
    add<Knob> (*this, owned, st, IDs::envAmt, "AMOUNT", EC, true)->setBounds (462, 372, 76, 86);
    add<Knob> (*this, owned, st, IDs::envAtt, "ATTACK", EC)->setBounds (540, 372, 76, 86);
    add<Knob> (*this, owned, st, IDs::envRel, "RELEASE", EC)->setBounds (618, 372, 76, 86);

    add<Combo> (*this, owned, st, IDs::keyMode, KC)->setBounds (722, 372, 152, 22);
    add<Combo> (*this, owned, st, IDs::root, KC)->setBounds (722, 400, 152, 22);
    add<Combo> (*this, owned, st, IDs::os, C::amber)->setBounds (742, 22, 60, 22);

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setColour (juce::ComboBox::textColourId, FC);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox);
    for (auto* b : { &prevB, &nextB }) { b->setColour (juce::TextButton::buttonColourId, FC); addAndMakeVisible (*b); }
    prevB.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };

    setSize (W, HH);
    startTimerHz (30);
}

LumenFilterEditor::~LumenFilterEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenFilterEditor::resized()
{
    prevB.setBounds (300, 18, 28, 28);
    presetBox.setBounds (334, 18, 250, 28);
    nextB.setBounds (590, 18, 28, 28);
}

void LumenFilterEditor::timerCallback()
{
    if (proc.programVersion.load() != lastProgram) { lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    outM = juce::jmax (proc.outPeak.load(), outM * 0.8f);
    inM = juce::jmax (proc.inPeak.load(), inM * 0.8f);
    lfoTrail[trailPos] = proc.lfoVal.load(); trailPos = (trailPos + 1) % 64;
    const bool sync = pv (IDs::lfoSync) > 0.5f;
    rateK->setVisible (! sync); divC->setVisible (sync);
    repaint();
}

void LumenFilterEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());

    // header
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (FC.withAlpha (0.0f), 0, 64, RC.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.3, FC.withAlpha (0.7f)); line.addColour (0.7, RC.withAlpha (0.7f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (FC.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (FC, 20, 0, RC, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 148.0f, 18.0f, 140.0f, 28.0f }, "FILTER", FC, 2.4f, juce::Justification::centredLeft, false, 10.5f);
    }
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("QUALITY", 742, 8, 60, 12, juce::Justification::centred);
    g.drawText ("IN / OUT", 812, 8, 76, 12, juce::Justification::centredLeft);
    ledMeter (g, { 812.0f, 24.0f, 76.0f, 8.0f }, inM, inM, false, 14);
    ledMeter (g, { 812.0f, 36.0f, 76.0f, 8.0f }, outM, outM, false, 14);

    // ---- filter panel ----
    panel (g, { 16.0f, 72.0f, 560.0f, 262.0f }, "FILTER", FC);
    auto a = juce::Rectangle<float> (26.0f, 104.0f, 540.0f, 136.0f);
    lcd (g, a, FC);
    auto pa = a.reduced (8.0f, 8.0f);
    dotGrid (g, pa, 6.0f, FC, 0.08f);
    auto xOf = [&] (float f) { return pa.getX() + pa.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); };
    auto yOf = [&] (float db) { return pa.getY() + pa.getHeight() * (1.0f - (juce::jlimit (-42.0f, 24.0f, db) + 42.0f) / 66.0f); };
    g.setFont (font (8.5f));
    for (float f : { 100.0f, 1000.0f, 10000.0f }) { g.setColour (FC.withAlpha (0.12f)); g.drawVerticalLine ((int) xOf (f), pa.getY(), pa.getBottom()); }
    const int type = (int) pv (IDs::type);
    const float res = juce::jlimit (0.0f, 1.0f, pv (IDs::res) + 0.3f * pv (IDs::rise));
    const float cL = proc.cutL.load(), cR = proc.cutR.load();
    const float rise = pv (IDs::rise); const int rmode = (int) pv (IDs::riseMode);
    auto curve = [&] (float fc, bool withRise)
    {
        juce::Path p;
        for (int i = 0; i <= 220; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / 220.0f);
            float db = magDb (type, f, fc, res);
            db = 20.0f * std::log10 (pv (IDs::mix) * std::pow (10.0f, db / 20.0f) + (1.0f - pv (IDs::mix)) + 1e-9f);
            if (withRise && rise > 0.001f)
            {
                const float w = juce::jlimit (0.0f, 1.0f, rise * 8.0f);
                float rdb = 0.0f;
                if (rmode != 1) rdb += magDb (4, f, proc.riseHp.load(), 0.15f + 0.45f * rise);
                if (rmode != 0) rdb += magDb (0, f, proc.riseLp.load(), 0.1f + 0.45f * rise);
                db += 20.0f * std::log10 (w * std::pow (10.0f, rdb / 20.0f) + (1.0f - w) + 1e-9f);
            }
            const float x = pa.getX() + pa.getWidth() * (float) i / 220.0f;
            if (i == 0) p.startNewSubPath (x, yOf (db)); else p.lineTo (x, yOf (db));
        }
        return p;
    };
    auto main = curve (cL, true);
    juce::Path fill (main); fill.lineTo (pa.getRight(), pa.getBottom()); fill.lineTo (pa.getX(), pa.getBottom()); fill.closeSubPath();
    juce::ColourGradient gr (FC.withAlpha (0.28f), 0, pa.getY(), FC.withAlpha (0.0f), 0, pa.getBottom(), false);
    g.setGradientFill (gr); g.fillPath (fill);
    if (std::abs (cL - cR) > 1.0f) glowPath (g, curve (cR, true), C::orange, 1.0f, 0.6f);
    glowPath (g, main, FC, 1.7f);
    drawLed (g, { xOf (cL), yOf (magDb (type, cL, cL, res)) }, 4.0f, juce::Colours::white.interpolatedWith (FC, 0.3f), 1.0f);
    if (std::abs (cL - cR) > 1.0f) drawLed (g, { xOf (cR), yOf (magDb (type, cR, cR, res)) }, 3.2f, C::orange, 1.0f);
    g.setFont (font (10.0f)); g.setColour (FC.withAlpha (0.85f));
    const juce::String hzTxt = cL >= 1000.0f ? juce::String (cL / 1000.0f, 2) + " kHz" : juce::String ((int) std::lround (cL)) + " Hz";
    g.drawText (hzTxt + "   " + noteName (cL), a.reduced (10.0f, 6.0f), juce::Justification::topRight);

    // ---- RISE ----
    panel (g, { 584.0f, 72.0f, 300.0f, 262.0f }, "RISE  -  BUILD-UP MACRO", RC, rise > 0.001f);
    {
                juce::String big = juce::String ((int) std::round (rise * 100.0f)) + "%";
        g.setFont (font (22.0f)); g.setColour (RC.interpolatedWith (juce::Colours::white, 0.2f + 0.5f * rise));
        g.drawText (big, juce::Rectangle<float> (594.0f, 296.0f, 62.0f, 24.0f), juce::Justification::centred);
        for (int k = 0; k < 24; ++k) drawLed (g, { 600.0f + k * 11.6f, 326.0f }, 2.4f, k > 18 ? C::red : RC, rise * 24.0f > (float) k ? 1.0f : 0.06f);
        g.setFont (font (9.0f)); g.setColour (C::dim);
        g.drawText (rmode == 0 ? "HP sweeps up, resonance + LFO speed rise" : rmode == 1 ? "LP closes down, resonance + LFO speed rise" : "HP + LP squeeze to a telephone band",
                    590, 98, 288, 12, juce::Justification::centred);
    }

    // ---- LFO ----
    panel (g, { 16.0f, 342.0f, 430.0f, 202.0f }, "LFO", LC, pv (IDs::lfoOn) > 0.5f);
    {
        auto d = juce::Rectangle<float> (26.0f, 372.0f, 240.0f, 162.0f);
        lcd (g, d, LC);
        auto da = d.reduced (10.0f, 14.0f);
        dotGrid (g, da, 6.0f, LC, 0.08f);
        g.setColour (LC.withAlpha (0.15f)); g.drawHorizontalLine ((int) da.getCentreY(), da.getX(), da.getRight());
        juce::Path p;
        const int sh = (int) pv (IDs::lfoShape);
        for (int i = 0; i <= 120; ++i)
        {
            const float ph = (float) i / 120.0f * 2.0f, fr = ph - std::floor (ph);
            const float v = lumen::flt::lfoShape (sh, juce::jmin (fr, 0.9999f), 0.2f, -0.6f);
            const float x = da.getX() + da.getWidth() * (float) i / 120.0f, y = da.getCentreY() - v * da.getHeight() * 0.45f * std::abs (pv (IDs::lfoDepth) * 0.6f + 0.4f);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        glowPath (g, p, LC, 1.5f, pv (IDs::lfoOn) > 0.5f ? 1.0f : 0.35f);
        // live value trail
        for (int k = 0; k < 64; ++k)
        {
            const float v = lfoTrail[(trailPos + k) % 64];
            drawLed (g, { da.getRight() - 2.0f - (63 - k) * 1.2f, da.getCentreY() - v * da.getHeight() * 0.45f }, 1.4f, juce::Colours::white.interpolatedWith (LC, 0.5f), pv (IDs::lfoOn) > 0.5f ? (float) k / 64.0f : 0.0f);
        }
    }
    // ---- ENVELOPE ----
    panel (g, { 454.0f, 342.0f, 250.0f, 202.0f }, "ENVELOPE", EC, pv (IDs::envOn) > 0.5f);
    {
        const float e = proc.envVal.load();
        for (int k = 0; k < 20; ++k) drawLed (g, { 470.0f + k * 11.5f, 480.0f }, 2.6f, EC, pv (IDs::envOn) > 0.5f && e * 20.0f > (float) k ? 1.0f : 0.06f);
        g.setFont (font (9.0f)); g.setColour (C::dim);
        g.drawText ("INPUT LEVEL -> CUTOFF", 462, 490, 232, 12, juce::Justification::centred);
        g.drawFittedText ("Sidechain: route your kick to Lumen Filter's sidechain input for a filter duck.", juce::Rectangle<int> (462, 506, 232, 30), juce::Justification::centred, 2);
    }
    // ---- KEY LOCK ----
    panel (g, { 712.0f, 342.0f, 172.0f, 202.0f }, "KEY LOCK", KC, (int) pv (IDs::keyMode) > 0);
    {
        noteMatrix.draw (g, { 722.0f, 432.0f, 152.0f, 34.0f }, noteName (cL).upToFirstOccurrenceOf (" ", false, false), KC, 3.2f, juce::Justification::centred, true, 10.6f);
        const float midi = 69.0f + 12.0f * std::log2 (std::max (cL, 1.0f) / 440.0f);
        const int pc = ((int) std::lround (midi) % 12 + 12) % 12;
        static const bool major[12] = { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 }, minor[12] = { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0 };
        const int km = (int) pv (IDs::keyMode), root = (int) pv (IDs::root);
        for (int k = 0; k < 12; ++k)
        {
            const bool inScale = km <= 1 || (km == 2 ? major : minor)[((k - root) % 12 + 12) % 12];
            drawLed (g, { 728.0f + k * 12.6f, 486.0f }, 2.8f, k == pc ? juce::Colours::white.interpolatedWith (KC, 0.3f) : KC, k == pc ? 1.0f : (inScale && km > 0 ? 0.3f : 0.05f));
            g.setFont (font (7.5f)); g.setColour (C::dim);
            g.drawText (noteNames()[k].substring (0, 2), (int) (722.0f + k * 12.6f), 494, 13, 10, juce::Justification::centred);
        }
        g.setFont (font (9.0f)); g.setColour (C::dim);
        g.drawFittedText ("Snaps the cutoff to notes so resonance sings in key", juce::Rectangle<int> (720, 510, 156, 26), juce::Justification::centred, 2);
    }
}
