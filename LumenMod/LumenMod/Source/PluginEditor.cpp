#include "PluginEditor.h"

using namespace lumen::led;

namespace
{
constexpr int W = 1000, HH = 612;
const juce::Colour MC[7] = { C::cyan, C::magenta, C::violet, C::amber, C::orange, C::green, C::blue };
const juce::Colour VC[8] = { C::cyan, C::magenta, C::amber, C::green, C::violet, C::orange, C::blue, C::red };

template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}
const char* modeHint (int m)
{
    static const char* h[] { "Modern multi-voice chorus: thick, wide, clean.", "BBD chorus of the classic 80s poly synth: warm, lush, a little dark.",
                             "Rack 'dimension' widener: subtle, natural, mix-friendly.", "3-phase string ensemble: shimmering pads and strings.",
                             "Feedback flanger: jet sweeps, metallic swooshes, risers.", "Allpass phaser, 4-16 stages: swirl and motion.",
                             "Pitch vibrato and rotary speaker: wobble, tape wow, Leslie." };
    return h[juce::jlimit (0, 6, m)];
}
}

LumenModEditor::LumenModEditor (LumenModProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    const auto names = modeNames();
    for (int m = 0; m < 7; ++m)
    {
        modeB[m].setButtonText (names[m].toUpperCase());
        modeB[m].onClick = [this, m] { proc.setChoice (IDs::mode, m); };
        addAndMakeVisible (modeB[m]);
        modeB[m].setBounds (26 + m * 137, 82, 130, 34);
    }
    for (int v = 0; v < 4; ++v)
    {
        varB[v].onClick = [this, v] { proc.setChoice (IDs::variant, v); };
        addAndMakeVisible (varB[v]);
        varB[v].setBounds (30 + v * 136, 318, 128, 26);
    }

    // main controls
    auto* rate = add<Knob> (*this, owned, st, IDs::rate, "RATE", C::text); rate->setBounds (596, 158, 92, 104);
    add<Toggle> (*this, owned, st, IDs::sync, "SYNC", C::green)->setBounds (600, 266, 84, 24);
    add<Combo> (*this, owned, st, IDs::div, C::green)->setBounds (600, 296, 84, 24);
    add<Knob> (*this, owned, st, IDs::depth, "DEPTH", C::text)->setBounds (690, 158, 92, 104);
    add<Knob> (*this, owned, st, IDs::delay, "DELAY / FREQ", C::text)->setBounds (782, 158, 96, 104);
    add<Knob> (*this, owned, st, IDs::feedback, "FEEDBACK", C::text, true)->setBounds (880, 158, 96, 104);
    voicesKnob = add<Knob> (*this, owned, st, IDs::voices, "VOICES", C::text); voicesKnob->setBounds (690, 262, 92, 92);
    add<Knob> (*this, owned, st, IDs::width, "WIDTH", C::text)->setBounds (782, 262, 96, 92);
    add<Knob> (*this, owned, st, IDs::mix, "MIX", C::text)->setBounds (880, 262, 96, 92);

    const char* bid[] { "tone", "character", "drift", "bass_mono", "duck", "env", "out" };
    const char* blb[] { "TONE", "BBD CHARACTER", "ANALOG DRIFT", "BASS MONO", "DUCK", "ENV > DEPTH", "OUTPUT" };
    const juce::Colour bc[] { C::amber, C::amber, C::amber, C::cyan, C::cyan, C::cyan, C::text };
    for (int i = 0; i < 7; ++i)
    {
        auto* k = add<Knob> (*this, owned, st, bid[i], blb[i], bc[i], i == 0 || i == 5 || i == 6);
        k->setBounds (26 + i * 118, 410, 112, 112);
        if (i == 3) k->slider.setTooltip ("Keeps everything below this frequency dry and mono - the low end stays solid in the club");
        if (i == 4) k->slider.setTooltip ("The effect steps back while the sound plays and blooms in the gaps");
        if (i == 5) k->slider.setTooltip ("Louder input = more (or, negative, less) modulation depth");
        if (i == 2) k->slider.setTooltip ("Slow random wander of rate and delay, like real analog hardware");
    }
    add<Combo> (*this, owned, st, IDs::shape, C::amber)->setBounds (852, 548, 120, 26);

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setColour (juce::ComboBox::textColourId, C::cyan);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (330, 18, 250, 28);
    for (auto* b : { &prevB, &nextB }) { b->setColour (juce::TextButton::buttonColourId, C::cyan); addAndMakeVisible (*b); }
    prevB.setBounds (296, 18, 28, 28); nextB.setBounds (586, 18, 28, 28);
    prevB.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    for (auto* b : { &aB, &bB, &copyB }) addAndMakeVisible (*b);
    aB.setBounds (626, 18, 30, 28); bB.setBounds (658, 18, 30, 28); copyB.setBounds (692, 18, 40, 28);
    aB.onClick = [this] { proc.switchAB (0); };
    bB.onClick = [this] { proc.switchAB (1); };
    copyB.onClick = [this] { proc.copyAB(); };
    copyB.setColour (juce::TextButton::buttonColourId, C::dim);

    setSize (W, HH);
    startTimerHz (30);
}

