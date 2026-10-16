#include "PluginEditor.h"

using namespace lumen::led;

namespace
{
constexpr int W = 1120, HH = 680;
const juce::Colour MAIN = C::amber;
template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}
struct Mod { const char* title; const char* onId; juce::Colour col; int x, y; };
const Mod mods[6] {
    { "WOBBLE", "wobble_on", C::magenta, 324, 114 }, { "SATURATE", "sat_on", C::orange, 586, 114 }, { "MAGNETIC", "magnetic_on", C::violet, 848, 114 },
    { "NOISE", "noise_on", C::green, 324, 298 }, { "DIGITAL", "digital_on", C::cyan, 586, 298 }, { "SPACE", "space_on", C::blue, 848, 298 } };
const char* modelNames[12] { "CASSETTE", "REEL", "VHS", "WALKMAN", "VINYL 45", "SHELLAC 78", "AM RADIO", "PHONE", "SP-1200", "MPC60", "8-BIT", "LO-FI HIP-HOP" };
}

LumenLoFiEditor::LumenLoFiEditor (LumenLoFiProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    for (int i = 0; i < 12; ++i)
    {
        modelB[i].setButtonText (modelNames[i]);
        modelB[i].setTooltip ("Load the " + juce::String (modelNames[i]) + " model (all modules)");
        modelB[i].onClick = [this, i] { proc.setCurrentProgram (i + 1); };
        addAndMakeVisible (modelB[i]); modelB[i].setBounds (92 + i * 84, 76, 81, 26);
    }
    for (auto& m : mods) { auto* t = add<Toggle> (*this, owned, st, m.onId, "", m.col, true); t->setBounds (m.x + 226, m.y + 3, 24, 24); t->setTooltip ("Module on / off"); }

    struct K { const char* id; const char* lb; int mod; int x, w; bool bi; const char* tip; };
    const K ks[] {
        { "wow", "WOW", 0, 0, 48, false, "Slow pitch wobble (warped tape / record)" },
        { "flutter", "FLUTTER", 0, 48, 48, false, "Fast pitch flutter (worn capstan)" },
        { "wow_rate", "RATE", 0, 96, 48, false, "Wow speed" },
        { "drift", "DRIFT", 0, 144, 48, false, "Random, non-repeating speed drift" },
        { "wobble_stereo", "STEREO", 0, 192, 48, false, "Left / right wobble differ - wide, seasick" },
        { "drive", "DRIVE", 1, 40, 80, false, "Saturation drive - alias-free (ADAA), no oversampling latency" },
        { "bias", "BIAS", 1, 140, 80, false, "Asymmetry: even harmonics (Tube / Tape)" },
        { "wear", "WEAR", 2, 0, 80, false, "Tape wear: high frequencies fade" },
        { "head_bump", "BUMP", 2, 80, 80, false, "Head bump: the warm low-end lift of a tape head" },
        { "dropouts", "DROPOUTS", 2, 160, 80, false, "Random level drops of old tape" },
        { "noise_level", "LEVEL", 3, 0, 80, false, "Noise level" },
        { "noise_tone", "TONE", 3, 80, 80, false, "Noise colour: dark .. bright" },
        { "noise_duck", "DUCK/FOLLOW", 3, 160, 80, true, "Left: the noise ducks under the music.  Right: the noise only appears WITH the music" },
        { "sample_rate", "RATE", 4, 0, 80, false, "Sample rate (SP-1200 = 26 kHz, MPC60 = 40 kHz)" },
        { "bits", "BITS", 4, 80, 80, false, "Bit depth" },
        { "vintage_filter", "FILTER", 4, 160, 80, false, "Vintage converter filters: 0% = raw aliasing, 100% = smooth like an MPC" },
        { "space_size", "SIZE", 5, 0, 80, false, "Room size" },
        { "space_decay", "DECAY", 5, 80, 80, false, "Decay" },
        { "space_mix", "MIX", 5, 160, 80, false, "Space amount" },
    };
    for (auto& k : ks)
    {
        const auto& m = mods[k.mod];
        const bool hasCombo = k.mod == 1 || k.mod == 3;
        auto* kn = add<Knob> (*this, owned, st, k.id, k.lb, m.col, k.bi);
        kn->setBounds (m.x + 8 + k.x, m.y + (hasCombo ? 70 : 52), k.w, k.w > 60 ? 98 : 90);
        kn->slider.setTooltip (k.tip);
    }
    auto* sc = add<Combo> (*this, owned, st, IDs::satType, mods[1].col); sc->setBounds (mods[1].x + 40, mods[1].y + 36, 176, 24);
    auto* nc = add<Combo> (*this, owned, st, IDs::noiseType, mods[3].col); nc->setBounds (mods[3].x + 40, mods[3].y + 36, 176, 24);

    auto* age = add<Knob> (*this, owned, st, IDs::age, "AGE", MAIN); age->setBounds (101, 316, 130, 132);
    age->slider.setTooltip ("AGE: one knob for the whole machine - 0% = clean, 100% = as set, 200% = falling apart");
    diceB.setColour (juce::TextButton::buttonColourId, C::green);
    diceB.setTooltip ("Roll a new random (but musical) character");
    diceB.onClick = [this] { proc.randomize(); };
    addAndMakeVisible (diceB); diceB.setBounds (250, 440, 56, 26);

    struct G { const char* id; const char* lb; int x; bool bi; const char* tip; };
    const G gs[] {
        { "low_cut", "LOW CUT", 30, false, "Bandwidth: low cut" }, { "high_cut", "HIGH CUT", 110, false, "Bandwidth: high cut" },
        { "width", "WIDTH", 190, false, "Stereo width" }, { "flux", "FLUX", 400, true, "FLUX: wobble and dropouts follow the dynamics (right = more when loud, left = more when quiet)" },
        { "mix", "MIX", 480, false, "Dry / wet (latency-compensated)" }, { "out", "OUTPUT", 560, true, "Output level" } };
    for (auto& gk : gs) { auto* kn = add<Knob> (*this, owned, st, gk.id, gk.lb, MAIN, gk.bi); kn->setBounds (gk.x, 516, 76, 96); kn->slider.setTooltip (gk.tip); }
    auto* mb = add<Toggle> (*this, owned, st, IDs::monoBass, "MONO BASS", C::cyan); mb->setBounds (276, 540, 110, 24);
    mb->setTooltip ("Keeps everything below 150 Hz mono (vinyl-safe, club-safe)");

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
    startTimerHz (30);
}