LumenModEditor::~LumenModEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenModEditor::timerCallback()
{
    if (proc.programVersion.load() != lastProgram) { lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    const int m = (int) pv (IDs::mode), var = (int) pv (IDs::variant);
    for (int i = 0; i < 7; ++i) modeB[i].setColour (juce::TextButton::buttonColourId, i == m ? MC[i] : C::dim);
    if (m != lastMode)
    {
        lastMode = m;
        const auto vn = variantNames (m);
        for (int v = 0; v < 4; ++v) varB[v].setButtonText (vn[v].toUpperCase());
    }
    for (int v = 0; v < 4; ++v) varB[v].setColour (juce::TextButton::buttonColourId, v == var ? MC[m] : C::dim);
    { const bool on = m == 0 && var == 0; voicesKnob->setAlpha (on ? 1.0f : 0.3f); voicesKnob->setEnabled (on); }
    for (int v = 0; v < 8; ++v) trail[v][trailPos] = proc.voicePos[v].load();
    trailPos = (trailPos + 1) % 96;
    corrSm += (proc.corr.load() - corrSm) * 0.2f;
    aB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 0 ? C::cyan : C::dim);
    bB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 1 ? C::cyan : C::dim);
    repaint();
}

void LumenModEditor::drawDisplay (juce::Graphics& g, juce::Rectangle<float> r)
{
    const int m = (int) pv (IDs::mode);
    const auto col = MC[m];
    lcd (g, r, col);
    if (m == lumen::mod::Phaser)
    {
        // phaser: magnitude response with the moving notches (wet+dry at the current mix)
        const int stages = 4 * ((int) pv (IDs::variant) + 1);
        for (int c = 0; c < 2; ++c)
        {
            const float fc = proc.notch[c].load();
            juce::Path pth;
            for (int i = 0; i <= 200; ++i)
            {
                const float f = 20.0f * std::pow (1000.0f, i / 200.0f);
                // allpass chain phase: stages * 2 * atan(fc / f) deviation -> notch where phases cancel
                const float phi = (float) stages * 2.0f * std::atan (f / std::max (1.0f, fc));
                const float mag = std::abs (std::cos (0.5f * phi));
                const float x = r.getX() + 8 + (r.getWidth() - 16) * i / 200.0f, y = r.getBottom() - 12 - mag * (r.getHeight() - 30);
                if (i == 0) pth.startNewSubPath (x, y); else pth.lineTo (x, y);
            }
            glowPath (g, pth, c == 0 ? col : col.interpolatedWith (C::cyan, 0.6f), 1.6f, c == 0 ? 1.0f : 0.6f);
        }
        g.setFont (font (8.5f)); g.setColour (C::dim);
        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = r.getX() + 8 + (r.getWidth() - 16) * std::log10 (f / 20.0f) / 3.0f;
            g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) x - 15, (int) r.getBottom() - 12, 30, 10, juce::Justification::centred);
        }
        return;
    }
    // delay machines: each voice's delay over time (scrolling traces) + current dot
    const int nv = proc.voiceCount.load();
    g.setColour (C::dim.withAlpha (0.25f));
    for (int k = 1; k < 6; ++k) g.fillRect (r.getX() + 6, r.getY() + r.getHeight() * k / 6.0f, r.getWidth() - 12, 1.0f);
    for (int v = 0; v < nv; ++v)
    {
        juce::Path pth;
        float mx = 0;
        for (int k = 0; k < 96; ++k) mx = std::max (mx, trail[v][k]);
        for (int k = 0; k < 96; ++k)
        {
            const float val = trail[v][(trailPos + k) % 96];
            const float x = r.getX() + 10 + (r.getWidth() - 40) * k / 95.0f;
            const float y = r.getBottom() - 10 - val * (r.getHeight() - 20);
            if (k == 0) pth.startNewSubPath (x, y); else pth.lineTo (x, y);
        }
        glowPath (g, pth, VC[v], 1.4f, 0.8f);
        const float cur = trail[v][(trailPos + 95) % 96];
        drawLed (g, { r.getRight() - 20.0f, r.getBottom() - 10 - cur * (r.getHeight() - 20) }, 3.5f, VC[v], 1.0f);
    }
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("delay of each voice over time", (int) r.getX() + 8, (int) r.getY() + 4, 200, 10, juce::Justification::centredLeft);
}

void LumenModEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (C::cyan.withAlpha (0.0f), 0, 64, C::magenta.withAlpha (0.0f), (float) W, 64, false);
    for (int i = 0; i < 7; ++i) line.addColour (0.1 + 0.8 * i / 6.0, MC[i].withAlpha (0.7f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (C::cyan.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (C::cyan, 20, 0, C::magenta, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 146.0f, 18.0f, 120.0f, 28.0f }, "MOD", C::cyan, 2.4f, juce::Justification::centredLeft, false, 10.5f);
    }
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("IN", 748, 18, 20, 10, juce::Justification::centredLeft);
    g.drawText ("OUT", 748, 36, 24, 10, juce::Justification::centredLeft);
    ledMeter (g, { 774.0f, 18.0f, 210.0f, 9.0f }, proc.inPk.load(), proc.inPk.load(), false, 28);
    ledMeter (g, { 774.0f, 36.0f, 210.0f, 9.0f }, proc.outPk.load(), proc.outPk.load(), false, 28);

    const int m = (int) pv (IDs::mode);
    panel (g, { 16.0f, 72.0f, 968.0f, 54.0f }, "", MC[m]);
    panel (g, { 16.0f, 134.0f, 556.0f, 220.0f }, modeNames()[m].toUpperCase(), MC[m]);
    g.setFont (font (9.5f)); g.setColour (C::text.withAlpha (0.7f));
    g.drawText (modeHint (m), 130, 139, 430, 16, juce::Justification::centredLeft);
    drawDisplay (g, { 28.0f, 160.0f, 532.0f, 150.0f });
    panel (g, { 580.0f, 134.0f, 404.0f, 220.0f }, "MOTION", MC[m]);
    g.setFont (font (9.5f)); g.setColour (C::green.withAlpha (0.85f));
    g.drawText (juce::String (proc.rateNow.load(), 2) + " Hz", 864, 139, 110, 16, juce::Justification::centredRight);

    panel (g, { 16.0f, 362.0f, 968.0f, 238.0f }, "COLOUR & MIX", C::amber);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("LFO SHAPE", 852, 534, 120, 12, juce::Justification::centredLeft);
    // mono compatibility (L/R correlation)
    {
        auto r = juce::Rectangle<float> (28.0f, 548.0f, 800.0f, 14.0f);
        g.drawText ("MONO CHECK (correlation)", (int) r.getX(), (int) r.getY() - 14, 200, 12, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff030508)); g.fillRoundedRectangle (r, 3.0f);
        const int segs = 81;
        const float sw = r.getWidth() / segs;
        for (int i = 0; i < segs; ++i)
        {
            const float t = -1.0f + 2.0f * (i + 0.5f) / segs;
            const bool lit = corrSm >= 0 ? (t >= -0.02f && t <= corrSm) : (t <= 0.02f && t >= corrSm);
            const juce::Colour c = t < -0.05f ? C::red : t < 0.3f ? C::amber : C::green;
            g.setColour (lit ? c : c.withAlpha (0.07f));
            g.fillRoundedRectangle (r.getX() + i * sw + 0.7f, r.getY() + 3, sw - 1.4f, r.getHeight() - 6, 1.0f);
        }
        g.setColour (C::text.withAlpha (0.5f)); g.fillRect (r.getCentreX() - 0.5f, r.getY() - 2, 1.0f, r.getHeight() + 4);
        g.setFont (mono (10.0f)); g.setColour (corrSm < 0 ? C::red : C::text.withAlpha (0.8f));
        g.drawText (juce::String (corrSm, 2), (int) r.getRight() - 60, (int) r.getY() - 14, 60, 12, juce::Justification::centredRight);
        g.setFont (font (8.5f)); g.setColour (C::dim);
        g.drawText ("+1 = mono-safe, below 0 = parts cancel in mono. BASS MONO keeps the low end solid.", (int) r.getX(), (int) r.getBottom() + 4, 600, 12, juce::Justification::centredLeft);
    }
}