LumenLoFiEditor::~LumenLoFiEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenLoFiEditor::timerCallback()
{
    if (proc.programVersion.load() != lastProgram) { lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    const int cp = proc.getCurrentProgram();
    for (int i = 0; i < 12; ++i) modelB[i].setColour (juce::TextButton::buttonColourId, cp == i + 1 ? MAIN : C::dim);
    inPk = juce::jmax (proc.inPeak.load(), inPk * 0.85f); outPk = juce::jmax (proc.outPeak.load(), outPk * 0.85f);
    const auto& e = proc.getEngine();
    const float wobDepth = pv (IDs::wobOn) > 0.5f ? pv (IDs::wow) * pv (IDs::age) : 0.0f;
    const float speed = 1.0f + 0.6f * wobDepth * std::sin (juce::MathConstants<float>::twoPi * e.wobblePos);
    if (outPk > 1e-4f) reelAngle += 0.09f * speed;
    dropVis += (e.dropNow - dropVis) * 0.4f;
    aB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 0 ? MAIN : C::dim);
    bB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 1 ? MAIN : C::dim);
    repaint();
}

void LumenLoFiEditor::drawCassette (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float age = juce::jlimit (0.0f, 2.0f, pv (IDs::age));
    // body
    juce::ColourGradient body (juce::Colour (0xff1a1e27), r.getX(), r.getY(), juce::Colour (0xff0d1016), r.getX(), r.getBottom(), false);
    g.setGradientFill (body); g.fillRoundedRectangle (r, 10.0f);
    g.setColour (MAIN.withAlpha (0.35f)); g.drawRoundedRectangle (r, 10.0f, 1.2f);
    // label
    auto lab = r.reduced (16.0f, 12.0f).withHeight (r.getHeight() * 0.62f);
    const float glow = 1.0f - 0.6f * dropVis;
    juce::ColourGradient lg (MAIN.withAlpha (0.20f * glow), lab.getX(), lab.getY(), C::magenta.withAlpha (0.14f * glow), lab.getRight(), lab.getBottom(), false);
    g.setGradientFill (lg); g.fillRoundedRectangle (lab, 6.0f);
    g.setColour (MAIN.withAlpha (0.5f)); g.drawRoundedRectangle (lab, 6.0f, 1.0f);
    for (int k = 0; k < 3; ++k) { g.setColour (MAIN.withAlpha (0.25f)); g.fillRect (lab.getX() + 10.0f, lab.getY() + 10.0f + k * 7.0f, lab.getWidth() - 20.0f, 1.0f); }
    g.setFont (font (11.0f)); g.setColour (MAIN.interpolatedWith (juce::Colours::white, 0.4f).withAlpha (glow));
    g.drawText (proc.getCurrentProgram() > 0 ? proc.getProgramName (proc.getCurrentProgram()).replace ("MODEL ", "") : juce::String ("LUMEN C-90"), lab.withTrimmedTop (28.0f).withHeight (16.0f), juce::Justification::centred);
    // window + reels
    auto win = juce::Rectangle<float> (lab.getCentreX() - 70.0f, lab.getBottom() - 46.0f, 140.0f, 40.0f);
    g.setColour (juce::Colour (0xff05070a)); g.fillRoundedRectangle (win, 16.0f);
    for (int k = 0; k < 2; ++k)
    {
        const juce::Point<float> c { win.getX() + 26.0f + k * 88.0f, win.getCentreY() };
        const float rad = 15.0f;
        g.setColour (juce::Colour (0xff2a2f3a)); g.fillEllipse (c.x - rad, c.y - rad, 2 * rad, 2 * rad);
        g.setColour (MAIN.withAlpha (0.8f));
        for (int s = 0; s < 6; ++s)
        {
            const float a = reelAngle * (k == 0 ? 1.0f : 1.15f) + s * juce::MathConstants<float>::twoPi / 6.0f;
            g.drawLine (c.x + std::cos (a) * 5.0f, c.y + std::sin (a) * 5.0f, c.x + std::cos (a) * (rad - 2.0f), c.y + std::sin (a) * (rad - 2.0f), 2.0f);
        }
        g.setColour (juce::Colour (0xff05070a)); g.fillEllipse (c.x - 4, c.y - 4, 8, 8);
    }
    // wear: scratches grow with AGE
    juce::Random rr (77);
    g.setColour (juce::Colours::white.withAlpha (0.05f + 0.06f * age));
    for (int k = 0; k < (int) (age * 14.0f); ++k)
    {
        const float x0 = r.getX() + rr.nextFloat() * r.getWidth(), y0 = r.getY() + rr.nextFloat() * r.getHeight();
        g.drawLine (x0, y0, x0 + (rr.nextFloat() - 0.5f) * 40.0f, y0 + (rr.nextFloat() - 0.5f) * 10.0f, 0.8f);
    }
    // bottom: tape path
    auto bot = r.withTrimmedTop (r.getHeight() * 0.78f).reduced (40.0f, 4.0f);
    g.setColour (juce::Colour (0xff222733)); g.fillRoundedRectangle (bot, 4.0f);
    for (int k = 0; k < 4; ++k) { g.setColour (juce::Colour (0xff05070a)); g.fillEllipse (bot.getX() + 14.0f + k * (bot.getWidth() - 28.0f) / 3.0f - 3.0f, bot.getCentreY() - 3.0f, 6.0f, 6.0f); }
}

void LumenLoFiEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (C::amber.withAlpha (0.0f), 0, 64, C::magenta.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.25, C::amber.withAlpha (0.75f)); line.addColour (0.75, C::magenta.withAlpha (0.75f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (MAIN.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (C::amber, 20, 0, C::magenta, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 146.0f, 18.0f, 220.0f, 28.0f }, "LO-FI", MAIN, 1.85f, juce::Justification::centredLeft, false, 10.5f);
    }
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("IN", 852, 14, 24, 10, juce::Justification::centredLeft);
    g.drawText ("OUT", 852, 36, 24, 10, juce::Justification::centredLeft);
    ledMeter (g, { 878.0f, 13.0f, 226.0f, 9.0f }, inPk, inPk, false, 30);
    ledMeter (g, { 878.0f, 35.0f, 226.0f, 9.0f }, outPk, outPk, false, 30);
    g.setFont (font (10.0f)); g.setColour (MAIN);
    g.drawText ("MODEL", 22, 76, 64, 26, juce::Justification::centredLeft);

    panel (g, { 16.0f, 114.0f, 300.0f, 360.0f }, "MACHINE", MAIN);
    drawCassette (g, { 30.0f, 142.0f, 272.0f, 166.0f });
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("CLEAN", 60, 438, 50, 12, juce::Justification::centredLeft);
    g.drawText ("RUINED", 210, 418, 60, 12, juce::Justification::centredLeft);
    g.setFont (mono (10.0f)); g.setColour (C::text.withAlpha (0.7f));
    g.drawText ("latency " + juce::String (proc.getLatencySamples()) + " smp", 26, 452, 160, 14, juce::Justification::centredLeft);

    for (auto& m : mods)
    {
        const bool on = pv (m.onId) > 0.5f;
        panel (g, { (float) m.x, (float) m.y, 256.0f, 176.0f }, m.title, m.col, on);
    }
    if (pv (IDs::noiseDuck) < -0.05f || pv (IDs::noiseDuck) > 0.05f)
    {
        g.setFont (font (8.5f)); g.setColour (mods[3].col);
        g.drawText (pv (IDs::noiseDuck) < 0 ? "DUCKING" : "FOLLOWING", mods[3].x + 150, mods[3].y + 6, 70, 14, juce::Justification::centredRight);
    }
    if (dropVis > 0.05f) { g.setFont (font (8.5f)); g.setColour (mods[2].col); g.drawText ("DROPOUT", mods[2].x + 150, mods[2].y + 6, 70, 14, juce::Justification::centredRight); }

    panel (g, { 16.0f, 482.0f, 1088.0f, 186.0f }, "TONE / STEREO / OUTPUT", MAIN);
    g.setColour (C::border); g.fillRect (392, 500, 1, 150); g.fillRect (646, 500, 1, 150);
    g.setFont (font (9.5f)); g.setColour (C::dim);
    g.drawFittedText ("AGE turns every module up or down together - 0% is clean, 200% is falling apart. MODEL buttons load a full machine. "
                      "DICE rolls a new musical character. Noise DUCK/FOLLOW: left = noise hides under the music, right = noise only with the music.",
                      juce::Rectangle<int> (664, 506, 430, 70), juce::Justification::topLeft, 5);
}
